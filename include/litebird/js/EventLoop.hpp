#pragma once

#include <chrono>
#include <functional>
#include <vector>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <mutex>
#include <memory>
#include <utility>
#include "litebird/js/JsValue.hpp"

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
    void post_microtask(std::function<void()> microtask);

    // Queued callbacks keep copies of JsValues (promise handlers, captured
    // closures, queueMicrotask arguments) inside std::function storage, which the
    // collector cannot inspect. An owner registers a provider that pushes the
    // addresses of those live values; the provider stays valid until the owner
    // withdraws it. Keyed by owner so multiple independent owners can contribute.
    using GcRootProvider = std::function<void(std::vector<JsValue*>&)>;
    void add_gc_root_provider(const void* owner, GcRootProvider provider);
    void remove_gc_root_provider(const void* owner);
    void collect_gc_roots(std::vector<JsValue*>& roots) const;
    size_t run_microtasks();

    // Processes all tasks and timers ready at 'now'.
    // Returns the total number of callbacks and tasks executed.
    size_t tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    bool has_pending_tasks() const;
    size_t active_timer_count() const;
    std::optional<std::chrono::milliseconds> time_until_next(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

    void clear();

private:
    uint32_t m_next_id{1};
    mutable std::mutex m_mutex;
    std::vector<TimerTask> m_timers;
    std::vector<std::function<void()>> m_task_queue;
    std::vector<std::function<void()>> m_microtask_queue;
    std::vector<std::pair<const void*, GcRootProvider>> m_gc_root_providers;
};

} // namespace litebird
