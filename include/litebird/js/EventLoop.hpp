#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <initializer_list>
#include <vector>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <memory>
#include <utility>
#include "litebird/js/JsValue.hpp"

namespace litebird {

using TimerCallback = std::function<void()>;

/// Values a queued callback keeps alive. The collector cannot look inside a
/// std::function, so whoever queues a callback that captured JsValues hands the
/// same values over here; they are treated as roots until the callback is gone.
using JsRootSet = std::shared_ptr<std::vector<JsValue>>;

inline JsRootSet js_roots(std::initializer_list<JsValue> values) {
    return std::make_shared<std::vector<JsValue>>(values);
}

struct TimerTask {
    uint32_t id{0};
    std::chrono::steady_clock::time_point trigger_time;
    std::chrono::milliseconds interval{0};
    bool is_interval{false};
    TimerCallback callback;
    bool cancelled{false};          // clearTimeout/clearInterval was called
    bool consumed{false};           // a one-shot timer that has been picked up to run
    uint64_t sequence{0};           // creation order, the tie-break for equal due times
    std::vector<JsRootSet> roots;
};

/// One agent's event loop: its task queue, microtask queue and timers.
///
/// A page and each of its workers are separate agents. A thread is bound to at
/// most one loop; default_loop() returns that loop, or the process-wide loop
/// for threads that were never bound (the main thread, unit tests).
class EventLoop {
public:
    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// The loop of the agent running on this thread.
    static EventLoop& default_loop();
    /// Binds this thread to `loop`; nullptr unbinds it. Threads an agent starts
    /// to run its own script (suspended function frames) bind to its loop.
    static void set_current(EventLoop* loop);

    /// Script-entry bookkeeping for JsScriptScope: the number of host-to-script
    /// calls in progress in this agent. Zero means its JavaScript stack is empty.
    int enter_script() { return ++m_script_depth; }
    int leave_script() { return --m_script_depth; }
    int script_depth() const { return m_script_depth.load(); }

    uint32_t set_timeout(TimerCallback cb, int delay_ms, std::vector<JsRootSet> roots = {});
    uint32_t set_interval(TimerCallback cb, int interval_ms, std::vector<JsRootSet> roots = {});
    void clear_timer(uint32_t id);

    void post_task(std::function<void()> task, std::vector<JsRootSet> roots = {});
    void post_microtask(std::function<void()> microtask, std::vector<JsRootSet> roots = {});

    // Queued callbacks keep copies of JsValues (promise handlers, captured
    // closures, queueMicrotask arguments) inside std::function storage, which the
    // collector cannot inspect. An owner registers a provider that pushes the
    // addresses of those live values; the provider stays valid until the owner
    // withdraws it. Keyed by owner so multiple independent owners can contribute.
    using GcRootProvider = std::function<void(std::vector<JsValue*>&)>;
    void add_gc_root_provider(const void* owner, GcRootProvider provider);
    void remove_gc_root_provider(const void* owner);
    void collect_gc_roots(std::vector<JsValue*>& roots) const;
    /// Drains the microtask queue. A call made while a drain is already in
    /// progress returns 0 unless `nested` is set: microtasks run in FIFO order,
    /// and a nested drain would let newer jobs overtake older ones. Only the
    /// blocking `await` fallback asks for a nested drain.
    size_t run_microtasks(bool nested = false);

    // Processes all tasks and timers ready at 'now'.
    // Returns the total number of callbacks and tasks executed.
    size_t tick(std::chrono::steady_clock::time_point now);
    size_t tick() { return tick(now()); }

    /// One loop turn for code that blocks waiting on a promise: like tick(), but
    /// the microtask drain is allowed to nest.
    size_t spin_once();

    /// The loop's clock: steady time plus however far run_until_idle() has
    /// skipped ahead. Timers are scheduled and compared against this.
    std::chrono::steady_clock::time_point now() const;
    /// How far the loop's clock runs ahead of real time.
    std::chrono::milliseconds clock_skew() const;

    /// Runs until nothing is pending, or until `budget` of loop time or
    /// `max_callbacks` callbacks have been spent. The loop does not sleep while
    /// waiting for a timer: the clock jumps to the next due time, so a one-shot
    /// host (rendering a page once, running a script file) settles quickly and
    /// produces the same result every run. Returns the callbacks executed.
    /// `wall_limit` bounds the real time spent, for callbacks that are slow.
    size_t run_until_idle(std::chrono::milliseconds budget, size_t max_callbacks = 100000,
                          std::chrono::milliseconds wall_limit = std::chrono::seconds(10));

    /// Waits for the next timer: by skipping the clock when `allow_skip` is set
    /// (see run_until_idle), otherwise by sleeping briefly.
    void wait_for_work(bool allow_skip);
    /// One-shot hosts turn this on so that blocking waits skip time too.
    void set_time_skipping(bool enabled) { m_time_skipping = enabled; }
    bool time_skipping() const { return m_time_skipping; }

    bool has_pending_tasks() const;
    size_t active_timer_count() const;
    /// How long until the next timer is due, measured on the loop's clock;
    /// zero when something is ready now, empty when nothing is scheduled.
    std::optional<std::chrono::milliseconds> time_until_next(std::chrono::steady_clock::time_point now) const;
    std::optional<std::chrono::milliseconds> time_until_next() const { return time_until_next(now()); }

    void clear();

private:
    uint32_t m_next_id{1};
    mutable std::mutex m_mutex;
    struct QueuedCallback {
        std::function<void()> run;
        std::vector<JsRootSet> roots;
    };

    size_t turn(std::chrono::steady_clock::time_point now, bool nested);
    void run_callback(QueuedCallback& callback);
    void skip_time(std::chrono::milliseconds amount);

    std::vector<TimerTask> m_timers;
    std::deque<QueuedCallback> m_task_queue;
    std::deque<QueuedCallback> m_microtask_queue;
    std::vector<std::pair<const void*, GcRootProvider>> m_gc_root_providers;

    // Callbacks that have been taken off a queue and are running or about to
    // run still own captured values; their root sets stay listed here.
    std::vector<JsRootSet> m_in_flight_roots;
    uint64_t m_next_sequence{0};
    int m_drain_depth{0};
    int m_turn_depth{0};
    std::atomic<int> m_script_depth{0};
    bool m_time_skipping{false};
    std::chrono::milliseconds m_clock_skew{0};
};

} // namespace litebird
