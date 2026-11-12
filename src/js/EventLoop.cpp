#include "litebird/js/EventLoop.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/Heap.hpp"
#include <thread>

namespace litebird {

namespace {

/// Keeps a counter raised for the lifetime of a scope, under the loop's mutex.
struct DepthScope {
    std::mutex& mutex;
    int& depth;
    DepthScope(std::mutex& m, int& d) : mutex(m), depth(d) {}       // caller has already incremented
    ~DepthScope() {
        std::lock_guard<std::mutex> lock(mutex);
        --depth;
    }
};

} // namespace

namespace {
thread_local EventLoop* t_current_loop = nullptr;
}

EventLoop::EventLoop() {
    // Callbacks waiting in this loop hold script values the collector cannot
    // otherwise see.
    Heap::default_heap().add_root_provider(this, [this](std::vector<JsValue*>& roots) { collect_gc_roots(roots); });
}

EventLoop::~EventLoop() {
    Heap::default_heap().remove_root_provider(this);
    if (t_current_loop == this) t_current_loop = nullptr;
}

EventLoop& EventLoop::default_loop() {
    if (t_current_loop) return *t_current_loop;
    // Leaked on purpose: callbacks queued here may own values whose
    // destructors must not run during static destruction.
    static EventLoop* s_loop = new EventLoop();
    return *s_loop;
}

void EventLoop::set_current(EventLoop* loop) {
    t_current_loop = loop;
}

std::chrono::steady_clock::time_point EventLoop::now() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::chrono::steady_clock::now() + m_clock_skew;
}

std::chrono::milliseconds EventLoop::clock_skew() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clock_skew;
}

void EventLoop::skip_time(std::chrono::milliseconds amount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clock_skew += amount;
}

uint32_t EventLoop::set_timeout(TimerCallback cb, int delay_ms, std::vector<JsRootSet> roots) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t id = m_next_id++;
    TimerTask task;
    task.id = id;
    task.trigger_time = std::chrono::steady_clock::now() + m_clock_skew + std::chrono::milliseconds(std::max(0, delay_ms));
    task.is_interval = false;
    task.callback = std::move(cb);
    task.sequence = m_next_sequence++;
    task.roots = std::move(roots);
    m_timers.push_back(std::move(task));
    return id;
}

uint32_t EventLoop::set_interval(TimerCallback cb, int interval_ms, std::vector<JsRootSet> roots) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t id = m_next_id++;
    int delay = std::max(1, interval_ms);
    TimerTask task;
    task.id = id;
    task.trigger_time = std::chrono::steady_clock::now() + m_clock_skew + std::chrono::milliseconds(delay);
    task.interval = std::chrono::milliseconds(delay);
    task.is_interval = true;
    task.callback = std::move(cb);
    task.sequence = m_next_sequence++;
    task.roots = std::move(roots);
    m_timers.push_back(std::move(task));
    return id;
}

void EventLoop::clear_timer(uint32_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& t : m_timers) {
        if (t.id == id) {
            t.cancelled = true;
            break;
        }
    }
}

void EventLoop::post_task(std::function<void()> task, std::vector<JsRootSet> roots) {
    if (task) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_task_queue.push_back({std::move(task), std::move(roots)});
    }
}

void EventLoop::post_microtask(std::function<void()> microtask, std::vector<JsRootSet> roots) {
    if (microtask) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_microtask_queue.push_back({std::move(microtask), std::move(roots)});
    }
}

void EventLoop::add_gc_root_provider(const void* owner, GcRootProvider provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& entry : m_gc_root_providers) {
        if (entry.first == owner) {
            entry.second = std::move(provider);
            return;
        }
    }
    m_gc_root_providers.emplace_back(owner, std::move(provider));
}

void EventLoop::remove_gc_root_provider(const void* owner) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gc_root_providers.erase(
        std::remove_if(m_gc_root_providers.begin(), m_gc_root_providers.end(),
                       [owner](const auto& e) { return e.first == owner; }),
        m_gc_root_providers.end());
}

// Called from within Heap::collect_garbage, which already holds the heap mutex;
// this only takes the event-loop mutex.
void EventLoop::collect_gc_roots(std::vector<JsValue*>& roots) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& entry : m_gc_root_providers) {
        if (entry.second) entry.second(roots);
    }
    auto add = [&roots](const std::vector<JsRootSet>& sets) {
        for (const auto& set : sets) {
            if (!set) continue;
            for (auto& value : *set) roots.push_back(&value);
        }
    };
    // Everything a queued or currently running callback captured is live.
    for (const auto& task : m_task_queue) add(task.roots);
    for (const auto& task : m_microtask_queue) add(task.roots);
    for (const auto& timer : m_timers) add(timer.roots);
    add(m_in_flight_roots);
}

// Runs one callback with its captured values registered as in-flight roots.
void EventLoop::run_callback(QueuedCallback& callback) {
    struct InFlight {
        EventLoop& loop;
        const std::vector<JsRootSet>& sets;
        InFlight(EventLoop& l, const std::vector<JsRootSet>& s) : loop(l), sets(s) {
            if (sets.empty()) return;
            std::lock_guard<std::mutex> lock(loop.m_mutex);
            for (const auto& set : sets) loop.m_in_flight_roots.push_back(set);
        }
        ~InFlight() {
            if (sets.empty()) return;
            std::lock_guard<std::mutex> lock(loop.m_mutex);
            for (const auto& set : sets) {
                // Search from the back: nested callbacks finish in LIFO order.
                for (auto it = loop.m_in_flight_roots.rbegin(); it != loop.m_in_flight_roots.rend(); ++it) {
                    if (*it == set) {
                        loop.m_in_flight_roots.erase(std::next(it).base());
                        break;
                    }
                }
            }
        }
    } in_flight(*this, callback.roots);

    // A callback that throws must not take the loop down with it.
    js_run_guarded(callback.run);
}

