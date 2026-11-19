#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace litebird {

class EventLoop;

enum class WebSocketReadyState {
    Connecting = 0,
    Open = 1,
    Closing = 2,
    Closed = 3
};

struct WebSocketContext {
    std::string document_url{"about:blank"};
    std::string base_url{"about:blank"};
    bool opaque_origin{true};

    WebSocketContext() = default;
    WebSocketContext(const ResourceRequestContext& rc, bool opaque = false)
        : document_url(rc.document_url.empty() ? std::string("about:blank") : rc.document_url),
          base_url(rc.base_url.empty() ? document_url : rc.base_url),
          opaque_origin(opaque) {}
};

struct WebSocketEventSink {
    std::mutex mutex;
    EventLoop* loop{nullptr};
    bool alive{true};
    std::shared_ptr<std::atomic<bool>> owner_alive;
    std::function<void()> wake;
    std::vector<std::weak_ptr<class WebSocketObject>> attached;

    bool is_owner_alive() const {
        return alive && (!owner_alive || owner_alive->load(std::memory_order_acquire));
    }

    void shutdown();
};

class WebSocketObject : public std::enable_shared_from_this<WebSocketObject> {
public:
    explicit WebSocketObject(std::string url,
                             std::string protocols = "",
                             WebSocketContext context = {},
                             std::shared_ptr<WebSocketEventSink> event_sink = nullptr);

    WebSocketObject(std::string url,
                    std::vector<std::string> protocols,
                    WebSocketContext context = {},
                    std::shared_ptr<WebSocketEventSink> event_sink = nullptr);

    ~WebSocketObject();

    void connect();
    void send(const std::string& message);
    void send_binary(const std::vector<uint8_t>& data);
    void close(int code = 1000, const std::string& reason = "");
    void request_stop();
    void join_for_shutdown();

    WebSocketReadyState ready_state() const { return m_ready_state.load(std::memory_order_acquire); }
    WebSocketReadyState js_ready_state() const { return m_js_ready_state.load(std::memory_order_acquire); }
    const std::string& url() const { return m_url; }
    std::string protocol() const;
    std::string js_protocol() const;
    std::string extensions() const { return {}; }
    std::string binary_type() const;
    void set_binary_type(std::string_view type);
    uint64_t buffered_amount() const { return m_buffered_amount.load(std::memory_order_acquire); }
    std::string origin_for_event() const;

    static JsValue create_constructor(
        std::function<WebSocketContext()> context_fn = {},
        std::shared_ptr<WebSocketEventSink> event_sink = nullptr);

    static JsValue create_js_instance(
        std::string_view url,
        std::string_view protocols = "",
        const WebSocketContext& context = {},
        const std::shared_ptr<WebSocketEventSink>& event_sink = nullptr);

    static void set_ca_bundle_for_testing(std::string ca_file_path);
    static std::string ca_bundle_for_testing();
    static bool is_valid_utf8(std::string_view bytes);

    void set_onopen(JsValue cb);
    void set_onmessage(JsValue cb);
    void set_onerror(JsValue cb);
    void set_onclose(JsValue cb);

    void add_event_listener(const std::string& type, JsValue listener, bool once = false);
    void remove_event_listener(const std::string& type, const JsValue& listener);

    // Testing helper: create in-process loopback WebSocket server
    struct LoopbackServer {
        int listen_fd{-1};
        uint16_t port{0};
        std::atomic<bool> running{true};
        std::thread thread;
        ~LoopbackServer();
    };
    static std::shared_ptr<LoopbackServer> start_loopback_server(std::function<void(int client_fd)> on_client);

    // Utility helpers for RFC 6455
    static std::string calculate_accept_key(const std::string& client_key);
    static std::string sha1_digest(const std::string& input);
    static std::string base64_encode(const std::string& input);
    static void random_bytes(uint8_t* dst, size_t len);

private:
    struct OutboundFrame {
        std::vector<uint8_t> bytes;
        size_t payload_bytes{0};
        bool is_data{false};
        bool is_close{false};
    };

    struct JsOwnerState;

    void init_parsed_request(std::vector<std::string> protocols, const WebSocketContext& context);
    void post_to_owner(std::function<void()> task);
    void enqueue_frame(uint8_t opcode, const uint8_t* data, size_t len, bool is_data, bool is_close = false);
    void wake_io_thread();
    void receiver_thread_loop();
    void dispatch_open_on_owner(const std::string& selected_protocol);
    void dispatch_message_on_owner(const std::string& payload, bool is_binary);
    void dispatch_error_and_close_on_owner(const std::string& err_msg, int close_code,
                                           const std::string& close_reason, bool was_clean,
                                           bool emit_error);
    void release_owner_js_on_owner();

    std::string m_raw_url;
    std::string m_url;
    std::vector<std::string> m_offered_protocols;
    std::string m_origin_header{"null"};
    std::string m_event_origin;

    bool m_request_valid{false};
    bool m_mixed_content_blocked{false};
    bool m_secure{false};
    bool m_host_is_ip{false};
    std::string m_validation_error;
    std::string m_validation_dom_error{"SyntaxError"};
    std::string m_connect_host;
    std::string m_host_header;
    uint16_t m_port{80};
    std::string m_path{"/"};

    int m_wake_fds[2]{-1, -1};
    std::atomic<WebSocketReadyState> m_ready_state{WebSocketReadyState::Connecting};
    std::atomic<WebSocketReadyState> m_js_ready_state{WebSocketReadyState::Connecting};
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_close_initiated{false};
    std::atomic<uint64_t> m_buffered_amount{0};

    mutable std::mutex m_state_mutex;
    std::string m_selected_protocol;
    std::string m_js_selected_protocol;
    std::string m_binary_type{"blob"};
    int m_pending_close_code{1000};
    std::string m_pending_close_reason;

    std::mutex m_outbox_mutex;
    std::vector<OutboundFrame> m_outbox;

    std::mutex m_thread_mutex;
    std::thread m_receiver_thread;

    std::thread::id m_owner_thread_id;
    std::shared_ptr<WebSocketEventSink> m_event_sink;
    std::shared_ptr<JsOwnerState> m_owner_js;
};

} // namespace litebird
