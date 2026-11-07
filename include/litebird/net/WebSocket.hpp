#pragma once

#include "litebird/js/JsValue.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

namespace litebird {

enum class WebSocketReadyState {
    Connecting = 0,
    Open = 1,
    Closing = 2,
    Closed = 3
};

class WebSocketObject : public std::enable_shared_from_this<WebSocketObject> {
public:
    explicit WebSocketObject(std::string url, std::string protocols = "");
    ~WebSocketObject();

    void connect();
    void send(const std::string& message);
    void close(int code = 1000, const std::string& reason = "");

    WebSocketReadyState ready_state() const { return m_ready_state.load(); }
    const std::string& url() const { return m_url; }
    const std::string& protocol() const { return m_protocol; }
    uint32_t buffered_amount() const { return 0; }

    static JsValue create_constructor();
    static JsValue create_js_instance(std::string_view url, std::string_view protocols = "");

    void set_onopen(JsValue cb) { m_onopen = cb; }
    void set_onmessage(JsValue cb) { m_onmessage = cb; }
    void set_onerror(JsValue cb) { m_onerror = cb; }
    void set_onclose(JsValue cb) { m_onclose = cb; }

    void add_event_listener(const std::string& type, JsValue listener);

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

private:
    void receiver_thread_loop();
    bool perform_handshake(int sock);
    bool read_exact(int sock, void* buf, size_t len);

    std::string m_url;
    std::string m_protocol;
    std::string m_host;
    uint16_t m_port{80};
    std::string m_path{"/"};

    int m_socket_fd{-1};
    std::atomic<WebSocketReadyState> m_ready_state{WebSocketReadyState::Connecting};
    std::atomic<bool> m_closed{false};
    int m_close_code{1000};
    std::string m_close_reason;

    std::thread m_receiver_thread;
    mutable std::mutex m_send_mutex;

    JsValue m_onopen{JsValue::make_undefined()};
    JsValue m_onmessage{JsValue::make_undefined()};
    JsValue m_onerror{JsValue::make_undefined()};
    JsValue m_onclose{JsValue::make_undefined()};

    std::vector<JsValue> m_open_listeners;
    std::vector<JsValue> m_message_listeners;
    std::vector<JsValue> m_error_listeners;
    std::vector<JsValue> m_close_listeners;
};

} // namespace litebird
