#pragma once

#include <chrono>
#include <functional>
#include <vector>
#include <optional>
#include <cstdint>
#include <algorithm>

namespace litebird {

using TimerCallback = std::function<void()>;

struct TimerTask {
    uint32_t id{0};
    std::chrono::steady_clock::time_point trigger_time;
    std::chrono::milliseconds interval{0};
    bool is_interval{false};
    TimerCallback callback;
    bool cancelled{false};
};

class EventLoop {
public:
    EventLoop() = default;

    static EventLoop& default_loop();

    uint32_t set_timeout(TimerCallback cb, int delay_ms);
    uint32_t set_interval(TimerCallback cb, int interval_ms);
    void clear_timer(uint32_t id);

    void post_task(std::function<void()> task);

    // Processes all tasks and timers ready at 'now'.
    // Returns the total number of callbacks and tasks executed.
    size_t tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    bool has_pending_tasks() const;
    size_t active_timer_count() const;
    std::optional<std::chrono::milliseconds> time_until_next(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

    void clear();

private:
    uint32_t m_next_id{1};
    std::vector<TimerTask> m_timers;
    std::vector<std::function<void()>> m_task_queue;
};

} // namespace litebird
