#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <vector>

namespace litebird {

class WorkerObject : public std::enable_shared_from_this<WorkerObject> {
public:
    WorkerObject(std::string script_url, ResourceRequestContext context);
    ~WorkerObject();

    void start();
    void post_message(const std::string& serialized_json);
    void terminate();

    bool is_running() const { return m_running; }

    using ContextProvider = std::function<ResourceRequestContext()>;
    static JsValue create_constructor(ContextProvider context_provider);
    static JsValue create_js_instance(std::string_view script_url,
                                      ResourceRequestContext context);

    void set_onmessage(JsValue handler) { m_onmessage = handler; }
    JsValue onmessage() const { return m_onmessage; }

    void set_onerror(JsValue handler) { m_onerror = handler; }
    JsValue onerror() const { return m_onerror; }

    void add_event_listener(const std::string& event, JsValue listener);

private:
    void worker_thread_main(std::string script_source);
    void post_error(const std::string& message);

    std::string m_script_url;
    ResourceRequestContext m_context;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_terminated{false};

    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::queue<std::string> m_incoming_messages;

    /// The event loop of the page that created the worker. Messages and errors
    /// from the worker are delivered there, on the page's thread.
    class EventLoop* m_owner_loop{nullptr};

    JsValue m_onmessage{JsValue::make_undefined()};
    JsValue m_onerror{JsValue::make_undefined()};
    std::vector<JsValue> m_message_listeners;
    std::vector<JsValue> m_error_listeners;
};

} // namespace litebird
