#include "litebird/js/EventLoop.hpp"

namespace litebird {

EventLoop& EventLoop::default_loop() {
    static EventLoop s_loop;
    return s_loop;
}

uint32_t EventLoop::set_timeout(TimerCallback cb, int delay_ms) {
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
    for (auto& t : m_timers) {
        if (t.id == id) {
            t.cancelled = true;
            break;
        }
    }
}

void EventLoop::post_task(std::function<void()> task) {
    if (task) {
        m_task_queue.push_back(std::move(task));
    }
}

size_t EventLoop::tick(std::chrono::steady_clock::time_point now) {
    size_t executed_count = 0;

    // 1. Drain microtasks / immediate task queue
    if (!m_task_queue.empty()) {
        auto tasks = std::move(m_task_queue);
        m_task_queue.clear();
        for (auto& task : tasks) {
            if (task) {
                task();
                executed_count++;
            }
        }
    }

    // 2. Identify due timers
    std::vector<TimerTask> due_timers;
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

    // Execute due timers
    for (const auto& dt : due_timers) {
        // Re-check if this timer was cancelled by an earlier timer in the same batch
        bool still_active = true;
        for (const auto& t : m_timers) {
            if (t.id == dt.id && t.cancelled && !dt.is_interval) {
                // Was one-shot and already marked
            } else if (t.id == dt.id && t.cancelled) {
                still_active = false;
                break;
            }
        }
        if (still_active && dt.callback) {
            dt.callback();
            executed_count++;
        }
    }

    // 3. Remove cancelled timers
    m_timers.erase(
        std::remove_if(m_timers.begin(), m_timers.end(), [](const TimerTask& t) {
            return t.cancelled;
        }),
        m_timers.end()
    );

    return executed_count;
}

bool EventLoop::has_pending_tasks() const {
    if (!m_task_queue.empty()) return true;
    for (const auto& t : m_timers) {
        if (!t.cancelled) return true;
    }
    return false;
}

size_t EventLoop::active_timer_count() const {
    size_t count = 0;
    for (const auto& t : m_timers) {
        if (!t.cancelled) count++;
    }
    return count;
}

std::optional<std::chrono::milliseconds> EventLoop::time_until_next(std::chrono::steady_clock::time_point now) const {
    if (!m_task_queue.empty()) {
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
    m_timers.clear();
    m_task_queue.clear();
}

} // namespace litebird
