#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"

namespace litebird {

EventLoop& EventLoop::default_loop() {
    static EventLoop s_loop;
    // Deliberately leaked: the event loop outlives every interpreter, and a
    // static destructor would run after the heap it reports roots into.
    static bool registered = [] {
        Heap::default_heap().add_root_provider(
            &s_loop, [](std::vector<JsValue*>& roots) { s_loop.collect_gc_roots(roots); });
        return true;
    }();
    (void)registered;
    return s_loop;
}

uint32_t EventLoop::set_timeout(TimerCallback cb, int delay_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t id = m_next_id++;
    auto now = std::chrono::steady_clock::now();
    TimerTask task;
    task.id = id;
    task.trigger_time = now + std::chrono::milliseconds(std::max(0, delay_ms));
    task.is_interval = false;
    task.callback = std::move(cb);
    task.cancelled = false;
    m_timers.push_back(std::move(task));
    return id;
}

uint32_t EventLoop::set_interval(TimerCallback cb, int interval_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t id = m_next_id++;
    int delay = std::max(1, interval_ms);
    auto now = std::chrono::steady_clock::now();
    TimerTask task;
    task.id = id;
    task.trigger_time = now + std::chrono::milliseconds(delay);
    task.interval = std::chrono::milliseconds(delay);
    task.is_interval = true;
    task.callback = std::move(cb);
    task.cancelled = false;
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

void EventLoop::post_task(std::function<void()> task) {
    if (task) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_task_queue.push_back(std::move(task));
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
}

void EventLoop::post_microtask(std::function<void()> microtask) {
    if (microtask) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_microtask_queue.push_back(std::move(microtask));
    }
}

size_t EventLoop::run_microtasks() {
    size_t count = 0;
    while (true) {
        std::vector<std::function<void()>> microtasks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_microtask_queue.empty()) break;
            microtasks = std::move(m_microtask_queue);
            m_microtask_queue.clear();
        }
        for (auto& task : microtasks) {
            if (task) {
                task();
                count++;
            }
        }
    }
    return count;
}

size_t EventLoop::tick(std::chrono::steady_clock::time_point now) {
    size_t executed_count = 0;

    // 1. Drain microtask queue first
    executed_count += run_microtasks();

    // 2. Drain macrotask immediate queue
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_task_queue.empty()) {
            tasks = std::move(m_task_queue);
            m_task_queue.clear();
        }
    }
    for (auto& task : tasks) {
        if (task) {
            task();
            executed_count++;
        }
    }
    // Microtask checkpoint after task
    executed_count += run_microtasks();

    // 3. Identify due timers
    std::vector<TimerTask> due_timers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& t : m_timers) {
            if (!t.cancelled && t.trigger_time <= now) {
                due_timers.push_back(t);
                if (t.is_interval && !t.cancelled) {
                    t.trigger_time = now + t.interval;
                } else {
                    t.cancelled = true; // Mark one-shot timer as consumed
                }
            }
        }
    }

    // Execute due timers
    for (const auto& dt : due_timers) {
        bool still_active = true;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& t : m_timers) {
                if (t.id == dt.id && t.cancelled && !dt.is_interval) {
                    // Was one-shot and already marked
                } else if (t.id == dt.id && t.cancelled) {
                    still_active = false;
                    break;
                }
            }
        }
        if (still_active && dt.callback) {
            dt.callback();
            executed_count++;
            executed_count += run_microtasks();
        }
    }

    // 4. Remove cancelled timers
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timers.erase(
            std::remove_if(m_timers.begin(), m_timers.end(), [](const TimerTask& t) {
                return t.cancelled;
            }),
            m_timers.end()
        );
    }

    return executed_count;
}

bool EventLoop::has_pending_tasks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_task_queue.empty() || !m_microtask_queue.empty()) return true;
    for (const auto& t : m_timers) {
        if (!t.cancelled) return true;
    }
    return false;
}

size_t EventLoop::active_timer_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& t : m_timers) {
        if (!t.cancelled) count++;
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
        if (t.cancelled) continue;
        if (t.trigger_time <= now) {
            return std::chrono::milliseconds(0);
        }
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(t.trigger_time - now);
        if (!min_delay.has_value() || diff < *min_delay) {
            min_delay = diff;
        }
    }
    return min_delay;
}

void EventLoop::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.clear();
    m_task_queue.clear();
    m_microtask_queue.clear();
}

} // namespace litebird
