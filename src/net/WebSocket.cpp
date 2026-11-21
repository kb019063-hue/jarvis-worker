#include "litebird/net/WebSocket.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/UrlParser.hpp"

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace litebird {

namespace {

constexpr size_t kMaxHandshakeBytes = 65536;
constexpr size_t kMaxMessageBytes = 16u * 1024u * 1024u; // 16 MiB per frame/message
constexpr int kConnectTimeoutMs = 5000;
constexpr int kHandshakeTimeoutMs = 5000;
constexpr int kCloseHandshakeTimeoutMs = 1500;

void ensure_sigpipe_ignored() {
#ifdef SIGPIPE
    static const bool installed = []() {
        struct sigaction old_sa{};
        if (::sigaction(SIGPIPE, nullptr, &old_sa) == 0 && old_sa.sa_handler == SIG_DFL) {
            struct sigaction sa{};
            sa.sa_handler = SIG_IGN;
            sigemptyset(&sa.sa_mask);
            (void)::sigaction(SIGPIPE, &sa, nullptr);
        }
        return true;
    }();
    (void)installed;
#endif
}

std::mutex& ca_bundle_mutex() {
    static auto* m = new std::mutex();
    return *m;
}

std::string& ca_bundle_storage() {
    static auto* s = new std::string();
    return *s;
}

std::string effective_ca_bundle() {
    {
        std::lock_guard<std::mutex> lock(ca_bundle_mutex());
        if (!ca_bundle_storage().empty()) return ca_bundle_storage();
    }
    if (const char* env = std::getenv("LITEBIRD_SSL_CA_FILE"); env && *env) {
        return std::string(env);
    }
    return {};
}

// Runtime-loaded OpenSSL 3 / 1.1 client bindings (no -dev headers needed).
struct WsOpenSslApi {
    using SSL_METHOD = void;
    using SSL_CTX = void;
    using SSL = void;
    using X509_VERIFY_PARAM = void;

    void* ssl_handle{nullptr};
    void* crypto_handle{nullptr};
    bool available{false};

    const SSL_METHOD* (*TLS_client_method_fn)() = nullptr;
    SSL_CTX* (*SSL_CTX_new_fn)(const SSL_METHOD*) = nullptr;
    void (*SSL_CTX_free_fn)(SSL_CTX*) = nullptr;
    long (*SSL_CTX_ctrl_fn)(SSL_CTX*, int, long, void*) = nullptr;
    int (*SSL_CTX_set_default_verify_paths_fn)(SSL_CTX*) = nullptr;
    int (*SSL_CTX_load_verify_locations_fn)(SSL_CTX*, const char*, const char*) = nullptr;
    void (*SSL_CTX_set_verify_fn)(SSL_CTX*, int, void*) = nullptr;
    SSL* (*SSL_new_fn)(SSL_CTX*) = nullptr;
    void (*SSL_free_fn)(SSL*) = nullptr;
    int (*SSL_set_fd_fn)(SSL*, int) = nullptr;
    long (*SSL_ctrl_fn)(SSL*, int, long, void*) = nullptr;
    int (*SSL_set1_host_fn)(SSL*, const char*) = nullptr;
    X509_VERIFY_PARAM* (*SSL_get0_param_fn)(SSL*) = nullptr;
    int (*X509_VERIFY_PARAM_set1_ip_asc_fn)(X509_VERIFY_PARAM*, const char*) = nullptr;
    int (*SSL_connect_fn)(SSL*) = nullptr;
    int (*SSL_read_fn)(SSL*, void*, int) = nullptr;
    int (*SSL_write_fn)(SSL*, const void*, int) = nullptr;
    int (*SSL_pending_fn)(const SSL*) = nullptr;
    int (*SSL_shutdown_fn)(SSL*) = nullptr;
    int (*SSL_get_error_fn)(const SSL*, int) = nullptr;
    long (*SSL_get_verify_result_fn)(const SSL*) = nullptr;
    // Optional, as in the loader: without it the message keeps the code but
    // loses the words. It never takes the socket's TLS layer with it.
    const char* (*X509_verify_cert_error_string_fn)(long) = nullptr;
    int (*RAND_bytes_fn)(unsigned char*, int) = nullptr;

    static const WsOpenSslApi& instance() {
        static const WsOpenSslApi* api = []() {
            auto* out = new WsOpenSslApi();
            static constexpr const char* kSslCandidates[] = {
                "libssl.so.3",
                "libssl.so.1.1",
                "libssl.so",
                "/usr/lib/x86_64-linux-gnu/libssl.so.3",
                "/lib/x86_64-linux-gnu/libssl.so.3",
                "/usr/lib/aarch64-linux-gnu/libssl.so.3"
            };
            static constexpr const char* kCryptoCandidates[] = {
                "libcrypto.so.3",
                "libcrypto.so.1.1",
                "libcrypto.so",
                "/usr/lib/x86_64-linux-gnu/libcrypto.so.3",
                "/lib/x86_64-linux-gnu/libcrypto.so.3",
                "/usr/lib/aarch64-linux-gnu/libcrypto.so.3"
            };
            for (const char* name : kSslCandidates) {
                out->ssl_handle = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
                if (out->ssl_handle) break;
            }
            for (const char* name : kCryptoCandidates) {
                out->crypto_handle = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
                if (out->crypto_handle) break;
            }
            if (!out->ssl_handle) return out;

            auto load_ssl = [&](auto& fn, const char* sym) -> bool {
                void* p = ::dlsym(out->ssl_handle, sym);
                if (!p && out->crypto_handle) p = ::dlsym(out->crypto_handle, sym);
                if (!p) return false;
                std::memcpy(&fn, &p, sizeof(p));
                return true;
            };

            const bool ok =
                load_ssl(out->TLS_client_method_fn, "TLS_client_method") &&
                load_ssl(out->SSL_CTX_new_fn, "SSL_CTX_new") &&
                load_ssl(out->SSL_CTX_free_fn, "SSL_CTX_free") &&
                load_ssl(out->SSL_CTX_set_default_verify_paths_fn, "SSL_CTX_set_default_verify_paths") &&
                load_ssl(out->SSL_CTX_load_verify_locations_fn, "SSL_CTX_load_verify_locations") &&
                load_ssl(out->SSL_CTX_set_verify_fn, "SSL_CTX_set_verify") &&
                load_ssl(out->SSL_new_fn, "SSL_new") &&
                load_ssl(out->SSL_free_fn, "SSL_free") &&
                load_ssl(out->SSL_set_fd_fn, "SSL_set_fd") &&
                load_ssl(out->SSL_ctrl_fn, "SSL_ctrl") &&
                load_ssl(out->SSL_set1_host_fn, "SSL_set1_host") &&
                load_ssl(out->SSL_get0_param_fn, "SSL_get0_param") &&
                load_ssl(out->X509_VERIFY_PARAM_set1_ip_asc_fn, "X509_VERIFY_PARAM_set1_ip_asc") &&
                load_ssl(out->SSL_connect_fn, "SSL_connect") &&
                load_ssl(out->SSL_read_fn, "SSL_read") &&
                load_ssl(out->SSL_write_fn, "SSL_write") &&
                load_ssl(out->SSL_shutdown_fn, "SSL_shutdown") &&
                load_ssl(out->SSL_get_error_fn, "SSL_get_error") &&
                load_ssl(out->SSL_get_verify_result_fn, "SSL_get_verify_result");

            (void)load_ssl(out->SSL_CTX_ctrl_fn, "SSL_CTX_ctrl");
            (void)load_ssl(out->SSL_pending_fn, "SSL_pending");
            (void)load_ssl(out->X509_verify_cert_error_string_fn, "X509_verify_cert_error_string");
            (void)load_ssl(out->RAND_bytes_fn, "RAND_bytes");
            out->available = ok;
            return out;
        }();
        return *api;
    }
};

// ---------------------------------------------------------------------------
// Process-wide exit safety & active WebSocket registry
// ---------------------------------------------------------------------------

std::atomic<bool> g_ws_shutting_down{false};

std::shared_ptr<WebSocketEventSink> main_ws_event_sink() {
    static auto* holder = new std::shared_ptr<WebSocketEventSink>([]() {
        auto s = std::make_shared<WebSocketEventSink>();
        s->loop = &EventLoop::default_loop();
        s->alive = true;
        return s;
    }());
    return *holder;
}

struct WebSocketRegistry {
    std::mutex mutex;
    std::vector<std::weak_ptr<WebSocketObject>> sockets;

    static WebSocketRegistry& instance() {
        static auto* reg = new WebSocketRegistry();
        return *reg;
    }

    void add(const std::shared_ptr<WebSocketObject>& ws) {
        std::lock_guard<std::mutex> lock(mutex);
        sockets.erase(
            std::remove_if(sockets.begin(), sockets.end(),
                           [](const std::weak_ptr<WebSocketObject>& w) { return w.expired(); }),
            sockets.end());
        sockets.push_back(ws);
    }

    std::vector<std::shared_ptr<WebSocketObject>> take_live() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::shared_ptr<WebSocketObject>> live;
        for (const auto& w : sockets) {
            if (auto s = w.lock()) live.push_back(std::move(s));
        }
        sockets.clear();
        return live;
    }
};

void atexit_shutdown_websockets() {
    g_ws_shutting_down.store(true, std::memory_order_release);
    main_ws_event_sink()->shutdown();
    while (true) {
        auto live = WebSocketRegistry::instance().take_live();
        if (live.empty()) break;
        for (const auto& ws : live) {
            ws->request_stop();
        }
        for (const auto& ws : live) {
            ws->join_for_shutdown();
        }
    }
}

void ensure_ws_atexit_registered() {
    static std::once_flag once;
    std::call_once(once, []() {
        // Warm up singletons and OpenSSL (so OPENSSL_cleanup is registered via
        // atexit() BEFORE atexit_shutdown_websockets and therefore runs AFTER
        // all WebSocket threads have been stopped and joined).
        (void)Heap::default_heap();
        (void)main_ws_event_sink();
        (void)ca_bundle_mutex();
        (void)ca_bundle_storage();
        const WsOpenSslApi& ssl_api = WsOpenSslApi::instance();
        if (ssl_api.available && ssl_api.TLS_client_method_fn &&
            ssl_api.SSL_CTX_new_fn && ssl_api.SSL_CTX_free_fn) {
            void* warm_ctx = ssl_api.SSL_CTX_new_fn(ssl_api.TLS_client_method_fn());
            if (warm_ctx) ssl_api.SSL_CTX_free_fn(warm_ctx);
        }
        if (ssl_api.RAND_bytes_fn) {
            uint8_t seed_probe = 0;
            (void)ssl_api.RAND_bytes_fn(&seed_probe, 1);
        }
        std::atexit(atexit_shutdown_websockets);
    });
}

// ---------------------------------------------------------------------------
// Parsing, HTTP token, origin, mixed-content, and UTF-8 helpers
// ---------------------------------------------------------------------------

std::string ascii_lower(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string trim_ows(std::string_view sv) {
    size_t b = 0;
    size_t e = sv.size();
    while (b < e && (sv[b] == ' ' || sv[b] == '\t')) ++b;
    while (e > b && (sv[e - 1] == ' ' || sv[e - 1] == '\t')) --e;
    return std::string(sv.substr(b, e - b));
}

bool contains_control_chars(std::string_view sv) {
    for (unsigned char c : sv) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

bool is_http_token_char(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

bool is_http_token(std::string_view sv) {
    if (sv.empty()) return false;
    for (unsigned char c : sv) {
        if (!is_http_token_char(c)) return false;
    }
    return true;
}

bool has_explicit_scheme(std::string_view sv) {
    if (sv.empty()) return false;
    unsigned char first = static_cast<unsigned char>(sv[0]);
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) return false;
    for (size_t i = 1; i < sv.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(sv[i]);
        if (c == ':') return true;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
            continue;
        }
        return false;
    }
    return false;
}

bool header_has_token(std::string_view header_val, std::string_view token_lower) {
    size_t pos = 0;
    while (pos <= header_val.size()) {
        size_t comma = header_val.find(',', pos);
        std::string_view part = comma == std::string_view::npos
            ? header_val.substr(pos)
            : header_val.substr(pos, comma - pos);
        if (ascii_lower(trim_ows(part)) == token_lower) return true;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return false;
}

bool is_ip_literal(const std::string& host_unbracketed) {
    struct in_addr v4{};
    struct in6_addr v6{};
    return ::inet_pton(AF_INET, host_unbracketed.c_str(), &v4) == 1 ||
           ::inet_pton(AF_INET6, host_unbracketed.c_str(), &v6) == 1;
}

bool is_loopback_ip(const std::string& host_unbracketed) {
    struct in_addr v4{};
    if (::inet_pton(AF_INET, host_unbracketed.c_str(), &v4) == 1) {
        const uint32_t addr = ntohl(v4.s_addr);
        return (addr & 0xFF000000u) == 0x7F000000u; // 127.0.0.0/8
    }
    struct in6_addr v6{};
    if (::inet_pton(AF_INET6, host_unbracketed.c_str(), &v6) == 1) {
        static constexpr uint8_t kLoopback6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        return std::memcmp(v6.s6_addr, kLoopback6, 16) == 0;
    }
    return false;
}

bool is_potentially_trustworthy_ws_url(const Url& url, const std::string& connect_host) {
    if (url.scheme == "wss") return true;
    if (url.scheme != "ws") return false;
    const std::string host = ascii_lower(connect_host);
    if (host == "localhost" || host == "localhost.") return true;
    if (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0) return true;
    if (host.size() > 11 && host.compare(host.size() - 11, 11, ".localhost.") == 0) return true;
    return is_loopback_ip(connect_host);
}

bool is_reserved_nonexistent_domain(const std::string& host) {
    const std::string h = ascii_lower(host);
    auto ends_with = [&](std::string_view suffix) {
        return h.size() > suffix.size() && h.compare(h.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return h == "test" || ends_with(".test") ||
           h == "invalid" || ends_with(".invalid");
}

bool is_valid_close_code(int code) {
    if (code == 1000) return true;
    if (code >= 1001 && code <= 1003) return true;
    if (code >= 1007 && code <= 1014) return true;
    if (code >= 3000 && code <= 4999) return true;
    return false;
}

bool parse_protocol_list_string(std::string_view raw, std::vector<std::string>& out, std::string& err) {
    out.clear();
    if (raw.empty()) return true;
    if (contains_control_chars(raw)) {
        err = "Subprotocol header contains control characters";
        return false;
    }
    size_t pos = 0;
    while (pos <= raw.size()) {
        size_t comma = raw.find(',', pos);
        std::string_view part = comma == std::string_view::npos
            ? raw.substr(pos)
            : raw.substr(pos, comma - pos);
        std::string token = trim_ows(part);
        if (!is_http_token(token)) {
            err = "Subprotocol must be a valid non-empty HTTP token";
            return false;
        }
        for (const auto& existing : out) {
            if (existing == token) {
                err = "Duplicate subprotocol '" + token + "'";
                return false;
            }
        }
        out.push_back(std::move(token));
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return true;
}

bool parse_js_protocols_arg(const JsValue& arg, std::vector<std::string>& out, std::string& err) {
    out.clear();
    if (arg.is_undefined()) return true;

    auto push_token = [&](std::string token) -> bool {
        if (contains_control_chars(token) || !is_http_token(token)) {
            err = "Subprotocol '" + token + "' is invalid";
            return false;
        }
        for (const auto& existing : out) {
            if (existing == token) {
                err = "Duplicate subprotocol '" + token + "'";
                return false;
            }
        }
        out.push_back(std::move(token));
        return true;
    };

    if (arg.is_object_like() && arg.object_val && arg.object_val->is_array) {
        const size_t n = arg.object_val->array_elements.size();
        for (size_t i = 0; i < n; ++i) {
            if (!push_token(arg.object_val->array_elements[i].to_string())) return false;
        }
        return true;
    }
    return push_token(arg.to_string());
}

bool set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// Returns > 0 if sock is ready for desired event (or error/hangup), 0 if timeout, -1 if stop_requested/wake.
int poll_sock_or_wake(int sock,
                      int wake_rd,
                      bool want_write,
                      std::chrono::steady_clock::time_point deadline,
                      const std::atomic<bool>& stop_requested) {
    while (!stop_requested.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return 0;
        const int timeout_ms = static_cast<int>(
            std::min<int64_t>(500, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() + 1));

        struct pollfd pfds[2]{};
        nfds_t nfds = 1;
        pfds[0].fd = sock;
        pfds[0].events = want_write ? (POLLIN | POLLOUT) : POLLIN;
        if (wake_rd >= 0) {
            pfds[1].fd = wake_rd;
            pfds[1].events = POLLIN;
            nfds = 2;
        }
        int rc = ::poll(pfds, nfds, timeout_ms);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (rc == 0) continue;
        if (nfds == 2 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            char drain[64];
            while (::read(wake_rd, drain, sizeof(drain)) > 0) {}
            if (stop_requested.load(std::memory_order_acquire)) return -1;
        }
        if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL | (want_write ? POLLOUT : POLLIN))) {
            return 1;
        }
        if (pfds[0].revents & POLLIN) return 1;
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// WebSocketEventSink & JsOwnerState
// ---------------------------------------------------------------------------

void WebSocketEventSink::shutdown() {
    std::vector<std::shared_ptr<WebSocketObject>> to_stop;
    {
        std::lock_guard<std::mutex> lock(mutex);
        alive = false;
        loop = nullptr;
        wake = nullptr;
        for (const auto& w : attached) {
            if (auto s = w.lock()) to_stop.push_back(std::move(s));
        }
        attached.clear();
    }
    for (const auto& s : to_stop) {
        s->request_stop();
    }
}

struct WebSocketObject::JsOwnerState {
    struct ListenerEntry {
        std::string type;
        JsValue callback;
        bool once{false};
    };

    JsValue inst{JsValue::make_undefined()};
    mutable std::mutex listeners_mutex;
    std::vector<ListenerEntry> listeners;
    std::shared_ptr<WebSocketEventSink> sink;
    bool gc_registered{false};

    void register_gc_roots() {
        if (!sink || gc_registered) return;
        std::lock_guard<std::mutex> lock(sink->mutex);
        if (!sink->alive || !sink->loop) return;
        // Called under Heap's lock during GC: only acquires listeners_mutex cleanly.
        sink->loop->add_gc_root_provider(this, [this](std::vector<JsValue*>& roots) {
            std::lock_guard<std::mutex> llock(listeners_mutex);
            if (!inst.is_undefined()) roots.push_back(&inst);
            for (auto& entry : listeners) {
                roots.push_back(&entry.callback);
            }
        });
        gc_registered = true;
    }

    void unregister_gc_roots() {
        if (!sink || !gc_registered) return;
        std::lock_guard<std::mutex> lock(sink->mutex);
        if (sink->alive && sink->loop) {
            sink->loop->remove_gc_root_provider(this);
        }
        gc_registered = false;
    }

    void clear_on_owner() {
        unregister_gc_roots();
        std::lock_guard<std::mutex> llock(listeners_mutex);
        inst = JsValue::make_undefined();
        listeners.clear();
    }

    void add_listener(const std::string& type, JsValue fn, bool once) {
        if (!fn.is_function() && !fn.is_object_like()) return;
        std::lock_guard<std::mutex> llock(listeners_mutex);
        for (const auto& existing : listeners) {
            if (existing.type == type && strict_equals(existing.callback, fn)) return;
        }
        listeners.push_back(ListenerEntry{type, std::move(fn), once});
    }

    void remove_listener(const std::string& type, const JsValue& fn) {
        std::lock_guard<std::mutex> llock(listeners_mutex);
        for (auto it = listeners.begin(); it != listeners.end(); ++it) {
            if (it->type == type && strict_equals(it->callback, fn)) {
                listeners.erase(it);
                return;
            }
        }
    }

    std::vector<ListenerEntry> snapshot_and_consume_once(const std::string& type) {
        std::lock_guard<std::mutex> llock(listeners_mutex);
        std::vector<ListenerEntry> out;
        for (const auto& entry : listeners) {
            if (entry.type == type) out.push_back(entry);
        }
        listeners.erase(
            std::remove_if(listeners.begin(), listeners.end(),
                           [&](const ListenerEntry& e) { return e.type == type && e.once; }),
            listeners.end());
        return out;
    }
};

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

void WebSocketObject::set_ca_bundle_for_testing(std::string ca_file_path) {
    std::lock_guard<std::mutex> lock(ca_bundle_mutex());
    ca_bundle_storage() = std::move(ca_file_path);
}

std::string WebSocketObject::ca_bundle_for_testing() {
    std::lock_guard<std::mutex> lock(ca_bundle_mutex());
    return ca_bundle_storage();
}

void WebSocketObject::random_bytes(uint8_t* dst, size_t len) {
    if (!dst || len == 0) return;
    const auto& ssl_api = WsOpenSslApi::instance();
    if (ssl_api.RAND_bytes_fn && ssl_api.RAND_bytes_fn(dst, static_cast<int>(len)) == 1) {
        return;
    }
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t got = 0;
        while (got < len) {
            ssize_t r = ::read(fd, dst + got, len - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            got += static_cast<size_t>(r);
        }
        ::close(fd);
        if (got == len) return;
    }
    static auto* rd_mutex = new std::mutex();
    static auto* rng = new std::mt19937_64(std::random_device{}());
    std::lock_guard<std::mutex> lock(*rd_mutex);
    for (size_t i = 0; i < len; ++i) {
        dst[i] = static_cast<uint8_t>((*rng)() & 0xFFu);
    }
}

bool WebSocketObject::is_valid_utf8(std::string_view bytes) {
    const auto* s = reinterpret_cast<const uint8_t*>(bytes.data());
    const size_t n = bytes.size();
    size_t i = 0;
    while (i < n) {
        uint8_t b0 = s[i];
        if (b0 <= 0x7Fu) {
            ++i;
            continue;
        }
        if (b0 >= 0xC2u && b0 <= 0xDFu) {
            if (i + 1 >= n) return false;
            uint8_t b1 = s[i + 1];
            if ((b1 & 0xC0u) != 0x80u) return false;
            i += 2;
        } else if (b0 >= 0xE0u && b0 <= 0xEFu) {
            if (i + 2 >= n) return false;
            uint8_t b1 = s[i + 1];
            uint8_t b2 = s[i + 2];
            if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) return false;
            if (b0 == 0xE0u && b1 < 0xA0u) return false; // overlong
            if (b0 == 0xEDu && b1 > 0x9Fu) return false; // UTF-16 surrogates U+D800..U+DFFF
            i += 3;
        } else if (b0 >= 0xF0u && b0 <= 0xF4u) {
            if (i + 3 >= n) return false;
            uint8_t b1 = s[i + 1];
            uint8_t b2 = s[i + 2];
            uint8_t b3 = s[i + 3];
            if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) return false;
            if (b0 == 0xF0u && b1 < 0x90u) return false; // overlong
            if (b0 == 0xF4u && b1 > 0x8Fu) return false; // > U+10FFFF
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

std::string WebSocketObject::sha1_digest(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    std::vector<uint8_t> data(input.begin(), input.end());
    uint64_t original_bits = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while ((data.size() % 64) != 56) data.push_back(0x00);
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((original_bits >> (i * 8)) & 0xFF));
    }

    auto rol = [](uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); };

    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            size_t idx = chunk + i * 4;
            w[i] = (static_cast<uint32_t>(data[idx]) << 24) |
                   (static_cast<uint32_t>(data[idx + 1]) << 16) |
                   (static_cast<uint32_t>(data[idx + 2]) << 8) |
                   static_cast<uint32_t>(data[idx + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::string digest(20, '\0');
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest[i * 4 + 0] = static_cast<char>((hs[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<char>((hs[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<char>((hs[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<char>(hs[i] & 0xFF);
    }
    return digest;
}

std::string WebSocketObject::base64_encode(const std::string& input) {
    static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kTable[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kTable[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string WebSocketObject::calculate_accept_key(const std::string& client_key) {
    return base64_encode(sha1_digest(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

// ---------------------------------------------------------------------------
// WebSocketObject lifecycle & URL/Origin/Mixed-Content/Subprotocol parsing
// ---------------------------------------------------------------------------

WebSocketObject::WebSocketObject(std::string url,
                                 std::string protocols,
                                 WebSocketContext context,
                                 std::shared_ptr<WebSocketEventSink> event_sink)
    : m_raw_url(std::move(url)),
      m_url(m_raw_url),
      m_owner_thread_id(std::this_thread::get_id()),
      m_event_sink(event_sink ? std::move(event_sink) : main_ws_event_sink()),
      m_owner_js(std::make_shared<JsOwnerState>()) {
    m_owner_js->sink = m_event_sink;
    std::vector<std::string> parsed_protocols;
    std::string proto_err;
    if (!parse_protocol_list_string(protocols, parsed_protocols, proto_err)) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = proto_err;
        return;
    }
    init_parsed_request(std::move(parsed_protocols), context);
}

WebSocketObject::WebSocketObject(std::string url,
                                 std::vector<std::string> protocols,
                                 WebSocketContext context,
                                 std::shared_ptr<WebSocketEventSink> event_sink)
    : m_raw_url(std::move(url)),
      m_url(m_raw_url),
      m_owner_thread_id(std::this_thread::get_id()),
      m_event_sink(event_sink ? std::move(event_sink) : main_ws_event_sink()),
      m_owner_js(std::make_shared<JsOwnerState>()) {
    m_owner_js->sink = m_event_sink;
    init_parsed_request(std::move(protocols), context);
}

void WebSocketObject::init_parsed_request(std::vector<std::string> protocols,
                                          const WebSocketContext& context) {
    m_offered_protocols = std::move(protocols);

    if (m_raw_url.empty() || m_raw_url.size() > 16384 || contains_control_chars(m_raw_url)) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "The URL '" + m_raw_url + "' is invalid.";
        return;
    }

    // Validate offered subprotocols again (defensive for direct C++ vector callers).
    for (size_t i = 0; i < m_offered_protocols.size(); ++i) {
        if (!is_http_token(m_offered_protocols[i]) || contains_control_chars(m_offered_protocols[i])) {
            m_request_valid = false;
            m_validation_dom_error = "SyntaxError";
            m_validation_error = "Invalid WebSocket subprotocol '" + m_offered_protocols[i] + "'.";
            return;
        }
        for (size_t j = 0; j < i; ++j) {
            if (m_offered_protocols[j] == m_offered_protocols[i]) {
                m_request_valid = false;
                m_validation_dom_error = "SyntaxError";
                m_validation_error = "Duplicate WebSocket subprotocol '" + m_offered_protocols[i] + "'.";
                return;
            }
        }
    }

    Url doc_url;
    const bool have_doc = !context.document_url.empty() && Url::parse(context.document_url, nullptr, doc_url);
    if (!context.opaque_origin && have_doc && (doc_url.scheme == "http" || doc_url.scheme == "https")) {
        m_origin_header = doc_url.origin();
    } else {
        m_origin_header = "null";
    }

    Url base_url;
    const Url* base_ptr = nullptr;
    const bool explicit_scheme = has_explicit_scheme(m_raw_url);
    if (!explicit_scheme) {
        const std::string& base_str = context.base_url.empty() ? context.document_url : context.base_url;
        if (!base_str.empty() && Url::parse(base_str, nullptr, base_url) &&
            (base_url.scheme == "http" || base_url.scheme == "https" ||
             base_url.scheme == "ws" || base_url.scheme == "wss")) {
            base_ptr = &base_url;
        }
    }

    Url parsed;
    if (!Url::parse(m_raw_url, base_ptr, parsed)) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "The URL '" + m_raw_url + "' is invalid.";
        return;
    }

    if (!explicit_scheme) {
        if (parsed.scheme == "http") parsed.scheme = "ws";
        else if (parsed.scheme == "https") parsed.scheme = "wss";
    }

    if (parsed.scheme != "ws" && parsed.scheme != "wss") {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "The URL's scheme must be either 'ws' or 'wss'. '" + parsed.scheme + "' is not allowed.";
        return;
    }
    if (parsed.has_fragment) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "The URL contains a fragment identifier ('#" + parsed.fragment + "'). Fragment identifiers are not allowed in WebSocket URLs.";
        return;
    }
    if (!parsed.has_host || parsed.host.empty()) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "The URL '" + m_raw_url + "' has no host.";
        return;
    }
    if (!parsed.username.empty() || !parsed.password.empty()) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "WebSocket URLs must not contain embedded credentials.";
        return;
    }
    if (parsed.port == 0 || parsed.port > 65535) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "The URL's port is invalid.";
        return;
    }

    m_secure = (parsed.scheme == "wss");
    m_port = parsed.port > 0 ? static_cast<uint16_t>(parsed.port) : static_cast<uint16_t>(m_secure ? 443 : 80);

    m_connect_host = parsed.host;
    if (m_connect_host.size() >= 2 && m_connect_host.front() == '[' && m_connect_host.back() == ']') {
        m_connect_host = m_connect_host.substr(1, m_connect_host.size() - 2);
    }
    m_host_is_ip = is_ip_literal(m_connect_host);

    if (parsed.port > 0 && parsed.port != (m_secure ? 443 : 80)) {
        m_host_header = parsed.host + ":" + std::to_string(parsed.port);
    } else {
        m_host_header = parsed.host;
    }

    m_path = parsed.pathname();
    if (m_path.empty()) m_path = "/";
    if (parsed.has_query) m_path += "?" + parsed.query;
    if (contains_control_chars(m_path) || contains_control_chars(m_host_header)) {
        m_request_valid = false;
        m_validation_dom_error = "SyntaxError";
        m_validation_error = "The URL contains invalid control characters.";
        return;
    }

    // Cookies for the upgrade. The jar only understands http(s) URLs, and a
    // `ws` request carries the same cookies as the `http` one, `wss` as `https`.
    m_cookie_url = (m_secure ? "https://" : "http://") + m_host_header + m_path;
    if (!context.opaque_origin && have_doc && (doc_url.scheme == "http" || doc_url.scheme == "https")) {
        m_cookie_site = context.document_url;
    }

    m_url = parsed.href(true);
    m_event_origin = parsed.origin();

    // W3C Mixed Content: HTTPS documents cannot connect to insecure ws:// unless potentially trustworthy.
    if (have_doc && doc_url.scheme == "https" && !m_secure &&
        !is_potentially_trustworthy_ws_url(parsed, m_connect_host)) {
        m_request_valid = false;
        m_mixed_content_blocked = true;
        m_validation_dom_error = "SecurityError";
        m_validation_error = "An insecure WebSocket connection may not be initiated from a page loaded over HTTPS.";
        return;
    }

    m_request_valid = true;
}

WebSocketObject::~WebSocketObject() {
    request_stop();
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(m_thread_mutex);
        if (m_receiver_thread.joinable()) {
            if (std::this_thread::get_id() == m_receiver_thread.get_id()) {
                m_receiver_thread.detach();
            } else {
                to_join = std::move(m_receiver_thread);
            }
        }
    }
    if (to_join.joinable()) {
        to_join.join();
    }

    if (m_wake_fds[0] >= 0) {
        ::close(m_wake_fds[0]);
        m_wake_fds[0] = -1;
    }
    if (m_wake_fds[1] >= 0) {
        ::close(m_wake_fds[1]);
        m_wake_fds[1] = -1;
    }

    if (m_owner_js) {
        if (std::this_thread::get_id() == m_owner_thread_id) {
            m_owner_js->clear_on_owner();
            m_owner_js.reset();
        } else {
            // Never decrement non-atomic owner-thread JsObject handles from the I/O thread.
            auto orphan = std::move(m_owner_js);
            bool posted = false;
            if (m_event_sink) {
                std::lock_guard<std::mutex> lock(m_event_sink->mutex);
                if (m_event_sink->alive && m_event_sink->loop) {
                    m_event_sink->loop->post_task([orphan]() mutable {
                        orphan->clear_on_owner();
                        orphan.reset();
                    });
                    if (m_event_sink->wake) m_event_sink->wake();
                    posted = true;
                }
            }
            if (!posted) {
                new std::shared_ptr<JsOwnerState>(std::move(orphan));
            }
        }
    }
}

void WebSocketObject::wake_io_thread() {
    if (m_wake_fds[1] >= 0) {
        const char b = 1;
        ssize_t ignored = ::write(m_wake_fds[1], &b, 1);
        (void)ignored;
    }
}

void WebSocketObject::request_stop() {
    m_stop_requested.store(true, std::memory_order_release);
    wake_io_thread();
}

void WebSocketObject::join_for_shutdown() {
    request_stop();
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(m_thread_mutex);
        if (m_receiver_thread.joinable() &&
            std::this_thread::get_id() != m_receiver_thread.get_id()) {
            to_join = std::move(m_receiver_thread);
        }
    }
    if (to_join.joinable()) {
        to_join.join();
    }
}

void WebSocketObject::post_to_owner(std::function<void()> task) {
    if (!task || !m_event_sink || g_ws_shutting_down.load(std::memory_order_acquire)) return;
    std::function<void()> wake;
    {
        std::lock_guard<std::mutex> lock(m_event_sink->mutex);
        if (!m_event_sink->is_owner_alive() || !m_event_sink->loop) return;
        m_event_sink->loop->post_task(std::move(task));
        wake = m_event_sink->wake;
    }
    if (wake) wake();
}

std::string WebSocketObject::protocol() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_selected_protocol;
}

std::string WebSocketObject::js_protocol() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_js_selected_protocol;
}

std::string WebSocketObject::binary_type() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_binary_type;
}

void WebSocketObject::set_binary_type(std::string_view type) {
    if (type == "blob" || type == "arraybuffer") {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_binary_type = std::string(type);
    }
}

std::string WebSocketObject::origin_for_event() const {
    return m_event_origin;
}

void WebSocketObject::set_onopen(JsValue cb) {
    if (m_owner_js && !m_owner_js->inst.is_undefined()) {
        m_owner_js->inst.set_property("onopen", std::move(cb));
    }
}

void WebSocketObject::set_onmessage(JsValue cb) {
    if (m_owner_js && !m_owner_js->inst.is_undefined()) {
        m_owner_js->inst.set_property("onmessage", std::move(cb));
    }
}

void WebSocketObject::set_onerror(JsValue cb) {
    if (m_owner_js && !m_owner_js->inst.is_undefined()) {
        m_owner_js->inst.set_property("onerror", std::move(cb));
    }
}

void WebSocketObject::set_onclose(JsValue cb) {
    if (m_owner_js && !m_owner_js->inst.is_undefined()) {
        m_owner_js->inst.set_property("onclose", std::move(cb));
    }
}

void WebSocketObject::add_event_listener(const std::string& type, JsValue listener, bool once) {
    if (m_owner_js) m_owner_js->add_listener(type, std::move(listener), once);
}

void WebSocketObject::remove_event_listener(const std::string& type, const JsValue& listener) {
    if (m_owner_js) m_owner_js->remove_listener(type, listener);
}

void WebSocketObject::release_owner_js_on_owner() {
    if (std::this_thread::get_id() == m_owner_thread_id && m_owner_js) {
        m_owner_js->clear_on_owner();
    }
}

namespace {

void invoke_ws_handler(const JsValue& fn, const JsValue& target, const JsValue& event) {
    if (fn.is_function()) {
        JsValue ignored;
        (void)js_capture_throw([&]() { fn.call_with_this(target, {event}); }, ignored);
    } else if (fn.is_object_like()) {
        JsValue handle_event = fn.get_property("handleEvent");
        if (handle_event.is_function()) {
            JsValue ignored;
            (void)js_capture_throw([&]() { handle_event.call_with_this(fn, {event}); }, ignored);
        }
    }
}

} // namespace

void WebSocketObject::dispatch_open_on_owner(const std::string& selected_protocol) {
    post_to_owner([self = shared_from_this(), selected_protocol]() {
        if (self->m_event_sink && self->m_event_sink->owner_alive &&
            !self->m_event_sink->owner_alive->load(std::memory_order_acquire)) {
            self->release_owner_js_on_owner();
            self->request_stop();
            return;
        }
        if (self->m_js_ready_state.load(std::memory_order_acquire) == WebSocketReadyState::Connecting) {
            self->m_js_ready_state.store(WebSocketReadyState::Open, std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> lock(self->m_state_mutex);
            self->m_js_selected_protocol = selected_protocol;
        }
        if (!self->m_owner_js || self->m_owner_js->inst.is_undefined()) return;
        JsValue target = self->m_owner_js->inst;
        JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
        ev.set_property("type", JsValue::make_string("open"));
        ev.set_property("target", target);
        ev.set_property("currentTarget", target);

        JsValue onopen = target.get_property("onopen");
        if (onopen.is_function()) invoke_ws_handler(onopen, target, ev);

        auto list = self->m_owner_js->snapshot_and_consume_once("open");
        for (const auto& entry : list) {
            invoke_ws_handler(entry.callback, target, ev);
        }
    });
}

void WebSocketObject::dispatch_message_on_owner(const std::string& payload, bool is_binary) {
    (void)is_binary;
    post_to_owner([self = shared_from_this(), payload]() {
        if (self->m_event_sink && self->m_event_sink->owner_alive &&
            !self->m_event_sink->owner_alive->load(std::memory_order_acquire)) {
            self->release_owner_js_on_owner();
            self->request_stop();
            return;
        }
        if (!self->m_owner_js || self->m_owner_js->inst.is_undefined()) return;
        JsValue target = self->m_owner_js->inst;
        JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
        ev.set_property("type", JsValue::make_string("message"));
        ev.set_property("data", JsValue::make_string(payload));
        ev.set_property("origin", JsValue::make_string(self->m_event_origin));
        ev.set_property("lastEventId", JsValue::make_string(""));
        ev.set_property("source", JsValue::make_null());
        ev.set_property("target", target);
        ev.set_property("currentTarget", target);

        JsValue onmsg = target.get_property("onmessage");
        if (onmsg.is_function()) invoke_ws_handler(onmsg, target, ev);

        auto list = self->m_owner_js->snapshot_and_consume_once("message");
        for (const auto& entry : list) {
            invoke_ws_handler(entry.callback, target, ev);
        }
    });
}

void WebSocketObject::dispatch_error_and_close_on_owner(const std::string& err_msg,
                                                        int close_code,
                                                        const std::string& close_reason,
                                                        bool was_clean,
                                                        bool emit_error) {
    post_to_owner([self = shared_from_this(), err_msg, close_code, close_reason, was_clean, emit_error]() {
        self->m_js_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);
        const bool realm_alive = !self->m_event_sink || !self->m_event_sink->owner_alive ||
                                 self->m_event_sink->owner_alive->load(std::memory_order_acquire);
        if (realm_alive && self->m_owner_js && !self->m_owner_js->inst.is_undefined()) {
            JsValue target = self->m_owner_js->inst;

            if (emit_error) {
                JsValue err_ev = JsValue::make_object(Heap::default_heap().allocate_object());
                err_ev.set_property("type", JsValue::make_string("error"));
                err_ev.set_property("message", JsValue::make_string(err_msg));
                err_ev.set_property("target", target);
                err_ev.set_property("currentTarget", target);

                JsValue onerr = target.get_property("onerror");
                if (onerr.is_function()) invoke_ws_handler(onerr, target, err_ev);

                auto err_list = self->m_owner_js->snapshot_and_consume_once("error");
                for (const auto& entry : err_list) {
                    invoke_ws_handler(entry.callback, target, err_ev);
                }
            }

            JsValue close_ev = JsValue::make_object(Heap::default_heap().allocate_object());
            close_ev.set_property("type", JsValue::make_string("close"));
            close_ev.set_property("code", JsValue::make_number(close_code));
            close_ev.set_property("reason", JsValue::make_string(close_reason));
            close_ev.set_property("wasClean", JsValue::make_boolean(was_clean));
            close_ev.set_property("target", target);
            close_ev.set_property("currentTarget", target);

            JsValue onclose = target.get_property("onclose");
            if (onclose.is_function()) invoke_ws_handler(onclose, target, close_ev);

            auto close_list = self->m_owner_js->snapshot_and_consume_once("close");
            for (const auto& entry : close_list) {
                invoke_ws_handler(entry.callback, target, close_ev);
            }
        }
        // Break inst <-> WebSocketObject reference cycle once close completes.
        self->release_owner_js_on_owner();
    });
}

void WebSocketObject::enqueue_frame(uint8_t opcode,
                                    const uint8_t* data,
                                    size_t len,
                                    bool is_data,
                                    bool is_close) {
    std::vector<uint8_t> frame;
    frame.reserve(len + 14);
    frame.push_back(static_cast<uint8_t>(0x80u | (opcode & 0x0Fu)));

    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(0x80u | len));
    } else if (len <= 65535) {
        frame.push_back(static_cast<uint8_t>(0x80u | 126u));
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
        frame.push_back(static_cast<uint8_t>(len & 0xFFu));
    } else {
        frame.push_back(static_cast<uint8_t>(0x80u | 127u));
        uint64_t ulen = static_cast<uint64_t>(len);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((ulen >> (i * 8)) & 0xFFu));
        }
    }

    uint8_t mask[4]{};
    random_bytes(mask, 4);
    frame.insert(frame.end(), mask, mask + 4);
    for (size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(data[i] ^ mask[i % 4]));
    }

    if (is_data) {
        m_buffered_amount.fetch_add(len, std::memory_order_acq_rel);
    }

    {
        std::lock_guard<std::mutex> lock(m_outbox_mutex);
        m_outbox.push_back(OutboundFrame{std::move(frame), len, is_data, is_close});
    }
    wake_io_thread();
}

void WebSocketObject::connect() {
    ensure_sigpipe_ignored();
    ensure_ws_atexit_registered();

    if (g_ws_shutting_down.load(std::memory_order_acquire)) {
        m_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);
        m_js_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);
        return;
    }

    if (m_owner_js) {
        m_owner_js->register_gc_roots();
    }

    if (!m_request_valid) {
        m_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);
        dispatch_error_and_close_on_owner(m_validation_error, 1006, "", false, true);
        return;
    }

    if (::pipe2(m_wake_fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        m_wake_fds[0] = -1;
        m_wake_fds[1] = -1;
    }

    auto self = shared_from_this();
    WebSocketRegistry::instance().add(self);
    if (m_event_sink) {
        std::lock_guard<std::mutex> lock(m_event_sink->mutex);
        if (m_event_sink->alive) {
            m_event_sink->attached.push_back(self);
        } else {
            m_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);
            return;
        }
    }

    std::lock_guard<std::mutex> lock(m_thread_mutex);
    if (g_ws_shutting_down.load(std::memory_order_acquire)) {
        m_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);
        return;
    }
    m_receiver_thread = std::thread([self = std::move(self)]() {
        self->receiver_thread_loop();
    });
}

