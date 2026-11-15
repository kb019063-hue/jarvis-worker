#pragma once

#include "litebird/shell/BrowserShell.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace litebird {

struct HttpServerRequest {
    std::string method;
    std::string path;       ///< Path without query string
    std::string query;      ///< Raw query string without '?'
    std::string raw_target; ///< Original request target
    std::unordered_map<std::string, std::string> headers; ///< Lowercase header names
    std::string body;
};

struct HttpServerResponse {
    int status{200};
    std::string status_text{"OK"};
    std::string content_type{"text/plain; charset=utf-8"};
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    bool chunked{false};
};

/// Embedded multi-threaded HTTP/1.1 graphical browser server (`--gui`) bound to
/// `0.0.0.0` (or `127.0.0.1` for hermetic unit tests). Drives a live `BrowserShell`
/// instance and streams PNG viewport frames rasterized by Litebird's C++20 engine.
class WebBrowserServer {
public:
    using CustomRouteHandler = std::function<bool(const HttpServerRequest&, HttpServerResponse&)>;

    WebBrowserServer(float viewport_w = 1120.0f, float viewport_h = 700.0f);
    ~WebBrowserServer();

    WebBrowserServer(const WebBrowserServer&) = delete;
    WebBrowserServer& operator=(const WebBrowserServer&) = delete;

    /// Navigates the underlying `BrowserShell` before or during server execution.
    bool navigate_to(const std::string& url);

    /// Registers an additional stateless HTTP route handler (used by `/demo/*`
    /// and hermetic network integration tests). Invoked without holding `m_shell_mutex`.
    void set_custom_handler(CustomRouteHandler handler);

    /// Starts listening on `host:port` (`port == 0` picks an ephemeral port) in a
    /// background accept thread. Returns false if bind/listen fails.
    bool start_background(uint16_t port = 8080, const std::string& bind_host = "0.0.0.0");

    /// Runs the accept loop synchronously on the calling thread until `stop()`.
    bool run_blocking(uint16_t port = 8080, const std::string& bind_host = "0.0.0.0");

    /// Stops the listening socket and waits for the accept thread to exit.
    void stop();

    uint16_t bound_port() const { return m_bound_port.load(); }
    bool is_running() const { return m_running.load(); }

private:
    bool open_listener(uint16_t port, const std::string& bind_host);
    void accept_loop();
    void handle_client(int client_fd);
    HttpServerResponse dispatch_request(const HttpServerRequest& req);
    bool serve_demo_route(const HttpServerRequest& req, HttpServerResponse& resp);

    mutable std::mutex m_shell_mutex;
    BrowserShell m_shell;
    uint64_t m_frame_revision{1};
    std::vector<uint8_t> m_cached_png;
    std::string m_hovered_summary;

    std::mutex m_handler_mutex;
    CustomRouteHandler m_custom_handler;

    int m_listen_fd{-1};
    std::atomic<bool> m_running{false};
    std::atomic<uint16_t> m_bound_port{0};
    std::atomic<int> m_active_clients{0};
    std::thread m_accept_thread;
};

} // namespace litebird