size_t EventLoop::run_microtasks(bool nested) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_drain_depth > 0 && !nested) return 0;
        ++m_drain_depth;
    }
    DepthScope depth(m_mutex, m_drain_depth);

    size_t count = 0;
    while (true) {
        QueuedCallback job;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_microtask_queue.empty()) break;
            job = std::move(m_microtask_queue.front());
            m_microtask_queue.pop_front();
        }
        run_callback(job);
        count++;
    }
    return count;
}

size_t EventLoop::tick(std::chrono::steady_clock::time_point now) {
    return turn(now, false);
}

size_t EventLoop::spin_once() {
    return turn(now(), true);
}

size_t EventLoop::turn(std::chrono::steady_clock::time_point now, bool nested) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_turn_depth;
    }
    DepthScope depth(m_mutex, m_turn_depth);

    size_t executed_count = 0;

    // 1. Drain microtask queue first
    executed_count += run_microtasks(nested);

    // 2. Run the tasks that were queued before this turn started. A task queued
    //    by one of them waits for the next turn, so a task that keeps re-posting
    //    itself cannot starve the timers.
    size_t pending_tasks = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pending_tasks = m_task_queue.size();
    }
    for (size_t i = 0; i < pending_tasks; ++i) {
        QueuedCallback task;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_task_queue.empty()) break;
            task = std::move(m_task_queue.front());
            m_task_queue.pop_front();
        }
        run_callback(task);
        executed_count++;
        // Microtask checkpoint after every task, as HTML specifies.
        executed_count += run_microtasks(nested);
    }

    // 3. Identify due timers
    std::vector<TimerTask> due_timers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& t : m_timers) {
            if (t.cancelled || t.consumed || t.trigger_time > now) continue;
            due_timers.push_back(t);
            if (t.is_interval) {
                t.trigger_time = now + t.interval;
            } else {
                t.consumed = true;      // a one-shot fires once
            }
        }
    }

    // Timers fire in the order they come due, not the order they were set:
    // setTimeout(a, 20); setTimeout(b, 5) runs b first even when both are late.
    std::stable_sort(due_timers.begin(), due_timers.end(), [](const TimerTask& a, const TimerTask& b) {
        if (a.trigger_time != b.trigger_time) return a.trigger_time < b.trigger_time;
        return a.sequence < b.sequence;
    });

    for (auto& dt : due_timers) {
        // An earlier callback in this batch may have cleared this timer.
        bool still_active = true;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& t : m_timers) {
                if (t.id == dt.id) {
                    still_active = !t.cancelled;
                    break;
                }
            }
        }
        if (!still_active || !dt.callback) continue;
        QueuedCallback callback{dt.callback, dt.roots};
        run_callback(callback);
        executed_count++;
        executed_count += run_microtasks(nested);
    }

    // 4. Drop finished timers. A nested turn leaves this to the outermost one,
    //    which may still need to look its due timers up.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_turn_depth == 1) {
            m_timers.erase(
                std::remove_if(m_timers.begin(), m_timers.end(), [](const TimerTask& t) {
                    return t.cancelled || t.consumed;
                }),
                m_timers.end()
            );
        }
    }

    return executed_count;
}

bool EventLoop::has_pending_tasks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_task_queue.empty() || !m_microtask_queue.empty()) return true;
    for (const auto& t : m_timers) {
        if (!t.cancelled && !t.consumed) return true;
    }
    return false;
}

size_t EventLoop::active_timer_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& t : m_timers) {
        if (!t.cancelled && !t.consumed) count++;
    }
    return count;
}

std::optional<std::chrono::milliseconds> EventLoop::time_until_next(std::chrono::steady_clock::time_point now) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_task_queue.empty() || !m_microtask_queue.empty()) {
        return std::chrono::milliseconds(0);
    }

    std::optional<std::chrono::milliseconds> min_delay;
    for (const auto& t : m_timers) {
        if (t.cancelled || t.consumed) continue;
        if (t.trigger_time <= now) {
            return std::chrono::milliseconds(0);
        }
        // Round up: a timer 0.4 ms away is not due yet.
        auto diff = std::chrono::ceil<std::chrono::milliseconds>(t.trigger_time - now);
        if (!min_delay.has_value() || diff < *min_delay) {
            min_delay = diff;
        }
    }
    return min_delay;
}

void EventLoop::wait_for_work(bool allow_skip) {
    auto wait = time_until_next(now());
    if (wait && wait->count() > 0 && (allow_skip || m_time_skipping)) {
        skip_time(*wait);
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

size_t EventLoop::run_until_idle(std::chrono::milliseconds budget, size_t max_callbacks,
                                 std::chrono::milliseconds wall_limit) {
    const auto start = now();
    const auto wall_deadline = std::chrono::steady_clock::now() + wall_limit;
    size_t executed = 0;
    while (executed < max_callbacks && std::chrono::steady_clock::now() < wall_deadline) {
        executed += tick(now());
        if (!has_pending_tasks()) break;
        const auto current = now();
        auto wait = time_until_next(current);
        if (!wait) break;
        if (wait->count() <= 0) continue;                // more work is due right now
        const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(current - start);
        if (spent + *wait > budget) break;               // the next timer lies beyond the budget
        skip_time(*wait);
    }
    return executed;
}

void EventLoop::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.clear();
    m_task_queue.clear();
    m_microtask_queue.clear();
}

} // namespace litebird