void WebSocketObject::send(const std::string& message) {
    if (m_ready_state.load(std::memory_order_acquire) != WebSocketReadyState::Open) return;
    if (message.size() > kMaxMessageBytes) return;
    enqueue_frame(0x01u, reinterpret_cast<const uint8_t*>(message.data()), message.size(), true);
}

void WebSocketObject::send_binary(const std::vector<uint8_t>& data) {
    if (m_ready_state.load(std::memory_order_acquire) != WebSocketReadyState::Open) return;
    if (data.size() > kMaxMessageBytes) return;
    enqueue_frame(0x02u, data.data(), data.size(), true);
}

void WebSocketObject::close(int code, const std::string& reason) {
    if (code != 1005 && !is_valid_close_code(code)) code = 1000;
    std::string trimmed_reason = reason;
    if (trimmed_reason.size() > 123) trimmed_reason.resize(123);

    WebSocketReadyState cur = m_ready_state.load(std::memory_order_acquire);
    if (cur == WebSocketReadyState::Closing || cur == WebSocketReadyState::Closed) {
        return;
    }

    m_js_ready_state.store(WebSocketReadyState::Closing, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_pending_close_code = code;
        m_pending_close_reason = trimmed_reason;
    }

    if (cur == WebSocketReadyState::Connecting) {
        m_ready_state.store(WebSocketReadyState::Closing, std::memory_order_release);
        m_close_initiated.store(true, std::memory_order_release);
        request_stop();
        return;
    }

    if (m_ready_state.compare_exchange_strong(cur, WebSocketReadyState::Closing, std::memory_order_acq_rel)) {
        m_close_initiated.store(true, std::memory_order_release);
        wake_io_thread();
    }
}

