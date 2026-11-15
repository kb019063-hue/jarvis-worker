#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace litebird {

struct WorkerOwnerLoopState;
struct WorkerOwnerJsState;

enum class WorkerType {
    Classic,
    Module
};

struct WorkerOptions {
    WorkerType type{WorkerType::Classic};
    std::string name;
};

class WorkerObject : public std::enable_shared_from_this<WorkerObject> {
public:
    WorkerObject(std::string script_url,
                 ResourceRequestContext context,
                 WorkerOptions options = {});
    ~WorkerObject();

    void start();
    void post_message(const std::string& serialized_json);
    void terminate();

    bool is_running() const { return m_running.load(std::memory_order_acquire); }
    const WorkerOptions& options() const { return m_options; }

    using ContextProvider = std::function<ResourceRequestContext()>;
    static JsValue create_constructor(ContextProvider context_provider);
    static JsValue create_js_instance(std::string_view script_url,
                                      ResourceRequestContext context,
                                      WorkerOptions options = {});

    /// WHATWG MIME Sniffing §4.3: true iff the MIME type essence (ignoring
    /// parameters and surrounding ASCII HTTP whitespace, case-insensitively)
    /// is one of the 16 standard JavaScript MIME types.
    static bool is_javascript_mime_type(std::string_view content_type);

    /// Terminates and joins every currently active worker thread.
    static void shutdown_all_workers();

    void set_onmessage(JsValue handler);
    JsValue onmessage() const;

    void set_onerror(JsValue handler);
    JsValue onerror() const;

    void add_event_listener(const std::string& event, JsValue listener, bool once = false);
    void remove_event_listener(const std::string& event, const JsValue& listener);

private:
    static JsValue attach_js_instance(const std::shared_ptr<WorkerObject>& worker);
    void worker_thread_main(std::string script_source);
    void post_to_owner(std::function<void()> task);
    void post_error(const std::string& message,
                    const std::string& filename = {},
                    int lineno = 0,
                    int colno = 0,
                    bool release_after = false);
    void dispatch_page_error(const std::string& message,
                             const std::string& filename,
                             int lineno,
                             int colno);
    void dispatch_page_message(const std::string& serialized_json);
    void dispatch_page_custom_event(const std::string& type, const JsValue& event);
    void release_owner_js();
    void register_child(const std::shared_ptr<WorkerObject>& child);
    void terminate_children();

    std::string m_script_url;
    std::string m_resolved_url;
    ResourceRequestContext m_context;
    WorkerOptions m_options;

    std::mutex m_thread_mutex;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_terminated{false};

    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::queue<std::string> m_incoming_messages;
    bool m_owner_task_pending{false};

    std::mutex m_children_mutex;
    std::vector<std::weak_ptr<WorkerObject>> m_children;

    /// Lifetime-guarded reference to the EventLoop of the thread that created
    /// this worker (the main page loop or a parent worker's loop).
    std::shared_ptr<WorkerOwnerLoopState> m_owner_state;
    std::thread::id m_owner_thread_id;

    /// Owner-thread-only JS values (the Worker JS wrapper, onmessage/onerror
    /// handlers, and EventTarget listener lists). Accessed and destroyed only
    /// on the owner thread so non-atomic JsObject handle counts never race.
    std::shared_ptr<WorkerOwnerJsState> m_owner_js;
};

} // namespace litebird
