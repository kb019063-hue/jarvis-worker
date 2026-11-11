#pragma once

#include <litebird/js/JsValue.hpp>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <exception>

namespace litebird {

/// Unwinds a suspended frame that will never be resumed. It is thrown on the
/// frame's own thread, from the `yield` or `await` it was parked at.
class GeneratorAbortException : public std::exception {
public:
    explicit GeneratorAbortException(bool finalizers = false) : run_finalizers(finalizers) {}
    const char* what() const noexcept override {
        return "Generator execution aborted";
    }
    /// True when script closed the frame (iterator.return(), leaving a for-of
    /// loop early): `finally` blocks run on the way out. False when the engine
    /// is discarding the frame, where running script would be unsafe.
    bool run_finalizers;
};

/// How deep in a chain of frames the code on this thread is running: 0 on an
/// agent's own thread, and one more than its resumer's inside a frame.
///
/// A frame that resumes another frame blocks until that one suspends, so
/// frames resumed from inside one another stack up exactly like nested calls,
/// except that each level holds a thread instead of a stack frame. A recursion
/// through them (`function* g() { yield* g(); }`, or an async function that
/// awaits itself) therefore has to be stopped the way a runaway recursion is.
/// Left alone it ends when the system refuses another thread, which is a
/// different number on every machine and can be more than its memory holds.
inline thread_local int t_frame_nesting = 0;
inline constexpr int kMaxFrameNesting = 2000;

/// A suspendable function activation: a generator, or an async function that
/// is parked at an `await`.
///
/// The body runs on its own thread so that it has its own native stack to
/// suspend on. Control is handed back and forth with a strict handshake: at any
/// moment either the frame's thread runs or the thread that resumed it does,
/// never both, so script code is never actually concurrent.
struct GeneratorData {
    std::mutex mtx;
    std::condition_variable cv_main;
    std::condition_variable cv_gen;

    bool started{false};
    bool waiting{false};
    bool finished{false};
    bool running{false};
    bool should_terminate{false};
    bool abort_runs_finalizers{false};

    /// The frame belongs to an async function: `await` suspends it. (In a
    /// generator, `await` blocks instead; see JsInterpreter::eval_await.)
    bool is_coroutine{false};
    /// Set when the engine has discarded the frame; nothing may resume it or
    /// act on its result afterwards.
    bool cancelled{false};

    JsValue send_value{JsValue::make_undefined()};
    JsValue yielded_value{JsValue::make_undefined()};
    bool is_done{false};

    /// t_frame_nesting for the frame's thread, set by whoever resumes it.
    int nesting{0};

    // An exception that escaped the body, to be rethrown to whoever resumed
    // it; and one injected from outside, to be raised at the suspended
    // `yield` or `await`.
    bool has_error{false};
    JsValue error{JsValue::make_undefined()};
    bool has_pending_throw{false};
    JsValue pending_throw{JsValue::make_undefined()};

    /// The frame's scope (a JsEnvironment), kept so the collector can reach
    /// the frame's local variables while it is suspended.
    std::shared_ptr<void> frame_env;

    std::thread worker;

    ~GeneratorData() {
        cancelled = true;
        terminate(false);
        // Only reachable if the last reference died on the frame's own thread.
        if (worker.joinable()) worker.detach();
    }

    /// Unwinds the frame if it is suspended and waits for its thread to end.
    void terminate(bool run_finalizers = false) {
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (!finished) {
                should_terminate = true;
                abort_runs_finalizers = run_finalizers;
                waiting = false;
                cv_gen.notify_all();
            }
            is_done = true;
        }
        // A frame may close itself (a generator calling its own return()). It
        // cannot wait for its own thread; it unwinds at its next suspension.
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
            worker.join();
        }
    }

    /// Waits for the frame's thread once its body has ended.
    void reap() {
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
    }

    /// Called on the frame's thread: parks it and hands `val` to the resumer.
    /// Returns the value passed to the next resume().
    JsValue yield_to_main(JsValue val) {
        std::unique_lock<std::mutex> lock(mtx);
        if (should_terminate) throw GeneratorAbortException(abort_runs_finalizers);
        yielded_value = std::move(val);
        waiting = true;
        running = false;
        cv_main.notify_one();

        cv_gen.wait(lock, [this] {
            return !waiting || should_terminate;
        });

        if (should_terminate) {
            throw GeneratorAbortException(abort_runs_finalizers);
        }
        running = true;
        t_frame_nesting = nesting;
        return send_value;
    }

    /// Called on the frame's thread before the body starts. Returns false when
    /// the frame was discarded before it ever ran.
    bool wait_for_start() {
        std::unique_lock<std::mutex> lock(mtx);
        cv_gen.wait(lock, [this] { return started || should_terminate; });
        if (should_terminate) return false;
        running = true;
        t_frame_nesting = nesting;
        return true;
    }

    /// Called on the frame's thread when the body has ended.
    void finish(JsValue result, bool failed, JsValue thrown) {
        std::unique_lock<std::mutex> lock(mtx);
        yielded_value = failed ? JsValue::make_undefined() : std::move(result);
        has_error = failed;
        error = std::move(thrown);
        is_done = true;
        finished = true;
        running = false;
        cv_main.notify_one();
    }

    /// Runs the frame until it suspends again or ends. Returns false, without
    /// doing anything, when the frame is the one currently running: it cannot
    /// resume itself.
    bool resume(JsValue send_arg) {
        std::unique_lock<std::mutex> lock(mtx);
        if (running) return false;
        if (is_done || finished || should_terminate) return true;
        // Scripts see this as "Maximum call stack size exceeded". Nothing has
        // been changed yet, so the frame can still be resumed from higher up.
        if (t_frame_nesting >= kMaxFrameNesting) throw JsStackOverflow();
        nesting = t_frame_nesting + 1;
        send_value = std::move(send_arg);
        if (!started) {
            started = true;
        } else {
            waiting = false;
        }
        cv_gen.notify_one();
        cv_main.wait(lock, [this] {
            return waiting || finished;
        });
        return true;
    }

    /// True while the frame's body is executing (as opposed to parked or done).
    bool is_running() {
        std::unique_lock<std::mutex> lock(mtx);
        return running;
    }

    /// Makes the next resume() raise `value` at the suspension point.
    void inject_throw(JsValue value) {
        std::unique_lock<std::mutex> lock(mtx);
        has_pending_throw = true;
        pending_throw = std::move(value);
    }

    /// If a throw was injected, takes it.
    bool take_pending_throw(JsValue& out) {
        std::unique_lock<std::mutex> lock(mtx);
        if (!has_pending_throw) return false;
        has_pending_throw = false;
        out = std::move(pending_throw);
        pending_throw = JsValue::make_undefined();
        return true;
    }

    /// If the body ended with an exception, takes it.
    bool take_error(JsValue& out) {
        std::unique_lock<std::mutex> lock(mtx);
        if (!has_error) return false;
        has_error = false;
        out = std::move(error);
        error = JsValue::make_undefined();
        return true;
    }

    /// resume() plus the `{ value, done }` object the iterator protocol wants.
    JsValue step(JsValue send_arg) {
        if (!is_done) resume(std::move(send_arg));
        JsValue res = JsValue::make_object();
        res.set_property("value", yielded_value);
        res.set_property("done", JsValue::make_boolean(is_done));
        return res;
    }
};

class GeneratorContext {
public:
    static GeneratorData* get_active();
    static void set_active(GeneratorData* gen);
};

} // namespace litebird