void WebSocketObject::receiver_thread_loop() {
    auto finish_abnormal = [&](const char* msg, int close_code = 1006, bool emit_error = true) {
        m_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);
        dispatch_error_and_close_on_owner(msg, close_code, "", false, emit_error);
    };

    if (is_reserved_nonexistent_domain(m_connect_host)) {
        finish_abnormal("Refused resolution of reserved nonexistent domain");
        return;
    }

    // 1. Thread-safe address resolution + non-blocking TCP connect with timeout
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    const std::string port_str = std::to_string(m_port);
    if (::getaddrinfo(m_connect_host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        finish_abnormal("WebSocket address resolution failed");
        return;
    }

    int sock = -1;
    const auto connect_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kConnectTimeoutMs);
    for (struct addrinfo* rp = res; rp != nullptr && !m_stop_requested.load(std::memory_order_acquire); rp = rp->ai_next) {
        int fd = ::socket(rp->ai_family, rp->ai_socktype | SOCK_CLOEXEC, rp->ai_protocol);
        if (fd < 0) continue;
        if (!set_nonblocking(fd)) {
            ::close(fd);
            continue;
        }
        int cr = ::connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (cr == 0) {
            sock = fd;
            break;
        }
        if (errno == EINPROGRESS) {
            int pr = poll_sock_or_wake(fd, m_wake_fds[0], true, connect_deadline, m_stop_requested);
            if (pr > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    sock = fd;
                    break;
                }
            }
        }
        ::close(fd);
    }
    ::freeaddrinfo(res);

    if (sock < 0) {
        finish_abnormal("WebSocket connection failed");
        return;
    }

    // RAII cleanup for socket + optional TLS session
    const WsOpenSslApi& ssl_api = WsOpenSslApi::instance();
    void* ssl_ctx = nullptr;
    void* ssl = nullptr;
    bool tls_fatal_error = false;
    auto cleanup_transport = [&]() {
        if (ssl) {
            if (!tls_fatal_error) (void)ssl_api.SSL_shutdown_fn(ssl);
            ssl_api.SSL_free_fn(ssl);
            ssl = nullptr;
        }
        if (ssl_ctx) {
            ssl_api.SSL_CTX_free_fn(ssl_ctx);
            ssl_ctx = nullptr;
        }
        if (sock >= 0) {
            ::close(sock);
            sock = -1;
        }
    };

    // 2. Optional TLS handshake & strict hostname / IP SAN verification
    if (m_secure) {
        if (!ssl_api.available) {
            cleanup_transport();
            finish_abnormal("TLS library (libssl.so.3) is unavailable on this host");
            return;
        }
        ssl_ctx = ssl_api.SSL_CTX_new_fn(ssl_api.TLS_client_method_fn());
        if (!ssl_ctx) {
            cleanup_transport();
            finish_abnormal("Failed to initialize TLS context");
            return;
        }
        if (ssl_api.SSL_CTX_ctrl_fn) {
            // SSL_CTRL_SET_MIN_PROTO_VERSION = 123, TLS1_2_VERSION = 0x0303
            (void)ssl_api.SSL_CTX_ctrl_fn(ssl_ctx, 123, 0x0303, nullptr);
        }
        const std::string custom_ca = effective_ca_bundle();
        if (!custom_ca.empty()) {
            if (ssl_api.SSL_CTX_load_verify_locations_fn(ssl_ctx, custom_ca.c_str(), nullptr) != 1) {
                cleanup_transport();
                finish_abnormal("Failed to load configured TLS CA bundle");
                return;
            }
        } else {
            (void)ssl_api.SSL_CTX_set_default_verify_paths_fn(ssl_ctx);
        }
        // SSL_VERIFY_PEER = 1
        ssl_api.SSL_CTX_set_verify_fn(ssl_ctx, 1, nullptr);

        ssl = ssl_api.SSL_new_fn(ssl_ctx);
        if (!ssl || ssl_api.SSL_set_fd_fn(ssl, sock) != 1) {
            cleanup_transport();
            finish_abnormal("Failed to create TLS session");
            return;
        }

        if (m_host_is_ip) {
            void* param = ssl_api.SSL_get0_param_fn(ssl);
            if (!param || ssl_api.X509_VERIFY_PARAM_set1_ip_asc_fn(param, m_connect_host.c_str()) != 1) {
                cleanup_transport();
                finish_abnormal("Failed to configure TLS IP SAN check");
                return;
            }
        } else {
            // SSL_CTRL_SET_TLSEXT_HOSTNAME = 55, TLSEXT_NAMETYPE_host_name = 0
            (void)ssl_api.SSL_ctrl_fn(ssl, 55, 0, const_cast<char*>(m_connect_host.c_str()));
            if (ssl_api.SSL_set1_host_fn(ssl, m_connect_host.c_str()) != 1) {
                cleanup_transport();
                finish_abnormal("Failed to configure TLS hostname check");
                return;
            }
        }

        const auto tls_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHandshakeTimeoutMs);
        bool tls_ok = false;
        while (!m_stop_requested.load(std::memory_order_acquire)) {
            int rc = ssl_api.SSL_connect_fn(ssl);
            if (rc == 1) {
                tls_ok = (ssl_api.SSL_get_verify_result_fn(ssl) == 0);
                break;
            }
            int serr = ssl_api.SSL_get_error_fn(ssl, rc);
            if (serr == 2 /* SSL_ERROR_WANT_READ */ || serr == 3 /* SSL_ERROR_WANT_WRITE */) {
                if (poll_sock_or_wake(sock, m_wake_fds[0], serr == 3, tls_deadline, m_stop_requested) <= 0) {
                    tls_fatal_error = true;
                    break;
                }
                continue;
            }
            tls_fatal_error = true;
            break;
        }
        if (!tls_ok) {
            // Ask why, whichever way the handshake failed. A refused
            // certificate usually fails SSL_connect itself rather than
            // returning 1 with a non-zero verify result, so the reason is read
            // after the loop rather than only on the rc==1 path -- and before
            // cleanup_transport(), which may free the SSL object it needs.
            // This is the same message the loader prints, from the same codes.
            const long verify_code = ssl_api.SSL_get_verify_result_fn(ssl);
            cleanup_transport();
            if (verify_code != 0) {
                std::string message = "TLS certificate verification failed (code " +
                                      std::to_string(verify_code);
                if (ssl_api.X509_verify_cert_error_string_fn) {
                    message += ", ";
                    message += ssl_api.X509_verify_cert_error_string_fn(verify_code);
                }
                message += ")";
                finish_abnormal(message.c_str());
            } else {
                finish_abnormal("TLS handshake failed for a reason other than a certificate");
            }
            return;
        }
    }

    // 3. RFC 6455 HTTP/1.1 101 Upgrade Handshake
    uint8_t key_bytes[16]{};
    random_bytes(key_bytes, sizeof(key_bytes));
    const std::string sec_key = base64_encode(std::string(reinterpret_cast<const char*>(key_bytes), sizeof(key_bytes)));
    const std::string expected_accept = calculate_accept_key(sec_key);

    std::string handshake_req =
        "GET " + m_path + " HTTP/1.1\r\n"
        "Host: " + m_host_header + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Origin: " + m_origin_header + "\r\n"
        "Sec-WebSocket-Key: " + sec_key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n";
    if (!m_cookie_url.empty()) {
        const std::string cookie_val =
            CookieJar::default_jar().get_cookie_header(m_cookie_url, m_cookie_site, false, "GET");
        if (!cookie_val.empty() && cookie_val.find_first_of("\r\n") == std::string::npos) {
            handshake_req += "Cookie: " + cookie_val + "\r\n";
        }
    }
    if (!m_offered_protocols.empty()) {
        handshake_req += "Sec-WebSocket-Protocol: ";
        for (size_t i = 0; i < m_offered_protocols.size(); ++i) {
            if (i > 0) handshake_req += ", ";
            handshake_req += m_offered_protocols[i];
        }
        handshake_req += "\r\n";
    }
    handshake_req += "\r\n";

    const auto hs_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHandshakeTimeoutMs);
    size_t hs_sent = 0;
    while (hs_sent < handshake_req.size() && !m_stop_requested.load(std::memory_order_acquire)) {
        if (m_secure) {
            int chunk = static_cast<int>(std::min<size_t>(handshake_req.size() - hs_sent, 16384u));
            int n = ssl_api.SSL_write_fn(ssl, handshake_req.data() + hs_sent, chunk);
            if (n > 0) {
                hs_sent += static_cast<size_t>(n);
                continue;
            }
            int serr = ssl_api.SSL_get_error_fn(ssl, n);
            if (serr == 2 || serr == 3) {
                if (poll_sock_or_wake(sock, m_wake_fds[0], serr == 3, hs_deadline, m_stop_requested) <= 0) {
                    tls_fatal_error = true;
                    break;
                }
                continue;
            }
            tls_fatal_error = true;
            break;
        } else {
            ssize_t n = ::send(sock, handshake_req.data() + hs_sent, handshake_req.size() - hs_sent, MSG_NOSIGNAL);
            if (n > 0) {
                hs_sent += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (poll_sock_or_wake(sock, m_wake_fds[0], true, hs_deadline, m_stop_requested) <= 0) break;
                continue;
            }
            break;
        }
    }

    if (hs_sent < handshake_req.size()) {
        cleanup_transport();
        finish_abnormal("Failed to transmit WebSocket handshake");
        return;
    }

    std::string rx_raw;
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos &&
           rx_raw.size() < kMaxHandshakeBytes &&
           !m_stop_requested.load(std::memory_order_acquire)) {
        char buf[4096];
        if (m_secure) {
            int n = ssl_api.SSL_read_fn(ssl, buf, sizeof(buf));
            if (n > 0) {
                rx_raw.append(buf, static_cast<size_t>(n));
                header_end = rx_raw.find("\r\n\r\n");
                continue;
            }
            int serr = ssl_api.SSL_get_error_fn(ssl, n);
            if (serr == 2 || serr == 3) {
                if (poll_sock_or_wake(sock, m_wake_fds[0], serr == 3, hs_deadline, m_stop_requested) <= 0) {
                    tls_fatal_error = true;
                    break;
                }
                continue;
            }
            break;
        } else {
            ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
            if (n > 0) {
                rx_raw.append(buf, static_cast<size_t>(n));
                header_end = rx_raw.find("\r\n\r\n");
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (poll_sock_or_wake(sock, m_wake_fds[0], false, hs_deadline, m_stop_requested) <= 0) break;
                continue;
            }
            break;
        }
    }

    if (header_end == std::string::npos || header_end >= kMaxHandshakeBytes) {
        cleanup_transport();
        finish_abnormal("WebSocket handshake timed out or exceeded size limit");
        return;
    }

    const std::string header_block = rx_raw.substr(0, header_end);
    std::vector<uint8_t> frame_buf(rx_raw.begin() + static_cast<ptrdiff_t>(header_end + 4), rx_raw.end());

    // Validate 101 response status + headers per RFC 6455 §4.1/§4.2.2
    size_t line_end = header_block.find("\r\n");
    std::string_view status_line(header_block.data(), line_end == std::string::npos ? header_block.size() : line_end);
    if (status_line.size() < 12 || status_line.rfind("HTTP/1.1 101", 0) != 0 ||
        (status_line.size() > 12 && status_line[12] != ' ')) {
        cleanup_transport();
        finish_abnormal("WebSocket upgrade failed: non-101 response");
        return;
    }

    bool saw_upgrade = false;
    bool saw_connection = false;
    int accept_count = 0;
    std::string accept_val;
    int protocol_count = 0;
    std::string selected_protocol;
    int ext_count = 0;
    std::vector<std::string> set_cookies;

    if (line_end != std::string::npos) {
        size_t pos = line_end + 2;
        while (pos < header_block.size()) {
            size_t next = header_block.find("\r\n", pos);
            std::string_view line = next == std::string::npos
                ? std::string_view(header_block).substr(pos)
                : std::string_view(header_block).substr(pos, next - pos);
            size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                cleanup_transport();
                finish_abnormal("Malformed handshake response header");
                return;
            }
            std::string name = ascii_lower(trim_ows(line.substr(0, colon)));
            std::string val = trim_ows(line.substr(colon + 1));
            if (name == "upgrade" && header_has_token(val, "websocket")) {
                saw_upgrade = true;
            } else if (name == "connection" && header_has_token(val, "upgrade")) {
                saw_connection = true;
            } else if (name == "sec-websocket-accept") {
                ++accept_count;
                accept_val = std::move(val);
            } else if (name == "sec-websocket-protocol") {
                ++protocol_count;
                selected_protocol = std::move(val);
            } else if (name == "sec-websocket-extensions") {
                ++ext_count;
            } else if (name == "set-cookie") {
                set_cookies.push_back(std::move(val));
            }
            if (next == std::string::npos) break;
            pos = next + 2;
        }
    }

    // Cookies the server set on the upgrade. They arrive on an ordinary HTTP
    // response and are stored whether or not the upgrade itself is accepted,
    // as an HTTP client would do with any other response.
    if (!set_cookies.empty() && !m_cookie_url.empty()) {
        for (const std::string& set_cookie : set_cookies) {
            CookieJar::default_jar().set_cookie_from_header(set_cookie, m_cookie_url);
        }
    }

    if (!saw_upgrade || !saw_connection || accept_count != 1 || accept_val != expected_accept || ext_count > 0) {
        cleanup_transport();
        finish_abnormal("WebSocket handshake validation failed");
        return;
    }

    if (m_offered_protocols.empty()) {
        if (protocol_count > 0) {
            cleanup_transport();
            finish_abnormal("Server returned unexpected Sec-WebSocket-Protocol");
            return;
        }
    } else {
        if (protocol_count != 1 || !is_http_token(selected_protocol)) {
            cleanup_transport();
            finish_abnormal("Server returned missing or invalid Sec-WebSocket-Protocol");
            return;
        }
        bool matched = false;
        for (const auto& offered : m_offered_protocols) {
            if (offered == selected_protocol) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            cleanup_transport();
            finish_abnormal("Server selected unoffered WebSocket subprotocol");
            return;
        }
    }

    if (m_stop_requested.load(std::memory_order_acquire)) {
        cleanup_transport();
        finish_abnormal("Connection aborted during close", 1006, true);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_selected_protocol = selected_protocol;
    }
    m_ready_state.store(WebSocketReadyState::Open, std::memory_order_release);
    dispatch_open_on_owner(selected_protocol);

    // 4. Single-I/O-thread multiplexed frame & socket loop
    bool client_close_queued = false;
    bool client_close_sent = false;
    bool received_peer_close = false;
    int peer_close_code = 1005;
    std::string peer_close_reason;
    bool fatal_error = false;
    std::string fatal_msg;
    int fatal_close_code = 1006;

    bool has_close_deadline = false;
    std::chrono::steady_clock::time_point close_deadline;

    bool frag_active = false;
    uint8_t frag_opcode = 0;
    std::vector<uint8_t> frag_buf;

    size_t write_offset = 0;
    std::vector<OutboundFrame> pending_write;
    bool ssl_want_write = false;
    bool peer_eof = false;

    auto queue_client_close = [&](int code, const std::string& reason) {
        if (client_close_queued) return;
        client_close_queued = true;
        std::vector<uint8_t> payload;
        if (code != 1005) {
            payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFFu));
            payload.push_back(static_cast<uint8_t>(code & 0xFFu));
            payload.insert(payload.end(), reason.begin(), reason.end());
        }
        enqueue_frame(0x08u, payload.data(), payload.size(), false, true);
        if (!has_close_deadline) {
            has_close_deadline = true;
            close_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCloseHandshakeTimeoutMs);
        }
    };

    auto trigger_protocol_fail = [&](int close_code, const char* msg) {
        fatal_error = true;
        fatal_close_code = close_code;
        fatal_msg = msg;
        if (!received_peer_close && !client_close_queued) {
            queue_client_close(close_code, "");
        }
    };

    while (!m_stop_requested.load(std::memory_order_acquire)) {
        if (m_wake_fds[0] >= 0) {
            char drain[64];
            while (::read(m_wake_fds[0], drain, sizeof(drain)) > 0) {}
        }

        if (m_close_initiated.load(std::memory_order_acquire) && !client_close_queued) {
            int ccode = 1000;
            std::string creason;
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                ccode = m_pending_close_code;
                creason = m_pending_close_reason;
            }
            queue_client_close(ccode, creason);
        }

        // Drain outbound queue into local pending_write list
        {
            std::lock_guard<std::mutex> lock(m_outbox_mutex);
            if (!m_outbox.empty()) {
                for (auto& f : m_outbox) pending_write.push_back(std::move(f));
                m_outbox.clear();
            }
        }

        // Flush pending outbound frames
        ssl_want_write = false;
        while (!pending_write.empty()) {
            const auto& front = pending_write.front();
            const size_t rem = front.bytes.size() - write_offset;
            if (rem == 0) {
                if (front.is_data) {
                    m_buffered_amount.fetch_sub(front.payload_bytes, std::memory_order_acq_rel);
                }
                if (front.is_close) client_close_sent = true;
                pending_write.erase(pending_write.begin());
                write_offset = 0;
                continue;
            }
            if (m_secure) {
                int chunk = static_cast<int>(std::min<size_t>(rem, 16384u));
                int n = ssl_api.SSL_write_fn(ssl, front.bytes.data() + write_offset, chunk);
                if (n > 0) {
                    write_offset += static_cast<size_t>(n);
                    continue;
                }
                int serr = ssl_api.SSL_get_error_fn(ssl, n);
                if (serr == 3 /* WANT_WRITE */ || serr == 2 /* WANT_READ */) {
                    ssl_want_write = (serr == 3);
                    break;
                }
                if (!received_peer_close) {
                    tls_fatal_error = true;
                    fatal_error = true;
                    fatal_msg = "WebSocket TLS write error";
                }
                pending_write.clear();
                client_close_sent = true;
                break;
            } else {
                ssize_t n = ::send(sock, front.bytes.data() + write_offset, rem, MSG_NOSIGNAL);
                if (n > 0) {
                    write_offset += static_cast<size_t>(n);
                    continue;
                }
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    ssl_want_write = true;
                    break;
                }
                if (!received_peer_close) {
                    fatal_error = true;
                    fatal_msg = "WebSocket socket write error";
                }
                pending_write.clear();
                client_close_sent = true;
                break;
            }
        }

        if (fatal_error && (pending_write.empty() || client_close_sent)) {
            break;
        }
        if (received_peer_close && client_close_sent && pending_write.empty()) {
            break;
        }

        // Read available inbound data without blocking
        bool transport_read_err = false;
        std::string transport_err_msg;
        if (!received_peer_close && !fatal_error && !peer_eof) {
            while (frame_buf.size() < kMaxMessageBytes + 128) {
                uint8_t buf[8192];
                if (m_secure) {
                    int n = ssl_api.SSL_read_fn(ssl, buf, sizeof(buf));
                    if (n > 0) {
                        frame_buf.insert(frame_buf.end(), buf, buf + n);
                        if (ssl_api.SSL_pending_fn && ssl_api.SSL_pending_fn(ssl) > 0) continue;
                        break;
                    }
                    int serr = ssl_api.SSL_get_error_fn(ssl, n);
                    if (serr == 2 /* WANT_READ */) break;
                    if (serr == 3 /* WANT_WRITE */) {
                        ssl_want_write = true;
                        break;
                    }
                    if (serr == 6 /* ZERO_RETURN */) {
                        peer_eof = true;
                        break;
                    }
                    tls_fatal_error = true;
                    transport_read_err = true;
                    transport_err_msg = "WebSocket TLS read error";
                    break;
                } else {
                    ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
                    if (n > 0) {
                        frame_buf.insert(frame_buf.end(), buf, buf + n);
                        continue;
                    }
                    if (n == 0) {
                        peer_eof = true;
                        break;
                    }
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    transport_read_err = true;
                    transport_err_msg = "WebSocket socket read error";
                    break;
                }
            }
        }

        // Parse complete RFC 6455 frames from frame_buf
        size_t consumed = 0;
        while (!fatal_error && !received_peer_close && frame_buf.size() - consumed >= 2) {
            const uint8_t b0 = frame_buf[consumed];
            const uint8_t b1 = frame_buf[consumed + 1];

            const bool fin = (b0 & 0x80u) != 0;
            const uint8_t rsv = b0 & 0x70u;
            const uint8_t opcode = b0 & 0x0Fu;
            const bool masked = (b1 & 0x80u) != 0;
            uint64_t payload_len = b1 & 0x7Fu;
            size_t header_len = 2;

            if (rsv != 0) {
                trigger_protocol_fail(1002, "Unexpected reserved WebSocket frame bits");
                break;
            }
            if (masked) {
                // RFC 6455 §5.1: Server MUST NOT mask frames sent to client.
                trigger_protocol_fail(1002, "Server sent masked WebSocket frame");
                break;
            }

            if (payload_len == 126) {
                if (frame_buf.size() - consumed < 4) break;
                payload_len = (static_cast<uint64_t>(frame_buf[consumed + 2]) << 8) |
                              static_cast<uint64_t>(frame_buf[consumed + 3]);
                header_len = 4;
            } else if (payload_len == 127) {
                if (frame_buf.size() - consumed < 10) break;
                if ((frame_buf[consumed + 2] & 0x80u) != 0) {
                    trigger_protocol_fail(1002, "Invalid 64-bit WebSocket payload length");
                    break;
                }
                payload_len = 0;
                for (int i = 0; i < 8; ++i) {
                    payload_len = (payload_len << 8) | frame_buf[consumed + 2 + i];
                }
                header_len = 10;
            }

            if (opcode >= 0x08u) {
                if (!fin || payload_len > 125) {
                    trigger_protocol_fail(1002, "Invalid WebSocket control frame");
                    break;
                }
            }
            if (payload_len > kMaxMessageBytes) {
                trigger_protocol_fail(1009, "WebSocket message exceeded size limit");
                break;
            }

            const size_t total_frame_len = header_len + static_cast<size_t>(payload_len);
            if (frame_buf.size() - consumed < total_frame_len) break;

            const uint8_t* payload_ptr = frame_buf.data() + consumed + header_len;
            const size_t plen = static_cast<size_t>(payload_len);
            consumed += total_frame_len;

            if (opcode == 0x08u) { // Close
                if (plen == 1) {
                    trigger_protocol_fail(1002, "Invalid 1-byte close frame");
                    break;
                }
                int code = 1005;
                std::string reason;
                if (plen >= 2) {
                    code = (static_cast<int>(payload_ptr[0]) << 8) | static_cast<int>(payload_ptr[1]);
                    if (!is_valid_close_code(code)) {
                        trigger_protocol_fail(1002, "Invalid WebSocket close status code");
                        break;
                    }
                    reason.assign(reinterpret_cast<const char*>(payload_ptr + 2), plen - 2);
                    if (!is_valid_utf8(reason)) {
                        trigger_protocol_fail(1007, "Close reason is not valid UTF-8");
                        break;
                    }
                }
                received_peer_close = true;
                peer_close_code = code;
                peer_close_reason = std::move(reason);
                m_ready_state.store(WebSocketReadyState::Closing, std::memory_order_release);
                if (!client_close_queued) {
                    queue_client_close(code == 1005 ? 1000 : code, peer_close_reason);
                }
            } else if (opcode == 0x09u) { // Ping
                enqueue_frame(0x0Au, payload_ptr, plen, false, false);
            } else if (opcode == 0x0Au) { // Pong
                // Ignore unsolicited/solicited Pong
            } else if (opcode >= 0x0Bu || (opcode >= 0x03u && opcode <= 0x07u)) {
                trigger_protocol_fail(1002, "Unsupported WebSocket frame opcode");
                break;
            } else if (opcode == 0x01u || opcode == 0x02u) {
                if (frag_active) {
                    trigger_protocol_fail(1002, "Received new data frame before previous fragmented message ended");
                    break;
                }
                if (fin) {
                    std::string_view sv(reinterpret_cast<const char*>(payload_ptr), plen);
                    if (opcode == 0x01u && !is_valid_utf8(sv)) {
                        trigger_protocol_fail(1007, "Text frame contains invalid UTF-8");
                        break;
                    }
                    dispatch_message_on_owner(std::string(sv), opcode == 0x02u);
                } else {
                    frag_active = true;
                    frag_opcode = opcode;
                    frag_buf.assign(payload_ptr, payload_ptr + plen);
                }
            } else if (opcode == 0x00u) { // Continuation
                if (!frag_active) {
                    trigger_protocol_fail(1002, "Unexpected WebSocket continuation frame");
                    break;
                }
                if (frag_buf.size() + plen > kMaxMessageBytes) {
                    trigger_protocol_fail(1009, "Fragmented WebSocket message exceeded size limit");
                    break;
                }
                frag_buf.insert(frag_buf.end(), payload_ptr, payload_ptr + plen);
                if (fin) {
                    std::string_view sv(reinterpret_cast<const char*>(frag_buf.data()), frag_buf.size());
                    if (frag_opcode == 0x01u && !is_valid_utf8(sv)) {
                        trigger_protocol_fail(1007, "Fragmented text message contains invalid UTF-8");
                        break;
                    }
                    dispatch_message_on_owner(std::string(sv), frag_opcode == 0x02u);
                    frag_active = false;
                    frag_buf.clear();
                }
            }
        }
        if (consumed > 0) {
            frame_buf.erase(frame_buf.begin(), frame_buf.begin() + static_cast<ptrdiff_t>(consumed));
        }

        if (!received_peer_close && transport_read_err && !fatal_error) {
            fatal_error = true;
            fatal_msg = std::move(transport_err_msg);
        }
        if (received_peer_close && transport_read_err) {
            // Peer closed the TLS/TCP stream immediately after sending its Close frame.
            peer_eof = true;
        }
        if (transport_read_err && fatal_error) break;

        // If we need to flush a freshly queued Pong / Close / Error-Close right now, loop once before polling.
        {
            std::lock_guard<std::mutex> lock(m_outbox_mutex);
            if (!m_outbox.empty() && !ssl_want_write) continue;
        }
        if (m_secure && ssl_api.SSL_pending_fn && ssl_api.SSL_pending_fn(ssl) > 0 &&
            !received_peer_close && !fatal_error) {
            continue;
        }
        if (fatal_error && client_close_sent) break;
        if (received_peer_close && client_close_sent && pending_write.empty()) break;
        if (peer_eof && pending_write.empty()) break;

        if (has_close_deadline && std::chrono::steady_clock::now() >= close_deadline) {
            break;
        }

        int poll_timeout_ms = (m_event_sink && m_event_sink->owner_alive) ? 250 : -1;
        if (has_close_deadline) {
            auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                close_deadline - std::chrono::steady_clock::now()).count();
            int clamped = static_cast<int>(std::max<int64_t>(1, rem_ms));
            poll_timeout_ms = (poll_timeout_ms < 0) ? clamped : std::min(poll_timeout_ms, clamped);
        }

        struct pollfd pfds[2]{};
        nfds_t nfds = 1;
        pfds[0].fd = sock;
        pfds[0].events = (!pending_write.empty() || ssl_want_write) ? (POLLIN | POLLOUT) : POLLIN;
        if (m_wake_fds[0] >= 0) {
            pfds[1].fd = m_wake_fds[0];
            pfds[1].events = POLLIN;
            nfds = 2;
        }
        int pr = ::poll(pfds, nfds, poll_timeout_ms);
        if (pr < 0 && errno != EINTR) break;
        if (m_event_sink && m_event_sink->owner_alive &&
            !m_event_sink->owner_alive->load(std::memory_order_acquire)) {
            m_stop_requested.store(true, std::memory_order_release);
            break;
        }
    }

    cleanup_transport();
    m_ready_state.store(WebSocketReadyState::Closed, std::memory_order_release);

    if (received_peer_close && !fatal_error) {
        dispatch_error_and_close_on_owner("", peer_close_code, peer_close_reason, true, false);
    } else if (fatal_error) {
        dispatch_error_and_close_on_owner(fatal_msg, fatal_close_code, "", false, true);
    } else {
        dispatch_error_and_close_on_owner("WebSocket closed abnormally", 1006, "", false, !m_close_initiated.load());
    }
}

