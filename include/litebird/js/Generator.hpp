#pragma once

#include <litebird/js/JsValue.hpp>
#include <litebird/js/Heap.hpp>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <exception>
#include <functional>
#include <system_error>

namespace litebird {

class JsEnvironment;

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

/// Unwinds a generator body with a return value: raised at the suspended
/// `yield` when the consumer calls `.return(value)`. Runs `finally` blocks
/// (never a `catch`); the frame then completes with the carried value. The
/// argument of `.return()` is awaited at the resumption boundary for an
/// async generator (AsyncGeneratorUnwrapYieldResumption), so this always
/// carries the settled value.
class GeneratorReturnException : public std::exception {
public:
    explicit GeneratorReturnException(JsValue return_value) : value(std::move(return_value)) {}
    JsValue value;
    const char* what() const noexcept override {
        return "Generator return";
    }
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
    /// The frame belongs to an async generator (`async function*`), as
    /// distinct from an async function: `yield` is the consumer-visible
    /// suspension, and an async generator's own promise machinery wraps its
    /// result objects (see make_generator in JsEval.cpp).
    bool is_async{false};
    /// Set when the engine has discarded the frame; nothing may resume it or
    /// act on its result afterwards.
    bool cancelled{false};

    JsValue send_value{JsValue::make_undefined()};
    JsValue yielded_value{JsValue::make_undefined()};
    /// True when the yielded value is an iterator result object that the
    /// consumer should receive as-is (a sync generator's `yield*` hands the
    /// delegate's raw step object out; a normal `yield` wraps in a fresh
    /// {value, done}). Checked and cleared when the resumer takes the yield.
    bool yielded_raw{false};
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
    /// A `.return(value)` that is waiting to resume the frame. Unlike a
    /// terminate() it unwinds the body as a return completion, so `finally`
    /// blocks and (inside a `yield*`) the delegate's `return` method run.
    bool has_pending_return{false};
    JsValue pending_return{JsValue::make_undefined()};

    /// The frame's scope (a JsEnvironment), kept so the collector can reach
    /// the frame's local variables while it is suspended. Cleared once the
    /// thread has finished or been terminated so a completed generator does
    /// not pin its enclosing scope.
    std::shared_ptr<JsEnvironment> frame_env;

    /// Deferred worker body for generators: spawned lazily on the first
    /// resume() so an unstarted generator owns no OS thread at all.
    std::function<void()> start_thunk;

    /// Pointer to the worker thread's `ScopeStack` while the worker is alive
    /// (cleared before `finish()` notifies `cv_main`). Read only while the
    /// worker is parked inside `cv_gen.wait` (`!running`).
    void* active_scope_stack{nullptr};

    std::thread worker;

    ~GeneratorData() {
        cancelled = true;
        terminate(false);
        // Only reachable if the last reference died on the frame's own thread.
        if (worker.joinable()) worker.detach();
    }

    /// Unwinds the frame if it is suspended and waits for its thread to end.
    void terminate(bool run_finalizers = false) {
        std::function<void()> dropped_thunk;
        {
            std::unique_lock<std::mutex> lock(mtx);
            dropped_thunk = std::move(start_thunk);
            start_thunk = nullptr;
            if (!finished) {
                should_terminate = true;
                abort_runs_finalizers = run_finalizers;
                waiting = false;
                cv_gen.notify_all();
            }
            is_done = true;
        }
        dropped_thunk = nullptr;
        // A frame may close itself (a generator calling its own return()). It
        // cannot wait for its own thread; it unwinds at its next suspension.
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
            worker.join();
        }
        if (!worker.joinable()) {
            frame_env.reset();
            send_value = JsValue::make_undefined();
            yielded_value = JsValue::make_undefined();
            error = JsValue::make_undefined();
            pending_throw = JsValue::make_undefined();
            pending_return = JsValue::make_undefined();
        }
    }

    /// Waits for the frame's thread once its body has ended, and drops the
    /// frame's environment so a finished generator does not root its closure.
    void reap() {
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
        if (!worker.joinable()) {
            start_thunk = nullptr;
            frame_env.reset();
            send_value = JsValue::make_undefined();
            pending_throw = JsValue::make_undefined();
            pending_return = JsValue::make_undefined();
        }
    }

    /// Called on the frame's thread: parks it and hands `val` to the resumer.
    /// Returns the value passed to the next resume(). With `raw`, the value
    /// is an iterator result object for the consumer to receive untouched
    /// (a sync generator's `yield*`).
    JsValue yield_to_main(JsValue val, bool raw = false) {
        std::unique_lock<std::mutex> lock(mtx);
        if (should_terminate) throw GeneratorAbortException(abort_runs_finalizers);
        yielded_value = std::move(val);
        yielded_raw = raw;
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
        yielded_raw = false;
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
            if (start_thunk) {
                auto thunk = std::make_shared<std::function<void()>>(std::move(start_thunk));
                start_thunk = nullptr;
                lock.unlock();
                auto launch = [&]() {
                    worker = std::thread([thunk]() { (*thunk)(); });
                };
                try {
                    launch();
                } catch (const std::system_error&) {
                    // Reclaim any unreachable suspended generators holding OS
                    // threads and retry once before reporting thread exhaustion.
                    Heap::default_heap().collect_garbage({});
                    try {
                        launch();
                    } catch (const std::system_error&) {
                        lock.lock();
                        finished = true;
                        is_done = true;
                        running = false;
                        frame_env.reset();
                        throw;
                    }
                }
                lock.lock();
            }
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

    /// Makes the next resume() finish the frame as a return completion.
    /// The value is awaited at the resumption boundary before the body sees
    /// the completion (an async generator awaits it; a sync one takes it raw).
    void inject_return(JsValue value) {
        std::unique_lock<std::mutex> lock(mtx);
        has_pending_return = true;
        pending_return = std::move(value);
    }

    /// If a return was injected, takes it.
    bool take_pending_return(JsValue& out) {
        std::unique_lock<std::mutex> lock(mtx);
        if (!has_pending_return) return false;
        has_pending_return = false;
        out = std::move(pending_return);
        pending_return = JsValue::make_undefined();
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