// ---------------------------------------------------------------------------
// JavaScript constructor & instance bindings
// ---------------------------------------------------------------------------

namespace {

JsValue attach_ws_js_instance(const std::shared_ptr<WebSocketObject>& ws) {
    JsValue inst = JsValue::make_object(Heap::default_heap().allocate_object());

    const uint8_t ro_flags = PropertyMap::Enumerable | PropertyMap::Configurable;
    inst.define_accessor("url",
        JsValue::make_native_function([ws](const std::vector<JsValue>&) {
            return JsValue::make_string(ws->url());
        }),
        JsValue::make_undefined(), ro_flags);
    inst.define_accessor("readyState",
        JsValue::make_native_function([ws](const std::vector<JsValue>&) {
            return JsValue::make_number(static_cast<int>(ws->js_ready_state()));
        }),
        JsValue::make_undefined(), ro_flags);
    inst.define_accessor("protocol",
        JsValue::make_native_function([ws](const std::vector<JsValue>&) {
            return JsValue::make_string(ws->js_protocol());
        }),
        JsValue::make_undefined(), ro_flags);
    inst.define_accessor("extensions",
        JsValue::make_native_function([ws](const std::vector<JsValue>&) {
            return JsValue::make_string(ws->extensions());
        }),
        JsValue::make_undefined(), ro_flags);
    inst.define_accessor("bufferedAmount",
        JsValue::make_native_function([ws](const std::vector<JsValue>&) {
            return JsValue::make_number(static_cast<double>(ws->buffered_amount()));
        }),
        JsValue::make_undefined(), ro_flags);
    inst.define_accessor("binaryType",
        JsValue::make_native_function([ws](const std::vector<JsValue>&) {
            return JsValue::make_string(ws->binary_type());
        }),
        JsValue::make_native_function([ws](const std::vector<JsValue>& args) {
            if (!args.empty()) ws->set_binary_type(args[0].to_string());
            return JsValue::make_undefined();
        }),
        ro_flags);

    inst.set_property("CONNECTING", JsValue::make_number(0));
    inst.set_property("OPEN", JsValue::make_number(1));
    inst.set_property("CLOSING", JsValue::make_number(2));
    inst.set_property("CLOSED", JsValue::make_number(3));

    inst.set_property("onopen", JsValue::make_null());
    inst.set_property("onmessage", JsValue::make_null());
    inst.set_property("onerror", JsValue::make_null());
    inst.set_property("onclose", JsValue::make_null());

    inst.set_property("send", JsValue::make_native_function([ws](const std::vector<JsValue>& sargs) {
        if (sargs.empty()) {
            js_throw_type_error("Failed to execute 'send' on 'WebSocket': 1 argument required, but only 0 present.");
        }
        if (ws->js_ready_state() == WebSocketReadyState::Connecting ||
            ws->ready_state() == WebSocketReadyState::Connecting) {
            dom::throw_dom(dom::current_realm(), "InvalidStateError",
                           "Failed to execute 'send' on 'WebSocket': Still in CONNECTING state.");
        }
        if (ws->ready_state() == WebSocketReadyState::Open) {
            ws->send(sargs[0].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("close", JsValue::make_native_function([ws](const std::vector<JsValue>& cargs) {
        int code = 1005;
        std::string reason;
        if (!cargs.empty() && !cargs[0].is_undefined()) {
            double num = cargs[0].to_number();
            if (!std::isfinite(num)) {
                dom::throw_dom(dom::current_realm(), "InvalidAccessError",
                               "Failed to execute 'close' on 'WebSocket': The close code is not a valid integer.");
            }
            int32_t truncated = static_cast<int32_t>(std::trunc(num));
            uint16_t ucode = static_cast<uint16_t>(truncated & 0xFFFF);
            if (ucode != 1000 && (ucode < 3000 || ucode > 4999)) {
                dom::throw_dom(dom::current_realm(), "InvalidAccessError",
                               "Failed to execute 'close' on 'WebSocket': The code must be either 1000, or between 3000 and 4999.");
            }
            code = static_cast<int>(ucode);
        }
        if (cargs.size() >= 2 && !cargs[1].is_undefined()) {
            reason = cargs[1].to_string();
            if (reason.size() > 123) {
                dom::throw_dom(dom::current_realm(), "SyntaxError",
                               "Failed to execute 'close' on 'WebSocket': The close reason must not be greater than 123 UTF-8 bytes.");
            }
            if (code == 1005) code = 1000;
        }
        ws->close(code, reason);
        return JsValue::make_undefined();
    }));

    inst.set_property("addEventListener", JsValue::make_native_function([ws](const std::vector<JsValue>& eargs) {
        if (eargs.size() >= 2) {
            bool once = false;
            if (eargs.size() >= 3 && eargs[2].is_object_like()) {
                once = eargs[2].get_property("once").is_truthy();
            }
            ws->add_event_listener(eargs[0].to_string(), eargs[1], once);
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("removeEventListener", JsValue::make_native_function([ws](const std::vector<JsValue>& eargs) {
        if (eargs.size() >= 2) {
            ws->remove_event_listener(eargs[0].to_string(), eargs[1]);
        }
        return JsValue::make_undefined();
    }));

    return inst;
}

} // namespace

JsValue WebSocketObject::create_js_instance(std::string_view url,
                                            std::string_view protocols,
                                            const WebSocketContext& context,
                                            const std::shared_ptr<WebSocketEventSink>& event_sink) {
    auto ws = std::make_shared<WebSocketObject>(std::string(url), std::string(protocols), context, event_sink);
    if (!ws->m_request_valid) {
        dom::throw_dom(dom::current_realm(), ws->m_validation_dom_error.c_str(), ws->m_validation_error);
    }
    JsValue inst = attach_ws_js_instance(ws);
    ws->m_owner_js->inst = inst;
    ws->connect();
    return inst;
}

JsValue WebSocketObject::create_constructor(std::function<WebSocketContext()> context_fn,
                                            std::shared_ptr<WebSocketEventSink> event_sink) {
    JsValue ctor = JsValue::make_native_function(
        [context_fn = std::move(context_fn), event_sink = std::move(event_sink)](const std::vector<JsValue>& args) {
            if (!JsValue::current_is_construct()) {
                js_throw_type_error("Failed to construct 'WebSocket': Please use the 'new' operator, this DOM object constructor cannot be called as a function.");
            }
            if (args.empty()) {
                js_throw_type_error("Failed to construct 'WebSocket': 1 argument required, but only 0 present.");
            }
            std::string url = args[0].to_string();
            std::vector<std::string> protocols;
            if (args.size() >= 2 && !args[1].is_undefined()) {
                std::string proto_err;
                if (!parse_js_protocols_arg(args[1], protocols, proto_err)) {
                    dom::throw_dom(dom::current_realm(), "SyntaxError",
                                   "Failed to construct 'WebSocket': " + proto_err);
                }
            }

            WebSocketContext ctx = context_fn ? context_fn() : WebSocketContext{};
            auto ws = std::make_shared<WebSocketObject>(std::move(url), std::move(protocols), ctx, event_sink);
            if (!ws->m_request_valid) {
                dom::throw_dom(dom::current_realm(), ws->m_validation_dom_error.c_str(),
                               "Failed to construct 'WebSocket': " + ws->m_validation_error);
            }
            JsValue inst = attach_ws_js_instance(ws);
            ws->m_owner_js->inst = inst;
            ws->connect();
            return inst;
        });

    ctor.set_property("CONNECTING", JsValue::make_number(0));
    ctor.set_property("OPEN", JsValue::make_number(1));
    ctor.set_property("CLOSING", JsValue::make_number(2));
    ctor.set_property("CLOSED", JsValue::make_number(3));
    return ctor;
}

// ---------------------------------------------------------------------------
// Loopback server helper (for in-process tests)
// ---------------------------------------------------------------------------

WebSocketObject::LoopbackServer::~LoopbackServer() {
    running = false;
    if (listen_fd >= 0) {
        ::shutdown(listen_fd, SHUT_RDWR);
        ::close(listen_fd);
        listen_fd = -1;
    }
    if (thread.joinable()) thread.join();
}

std::shared_ptr<WebSocketObject::LoopbackServer> WebSocketObject::start_loopback_server(
    std::function<void(int client_fd)> on_client) {
    auto server = std::make_shared<LoopbackServer>();
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return nullptr;

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(fd, 8) != 0) {
        ::close(fd);
        return nullptr;
    }

    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    server->port = ntohs(addr.sin_port);
    server->listen_fd = fd;

    server->thread = std::thread([server, on_client = std::move(on_client)]() {
        while (server->running.load()) {
            sockaddr_in caddr{};
            socklen_t clen = sizeof(caddr);
            int client_fd = ::accept(server->listen_fd, reinterpret_cast<sockaddr*>(&caddr), &clen);
            if (client_fd < 0) break;
            on_client(client_fd);
        }
    });

    return server;
}

} // namespace litebird
