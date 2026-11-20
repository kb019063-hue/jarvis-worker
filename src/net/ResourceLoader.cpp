#include "litebird/net/ResourceLoader.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/Brotli.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/Deflate.hpp"
#include "litebird/net/UrlParser.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace litebird {
namespace {

std::mutex g_ca_mutex;
std::string g_test_ca_bundle;

std::mutex g_origin_map_mutex;
std::unordered_map<std::string, std::string> g_origin_roots;

std::string lookup_origin_root(const std::string& origin) {
    {
        std::lock_guard<std::mutex> lock(g_origin_map_mutex);
        auto it = g_origin_roots.find(origin);
        if (it != g_origin_roots.end()) return it->second;
    }
    if (const char* env = std::getenv("LITEBIRD_ORIGIN_MAP"); env && *env) {
        std::string_view rest(env);
        while (!rest.empty()) {
            const auto semi = rest.find(';');
            const std::string_view pair = (semi == std::string_view::npos) ? rest : rest.substr(0, semi);
            rest = (semi == std::string_view::npos) ? std::string_view{} : rest.substr(semi + 1);
            const auto eq = pair.find('=');
            if (eq == std::string_view::npos) continue;
            Url u;
            if (Url::parse(std::string(pair.substr(0, eq)), nullptr, u) && u.origin() == origin) {
                return std::string(pair.substr(eq + 1));
            }
        }
    }
    return {};
}

bool percent_decode_url_path(std::string_view in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            const int hi = hex(in[i + 1]);
            const int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                const char decoded = static_cast<char>((hi << 4) | lo);
                if (decoded == '\0') return false;
                out.push_back(decoded);
                i += 2;
                continue;
            }
        }
        if (in[i] == '\0') return false;
        out.push_back(in[i]);
    }
    return true;
}

std::string get_test_ca_bundle() {
    std::lock_guard<std::mutex> lock(g_ca_mutex);
    if (!g_test_ca_bundle.empty()) return g_test_ca_bundle;
    if (const char* env = std::getenv("LITEBIRD_SSL_CA_FILE"); env && *env) {
        return std::string(env);
    }
    return {};
}

// Runtime-loaded OpenSSL 3 / 1.1 client bindings (zero -dev header dependency).
struct OpenSslApi {
    using SSL_METHOD = void;
    using SSL_CTX = void;
    using SSL = void;
    using X509_VERIFY_PARAM = void;

    void* handle{nullptr};
    bool available{false};

    const SSL_METHOD* (*TLS_client_method_fn)() = nullptr;
    SSL_CTX* (*SSL_CTX_new_fn)(const SSL_METHOD*) = nullptr;
    void (*SSL_CTX_free_fn)(SSL_CTX*) = nullptr;
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
    int (*SSL_peek_fn)(SSL*, void*, int) = nullptr;
    int (*SSL_pending_fn)(const SSL*) = nullptr;
    int (*SSL_write_fn)(SSL*, const void*, int) = nullptr;
    int (*SSL_shutdown_fn)(SSL*) = nullptr;
    int (*SSL_get_error_fn)(const SSL*, int) = nullptr;
    long (*SSL_get_verify_result_fn)(const SSL*) = nullptr;

    static const OpenSslApi& instance() {
        static const OpenSslApi api = []() {
            OpenSslApi out;
            static constexpr const char* kCandidates[] = {
                "libssl.so.3",
                "libssl.so.1.1",
                "libssl.so",
                "/usr/lib/x86_64-linux-gnu/libssl.so.3",
                "/lib/x86_64-linux-gnu/libssl.so.3",
                "/usr/lib/aarch64-linux-gnu/libssl.so.3"
            };
            for (const char* name : kCandidates) {
                out.handle = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
                if (out.handle) break;
            }
            if (!out.handle) return out;

            auto load = [&](auto& fn, const char* sym) -> bool {
                void* p = ::dlsym(out.handle, sym);
                if (!p) return false;
                std::memcpy(&fn, &p, sizeof(p));
                return true;
            };

            const bool ok =
                load(out.TLS_client_method_fn, "TLS_client_method") &&
                load(out.SSL_CTX_new_fn, "SSL_CTX_new") &&
                load(out.SSL_CTX_free_fn, "SSL_CTX_free") &&
                load(out.SSL_CTX_set_default_verify_paths_fn, "SSL_CTX_set_default_verify_paths") &&
                load(out.SSL_CTX_load_verify_locations_fn, "SSL_CTX_load_verify_locations") &&
                load(out.SSL_CTX_set_verify_fn, "SSL_CTX_set_verify") &&
                load(out.SSL_new_fn, "SSL_new") &&
                load(out.SSL_free_fn, "SSL_free") &&
                load(out.SSL_set_fd_fn, "SSL_set_fd") &&
                load(out.SSL_ctrl_fn, "SSL_ctrl") &&
                load(out.SSL_set1_host_fn, "SSL_set1_host") &&
                load(out.SSL_get0_param_fn, "SSL_get0_param") &&
                load(out.X509_VERIFY_PARAM_set1_ip_asc_fn, "X509_VERIFY_PARAM_set1_ip_asc") &&
                load(out.SSL_connect_fn, "SSL_connect") &&
                load(out.SSL_read_fn, "SSL_read") &&
                load(out.SSL_write_fn, "SSL_write") &&
                load(out.SSL_shutdown_fn, "SSL_shutdown") &&
                load(out.SSL_get_error_fn, "SSL_get_error") &&
                load(out.SSL_get_verify_result_fn, "SSL_get_verify_result");

            (void)load(out.SSL_peek_fn, "SSL_peek");
            (void)load(out.SSL_pending_fn, "SSL_pending");

            out.available = ok;
            return out;
        }();
        return api;
    }
};

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

bool is_safe_header_value(std::string_view value) {
    for (unsigned char c : value) {
        if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
    }
    return true;
}

bool is_safe_request_target(std::string_view value) {
    if (value.empty() || value.front() != '/') return false;
    for (unsigned char c : value) {
        if (c <= 0x20 || c == 0x7f) return false;
    }
    return true;
}

std::string ascii_lower(std::string_view value) {
    std::string result(value);
    for (char& c : result) {
        const auto ch = static_cast<unsigned char>(c);
        if (ch >= 'A' && ch <= 'Z') c = static_cast<char>(ch + ('a' - 'A'));
    }
    return result;
}

std::string trim_http_ws(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return std::string(sv);
}

bool has_url_scheme(std::string_view value) {
    if (value.empty()) return false;
    const auto alpha = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    if (!alpha(value.front())) return false;
    for (std::size_t i = 1; i < value.size(); ++i) {
        const char c = value[i];
        if (c == ':') return true;
        const bool scheme_char = alpha(c) || (c >= '0' && c <= '9') ||
                                 c == '+' || c == '-' || c == '.';
        if (!scheme_char) return false;
    }
    return false;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode_path(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex_digit(text[i + 1]);
            const int lo = hex_digit(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string file_path_from_url(const Url& url) {
    if (url.scheme != "file" || !url.host.empty()) return {};
    std::string path = percent_decode_path(url.pathname());
    if (path.empty() || path.find('\0') != std::string::npos) return {};
    return path;
}

// RFC 6761 §6.2 & §6.4: `.test` and `.invalid` are guaranteed never to exist
// in the public DNS; fail immediately rather than waiting on resolver timeouts.
bool is_rfc6761_nonexistent_domain(std::string_view host) {
    const std::string lower = ascii_lower(host);
    auto ends_with = [&](std::string_view suffix) {
        return lower.size() >= suffix.size() &&
               std::string_view(lower).substr(lower.size() - suffix.size()) == suffix;
    };
    return lower == "test" || ends_with(".test") || lower == "invalid" || ends_with(".invalid");
}

bool is_ip_literal(const std::string& host) {
    in_addr v4{};
    in6_addr v6{};
    return ::inet_pton(AF_INET, host.c_str(), &v4) == 1 ||
           ::inet_pton(AF_INET6, host.c_str(), &v6) == 1;
}

int connect_with_timeout(const std::string& host, int port, int timeout_ms, std::string& error_out) {
    if (is_rfc6761_nonexistent_domain(host)) {
        error_out = "Reserved non-existent domain: " + host;
        return -1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    const int status = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (status != 0 || !res) {
        error_out = "Failed to resolve host: " + host;
        return -1;
    }

    int connected_fd = -1;
    for (addrinfo* cur = res; cur != nullptr; cur = cur->ai_next) {
        const int fd = ::socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (fd < 0) continue;

        const int orig_flags = ::fcntl(fd, F_GETFL, 0);
        if (orig_flags >= 0) {
            (void)::fcntl(fd, F_SETFL, orig_flags | O_NONBLOCK);
        }

        const int rc = ::connect(fd, cur->ai_addr, cur->ai_addrlen);
        bool ok = (rc == 0);
        if (!ok && errno == EINPROGRESS) {
            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int pr = 0;
            do {
                pr = ::poll(&pfd, 1, timeout_ms);
            } while (pr < 0 && errno == EINTR);
            if (pr > 0 && (pfd.revents & POLLOUT)) {
                int so_err = 0;
                socklen_t len = sizeof(so_err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) == 0 && so_err == 0) {
                    ok = true;
                }
            }
        }

        if (ok) {
            if (orig_flags >= 0) {
                (void)::fcntl(fd, F_SETFL, orig_flags);
            }
            timeval tv{};
            tv.tv_sec = 3;
            tv.tv_usec = 0;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            connected_fd = fd;
            break;
        }
        ::close(fd);
    }

    ::freeaddrinfo(res);
    if (connected_fd < 0) {
        error_out = "Failed to connect to " + host + ":" + port_str;
    }
    return connected_fd;
}

constexpr std::size_t kMaxIdleConnectionsPerKey = 4;
constexpr std::size_t kMaxTotalIdleConnections = 32;
constexpr int kDefaultKeepAliveTimeoutSeconds = 30;
constexpr int kMaxKeepAliveTimeoutSeconds = 60;

struct HttpConnection {
    bool is_tls{false};
    int sockfd{-1};
    OpenSslApi::SSL_CTX* ssl_ctx{nullptr};
    OpenSslApi::SSL* ssl{nullptr};
    std::chrono::steady_clock::time_point expires_at{};
    int max_remaining{-1};
    std::size_t requests_served{0};
};

void close_http_connection(HttpConnection& conn, bool graceful_tls_shutdown = true) {
    if (conn.ssl) {
        const OpenSslApi& ssl_api = OpenSslApi::instance();
        if (ssl_api.available) {
            if (graceful_tls_shutdown && conn.sockfd >= 0) {
                (void)ssl_api.SSL_shutdown_fn(conn.ssl);
            }
            ssl_api.SSL_free_fn(conn.ssl);
        }
        conn.ssl = nullptr;
    }
    if (conn.ssl_ctx) {
        const OpenSslApi& ssl_api = OpenSslApi::instance();
        if (ssl_api.available) {
            ssl_api.SSL_CTX_free_fn(conn.ssl_ctx);
        }
        conn.ssl_ctx = nullptr;
    }
    if (conn.sockfd >= 0) {
        ::close(conn.sockfd);
        conn.sockfd = -1;
    }
}

bool is_http_connection_healthy(HttpConnection& conn) {
    if (conn.sockfd < 0) return false;
    if (conn.max_remaining == 0) return false;
    if (std::chrono::steady_clock::now() >= conn.expires_at) return false;

    pollfd pfd{};
    pfd.fd = conn.sockfd;
    pfd.events = POLLIN | POLLERR | POLLHUP;
    int pr = 0;
    do {
        pr = ::poll(&pfd, 1, 0);
    } while (pr < 0 && errno == EINTR);
    if (pr < 0) return false;

    if (!conn.is_tls) {
        if (pr == 0) return true;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
        // Any readable event on an idle plaintext HTTP/1.1 connection is either
        // a peer EOF or unsolicited trailing bytes (desync); discard either way.
        return false;
    }

    const OpenSslApi& ssl_api = OpenSslApi::instance();
    if (!ssl_api.available || !conn.ssl) return false;
    if (ssl_api.SSL_pending_fn && ssl_api.SSL_pending_fn(conn.ssl) > 0) {
        return false;
    }
    if (pr == 0) return true;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;

    if (pfd.revents & POLLIN) {
        char dummy = 0;
        ssize_t peek_rc = 0;
        do {
            peek_rc = ::recv(conn.sockfd, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
        } while (peek_rc < 0 && errno == EINTR);
        if (peek_rc <= 0) return false;

        if (!ssl_api.SSL_peek_fn) return false;
        const int orig_flags = ::fcntl(conn.sockfd, F_GETFL, 0);
        if (orig_flags < 0) return false;
        if (::fcntl(conn.sockfd, F_SETFL, orig_flags | O_NONBLOCK) < 0) return false;
        const int ssl_rc = ssl_api.SSL_peek_fn(conn.ssl, &dummy, 1);
        const int ssl_err = (ssl_rc <= 0) ? ssl_api.SSL_get_error_fn(conn.ssl, ssl_rc) : 0;
        (void)::fcntl(conn.sockfd, F_SETFL, orig_flags);

        // Consumed post-handshake TLS 1.3 NewSessionTicket without app data.
        return ssl_rc < 0 && ssl_err == 2; // SSL_ERROR_WANT_READ
    }
    return true;
}

class HttpConnectionPool {
public:
    ~HttpConnectionPool() {
        clear();
    }

    bool acquire(const std::string& key, HttpConnection& out) {
        std::vector<HttpConnection> to_close;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            prune_expired_locked(to_close);
            // Walk MRU (back to front) to pick the freshest idle connection for `key`.
            auto it = m_entries.end();
            while (it != m_entries.begin()) {
                --it;
                if (it->key != key) continue;
                HttpConnection candidate = std::move(it->conn);
                it = m_entries.erase(it);
                if (is_http_connection_healthy(candidate)) {
                    out = std::move(candidate);
                    found = true;
                    break;
                }
                to_close.push_back(std::move(candidate));
            }
        }
        for (auto& dead : to_close) {
            close_http_connection(dead, /*graceful_tls_shutdown=*/false);
        }
        return found;
    }

    void release(const std::string& key, HttpConnection conn) {
        if (conn.sockfd < 0 || conn.max_remaining == 0 ||
            std::chrono::steady_clock::now() >= conn.expires_at) {
            close_http_connection(conn, /*graceful_tls_shutdown=*/true);
            return;
        }
        std::vector<HttpConnection> to_close;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            prune_expired_locked(to_close);
            std::size_t key_count = 0;
            for (const auto& entry : m_entries) {
                if (entry.key == key) ++key_count;
            }
            for (auto it = m_entries.begin(); it != m_entries.end() && key_count >= kMaxIdleConnectionsPerKey; ) {
                if (it->key == key) {
                    to_close.push_back(std::move(it->conn));
                    it = m_entries.erase(it);
                    --key_count;
                } else {
                    ++it;
                }
            }
            while (m_entries.size() >= kMaxTotalIdleConnections) {
                to_close.push_back(std::move(m_entries.front().conn));
                m_entries.pop_front();
            }
            m_entries.push_back(PoolEntry{key, std::move(conn)});
        }
        for (auto& evicted : to_close) {
            close_http_connection(evicted, /*graceful_tls_shutdown=*/true);
        }
    }

    void clear() {
        std::vector<HttpConnection> to_close;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& entry : m_entries) {
                to_close.push_back(std::move(entry.conn));
            }
            m_entries.clear();
        }
        for (auto& c : to_close) {
            close_http_connection(c, /*graceful_tls_shutdown=*/true);
        }
    }

    std::size_t size() {
        std::vector<HttpConnection> to_close;
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            prune_expired_locked(to_close);
            count = m_entries.size();
        }
        for (auto& dead : to_close) {
            close_http_connection(dead, /*graceful_tls_shutdown=*/false);
        }
        return count;
    }

private:
    struct PoolEntry {
        std::string key;
        HttpConnection conn;
    };

    void prune_expired_locked(std::vector<HttpConnection>& out_dead) {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = m_entries.begin(); it != m_entries.end(); ) {
            if (it->conn.max_remaining == 0 || now >= it->conn.expires_at) {
                out_dead.push_back(std::move(it->conn));
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::mutex m_mutex;
    std::list<PoolEntry> m_entries;
};

HttpConnectionPool& http_connection_pool() {
    static HttpConnectionPool pool;
    return pool;
}

struct IncrementalChunkDecoder {
    enum class State { SizeLine, Data, DataCrlf, Trailers, Done };
    State state{State::SizeLine};
    std::size_t remaining{0};
    std::size_t trailer_bytes{0};
    std::string decoded;

    bool feed(std::string_view buf, std::size_t& pos, std::size_t max_body_bytes, std::size_t max_trailer_bytes) {
        while (pos < buf.size() && state != State::Done) {
            if (state == State::SizeLine) {
                const auto line_end = buf.find("\r\n", pos);
                if (line_end == std::string_view::npos) {
                    return (buf.size() - pos) <= 1024u;
                }
                std::string_view size_line = buf.substr(pos, line_end - pos);
                if (const auto semi = size_line.find(';'); semi != std::string_view::npos) {
                    size_line = size_line.substr(0, semi);
                }
                while (!size_line.empty() && (size_line.front() == ' ' || size_line.front() == '\t')) size_line.remove_prefix(1);
                while (!size_line.empty() && (size_line.back() == ' ' || size_line.back() == '\t')) size_line.remove_suffix(1);
                if (size_line.empty()) return false;

                std::size_t chunk_size = 0;
                for (char c : size_line) {
                    const int d = hex_digit(c);
                    if (d < 0) return false;
                    if (chunk_size > (max_body_bytes - static_cast<std::size_t>(d)) / 16u) return false;
                    chunk_size = chunk_size * 16u + static_cast<std::size_t>(d);
                }
                pos = line_end + 2;
                if (chunk_size == 0) {
                    state = State::Trailers;
                    trailer_bytes = 0;
                } else {
                    if (chunk_size > max_body_bytes - decoded.size()) return false;
                    remaining = chunk_size;
                    state = State::Data;
                }
            } else if (state == State::Data) {
                const std::size_t avail = buf.size() - pos;
                const std::size_t take = std::min(avail, remaining);
                decoded.append(buf.data() + pos, take);
                pos += take;
                remaining -= take;
                if (remaining == 0) {
                    state = State::DataCrlf;
                }
            } else if (state == State::DataCrlf) {
                if (buf.size() - pos < 2) return true;
                if (buf[pos] != '\r' || buf[pos + 1] != '\n') return false;
                pos += 2;
                state = State::SizeLine;
            } else if (state == State::Trailers) {
                const auto line_end = buf.find("\r\n", pos);
                if (line_end == std::string_view::npos) {
                    return (buf.size() - pos) <= (max_trailer_bytes - trailer_bytes);
                }
                const std::size_t line_len = line_end - pos;
                if (line_len == 0) {
                    pos = line_end + 2;
                    state = State::Done;
                    return true;
                }
                if (line_len + 2 > max_trailer_bytes - trailer_bytes) return false;
                trailer_bytes += line_len + 2;
                pos = line_end + 2;
            }
        }
        return true;
    }
};

bool is_idempotent_http_method(std::string_view method) {
    const std::string lower = ascii_lower(method);
    return lower == "get" || lower == "head" || lower == "options" ||
           lower == "put" || lower == "delete" || lower == "trace";
}

bool parse_http_version(std::string_view ver, int& major_out, int& minor_out) {
    if (ver.rfind("HTTP/", 0) != 0) return false;
    ver.remove_prefix(5);
    const auto dot = ver.find('.');
    if (dot == std::string_view::npos) {
        const auto r = std::from_chars(ver.data(), ver.data() + ver.size(), major_out);
        if (ver.empty() || r.ec != std::errc{} || r.ptr != ver.data() + ver.size()) return false;
        minor_out = 0;
        return true;
    }
    const auto mj_sv = ver.substr(0, dot);
    const auto mn_sv = ver.substr(dot + 1);
    const auto r1 = std::from_chars(mj_sv.data(), mj_sv.data() + mj_sv.size(), major_out);
    const auto r2 = std::from_chars(mn_sv.data(), mn_sv.data() + mn_sv.size(), minor_out);
    return !mj_sv.empty() && !mn_sv.empty() &&
           r1.ec == std::errc{} && r1.ptr == mj_sv.data() + mj_sv.size() &&
           r2.ec == std::errc{} && r2.ptr == mn_sv.data() + mn_sv.size();
}

bool is_redirect_status(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool cors_allows_response(
    const ResourceResponse& resp,
    const std::string& request_origin,
    bool include_credentials
) {
    auto it = resp.headers.find("access-control-allow-origin");
    if (it == resp.headers.end()) return false;
    const std::string allowed_origin = trim_http_ws(it->second);
    if (allowed_origin.empty()) return false;

    if (include_credentials) {
        if (allowed_origin == "*" || request_origin == "null") return false;
        if (allowed_origin != request_origin) return false;
        auto cred = resp.headers.find("access-control-allow-credentials");
        if (cred == resp.headers.end() || trim_http_ws(cred->second) != "true") {
            return false;
        }
        return true;
    }

    return allowed_origin == "*" || allowed_origin == request_origin;
}

double now_epoch_ms() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool is_potentially_trustworthy_url(const Url& u) {
    if (u.scheme == "https" || u.scheme == "data" || u.scheme == "file") return true;
    if (u.scheme != "http") return false;
    const std::string h = ascii_lower(u.host);
    if (h == "localhost" || h == "[::1]") return true;
    if (h.size() > 10 && h.substr(h.size() - 10) == ".localhost") return true;
    in_addr v4{};
    if (::inet_pton(AF_INET, h.c_str(), &v4) == 1) {
        return (ntohl(v4.s_addr) >> 24) == 127u;
    }
    return false;
}

std::vector<std::string> split_http_comma_tokens(std::string_view text, bool lowercase) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t start = i;
        bool in_quotes = false;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == '\\' && in_quotes && i + 1 < text.size()) {
                ++i;
            } else if (c == ',' && !in_quotes) {
                break;
            }
            ++i;
        }
        std::string item = trim_http_ws(text.substr(start, i - start));
        if (!item.empty()) {
            out.push_back(lowercase ? ascii_lower(item) : item);
        }
        if (i < text.size() && text[i] == ',') ++i;
    }
    return out;
}

bool is_cors_safelisted_method(std::string_view method) {
    return method == "GET" || method == "HEAD" || method == "POST";
}

bool has_cors_unsafe_request_header_byte(std::string_view value) {
    for (unsigned char c : value) {
        if (c < 0x20 && c != 0x09) return true;
        if (c == '"' || c == '(' || c == ')' || c == ':' || c == '<' || c == '>' ||
            c == '?' || c == '@' || c == '[' || c == '\\' || c == ']' || c == '{' ||
            c == '}' || c == 0x7f) {
            return true;
        }
    }
    return false;
}

bool is_cors_safelisted_language_value(std::string_view value) {
    for (unsigned char c : value) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') || c == ' ' || c == '*' ||
                        c == ',' || c == '-' || c == '.' || c == ';' || c == '=';
        if (!ok) return false;
    }
    return true;
}

bool is_cors_safelisted_request_header(std::string_view lower_name, std::string_view value) {
    if (value.size() > 128) return false;
    if (lower_name == "accept") {
        return !has_cors_unsafe_request_header_byte(value);
    }
    if (lower_name == "accept-language" || lower_name == "content-language") {
        return is_cors_safelisted_language_value(value);
    }
    if (lower_name == "content-type") {
        if (has_cors_unsafe_request_header_byte(value)) return false;
        const auto semi = value.find(';');
        const std::string essence = ascii_lower(trim_http_ws(
            semi == std::string_view::npos ? value : value.substr(0, semi)
        ));
        return essence == "application/x-www-form-urlencoded" ||
               essence == "multipart/form-data" ||
               essence == "text/plain";
    }
    return false;
}

std::vector<std::string> collect_cors_unsafe_request_headers(
    const std::unordered_map<std::string, std::string>& caller_headers
) {
    std::vector<std::string> unsafe_names;
    for (const auto& [name, val] : caller_headers) {
        const std::string lower = ascii_lower(name);
        if (!is_cors_safelisted_request_header(lower, val)) {
            unsafe_names.push_back(lower);
        }
    }
    std::sort(unsafe_names.begin(), unsafe_names.end());
    unsafe_names.erase(std::unique(unsafe_names.begin(), unsafe_names.end()), unsafe_names.end());
    return unsafe_names;
}

struct CorsPreflightEntry {
    std::string key;
    std::unordered_set<std::string> allowed_methods;
    std::unordered_set<std::string> allowed_headers;
    bool allow_any_method{false};
    bool allow_any_header{false};
    double expires_at_ms{0.0};
};

std::mutex g_preflight_mutex;
std::unordered_map<std::string, CorsPreflightEntry> g_preflight_cache;
constexpr std::size_t kMaxPreflightEntries = 256;

bool preflight_entry_covers(
    const CorsPreflightEntry& entry,
    const std::string& method,
    const std::vector<std::string>& unsafe_headers,
    bool include_credentials
) {
    const bool method_ok = is_cors_safelisted_method(method) ||
                           entry.allowed_methods.count(method) > 0 ||
                           (!include_credentials && entry.allow_any_method);
    if (!method_ok) return false;

    for (const auto& h : unsafe_headers) {
        if (entry.allowed_headers.count(h) > 0) continue;
        if (!include_credentials && entry.allow_any_header && h != "authorization") continue;
        return false;
    }
    return true;
}

bool ensure_cors_preflight(
    const std::string& target_url,
    const std::string& origin,
    const std::string& method,
    const std::vector<std::string>& unsafe_headers,
    bool include_credentials,
    std::string& error_out
) {
    if (origin.empty() || origin == "null") {
        // An `"null"` origin can still preflight if the server replies `Access-Control-Allow-Origin: *`
        // (when credentials are omitted), so keep going with `"null"`.
    }
    const std::string effective_origin = origin.empty() ? "null" : origin;
    const std::string cache_key =
        (include_credentials ? "1|" : "0|") + effective_origin + "|" + target_url;
    const double now_ms = now_epoch_ms();

    {
        std::lock_guard<std::mutex> lock(g_preflight_mutex);
        auto it = g_preflight_cache.find(cache_key);
        if (it != g_preflight_cache.end()) {
            if (it->second.expires_at_ms <= now_ms) {
                g_preflight_cache.erase(it);
            } else if (preflight_entry_covers(it->second, method, unsafe_headers, include_credentials)) {
                return true;
            }
        }
    }

    std::unordered_map<std::string, std::string> preflight_headers;
    preflight_headers["Origin"] = effective_origin;
    preflight_headers["Access-Control-Request-Method"] = method;
    if (!unsafe_headers.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < unsafe_headers.size(); ++i) {
            if (i > 0) joined.push_back(',');
            joined += unsafe_headers[i];
        }
        preflight_headers["Access-Control-Request-Headers"] = std::move(joined);
    }

    // WHATWG Fetch §4.5: OPTIONS preflight excludes credentials and refuses redirects.
    ResourceResponse preflight = ResourceLoader::load_http_single_hop(
        target_url, "OPTIONS", preflight_headers, ""
    );
    if (!preflight.network_ok || preflight.status_code < 200 || preflight.status_code >= 300) {
        error_out = "Blocked by CORS preflight policy: preflight OPTIONS response did not succeed";
        return false;
    }
    if (!cors_allows_response(preflight, effective_origin, include_credentials)) {
        error_out = "Blocked by CORS preflight policy: missing or mismatched Access-Control-Allow-Origin";
        return false;
    }

    CorsPreflightEntry entry;
    entry.key = cache_key;
    if (auto it = preflight.headers.find("access-control-allow-methods"); it != preflight.headers.end()) {
        for (const auto& token : split_http_comma_tokens(it->second, /*lowercase=*/false)) {
            if (token == "*" && !include_credentials) entry.allow_any_method = true;
            else entry.allowed_methods.insert(token);
        }
    }
    if (auto it = preflight.headers.find("access-control-allow-headers"); it != preflight.headers.end()) {
        for (const auto& token : split_http_comma_tokens(it->second, /*lowercase=*/true)) {
            if (token == "*" && !include_credentials) entry.allow_any_header = true;
            else entry.allowed_headers.insert(token);
        }
    }

    if (!preflight_entry_covers(entry, method, unsafe_headers, include_credentials)) {
        error_out = "Blocked by CORS preflight policy: method or request header not permitted";
        return false;
    }

    double max_age_sec = 5.0; // WHATWG Fetch default preflight max-age when header is absent
    if (auto it = preflight.headers.find("access-control-max-age"); it != preflight.headers.end()) {
        const std::string raw = trim_http_ws(it->second);
        long long parsed_sec = -1;
        const auto res = std::from_chars(raw.data(), raw.data() + raw.size(), parsed_sec);
        if (!raw.empty() && res.ec == std::errc{} && res.ptr == raw.data() + raw.size()) {
            if (parsed_sec <= 0) max_age_sec = 0.0;
            else max_age_sec = std::min<double>(static_cast<double>(parsed_sec), 86400.0);
        } else {
            max_age_sec = 0.0;
        }
    }

    if (max_age_sec > 0.0) {
        entry.expires_at_ms = now_ms + max_age_sec * 1000.0;
        std::lock_guard<std::mutex> lock(g_preflight_mutex);
        if (g_preflight_cache.size() >= kMaxPreflightEntries && !g_preflight_cache.count(cache_key)) {
            g_preflight_cache.erase(g_preflight_cache.begin());
        }
        g_preflight_cache[cache_key] = std::move(entry);
    }
    return true;
}

struct CacheControlDirectives {
    bool no_store{false};
    bool no_cache{false};
    bool must_revalidate{false};
    bool immutable{false};
    bool only_if_cached{false};
    bool has_max_age{false};
    double max_age_sec{0.0};
    bool has_s_maxage{false};
    double s_maxage_sec{0.0};
};

CacheControlDirectives parse_cache_control(std::string_view header_val) {
    CacheControlDirectives cc;
    for (const auto& item : split_http_comma_tokens(header_val, /*lowercase=*/false)) {
        const auto eq = item.find('=');
        const std::string name = ascii_lower(trim_http_ws(
            eq == std::string::npos ? std::string_view(item) : std::string_view(item).substr(0, eq)
        ));
        std::string val = (eq == std::string::npos)
                              ? std::string()
                              : trim_http_ws(std::string_view(item).substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }

        if (name == "no-store") cc.no_store = true;
        else if (name == "no-cache") cc.no_cache = true;
        else if (name == "must-revalidate") cc.must_revalidate = true;
        else if (name == "immutable") cc.immutable = true;
        else if (name == "only-if-cached") cc.only_if_cached = true;
        else if (name == "max-age" && !val.empty()) {
            long long sec = -1;
            const auto r = std::from_chars(val.data(), val.data() + val.size(), sec);
            if (r.ec == std::errc{} && r.ptr == val.data() + val.size() && sec >= 0) {
                cc.has_max_age = true;
                cc.max_age_sec = std::min<double>(static_cast<double>(sec), 315360000.0);
            }
        } else if (name == "s-maxage" && !val.empty()) {
            long long sec = -1;
            const auto r = std::from_chars(val.data(), val.data() + val.size(), sec);
            if (r.ec == std::errc{} && r.ptr == val.data() + val.size() && sec >= 0) {
                cc.has_s_maxage = true;
                cc.s_maxage_sec = std::min<double>(static_cast<double>(sec), 315360000.0);
            }
        }
    }
    return cc;
}

std::string find_header_ci(
    const std::unordered_map<std::string, std::string>& headers,
    std::string_view lower_target
) {
    for (const auto& [k, v] : headers) {
        if (ascii_lower(k) == lower_target) return v;
    }
    return {};
}

bool has_header_ci(
    const std::unordered_map<std::string, std::string>& headers,
    std::string_view lower_target
) {
    for (const auto& [k, _] : headers) {
        if (ascii_lower(k) == lower_target) return true;
    }
    return false;
}

void ingest_response_cookies(const ResourceResponse& resp, const std::string& request_url) {
    for (const auto& [hname, hval] : resp.header_list) {
        if (hname == "set-cookie") {
            CookieJar::default_jar().set_cookie_from_header(hval, request_url);
        }
    }
}

ResourceResponse strip_set_cookie_headers_copy(const ResourceResponse& src) {
    ResourceResponse copy = src;
    copy.headers.erase("set-cookie");
    copy.headers.erase("set-cookie2");
    copy.header_list.erase(
        std::remove_if(copy.header_list.begin(), copy.header_list.end(), [](const auto& kv) {
            return kv.first == "set-cookie" || kv.first == "set-cookie2";
        }),
        copy.header_list.end()
    );
    return copy;
}

struct HttpCacheEntry {
    std::string partition;
    std::string url;
    ResourceResponse response;
    std::string etag;
    std::string last_modified;
    std::vector<std::pair<std::string, std::string>> vary_pairs;
    double stored_at_ms{0.0};
    double response_date_ms{0.0};
    bool has_response_date{false};
    double age_header_sec{0.0};
    bool has_explicit_freshness{false};
    double freshness_lifetime_ms{0.0};
    bool no_cache{false};
    bool immutable{false};
};

constexpr std::size_t kMaxCacheEntries = 256;
constexpr std::size_t kMaxCacheEntryBodyBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxCacheTotalBodyBytes = 32u * 1024u * 1024u;

std::mutex g_http_cache_mutex;
std::list<HttpCacheEntry> g_http_cache_lru;
std::size_t g_http_cache_total_bytes = 0;

bool compute_cache_metadata(
    const ResourceResponse& resp,
    const std::unordered_map<std::string, std::string>& req_headers,
    double stored_ms,
    HttpCacheEntry& out
) {
    CacheControlDirectives cc;
    if (auto it = resp.headers.find("cache-control"); it != resp.headers.end()) {
        cc = parse_cache_control(it->second);
    }
    if (cc.no_store) return false;

    bool pragma_no_cache = false;
    if (auto it = resp.headers.find("pragma"); it != resp.headers.end()) {
        if (ascii_lower(it->second).find("no-cache") != std::string::npos) {
            pragma_no_cache = true;
        }
    }

    out.vary_pairs.clear();
    if (auto it = resp.headers.find("vary"); it != resp.headers.end()) {
        for (const auto& vname : split_http_comma_tokens(it->second, /*lowercase=*/true)) {
            if (vname == "*") return false;
            out.vary_pairs.emplace_back(vname, find_header_ci(req_headers, vname));
        }
    }

    out.etag.clear();
    if (auto it = resp.headers.find("etag"); it != resp.headers.end()) {
        out.etag = trim_http_ws(it->second);
    }
    out.last_modified.clear();
    if (auto it = resp.headers.find("last-modified"); it != resp.headers.end()) {
        out.last_modified = trim_http_ws(it->second);
    }

    out.stored_at_ms = stored_ms;
    out.has_response_date = false;
    out.response_date_ms = stored_ms;
    if (auto it = resp.headers.find("date"); it != resp.headers.end()) {
        const double d = js_parse_date_string(it->second);
        if (!std::isnan(d)) {
            out.has_response_date = true;
            out.response_date_ms = d;
        }
    }

    out.age_header_sec = 0.0;
    if (auto it = resp.headers.find("age"); it != resp.headers.end()) {
        const std::string raw = trim_http_ws(it->second);
        long long sec = 0;
        const auto r = std::from_chars(raw.data(), raw.data() + raw.size(), sec);
        if (!raw.empty() && r.ec == std::errc{} && r.ptr == raw.data() + raw.size() && sec > 0) {
            out.age_header_sec = static_cast<double>(sec);
        }
    }

    out.no_cache = cc.no_cache || (!cc.has_max_age && pragma_no_cache);
    out.immutable = cc.immutable;
    out.has_explicit_freshness = false;
    out.freshness_lifetime_ms = 0.0;

    if (cc.has_max_age) {
        out.has_explicit_freshness = true;
        out.freshness_lifetime_ms = cc.max_age_sec * 1000.0;
    } else if (auto exp_it = resp.headers.find("expires"); exp_it != resp.headers.end()) {
        out.has_explicit_freshness = true;
        const double exp_ms = js_parse_date_string(exp_it->second);
        const double base_ms = out.has_response_date ? out.response_date_ms : stored_ms;
        if (!std::isnan(exp_ms) && exp_ms > base_ms) {
            out.freshness_lifetime_ms = exp_ms - base_ms;
        } else {
            out.freshness_lifetime_ms = 0.0;
        }
    } else if (cc.immutable) {
        out.has_explicit_freshness = true;
        out.freshness_lifetime_ms = 31536000.0 * 1000.0;
    }

    // Only store responses that declare explicit freshness or revalidation validators,
    // so unannotated dynamic responses continue hitting the origin as expected.
    if (!out.has_explicit_freshness && out.etag.empty() && out.last_modified.empty()) {
        return false;
    }

    out.response = strip_set_cookie_headers_copy(resp);
    out.response.from_cache = false;
    out.response.revalidated = false;
    return out.response.body.size() <= kMaxCacheEntryBodyBytes;
}

bool cache_entry_is_fresh(const HttpCacheEntry& entry, double now_ms, const CacheControlDirectives& req_cc) {
    if (entry.no_cache || !entry.has_explicit_freshness) return false;
    const double apparent_age_ms = entry.has_response_date
                                       ? std::max(0.0, entry.stored_at_ms - entry.response_date_ms)
                                       : 0.0;
    const double initial_age_ms = std::max(apparent_age_ms, entry.age_header_sec * 1000.0);
    const double current_age_ms = initial_age_ms + std::max(0.0, now_ms - entry.stored_at_ms);

    if (req_cc.has_max_age && current_age_ms >= req_cc.max_age_sec * 1000.0) {
        return false;
    }
    if (entry.immutable && !req_cc.has_max_age) {
        return true;
    }
    return current_age_ms < entry.freshness_lifetime_ms;
}

bool vary_matches_request(
    const HttpCacheEntry& entry,
    const std::unordered_map<std::string, std::string>& req_headers
) {
    for (const auto& [vname, expected_val] : entry.vary_pairs) {
        if (find_header_ci(req_headers, vname) != expected_val) {
            return false;
        }
    }
    return true;
}

void store_http_cache_entry(HttpCacheEntry&& entry) {
    std::lock_guard<std::mutex> lock(g_http_cache_mutex);
    for (auto it = g_http_cache_lru.begin(); it != g_http_cache_lru.end(); ) {
        if (it->partition == entry.partition && it->url == entry.url &&
            it->vary_pairs == entry.vary_pairs) {
            g_http_cache_total_bytes -= it->response.body.size();
            it = g_http_cache_lru.erase(it);
        } else {
            ++it;
        }
    }
    while (!g_http_cache_lru.empty() &&
           (g_http_cache_lru.size() >= kMaxCacheEntries ||
            g_http_cache_total_bytes + entry.response.body.size() > kMaxCacheTotalBodyBytes)) {
        g_http_cache_total_bytes -= g_http_cache_lru.back().response.body.size();
        g_http_cache_lru.pop_back();
    }
    g_http_cache_total_bytes += entry.response.body.size();
    g_http_cache_lru.push_front(std::move(entry));
}

void invalidate_http_cache_url(const std::string& target_url) {
    std::lock_guard<std::mutex> lock(g_http_cache_mutex);
    for (auto it = g_http_cache_lru.begin(); it != g_http_cache_lru.end(); ) {
        if (it->url == target_url) {
            g_http_cache_total_bytes -= it->response.body.size();
            it = g_http_cache_lru.erase(it);
        } else {
            ++it;
        }
    }
}

ResourceResponse merge_304_headers(const ResourceResponse& cached_200, const ResourceResponse& resp_304) {
    ResourceResponse merged = cached_200;
    std::unordered_set<std::string> updated_names;
    for (const auto& [hname, _] : resp_304.header_list) {
        if (hname == "set-cookie" || hname == "set-cookie2" ||
            hname == "content-length" || hname == "content-encoding" ||
            hname == "transfer-encoding") {
            continue;
        }
        updated_names.insert(hname);
    }

    if (!updated_names.empty()) {
        merged.header_list.erase(
            std::remove_if(merged.header_list.begin(), merged.header_list.end(), [&](const auto& kv) {
                return updated_names.count(kv.first) > 0;
            }),
            merged.header_list.end()
        );
        for (const auto& name : updated_names) {
            merged.headers.erase(name);
        }
        for (const auto& [hname, hval] : resp_304.header_list) {
            if (updated_names.count(hname) == 0) continue;
            merged.header_list.emplace_back(hname, hval);
            auto it = merged.headers.find(hname);
            if (it != merged.headers.end()) it->second += ", " + hval;
            else merged.headers[hname] = hval;
            if (hname == "content-type") merged.content_type = hval;
        }
    }
    return merged;
}

ResourceResponse execute_http_hop_with_cache(
    const std::string& hop_address,
    const std::string& method,
    const std::unordered_map<std::string, std::string>& request_headers,
    const std::string& body,
    CacheMode cache_mode,
    const std::string& cache_partition,
    bool ingest_cookies,
    const std::function<bool(const ResourceResponse&, std::string&)>& allow_response_fn
) {
    CacheControlDirectives req_cc;
    if (const std::string raw_cc = find_header_ci(request_headers, "cache-control"); !raw_cc.empty()) {
        req_cc = parse_cache_control(raw_cc);
    }
    const bool req_pragma_no_cache =
        ascii_lower(find_header_ci(request_headers, "pragma")).find("no-cache") != std::string::npos;

    const bool is_get = (method == "GET");
    const bool bypass_read = (cache_mode == CacheMode::NoStore || cache_mode == CacheMode::Reload || req_cc.no_store);
    const bool force_revalidate = (cache_mode == CacheMode::NoCache || req_cc.no_cache || req_pragma_no_cache ||
                                   (req_cc.has_max_age && req_cc.max_age_sec == 0.0));

    bool have_stale_candidate = false;
    HttpCacheEntry stale_candidate;
    const double lookup_ms = now_epoch_ms();

    if (is_get && !bypass_read) {
        std::lock_guard<std::mutex> lock(g_http_cache_mutex);
        for (auto it = g_http_cache_lru.begin(); it != g_http_cache_lru.end(); ++it) {
            if (it->partition != cache_partition || it->url != hop_address) continue;
            if (!vary_matches_request(*it, request_headers)) continue;

            if (cache_mode == CacheMode::ForceCache ||
                (!force_revalidate && cache_entry_is_fresh(*it, lookup_ms, req_cc))) {
                HttpCacheEntry hit_entry = *it;
                g_http_cache_lru.splice(g_http_cache_lru.begin(), g_http_cache_lru, it);
                ResourceResponse hit = std::move(hit_entry.response);
                std::string deny_reason;
                if (allow_response_fn && !allow_response_fn(hit, deny_reason)) {
                    ResourceResponse denied;
                    denied.error_message = deny_reason;
                    return denied;
                }
                hit.from_cache = true;
                hit.revalidated = false;
                return hit;
            }

            stale_candidate = *it;
            have_stale_candidate = true;
            break;
        }
    }

    if (is_get && req_cc.only_if_cached && !have_stale_candidate) {
        ResourceResponse miss;
        miss.status_code = 504;
        miss.status_text = "Gateway Timeout";
        miss.error_message = "Resource not in HTTP cache (only-if-cached)";
        return miss;
    }

    std::unordered_map<std::string, std::string> wire_headers = request_headers;
    const bool caller_supplied_validators =
        has_header_ci(request_headers, "if-none-match") ||
        has_header_ci(request_headers, "if-modified-since");
    const bool sent_revalidation =
        have_stale_candidate && !caller_supplied_validators &&
        (!stale_candidate.etag.empty() || !stale_candidate.last_modified.empty());

    if (sent_revalidation) {
        if (!stale_candidate.etag.empty()) {
            wire_headers["If-None-Match"] = stale_candidate.etag;
        }
        if (!stale_candidate.last_modified.empty()) {
            wire_headers["If-Modified-Since"] = stale_candidate.last_modified;
        }
    }

    ResourceResponse net_resp = ResourceLoader::load_http_single_hop(
        hop_address, method, wire_headers, body
    );
    if (!net_resp.network_ok) {
        return net_resp;
    }

    if (sent_revalidation && net_resp.status_code == 304) {
        ResourceResponse merged = merge_304_headers(stale_candidate.response, net_resp);
        std::string deny_reason;
        if (allow_response_fn && !allow_response_fn(merged, deny_reason)) {
            ResourceResponse denied;
            denied.error_message = deny_reason;
            return denied;
        }
        if (ingest_cookies) {
            ingest_response_cookies(net_resp, hop_address);
        }

        const double refreshed_ms = now_epoch_ms();
        HttpCacheEntry refreshed;
        refreshed.partition = cache_partition;
        refreshed.url = hop_address;
        if (cache_mode != CacheMode::NoStore && !req_cc.no_store &&
            compute_cache_metadata(merged, request_headers, refreshed_ms, refreshed)) {
            store_http_cache_entry(std::move(refreshed));
        } else {
            invalidate_http_cache_url(hop_address);
        }

        merged.from_cache = true;
        merged.revalidated = true;
        return merged;
    }

    std::string deny_reason;
    if (allow_response_fn && !allow_response_fn(net_resp, deny_reason)) {
        ResourceResponse denied;
        denied.error_message = deny_reason;
        return denied;
    }

    if (ingest_cookies) {
        ingest_response_cookies(net_resp, hop_address);
    }

    const std::string lower_method = ascii_lower(method);
    const bool is_unsafe_method =
        (lower_method == "post" || lower_method == "put" ||
         lower_method == "delete" || lower_method == "patch");
    if (is_unsafe_method && net_resp.status_code >= 200 && net_resp.status_code < 400) {
        invalidate_http_cache_url(hop_address);
    }

    if (is_get && net_resp.status_code == 200 && cache_mode != CacheMode::NoStore && !req_cc.no_store) {
        HttpCacheEntry new_entry;
        new_entry.partition = cache_partition;
        new_entry.url = hop_address;
        if (compute_cache_metadata(net_resp, request_headers, now_epoch_ms(), new_entry)) {
            store_http_cache_entry(std::move(new_entry));
        }
    }

    return net_resp;
}

// ---------------------------------------------------------------------------
// HTTP authentication challenges (RFC 7617 Basic, RFC 7616 Digest)
// ---------------------------------------------------------------------------
//
// A 401 is not an ordinary failure: RFC 9110 §15.5.2 says the server is asking
// for credentials, and the challenge in `WWW-Authenticate` says which kind.
// Nothing here fetches pre-emptively — the first request carries no
// `Authorization`, which is what a browser does and what keeps a credential
// from being offered to a server that never asked (RFC 7617 §2.2).
//
// The digests are written out rather than taken from a library: MD5 (RFC 1321)
// and SHA-256 (FIPS 180-4) are each a few dozen lines, and RFC 7616 §3.9.1
// publishes the expected answers, so the test can hold this code to a value it
// did not compute.

std::string auth_hex(std::string_view digest) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (unsigned char byte : digest) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

// RFC 1321. Four rounds over a 16-word state; the message is padded with a 1
// bit, zeros, and the length in bits little-endian.
std::string auth_md5(std::string_view message) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
        0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
        0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
        0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    std::string msg(message);
    const uint64_t bit_length = static_cast<uint64_t>(msg.size()) * 8ull;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back(static_cast<char>(0));
    for (int i = 0; i < 8; ++i) msg.push_back(static_cast<char>((bit_length >> (8 * i)) & 0xff));

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + chunk + i * 4);
            M[i] = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; ++i) {
            uint32_t F;
            int g;
            if (i < 16) {
                F = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                F = (D & B) | (~D & C);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                F = B ^ C ^ D;
                g = (3 * i + 5) % 16;
            } else {
                F = C ^ (B | ~D);
                g = (7 * i) % 16;
            }
            F = F + A + K[i] + M[g];
            A = D;
            D = C;
            C = B;
            B = B + ((F << S[i]) | (F >> (32 - S[i])));
        }
        a0 += A;
        b0 += B;
        c0 += C;
        d0 += D;
    }

    std::string out;
    for (uint32_t word : {a0, b0, c0, d0}) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((word >> (8 * i)) & 0xff));
    }
    return out;
}

// FIPS 180-4 §6.2. Two rounds of 48/64 words; the message is padded with a 1
// bit, zeros, and the length in bits big-endian.
std::string auth_sha256(std::string_view message) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    std::string msg(message);
    const uint64_t bit_length = static_cast<uint64_t>(msg.size()) * 8ull;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back(static_cast<char>(0));
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bit_length >> (8 * i)) & 0xff));

    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + chunk + i * 4);
            w[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::string out;
    for (uint32_t word : h) {
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((word >> (8 * i)) & 0xff));
    }
    return out;
}

// FIPS 180-4 §6.7, SHA-512/256: SHA-512's compression function with its own
// initial state and the 256-bit truncation. It is *not* "SHA-512 cut short" —
// §5.3.6.2 gives the IV, and taking the first half of SHA-512's output is a
// different value. RFC 7616 §3.2 names it `SHA-512-256` as the backup
// algorithm to SHA-256, and §3.9.2 exercises it.
std::string auth_sha512_256(std::string_view message) {
    static const uint64_t K[80] = {
        0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
        0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
        0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
        0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
        0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
        0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
        0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
        0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
        0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
        0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
        0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
        0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
        0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
        0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
        0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
        0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
        0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
        0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
        0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
        0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
    // FIPS 180-4 §5.3.6.2: SHA-512/t's IV is SHA-512's IV with each word
    // XORed with 0xa5a5a5a5a5a5a5a5, then run through SHA-512 over
    // "SHA-512/t" and kept. For t = 256 the result is these eight words,
    // which the standard publishes.
    uint64_t h[8] = {0x22312194fc2bf72cull, 0x9f555fa3c84c64c2ull, 0x2393b86b6f53b151ull,
                     0x963877195940eabdull, 0x96283ee2a88effe3ull, 0xbe5e1e2553863992ull,
                     0x2b0199fc2c85b8aaull, 0x0eb72ddc81c52ca2ull};

    std::string msg(message);
    // SHA-512 pads with a 128-bit length; a 64-bit one fills the low half and
    // the high half is zero for anything this client can hold.
    const uint64_t bit_length = static_cast<uint64_t>(msg.size()) * 8ull;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 128 != 112) msg.push_back(static_cast<char>(0));
    for (int i = 0; i < 8; ++i) msg.push_back(static_cast<char>(0));
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bit_length >> (8 * i)) & 0xff));

    auto rotr = [](uint64_t x, int n) { return (x >> n) | (x << (64 - n)); };
    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + chunk + i * 8);
            w[i] = 0;
            for (int b = 0; b < 8; ++b) w[i] = (w[i] << 8) | p[b];
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = rotr(w[i - 15], 1) ^ rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = rotr(w[i - 2], 19) ^ rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            const uint64_t S1 = rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41);
            const uint64_t ch = (e & f) ^ (~e & g);
            const uint64_t temp1 = hh + S1 + ch + K[i] + w[i];
            const uint64_t S0 = rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39);
            const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    // §6.7: the leftmost 256 bits, which is the first four words.
    std::string out;
    for (int word = 0; word < 4; ++word) {
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((h[word] >> (8 * i)) & 0xff));
    }
    return out;
}

// RFC 4648 §4, with padding. Used for `Basic`.
std::string auth_base64(std::string_view bytes) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < bytes.size()) {
        const uint32_t triple = (static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16) |
                                (static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8) |
                                static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 2]));
        out.push_back(table[(triple >> 18) & 0x3f]);
        out.push_back(table[(triple >> 12) & 0x3f]);
        out.push_back(table[(triple >> 6) & 0x3f]);
        out.push_back(table[triple & 0x3f]);
        i += 3;
    }
    if (i + 1 == bytes.size()) {
        const uint32_t triple = static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16;
        out.push_back(table[(triple >> 18) & 0x3f]);
        out.push_back(table[(triple >> 12) & 0x3f]);
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const uint32_t triple = (static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16) |
                                (static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8);
        out.push_back(table[(triple >> 18) & 0x3f]);
        out.push_back(table[(triple >> 12) & 0x3f]);
        out.push_back(table[(triple >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

/// The digest for `algorithm`, which is the RFC 7616 §3.2 set: MD5 (backward
/// compatibility), SHA-256 (mandatory to implement), and SHA-512-256 (the
/// backup). Returns false for anything else, because guessing a weak digest
/// for an unknown name is worse than not answering.
bool auth_digest(const std::string& algorithm, std::string_view data, std::string& out) {
    if (algorithm == "MD5") out = auth_hex(auth_md5(data));
    else if (algorithm == "SHA-256") out = auth_hex(auth_sha256(data));
    else if (algorithm == "SHA-512-256") out = auth_hex(auth_sha512_256(data));
    else return false;
    return true;
}

std::string auth_digest_hex(const std::string& algorithm, std::string_view data) {
    std::string out;
    auth_digest(algorithm, data, out);
    return out;
}

/// The three names RFC 7616 §3.2 defines and this client computes.
bool auth_digest_name_supported(const std::string& base) {
    return base == "MD5" || base == "SHA-256" || base == "SHA-512-256";
}

/// A `-sess` algorithm's A1 folds the nonce and cnonce in through the digest
/// itself (RFC 7616 §3.4.2), and it is *that* digest, not MD5 unconditionally:
/// `SHA-256-sess` hashes with SHA-256. Anything that is not one of the three
/// names, with or without a `-SESS` suffix, is refused.
bool auth_split_algorithm(const std::string& algorithm, std::string& base, bool& sess) {
    std::string upper = algorithm;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    sess = upper.size() > 5 && upper.compare(upper.size() - 5, 5, "-SESS") == 0;
    base = sess ? upper.substr(0, upper.size() - 5) : upper;
    return base == "MD5" || base == "SHA-256" || base == "SHA-512-256";
}

// RFC 9110 §11.6.1: a challenge is a scheme token and a comma-separated list
// of parameters. `token68` (Basic's bare token) is not used by either scheme
// here, and an unknown scheme is kept only so that its name can be reported.
struct AuthChallenge {
    std::string scheme;
    std::unordered_map<std::string, std::string> params;
};

std::string auth_unquote(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        std::string out;
        for (std::size_t i = 1; i + 1 < value.size(); ++i) {
            if (value[i] == '\\' && i + 2 < value.size()) {
                out.push_back(value[++i]);
            } else {
                out.push_back(value[i]);
            }
        }
        return out;
    }
    return std::string(value);
}

/// Every challenge in one `WWW-Authenticate` field, in the order the server
/// wrote them. Parsing is deliberately forgiving: a malformed parameter ends
/// that challenge rather than the whole header.
std::vector<AuthChallenge> auth_parse_challenges(std::string_view field) {
    std::vector<AuthChallenge> challenges;
    std::size_t pos = 0;
    auto skip_ws = [&] {
        while (pos < field.size() && (field[pos] == ' ' || field[pos] == '\t' || field[pos] == ',')) ++pos;
    };
    while (pos < field.size()) {
        skip_ws();
        if (pos >= field.size()) break;
        const std::size_t scheme_start = pos;
        while (pos < field.size() && field[pos] != ' ' && field[pos] != '\t' && field[pos] != ',') ++pos;
        AuthChallenge challenge;
        challenge.scheme = std::string(field.substr(scheme_start, pos - scheme_start));
        if (challenge.scheme.empty()) {
            ++pos;
            continue;
        }
        // Parameters run until the next scheme token. A `key=value` pair is a
        // parameter; a bare token followed by `=`-less text at the top level
        // starts the next challenge.
        while (pos < field.size()) {
            while (pos < field.size() && (field[pos] == ' ' || field[pos] == '\t' || field[pos] == ',')) ++pos;
            if (pos >= field.size()) break;
            const std::size_t name_start = pos;
            while (pos < field.size() && (std::isalnum(static_cast<unsigned char>(field[pos])) ||
                                          field[pos] == '-' || field[pos] == '_')) {
                ++pos;
            }
            if (pos == name_start) break;
            std::string name = ascii_lower(field.substr(name_start, pos - name_start));
            std::size_t look = pos;
            while (look < field.size() && (field[look] == ' ' || field[look] == '\t')) ++look;
            if (look >= field.size() || field[look] != '=') {
                // A bare token: the next challenge's scheme.
                pos = name_start;
                break;
            }
            pos = look + 1;
            while (pos < field.size() && (field[pos] == ' ' || field[pos] == '\t')) ++pos;
            std::string value;
            if (pos < field.size() && field[pos] == '"') {
                const std::size_t quote_start = pos++;
                while (pos < field.size()) {
                    if (field[pos] == '\\' && pos + 1 < field.size()) {
                        pos += 2;
                        continue;
                    }
                    if (field[pos] == '"') {
                        ++pos;
                        break;
                    }
                    ++pos;
                }
                value = auth_unquote(field.substr(quote_start, pos - quote_start));
            } else {
                const std::size_t value_start = pos;
                while (pos < field.size() && field[pos] != ',' && field[pos] != ' ' && field[pos] != '\t') ++pos;
                value = std::string(field.substr(value_start, pos - value_start));
            }
            challenge.params[name] = value;
        }
        challenges.push_back(std::move(challenge));
    }
    return challenges;
}

/// The credentials a server asked for: one entry per (origin, realm), held for
/// the life of the process. RFC 7617 §2.2 says the user is asked for them
/// again per realm, and a browser keeps them until the session ends; this is
/// that store, without the prompt (an embedder fills it).
struct AuthCredentials {
    std::string username;
    std::string password;
};

std::mutex g_auth_mutex;
std::unordered_map<std::string, AuthCredentials> g_auth_store;

std::string auth_store_key(std::string_view origin, std::string_view realm) {
    return std::string(origin) + "\n" + std::string(realm);
}

/// RFC 7616 §3.5: the `nextnonce` a server names in `Authentication-Info` is
/// the nonce to use for the next request in this protection space. A server
/// that issues one-time nonces needs it to be used; a server that does not
/// issues none and nothing is stored. The nonce count restarts at 1 for a new
/// nonce (§3.4.4: nc counts requests sent *with that nonce*).
/// The nonce a protection space told us to use next, and the count of requests
/// already sent with it. RFC 7616 §3.4.4: `nc` counts the requests sent *with
/// that nonce*, so a new nonce starts again at 1.
struct AuthNonceState {
    std::string nonce;
    uint32_t used = 0;
};

std::mutex g_nonce_mutex;
std::unordered_map<std::string, AuthNonceState> g_nonce_store;

std::string auth_next_nonce_for(std::string_view origin, std::string_view realm) {
    std::lock_guard<std::mutex> lock(g_nonce_mutex);
    auto it = g_nonce_store.find(auth_store_key(origin, realm));
    return it == g_nonce_store.end() ? std::string() : it->second.nonce;
}

void auth_remember_next_nonce(std::string_view origin, std::string_view realm, std::string nonce) {
    std::lock_guard<std::mutex> lock(g_nonce_mutex);
    AuthNonceState& state = g_nonce_store[auth_store_key(origin, realm)];
    if (state.nonce != nonce) {
        state.nonce = std::move(nonce);
        state.used = 0;    // a different nonce is a different count
    }
}

/// The `nc` for the next request that uses `nonce` in this space: the count of
/// requests already sent with it, plus one, as eight lower-case hex digits.
std::string auth_nonce_count(std::string_view origin, std::string_view realm,
                             const std::string& nonce) {
    std::lock_guard<std::mutex> lock(g_nonce_mutex);
    AuthNonceState& state = g_nonce_store[auth_store_key(origin, realm)];
    if (state.nonce != nonce) {
        state.nonce = nonce;
        state.used = 0;
    }
    ++state.used;
    char digits[9];
    std::snprintf(digits, sizeof(digits), "%08x", state.used);
    return std::string(digits);
}

/// Reads `Authentication-Info` off a response that authenticated, keeping only
/// the `nextnonce` this client can use. `rspauth` is not checked: it proves the
/// server knows the password, and this client has no use for that proof.
void auth_note_authentication_info(const ResourceResponse& resp, const std::string& origin,
                                   const std::vector<AuthChallenge>& challenges) {
    if (resp.status_code < 200 || resp.status_code >= 300) return;
    std::string realm;
    for (const AuthChallenge& challenge : challenges) {
        auto it = challenge.params.find("realm");
        if (it != challenge.params.end()) { realm = it->second; break; }
    }
    if (realm.empty()) return;
    for (const auto& [name, value] : resp.header_list) {
        if (ascii_lower(name) != "authentication-info") continue;
        // The same parameter syntax as a challenge, without a scheme token:
        // read it as one by giving it a placeholder, which is not reported.
        AuthChallenge info;
        info.scheme = "Digest";
        for (const AuthChallenge& parsed : auth_parse_challenges("Digest " + value)) {
            info.params = parsed.params;
            break;
        }
        auto it = info.params.find("nextnonce");
        if (it != info.params.end() && !it->second.empty() && is_safe_header_value(it->second)) {
            auth_remember_next_nonce(origin, realm, it->second);
        }
    }
}

/// RFC 7616 §3.4: the `username` in a Digest response is sent in the clear
/// unless `userhash` is used, so both fields are checked against the header
/// rules before they go into one.
bool auth_is_header_safe(std::string_view value) {
    return is_safe_header_value(value);
}


/// The `uri` a Digest response is computed over: the absolute path and the
/// query, as RFC 7616 §3.4.3 requires (the origin is not part of it).
std::string auth_request_target(const Url& url) {
    std::string target = url.pathname();
    if (target.empty()) target = "/";
    if (url.has_query) target += "?" + url.query;
    return target;
}

/// Records what a `401` asked for on the response, for the embedder and for
/// `auth_authorization_for`. Returns the parsed challenges.
std::vector<AuthChallenge> auth_note_challenge(ResourceResponse& resp) {
    std::vector<AuthChallenge> challenges;
    if (resp.status_code != 401) return challenges;
    for (const auto& [name, value] : resp.header_list) {
        if (ascii_lower(name) != "www-authenticate") continue;
        auto parsed = auth_parse_challenges(value);
        for (auto& challenge : parsed) {
            resp.auth_schemes.push_back(challenge.scheme);
            if (resp.auth_realm.empty()) {
                auto it = challenge.params.find("realm");
                if (it != challenge.params.end()) resp.auth_realm = it->second;
            }
            challenges.push_back(std::move(challenge));
        }
    }
    return challenges;
}

/// A `407` is the proxy's challenge, not the document's (RFC 9110 §15.5.8),
/// and it is answered with `Proxy-Authorization` rather than `Authorization`.
/// This client does not send to proxies; what it does is report what the proxy
/// asked for, so an embedder can tell a proxy that wants credentials from a
/// server that does.
void auth_note_proxy_challenge(ResourceResponse& resp) {
    if (resp.status_code != 407) return;
    for (const auto& [name, value] : resp.header_list) {
        if (ascii_lower(name) != "proxy-authenticate") continue;
        for (auto& challenge : auth_parse_challenges(value)) {
            resp.proxy_auth_schemes.push_back(challenge.scheme);
            if (resp.proxy_auth_realm.empty()) {
                auto it = challenge.params.find("realm");
                if (it != challenge.params.end()) resp.proxy_auth_realm = it->second;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Turning a challenge into an `Authorization` value
// ---------------------------------------------------------------------------

// RFC 7617 §2: `Basic base64(user:password)`. The colon is the separator, so a
// user name containing one would change what it means: refused here.
bool auth_basic_value(const AuthCredentials& creds, std::string& out) {
    if (creds.username.find(':') != std::string::npos) return false;
    out = "Basic " + auth_base64(creds.username + ":" + creds.password);
    return true;
}

std::string auth_quote(std::string_view value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

/// A random 16-byte client nonce, hex. `qop=auth` requires one (RFC 7616
/// §3.4.6) and it must not repeat, so this is read from the system's CSPRNG.
std::string auth_client_nonce() {
    std::string bytes(16, '\0');
    bool filled = false;
    if (FILE* f = std::fopen("/dev/urandom", "rb")) {
        filled = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
        std::fclose(f);
    }
    if (!filled) {
        // No CSPRNG: a predictable nonce is worse than none only for replay,
        // and the server rejects a repeated one, so time is the fallback.
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<char>((std::chrono::steady_clock::now().time_since_epoch().count() >> (i * 3)) & 0xff);
        }
    }
    return auth_hex(bytes);
}

/// RFC 7616 §3.4.1: `cnonce` is counted in the digest, so the client and the
/// server must agree on the octet it was built from. `nc` is the request
/// counter in eight hex digits.
/// The Digest response and the header that carries it (RFC 7616 §3.4). One
/// builder for every caller, so the loader and the exported test entry point
/// cannot drift: the algorithm set, the `-sess` fold, `auth-int` and `userhash`
/// are decided once, here.
///
/// `entity_body` is the request body, which only `qop=auth-int` digests; it is
/// passed as the bytes that would go on the wire before transfer-encoding.
bool auth_digest_build(const std::string& algorithm,
                       const std::string& username, const std::string& password,
                       const std::string& realm, const std::string& nonce,
                       const std::string& opaque,
                       const std::string& method, const std::string& request_target,
                       const std::string& qop, const std::string& cnonce,
                       const std::string& nc,
                       std::string_view entity_body, bool hash_username,
                       std::string& out, std::string* out_response_hex = nullptr) {
    std::string base;
    bool sess = false;
    if (!auth_split_algorithm(algorithm, base, sess)) return false;
    if (!auth_digest_name_supported(base)) return false;
    // §3.4.1: the response is defined for `auth` and `auth-int`, and for no
    // qop at all (the RFC 2069 form). Anything else has no definition to
    // compute, so it is refused rather than guessed at.
    if (!qop.empty() && qop != "auth" && qop != "auth-int") return false;
    // With a qop the nonce count and client nonce are both REQUIRED, and are
    // part of what is digested.
    if (!qop.empty() && (cnonce.empty() || nc.empty())) return false;

    std::string a1 = username + ":" + realm + ":" + password;
    if (sess) {
        // §3.4.2: A1 = H(username:realm:passwd) ":" nonce ":" cnonce, and H is
        // the algorithm in use — not MD5 whatever it is.
        std::string hashed;
        if (!auth_digest(base, a1, hashed)) return false;
        a1 = hashed + ":" + nonce + ":" + cnonce;
    }

    std::string body_hash;
    if (qop == "auth-int") {
        // §3.4.3: A2 = Method ":" request-uri ":" H(entity-body).
        if (!auth_digest(base, entity_body, body_hash)) return false;
    }
    const std::string a2 = qop == "auth-int"
        ? method + ":" + request_target + ":" + body_hash
        : method + ":" + request_target;

    std::string ha1, ha2;
    if (!auth_digest(base, a1, ha1) || !auth_digest(base, a2, ha2)) return false;

    const std::string response_input = qop.empty()
        ? ha1 + ":" + nonce + ":" + ha2
        : ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2;
    std::string response;
    if (!auth_digest(base, response_input, response)) return false;
    // The bare hex is what `auth_Digest_response` has always handed back, and
    // the header is what the loader sends; both come from this one calculation
    // so they cannot disagree.
    if (out_response_hex) *out_response_hex = response;

    // §3.4.4: with userhash the field carries H(username ":" realm), computed
    // with the same algorithm, and `userhash=true` is sent with it.
    std::string sent_username = username;
    if (hash_username) {
        if (!auth_digest(base, username + ":" + realm, sent_username)) return false;
    }

    out = "Digest username=" + auth_quote(sent_username) +
          ", realm=" + auth_quote(realm) +
          ", nonce=" + auth_quote(nonce) +
          ", uri=" + auth_quote(request_target) +
          ", response=" + auth_quote(response) +
          ", algorithm=" + algorithm;
    if (!qop.empty()) {
        out += ", qop=" + qop + ", nc=" + nc + ", cnonce=" + auth_quote(cnonce);
    }
    if (!opaque.empty()) out += ", opaque=" + auth_quote(opaque);
    if (hash_username) out += ", userhash=true";
    return true;
}

/// One challenge, answered from `creds`, with the body the request carries.
/// `qop` is the challenge's list; `auth-int` is chosen over `auth` when the
/// body is known, because it protects the message and not only the identity.
bool auth_digest_value(const AuthChallenge& challenge,
                       const std::string& origin,
                       const std::string& method,
                       const std::string& request_target,
                       const AuthCredentials& creds,
                       std::string_view entity_body,
                       std::string& out) {
    std::string algorithm = "MD5";
    if (auto it = challenge.params.find("algorithm"); it != challenge.params.end()) {
        algorithm = it->second;
    }

    auto param = [&](const char* name) -> std::string {
        auto it = challenge.params.find(name);
        return it == challenge.params.end() ? std::string() : it->second;
    };

    const std::string realm = param("realm");
    std::string nonce = param("nonce");
    const std::string opaque = param("opaque");
    if (nonce.empty()) return false;
    // RFC 7616 §3.5: a `nextnonce` this protection space handed us last time
    // replaces the one in the challenge, which may be spent.
    if (std::string remembered = auth_next_nonce_for(origin, realm); !remembered.empty()) {
        nonce = std::move(remembered);
    }
    // RFC 7616 §4: `charset` may only be "UTF-8", and this client only has
    // UTF-8 octets to offer; anything else is a server it cannot talk to.
    if (param("charset") != "" && ascii_lower(param("charset")) != "utf-8") return false;
    const bool hash_username = ascii_lower(param("userhash")) == "true";

    const std::string cnonce = auth_client_nonce();
    const std::string nc = auth_nonce_count(origin, realm, nonce);

    // A `qop` list is a comma-separated set of tokens; `auth-int` is preferred
    // when offered (it digests the body) and `auth` is the fallback.
    std::string chosen_qop;
    {
        std::string qop = param("qop");
        bool saw_auth = false;
        std::size_t start = 0;
        while (start <= qop.size()) {
            const std::size_t comma = qop.find(',', start);
            std::string token = trim_http_ws(std::string_view(qop).substr(
                start, comma == std::string::npos ? std::string::npos : comma - start));
            for (char& c : token) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (token == "auth-int") { chosen_qop = "auth-int"; break; }
            if (token == "auth") saw_auth = true;
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (chosen_qop.empty() && saw_auth) chosen_qop = "auth";
        if (chosen_qop.empty() && !qop.empty()) return false;    // only options this client cannot do
    }

    return auth_digest_build(algorithm, creds.username, creds.password, realm, nonce, opaque,
                             method, request_target, chosen_qop, cnonce, nc,
                             entity_body, hash_username, out);
}

/// Picks a challenge this client can answer, preferring the strongest scheme
/// the server offered: Digest with SHA-256, then Digest with MD5, then Basic.
/// A server that offers both gets Digest, which is what RFC 7616 §1 asks for,
/// and Basic is the last resort because it sends the password in the clear.
bool auth_authorization_for(const std::vector<AuthChallenge>& challenges,
                            const std::string& origin,
                            const std::string& method,
                            const std::string& request_target,
                            std::string_view entity_body,
                            std::string& out) {
    // RFC 7616 §3.2 lists the algorithms in order of preference: SHA-256 is
    // mandatory to implement, SHA-512-256 is the backup, MD5 is for backward
    // compatibility. A server may name one per challenge, so the strengths are
    // ranked here rather than taken in the order the server wrote them.
    const AuthChallenge* best_sha512 = nullptr;
    const AuthChallenge* best_sha256 = nullptr;
    const AuthChallenge* best_digest_md5 = nullptr;
    const AuthChallenge* basic = nullptr;
    for (const AuthChallenge& challenge : challenges) {
        std::string scheme = ascii_lower(challenge.scheme);
        if (scheme == "digest") {
            auto it = challenge.params.find("algorithm");
            std::string algorithm = it == challenge.params.end() ? "MD5" : it->second;
            std::string base;
            bool sess = false;
            if (!auth_split_algorithm(algorithm, base, sess)) continue;
            if (!best_sha512 && base == "SHA-512-256") best_sha512 = &challenge;
            else if (!best_sha256 && base == "SHA-256") best_sha256 = &challenge;
            else if (!best_digest_md5 && base == "MD5") best_digest_md5 = &challenge;
        } else if (scheme == "basic" && !basic) {
            basic = &challenge;
        }
    }

    for (const AuthChallenge* challenge : {best_sha512, best_sha256, best_digest_md5}) {
        if (!challenge) continue;
        auto it = challenge->params.find("realm");
        const std::string realm = it == challenge->params.end() ? std::string() : it->second;
        AuthCredentials creds;
        {
            std::lock_guard<std::mutex> lock(g_auth_mutex);
            auto found = g_auth_store.find(auth_store_key(origin, realm));
            if (found == g_auth_store.end()) continue;
            creds = found->second;
        }
        if (!auth_is_header_safe(creds.username) || !auth_is_header_safe(creds.password)) continue;
        if (auth_digest_value(*challenge, origin, method, request_target, creds, entity_body, out)) return true;
    }

    if (basic) {
        auto it = basic->params.find("realm");
        const std::string realm = it == basic->params.end() ? std::string() : it->second;
        AuthCredentials creds;
        {
            std::lock_guard<std::mutex> lock(g_auth_mutex);
            auto found = g_auth_store.find(auth_store_key(origin, realm));
            if (found == g_auth_store.end()) return false;
            creds = found->second;
        }
        if (!auth_is_header_safe(creds.username) || !auth_is_header_safe(creds.password)) return false;
        return auth_basic_value(creds, out);
    }
    return false;
}

} // namespace

std::string ResourceLoader::auth_md5_hex(std::string_view data) { return auth_hex(auth_md5(data)); }
std::string ResourceLoader::auth_sha256_hex(std::string_view data) { return auth_hex(auth_sha256(data)); }
std::string ResourceLoader::auth_base64(std::string_view bytes) { return ::litebird::auth_base64(bytes); }

std::string ResourceLoader::auth_sha512_256_hex(std::string_view data) {
    return ::litebird::auth_hex(auth_sha512_256(data));
}

bool ResourceLoader::auth_Digest_response(const std::string& algorithm,
                                          const std::string& username, const std::string& password,
                                          const std::string& realm, const std::string& nonce,
                                          const std::string& method, const std::string& request_target,
                                          const std::string& qop, const std::string& cnonce,
                                          const std::string& nc, std::string& out_hex) {
    std::string header;
    return ::litebird::auth_digest_build(algorithm, username, password, realm, nonce,
                                         /*opaque=*/"", method, request_target, qop, cnonce, nc,
                                         /*entity_body=*/std::string_view(),
                                         /*hash_username=*/false, header, &out_hex);
}

bool ResourceLoader::auth_Digest_build(const std::string& algorithm,
                                       const std::string& username, const std::string& password,
                                       const std::string& realm, const std::string& nonce,
                                       const std::string& opaque,
                                       const std::string& method, const std::string& request_target,
                                       const std::string& qop, const std::string& cnonce,
                                       const std::string& nc,
                                       std::string_view entity_body, bool hash_username,
                                       std::string& out) {
    return ::litebird::auth_digest_build(algorithm, username, password, realm, nonce, opaque,
                                          method, request_target, qop, cnonce, nc,
                                          entity_body, hash_username, out);
}

void ResourceLoader::set_auth_credentials(const std::string& origin, const std::string& realm,
                                         const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_auth_store[auth_store_key(origin, realm)] = AuthCredentials{username, password};
}

bool ResourceLoader::has_credentials(const std::string& origin, const std::string& realm) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    return g_auth_store.count(auth_store_key(origin, realm)) != 0;
}

void ResourceLoader::clear_auth_credentials() {
    {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        g_auth_store.clear();
    }
    // A nonce belongs to a protection space, and a space with no credentials
    // has no next request to spend one on.
    std::lock_guard<std::mutex> lock(g_nonce_mutex);
    g_nonce_store.clear();
}

std::string ResourceLoader::auth_next_nonce_for_testing(const std::string& origin,
                                                        const std::string& realm) {
    return auth_next_nonce_for(origin, realm);
}

std::size_t ResourceLoader::auth_credential_count() {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    return g_auth_store.size();
}

void ResourceLoader::clear_http_cache() {
    {
        std::lock_guard<std::mutex> lock(g_http_cache_mutex);
        g_http_cache_lru.clear();
        g_http_cache_total_bytes = 0;
    }
    {
        std::lock_guard<std::mutex> lock(g_preflight_mutex);
        g_preflight_cache.clear();
    }
}

std::size_t ResourceLoader::http_cache_size() {
    std::lock_guard<std::mutex> lock(g_http_cache_mutex);
    return g_http_cache_lru.size();
}

std::size_t ResourceLoader::cors_preflight_cache_size() {
    std::lock_guard<std::mutex> lock(g_preflight_mutex);
    return g_preflight_cache.size();
}

void ResourceLoader::clear_http_connection_pool() {
    http_connection_pool().clear();
}

std::size_t ResourceLoader::http_connection_pool_size() {
    return http_connection_pool().size();
}

void ResourceLoader::set_ca_bundle_for_testing(std::string ca_file_path) {
    {
        std::lock_guard<std::mutex> lock(g_ca_mutex);
        g_test_ca_bundle = std::move(ca_file_path);
    }
    clear_http_connection_pool();
}

std::string ResourceLoader::detect_mime_type(std::string_view path_or_url) {
    if (const auto q = path_or_url.find_first_of("?#"); q != std::string_view::npos) {
        path_or_url = path_or_url.substr(0, q);
    }
    auto dot_pos = path_or_url.rfind('.');
    if (dot_pos == std::string_view::npos) {
        return "text/plain";
    }
    auto ext = path_or_url.substr(dot_pos);
    std::string ext_lower;
    for (char c : ext) ext_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext_lower == ".html" || ext_lower == ".htm") return "text/html";
    if (ext_lower == ".xhtml" || ext_lower == ".xht") return "application/xhtml+xml";
    if (ext_lower == ".xml") return "application/xml";
    if (ext_lower == ".svg") return "image/svg+xml";
    if (ext_lower == ".css") return "text/css";
    if (ext_lower == ".js" || ext_lower == ".mjs") return "application/javascript";
    if (ext_lower == ".json") return "application/json";
    if (ext_lower == ".txt") return "text/plain";
    if (ext_lower == ".png") return "image/png";
    if (ext_lower == ".bmp") return "image/bmp";
    if (ext_lower == ".ppm") return "image/x-portable-pixmap";
    if (ext_lower == ".jpg" || ext_lower == ".jpeg") return "image/jpeg";
    return "application/octet-stream";
}

std::string ResourceLoader::address_of(const std::string& location) {
    for (const char* scheme : {"http://", "https://", "file://", "data:", "about:"}) {
        if (location.rfind(scheme, 0) != 0) continue;
        Url url;
        return Url::parse(location, nullptr, url) ? url.href() : location;
    }
    std::error_code failed;
    const auto absolute = std::filesystem::absolute(location, failed);
    return file_url_from_path(failed ? location : absolute.lexically_normal().string());
}

std::string ResourceLoader::resolve_url(std::string_view base_url, std::string_view relative_url) {
    if (relative_url.empty()) return std::string(base_url);

    if (relative_url.rfind("http://", 0) == 0 ||
        relative_url.rfind("https://", 0) == 0 ||
        relative_url.rfind("file://", 0) == 0) {
        return std::string(relative_url);
    }

    // An absolute URL is returned as it stands, whatever its scheme.
    //
    // The normalization below splits a URL on `/` and drops empty segments,
    // which is right for a path of segments and wrong for an opaque one:
    // `data:font/ttf;base64,HAAD//wMg...` is one string, and its `//` is two
    // characters of base64 and not an empty path segment. It was being
    // collapsed to `/`, so every data: URL whose payload happened to contain
    // the pair came back a few bytes shorter and decoded to something that
    // was not the thing it encoded -- a font that would not parse, an image
    // that would not decode. Which is to say: not an edge case, since a
    // base64 payload of any length contains `//` about once in every 64
    // characters by the time it is a few hundred long.
    //
    // The same was true of `mailto:`, `javascript:` and every other scheme
    // without a hierarchical path. Only `http`, `https` and `file` were
    // spared, because they were the three this function knew about.
    {
        // The scheme ends at the first `:`, `/`, `?` or `#` -- a colon that
        // comes after a slash is a colon in a path and not a scheme, so
        // "./a:b/c" stays relative and is resolved against the base.
        const auto end = relative_url.find_first_of(":/?#");
        const auto colon = relative_url.find(':');
        if (colon != std::string_view::npos && colon != 0 && colon == end) {
            bool scheme = std::isalpha(static_cast<unsigned char>(relative_url[0])) != 0;
            for (std::size_t i = 1; scheme && i < colon; ++i) {
                const char c = relative_url[i];
                scheme = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' ||
                         c == '-' || c == '.';
            }
            // A lone `/` before the colon is not a scheme: "./a:b" is relative.
            if (scheme) return std::string(relative_url);
        }
    }

    if (relative_url.front() == '/') {
        size_t scheme_len = 0;
        if (base_url.rfind("http://", 0) == 0) scheme_len = 7;
        else if (base_url.rfind("https://", 0) == 0) scheme_len = 8;

        if (scheme_len > 0) {
            auto slash_pos = base_url.find('/', scheme_len);
            if (slash_pos == std::string_view::npos) {
                return std::string(base_url) + std::string(relative_url);
            }
            return std::string(base_url.substr(0, slash_pos)) + std::string(relative_url);
        }
        return std::string(relative_url);
    }

    std::string base_dir(base_url);
    auto last_slash = base_dir.rfind('/');
    if (last_slash != std::string::npos) {
        base_dir = base_dir.substr(0, last_slash + 1);
    } else {
        base_dir.clear();
    }

    std::string combined = base_dir + std::string(relative_url);

    std::vector<std::string> parts;
    std::string segment;
    std::stringstream ss(combined);
    bool is_http = combined.rfind("http://", 0) == 0;
    bool is_https = combined.rfind("https://", 0) == 0;
    std::string prefix;
    if (is_http || is_https) {
        prefix = is_https ? "https://" : "http://";
        std::string without_scheme = combined.substr(prefix.size());
        std::stringstream ss2(without_scheme);
        while (std::getline(ss2, segment, '/')) {
            if (segment.empty() || segment == ".") continue;
            if (segment == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(segment);
            }
        }
        std::string res = prefix;
        for (size_t i = 0; i < parts.size(); ++i) {
            res += parts[i];
            if (i + 1 < parts.size()) res += "/";
        }
        return res;
    }

    while (std::getline(ss, segment, '/')) {
        if (segment.empty() || segment == ".") continue;
        if (segment == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(segment);
        }
    }

    std::string res;
    if (!combined.empty() && combined.front() == '/') res += "/";
    for (size_t i = 0; i < parts.size(); ++i) {
        res += parts[i];
        if (i + 1 < parts.size()) res += "/";
    }
    return res.empty() ? "." : res;
}

ResourceResponse ResourceLoader::load_for_page(
    const std::string& url,
    const ResourceRequestContext& context,
    const std::string& method,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body
) {
    ResourceResponse denied;
    denied.error_message = "Blocked by resource origin policy";

    Url document;
    const bool have_document = !context.document_url.empty() &&
                               Url::parse(context.document_url, nullptr, document);

    Url base;
    bool have_base = false;
    if (!context.base_url.empty()) have_base = Url::parse(context.base_url, nullptr, base);
    if (!have_base && have_document) {
        base = document;
        have_base = true;
    }

    Url target;
    if (!Url::parse(url, have_base ? &base : nullptr, target)) {
        denied.error_message = "Invalid request URL";
        return denied;
    }
    const std::string address = target.href(true);

    // data: holds its own bytes and needs neither disk nor network.
    if (target.scheme == "data") {
        MimeType type;
        std::string decoded;
        if (!process_data_url(address, type, decoded)) {
            denied.error_message = "Invalid data: URL";
            return denied;
        }
        ResourceResponse resp;
        resp.success = true;
        resp.network_ok = true;
        resp.status_code = 200;
        resp.status_text = "OK";
        resp.url = address;
        resp.body = std::move(decoded);
        resp.content_type = type.serialize();
        resp.headers["content-type"] = resp.content_type;
        resp.header_list.emplace_back("content-type", resp.content_type);
        return resp;
    }

    if (!have_document) {
        denied.error_message = "Request has no document origin";
        return denied;
    }

    const bool web_document = document.scheme == "http" || document.scheme == "https";
    const bool file_document = document.scheme == "file";

    if (target.scheme == "file") {
        if (!file_document || !context.allow_local_files) {
            denied.error_message = "Web pages may not read local files";
            return denied;
        }
        const std::string path = file_path_from_url(target);
        if (path.empty()) {
            denied.error_message = "Unsupported file: URL host or path";
            return denied;
        }
        ResourceResponse resp = load_file(path);
        if (resp.success) resp.url = address;
        return resp;
    }

    if (target.scheme == "http" || target.scheme == "https") {
        if (!web_document && !file_document) {
            denied.error_message = "Opaque documents may not make network requests";
            return denied;
        }

        const std::string doc_origin = document.origin();
        const bool https_document = (document.scheme == "https");
        Url cur_url = target;
        std::string cur_method = method.empty() ? "GET" : method;
        std::unordered_map<std::string, std::string> cur_caller_headers = headers;
        std::string cur_body = body;
        std::string effective_origin = doc_origin;
        bool tainted_cross_origin = false;

        for (int hop = 0; hop <= MaxRedirects; ++hop) {
            if (https_document && !is_potentially_trustworthy_url(cur_url)) {
                denied.error_message =
                    "Blocked by mixed-content policy: insecure HTTP request from HTTPS document";
                return denied;
            }

            const std::string hop_origin = cur_url.origin();
            const bool same_origin =
                (doc_origin != "null" && hop_origin == doc_origin && !tainted_cross_origin);
            if (!same_origin) {
                tainted_cross_origin = true;
            }

            if (context.mode == RequestMode::SameOrigin && !same_origin) {
                denied.error_message = "Blocked by same-origin policy";
                return denied;
            }

            const bool send_credentials =
                !context.omit_credentials && (same_origin || context.include_credentials);
            const std::string hop_address = cur_url.href(true);

            if (!same_origin && context.mode == RequestMode::Cors) {
                const std::vector<std::string> unsafe_headers =
                    collect_cors_unsafe_request_headers(cur_caller_headers);
                if (!is_cors_safelisted_method(cur_method) || !unsafe_headers.empty()) {
                    if (!ensure_cors_preflight(
                            hop_address,
                            effective_origin,
                            cur_method,
                            unsafe_headers,
                            send_credentials,
                            denied.error_message)) {
                        return denied;
                    }
                }
            }

            std::unordered_map<std::string, std::string> hop_headers = cur_caller_headers;
            if (!same_origin && context.mode == RequestMode::Cors &&
                !has_header_ci(hop_headers, "origin")) {
                hop_headers["Origin"] = effective_origin;
            }
            if (send_credentials && !has_header_ci(hop_headers, "cookie")) {
                const std::string cookie_val = CookieJar::default_jar().get_cookie_header(
                    hop_address,
                    context.document_url,
                    /*is_top_level_navigation=*/false,
                    cur_method
                );
                if (!cookie_val.empty() && is_safe_header_value(cookie_val)) {
                    hop_headers["Cookie"] = cookie_val;
                }
            }

            const std::string cache_partition =
                std::string(send_credentials ? "page:cred|" : "page:anon|") +
                ((!same_origin && context.mode == RequestMode::Cors)
                     ? ("cors:" + effective_origin)
                     : (same_origin ? "same" : "nocors"));

            const auto check_cors = [&](const ResourceResponse& candidate, std::string& reason_out) -> bool {
                if (!same_origin && context.mode == RequestMode::Cors) {
                    if (!cors_allows_response(candidate, effective_origin, send_credentials)) {
                        reason_out = "Blocked by CORS policy: missing or mismatched Access-Control-Allow-Origin";
                        return false;
                    }
                }
                return true;
            };

            ResourceResponse resp = execute_http_hop_with_cache(
                hop_address,
                cur_method,
                hop_headers,
                cur_body,
                context.cache_mode,
                cache_partition,
                send_credentials,
                check_cors
            );
            if (!resp.network_ok) {
                return resp;
            }

            if (is_redirect_status(resp.status_code)) {
                auto loc_it = resp.headers.find("location");
                if (loc_it != resp.headers.end() && !loc_it->second.empty()) {
                    if (hop == MaxRedirects) {
                        denied.error_message = "Too many HTTP redirects";
                        return denied;
                    }
                    Url next_url;
                    if (!Url::parse(trim_http_ws(loc_it->second), &cur_url, next_url)) {
                        denied.error_message = "Invalid HTTP redirect Location";
                        return denied;
                    }
                    if (next_url.scheme != "http" && next_url.scheme != "https") {
                        denied.error_message = "Redirect to non-HTTP(S) scheme refused";
                        return denied;
                    }
                    if (next_url.origin() != cur_url.origin()) {
                        effective_origin = "null";
                        tainted_cross_origin = true;
                        for (auto it = cur_caller_headers.begin(); it != cur_caller_headers.end(); ) {
                            if (ascii_lower(it->first) == "authorization") {
                                it = cur_caller_headers.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    if (resp.status_code == 303 ||
                        ((resp.status_code == 301 || resp.status_code == 302) && ascii_lower(cur_method) == "post")) {
                        cur_method = "GET";
                        cur_body.clear();
                        for (auto it = cur_caller_headers.begin(); it != cur_caller_headers.end(); ) {
                            const std::string ln = ascii_lower(it->first);
                            if (ln == "content-length" || ln == "content-type" || ln == "content-language" ||
                                ln == "content-encoding" || ln == "content-location") {
                                it = cur_caller_headers.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    cur_url = std::move(next_url);
                    continue;
                }
            }

            resp.url = hop_address;
            resp.cross_origin = tainted_cross_origin;
            return resp;
        }
    }

    denied.error_message = "Unsupported protocol: " + target.scheme + ":";
    return denied;
}

ResourceResponse ResourceLoader::load_navigation(const std::string& url) {
    Url target;
    if (Url::parse(url, nullptr, target)) {
        const std::string address = target.href(true);
        if (target.scheme == "data") {
            ResourceRequestContext context;
            return load_for_page(address, context);
        }
        if (target.scheme == "http" || target.scheme == "https") {
            return load_http(address);
        }
        if (target.scheme == "file") {
            const std::string path = file_path_from_url(target);
            if (path.empty()) {
                ResourceResponse resp;
                resp.error_message = "Unsupported file: URL host or path";
                return resp;
            }
            ResourceResponse resp = load_file(path);
            if (resp.success) resp.url = address;
            return resp;
        }
        ResourceResponse resp;
        resp.error_message = "Unsupported protocol: " + target.scheme + ":";
        return resp;
    }

    if (has_url_scheme(url)) {
        ResourceResponse resp;
        resp.error_message = "Invalid navigation URL";
        return resp;
    }
    ResourceResponse resp = load_file(url);
    if (resp.success) resp.url = file_url_from_path(std::filesystem::absolute(url).lexically_normal().string());
    return resp;
}

ResourceResponse ResourceLoader::load_file(const std::string& file_path) {
    ResourceResponse resp;
    if (file_path.empty() || file_path.find('\0') != std::string::npos) {
        resp.error_message = "Invalid file path";
        return resp;
    }
    std::error_code ec;
    if (std::filesystem::is_directory(file_path, ec)) {
        resp.error_message = "Path is a directory: " + file_path;
        return resp;
    }
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        resp.success = false;
        resp.network_ok = false;
        resp.status_code = 404;
        resp.status_text = "Not Found";
        resp.error_message = "File not found: " + file_path;
        return resp;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    resp.body = ss.str();
    resp.status_code = 200;
    resp.status_text = "OK";
    resp.success = true;
    resp.network_ok = true;
    resp.content_type = detect_mime_type(file_path);
    resp.headers["content-type"] = resp.content_type;
    resp.headers["content-length"] = std::to_string(resp.body.size());
    resp.header_list.emplace_back("content-type", resp.content_type);
    resp.header_list.emplace_back("content-length", resp.headers["content-length"]);
    return resp;
}

ResourceResponse ResourceLoader::load_http(
    const std::string& url,
    const std::string& method,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body
) {
    Url cur_url;
    if (!Url::parse(url, nullptr, cur_url) || (cur_url.scheme != "http" && cur_url.scheme != "https")) {
        ResourceResponse resp;
        resp.error_message = "Unsupported protocol: expected http:// or https://";
        return resp;
    }

    const std::string initial_site_url = cur_url.href(true);
    std::string cur_method = method.empty() ? "GET" : method;
    std::unordered_map<std::string, std::string> cur_caller_headers = headers;
    std::string cur_body = body;
    /// One retry per load, after the first 401: RFC 9110 leaves the number to
    /// the client, and one is what a browser does before giving up.
    bool auth_attempted = false;

    for (int hop = 0; hop <= MaxRedirects; ++hop) {
        const std::string hop_address = cur_url.href(true);
        std::unordered_map<std::string, std::string> hop_headers = cur_caller_headers;
        if (!has_header_ci(hop_headers, "cookie")) {
            const std::string cookie_val = CookieJar::default_jar().get_cookie_header(
                hop_address,
                initial_site_url,
                /*is_top_level_navigation=*/true,
                cur_method
            );
            if (!cookie_val.empty() && is_safe_header_value(cookie_val)) {
                hop_headers["Cookie"] = cookie_val;
            }
        }

        ResourceResponse resp = execute_http_hop_with_cache(
            hop_address,
            cur_method,
            hop_headers,
            cur_body,
            CacheMode::Default,
            "nav:cred",
            /*ingest_cookies=*/true,
            nullptr
        );
        if (!resp.network_ok) return resp;

        // RFC 9110 §15.5.2. The first request carried no `Authorization`; if
        // credentials are held for this origin *and* the realm the server
        // named, ask once with them. One retry, never a loop: a server that
        // answers 401 to the credentials themselves is left to fail.
        if (resp.status_code == 401 && !auth_attempted) {
            const std::vector<AuthChallenge> challenges = auth_note_challenge(resp);
            std::string authorization;
            if (!challenges.empty() &&
                auth_authorization_for(challenges, cur_url.origin(), cur_method,
                                       auth_request_target(cur_url), cur_body, authorization) &&
                is_safe_header_value(authorization)) {
                auth_attempted = true;
                hop_headers["Authorization"] = authorization;
                // Same hop again, with the header this time. The cookie jar and
                // the cache are consulted as they were; the response cache does
                // not keep a 401, so nothing is served from it here.
                resp = execute_http_hop_with_cache(
                    hop_address, cur_method, hop_headers, cur_body,
                    CacheMode::Default, "nav:cred", /*ingest_cookies=*/true, nullptr);
                if (!resp.network_ok) return resp;

                // A second 401 means the credentials themselves were refused.
                // A browser drops them and asks again next time rather than
                // replaying the same wrong password for the life of the
                // process, so the store forgets this (origin, realm).
                if (resp.status_code == 401) {
                    for (const AuthChallenge& challenge : challenges) {
                        auto it = challenge.params.find("realm");
                        if (it == challenge.params.end()) continue;
                        std::lock_guard<std::mutex> lock(g_auth_mutex);
                        g_auth_store.erase(auth_store_key(cur_url.origin(), it->second));
                    }
                } else {
                    // RFC 7616 §3.5: a successful authentication may name the
                    // nonce to use next, and a one-time-nonce server requires
                    // it. Kept against the origin and realm so the next load
                    // does not have to be challenged again.
                    auth_note_authentication_info(resp, cur_url.origin(), challenges);
                }
            }
        }
        auth_note_proxy_challenge(resp);

        if (is_redirect_status(resp.status_code)) {
            auto loc_it = resp.headers.find("location");
            if (loc_it != resp.headers.end() && !loc_it->second.empty()) {
                if (hop == MaxRedirects) {
                    ResourceResponse err;
                    err.error_message = "Too many HTTP redirects";
                    return err;
                }
                Url next_url;
                if (!Url::parse(trim_http_ws(loc_it->second), &cur_url, next_url)) {
                    ResourceResponse err;
                    err.error_message = "Invalid HTTP redirect Location";
                    return err;
                }
                if (next_url.scheme != "http" && next_url.scheme != "https") {
                    ResourceResponse err;
                    err.error_message = "Redirect to non-HTTP(S) scheme refused";
                    return err;
                }
                if (next_url.origin() != cur_url.origin()) {
                    for (auto it = cur_caller_headers.begin(); it != cur_caller_headers.end(); ) {
                        if (ascii_lower(it->first) == "authorization") {
                            it = cur_caller_headers.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                if (resp.status_code == 303 ||
                    ((resp.status_code == 301 || resp.status_code == 302) && ascii_lower(cur_method) == "post")) {
                    cur_method = "GET";
                    cur_body.clear();
                    for (auto it = cur_caller_headers.begin(); it != cur_caller_headers.end(); ) {
                        const std::string ln = ascii_lower(it->first);
                        if (ln == "content-length" || ln == "content-type" || ln == "content-language" ||
                            ln == "content-encoding" || ln == "content-location") {
                            it = cur_caller_headers.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                cur_url = std::move(next_url);
                continue;
            }
        }

        resp.url = hop_address;
        return resp;
    }

    ResourceResponse err;
    err.error_message = "Too many HTTP redirects";
    return err;
}

ResourceResponse ResourceLoader::load_http_single_hop(
    const std::string& url,
    const std::string& method,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body
) {
    ResourceResponse resp;
    Url parsed_url;
    if (!Url::parse(url, nullptr, parsed_url) || (parsed_url.scheme != "http" && parsed_url.scheme != "https")) {
        resp.error_message = "Unsupported protocol: expected http:// or https://";
        return resp;
    }

    if (const std::string mapped_root = lookup_origin_root(parsed_url.origin()); !mapped_root.empty()) {
        resp.url = parsed_url.href(true);
        std::string rel_path;
        if (!percent_decode_url_path(parsed_url.pathname(), rel_path)) {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
            resp.network_ok = true;
            resp.error_message = "Malformed URL path";
            return resp;
        }
        while (!rel_path.empty() && rel_path.front() == '/') rel_path.erase(rel_path.begin());

        std::error_code ec;
        const auto canonical_root = std::filesystem::weakly_canonical(mapped_root, ec);
        if (ec) {
            resp.error_message = "Invalid mapped origin directory";
            return resp;
        }
        const auto target_candidate = rel_path.empty() ? (canonical_root / "index.html") : (canonical_root / rel_path);
        const auto canonical_target = std::filesystem::weakly_canonical(target_candidate, ec);
        const std::string root_prefix = canonical_root.string();
        const std::string target_str = canonical_target.string();
        const bool inside_root =
            !ec && target_str.size() >= root_prefix.size() &&
            target_str.compare(0, root_prefix.size(), root_prefix) == 0 &&
            (target_str.size() == root_prefix.size() || root_prefix.back() == '/' ||
             target_str[root_prefix.size()] == '/');
        if (!inside_root || !std::filesystem::is_regular_file(canonical_target, ec)) {
            resp.status_code = 404;
            resp.status_text = "Not Found";
            resp.network_ok = true;
            resp.success = false;
            resp.error_message = "Mapped origin resource not found";
            return resp;
        }
        ResourceResponse file_resp = load_file(target_str);
        if (!file_resp.success) {
            resp.status_code = 404;
            resp.status_text = "Not Found";
            resp.network_ok = true;
            resp.success = false;
            resp.error_message = file_resp.error_message;
            return resp;
        }
        file_resp.url = parsed_url.href(true);
        // Honor optional WPT `<file>.headers` sidecar header declarations.
        std::filesystem::path sidecar = target_str + ".headers";
        if (std::filesystem::is_regular_file(sidecar, ec)) {
            auto trim_ows = [](std::string_view sv) {
                while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
                while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
                return std::string(sv);
            };
            std::ifstream hfs(sidecar, std::ios::binary);
            std::string line;
            while (std::getline(hfs, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto colon = line.find(':');
                if (colon == std::string::npos || colon == 0) continue;
                std::string hname = ascii_lower(trim_ows(std::string_view(line).substr(0, colon)));
                std::string hval = trim_ows(std::string_view(line).substr(colon + 1));
                if (hname.empty()) continue;
                file_resp.headers[hname] = hval;
                file_resp.header_list.emplace_back(hname, hval);
                if (hname == "content-type") {
                    file_resp.content_type = hval;
                }
            }
        }
        return file_resp;
    }

    const bool use_tls = (parsed_url.scheme == "https");

    const std::string req_method = method.empty() ? "GET" : method;
    if (!is_http_token(req_method)) {
        resp.error_message = "Invalid HTTP method token";
        return resp;
    }

    std::size_t user_header_bytes = 0;
    bool has_content_length = false;
    bool has_accept_encoding = false;
    bool has_accept = false;
    bool has_user_agent = false;
    for (const auto& [name, value] : headers) {
        if (!is_http_token(name) || !is_safe_header_value(value)) {
            resp.error_message = "Invalid HTTP request header name or value";
            return resp;
        }
        const std::string lower_name = ascii_lower(name);
        if (lower_name == "host" || lower_name == "transfer-encoding" || lower_name == "connection") {
            resp.error_message = "HTTP request header is managed by ResourceLoader: " + lower_name;
            return resp;
        }
        if (lower_name == "accept-encoding") has_accept_encoding = true;
        if (lower_name == "accept") has_accept = true;
        if (lower_name == "user-agent") has_user_agent = true;
        if (lower_name == "content-length") {
            std::size_t declared_size = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), declared_size);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                declared_size != body.size()) {
                resp.error_message = "HTTP Content-Length must match the request body size";
                return resp;
            }
            has_content_length = true;
        }
        if (name.size() > MaxHttpHeaderBytes - 4 ||
            value.size() > MaxHttpHeaderBytes - 4 - name.size() ||
            user_header_bytes > MaxHttpHeaderBytes - 4 - name.size() - value.size()) {
            resp.error_message = "HTTP request headers exceed 64 KiB limit";
            return resp;
        }
        user_header_bytes += name.size() + value.size() + 4;
    }

    std::string connect_host = parsed_url.host;
    if (connect_host.size() >= 2 && connect_host.front() == '[' && connect_host.back() == ']') {
        connect_host = connect_host.substr(1, connect_host.size() - 2);
    }
    if (connect_host.empty()) {
        resp.error_message = "Invalid HTTP URL host";
        return resp;
    }

    const int default_port = use_tls ? 443 : 80;
    const int port = (parsed_url.port >= 0) ? parsed_url.port : default_port;
    if (port <= 0 || port > 65535) {
        resp.error_message = "Invalid HTTP port";
        return resp;
    }

    std::string target_path = parsed_url.pathname();
    if (target_path.empty()) target_path = "/";
    target_path += parsed_url.search();
    if (!is_safe_request_target(target_path)) {
        resp.error_message = "Invalid HTTP request target";
        return resp;
    }

    const std::string host_header = parsed_url.host +
                                    ((parsed_url.port >= 0 && parsed_url.port != default_port)
                                         ? (":" + std::to_string(parsed_url.port))
                                         : "");
    std::string request = req_method + " " + target_path + " HTTP/1.1\r\n" +
                          "Host: " + host_header + "\r\n";
    if (!has_user_agent) {
        request += "User-Agent: Litebird/1.0 (Native Engine)\r\n";
    }
    if (!has_accept) {
        request += "Accept: text/html,application/xhtml+xml,application/xml,application/json;q=0.9,*/*;q=0.8\r\n";
    }
    if (!has_accept_encoding) {
        request += "Accept-Encoding: gzip, deflate, br\r\n";
    }
    for (const auto& [hk, hv] : headers) {
        request += hk + ": " + hv + "\r\n";
    }
    if (!has_content_length && !body.empty()) {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "Connection: keep-alive\r\n";
    if (request.size() + 2 > MaxHttpHeaderBytes) {
        resp.error_message = "HTTP request headers exceed 64 KiB limit";
        return resp;
    }
    request += "\r\n";
    if (!body.empty()) request += body;

    ensure_sigpipe_ignored();

    const std::string custom_ca = use_tls ? get_test_ca_bundle() : std::string();
    const std::string pool_key =
        std::string(use_tls ? "https|" : "http|") +
        ascii_lower(connect_host) + ":" + std::to_string(port) +
        (use_tls ? ("|" + custom_ca) : "");
    const std::string proto_label = use_tls ? "HTTPS" : "HTTP";

    auto dial_connection = [&](HttpConnection& out, std::string& err_out) -> bool {
        const int sockfd = connect_with_timeout(connect_host, port, 1500, err_out);
        if (sockfd < 0) return false;

        out = HttpConnection{};
        out.is_tls = use_tls;
        out.sockfd = sockfd;
        if (!use_tls) return true;

        const OpenSslApi& ssl_api = OpenSslApi::instance();
        if (!ssl_api.available) {
            close_http_connection(out, false);
            err_out = "TLS library (libssl.so.3) is unavailable on this host";
            return false;
        }

        out.ssl_ctx = ssl_api.SSL_CTX_new_fn(ssl_api.TLS_client_method_fn());
        if (!out.ssl_ctx) {
            close_http_connection(out, false);
            err_out = "Failed to initialize TLS context";
            return false;
        }

        if (!custom_ca.empty()) {
            if (ssl_api.SSL_CTX_load_verify_locations_fn(out.ssl_ctx, custom_ca.c_str(), nullptr) != 1) {
                close_http_connection(out, false);
                err_out = "Failed to load custom TLS CA bundle";
                return false;
            }
        } else {
            (void)ssl_api.SSL_CTX_set_default_verify_paths_fn(out.ssl_ctx);
        }
        // SSL_VERIFY_PEER = 0x01
        ssl_api.SSL_CTX_set_verify_fn(out.ssl_ctx, 0x01, nullptr);

        out.ssl = ssl_api.SSL_new_fn(out.ssl_ctx);
        if (!out.ssl) {
            close_http_connection(out, false);
            err_out = "Failed to allocate TLS session";
            return false;
        }

        (void)ssl_api.SSL_set_fd_fn(out.ssl, out.sockfd);
        bool identity_configured = false;
        if (is_ip_literal(connect_host)) {
            // IP literals require an iPAddress SAN, not a DNS SAN or CN.
            // SSL_set1_host is a DNS-name check; use the IP verification
            // parameter instead, and do not send an IP address as SNI.
            auto* param = ssl_api.SSL_get0_param_fn(out.ssl);
            identity_configured = param &&
                ssl_api.X509_VERIFY_PARAM_set1_ip_asc_fn(param, connect_host.c_str()) == 1;
        } else {
            // SSL_CTRL_SET_TLSEXT_HOSTNAME = 55, TLSEXT_NAMETYPE_host_name = 0
            (void)ssl_api.SSL_ctrl_fn(out.ssl, 55, 0, const_cast<char*>(connect_host.c_str()));
            identity_configured = ssl_api.SSL_set1_host_fn(out.ssl, connect_host.c_str()) == 1;
        }
        if (!identity_configured) {
            close_http_connection(out, false);
            err_out = "Failed to configure TLS certificate identity verification";
            return false;
        }

        if (ssl_api.SSL_connect_fn(out.ssl) != 1 || ssl_api.SSL_get_verify_result_fn(out.ssl) != 0) {
            close_http_connection(out, false);
            err_out = "TLS handshake or certificate verification failed for " + connect_host;
            return false;
        }
        return true;
    };

    auto send_all = [&](HttpConnection& conn) -> bool {
        if (conn.is_tls) {
            const OpenSslApi& ssl_api = OpenSslApi::instance();
            std::size_t sent = 0;
            while (sent < request.size()) {
                const int chunk = static_cast<int>(std::min<std::size_t>(request.size() - sent, 65536u));
                const int written = ssl_api.SSL_write_fn(conn.ssl, request.data() + sent, chunk);
                if (written <= 0) return false;
                sent += static_cast<std::size_t>(written);
            }
            return true;
        }
        std::size_t sent = 0;
        while (sent < request.size()) {
            const ssize_t count = ::send(conn.sockfd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            sent += static_cast<std::size_t>(count);
        }
        return true;
    };

    enum class ReadStatus { Ok, Eof, Error, Oversized };
    auto read_some = [&](HttpConnection& conn, std::string& dst, std::size_t& total_wire_bytes) -> ReadStatus {
        char buffer[8192];
        std::size_t received = 0;
        if (conn.is_tls) {
            const OpenSslApi& ssl_api = OpenSslApi::instance();
            const int n = ssl_api.SSL_read_fn(conn.ssl, buffer, sizeof(buffer));
            if (n == 0) return ReadStatus::Eof;
            if (n < 0) {
                const int err = ssl_api.SSL_get_error_fn(conn.ssl, n);
                if (err == 6) return ReadStatus::Eof; // SSL_ERROR_ZERO_RETURN
                return ReadStatus::Error;
            }
            received = static_cast<std::size_t>(n);
        } else {
            while (true) {
                const ssize_t n = ::recv(conn.sockfd, buffer, sizeof(buffer), 0);
                if (n == 0) return ReadStatus::Eof;
                if (n < 0) {
                    if (errno == EINTR) continue;
                    return ReadStatus::Error;
                }
                received = static_cast<std::size_t>(n);
                break;
            }
        }
        if (received > MaxHttpResponseBytes - total_wire_bytes) {
            return ReadStatus::Oversized;
        }
        total_wire_bytes += received;
        dst.append(buffer, received);
        return ReadStatus::Ok;
    };

    std::string body_part;
    bool bodyless_status = false;
    const bool allow_stale_retry = is_idempotent_http_method(req_method);

    for (int attempt = 0; attempt < 2; ++attempt) {
        resp.status_code = 0;
        resp.status_text.clear();
        resp.content_type.clear();
        resp.headers.clear();
        resp.header_list.clear();
        resp.error_message.clear();
        body_part.clear();

        HttpConnection conn;
        const bool from_pool = (attempt == 0) && http_connection_pool().acquire(pool_key, conn);
        if (!from_pool) {
            if (!dial_connection(conn, resp.error_message)) {
                return resp;
            }
        }

        if (!send_all(conn)) {
            close_http_connection(conn, false);
            if (from_pool && allow_stale_retry) continue;
            resp.error_message = use_tls ? "Failed to write HTTPS request" : "Failed to send HTTP request";
            return resp;
        }

        std::string raw_head;
        std::size_t total_wire_bytes = 0;
        std::size_t header_end = std::string::npos;
        std::size_t header_separator_size = 4;
        bool retry_stale = false;

        while (true) {
            const ReadStatus st = read_some(conn, raw_head, total_wire_bytes);
            if (st == ReadStatus::Oversized) {
                close_http_connection(conn, false);
                resp.error_message = proto_label + " response exceeds 16 MiB size limit";
                return resp;
            }
            if (st == ReadStatus::Error) {
                close_http_connection(conn, false);
                if (from_pool && allow_stale_retry && total_wire_bytes == 0) {
                    retry_stale = true;
                    break;
                }
                resp.error_message = use_tls ? "Failed while reading HTTPS response" : "Failed while reading HTTP response";
                return resp;
            }
            if (st == ReadStatus::Eof) {
                close_http_connection(conn, false);
                if (raw_head.empty()) {
                    if (from_pool && allow_stale_retry) {
                        retry_stale = true;
                        break;
                    }
                    resp.error_message = "Empty HTTP response";
                    return resp;
                }
                resp.error_message = "Malformed HTTP response: missing header terminator";
                return resp;
            }

            header_end = raw_head.find("\r\n\r\n");
            header_separator_size = 4;
            if (header_end == std::string::npos) {
                header_end = raw_head.find("\n\n");
                header_separator_size = 2;
            }
            if ((header_end == std::string::npos && raw_head.size() > MaxHttpHeaderBytes) ||
                (header_end != std::string::npos && header_end + header_separator_size > MaxHttpHeaderBytes)) {
                close_http_connection(conn, false);
                resp.error_message = proto_label + " response headers exceed 64 KiB size limit";
                return resp;
            }
            if (header_end != std::string::npos) break;
        }

        if (retry_stale) continue;

        const std::size_t header_total_bytes = header_end + header_separator_size;
        const std::string header_part = raw_head.substr(0, header_end);
        std::string wire_body = raw_head.substr(header_total_bytes);

        int http_major = 1;
        int http_minor = 1;
        std::stringstream hss(header_part);
        std::string status_line;
        if (std::getline(hss, status_line)) {
            if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();
            std::stringstream sl(status_line);
            std::string http_ver;
            int code = 0;
            if (sl >> http_ver >> code &&
                parse_http_version(http_ver, http_major, http_minor) &&
                code >= 100 && code <= 999) {
                resp.status_code = code;
                std::string rest;
                std::getline(sl, rest);
                resp.status_text = trim_http_ws(rest);
            } else {
                close_http_connection(conn, false);
                resp.error_message = "Malformed HTTP status line";
                return resp;
            }
        } else {
            close_http_connection(conn, false);
            resp.error_message = "Missing HTTP status line";
            return resp;
        }
        const bool http_1_1_or_higher = (http_major > 1) || (http_major == 1 && http_minor >= 1);

        std::string line;
        while (std::getline(hss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                const std::string header_name = ascii_lower(trim_http_ws(std::string_view(line).substr(0, colon)));
                const std::string header_val = trim_http_ws(std::string_view(line).substr(colon + 1));
                if (header_name.empty()) continue;
                resp.header_list.emplace_back(header_name, header_val);
                auto existing = resp.headers.find(header_name);
                if (existing != resp.headers.end()) {
                    existing->second += ", " + header_val;
                } else {
                    resp.headers[header_name] = header_val;
                }
                if (header_name == "content-type") {
                    resp.content_type = header_val;
                }
            }
        }

        bodyless_status =
            (resp.status_code >= 100 && resp.status_code < 200) ||
            resp.status_code == 204 ||
            resp.status_code == 304 ||
            ascii_lower(req_method) == "head";

        bool is_chunked = false;
        bool framing_clean_for_reuse = true;
        if (const auto te = resp.headers.find("transfer-encoding"); te != resp.headers.end()) {
            const std::vector<std::string> codings = split_http_comma_tokens(te->second, true);
            if (codings.empty()) {
                close_http_connection(conn, false);
                resp.error_message = "Malformed HTTP Transfer-Encoding";
                return resp;
            }
            for (std::size_t i = 0; i + 1 < codings.size(); ++i) {
                if (codings[i] != "identity") {
                    close_http_connection(conn, false);
                    resp.error_message = "Unsupported HTTP Transfer-Encoding: " + codings[i];
                    return resp;
                }
            }
            if (codings.back() != "chunked") {
                close_http_connection(conn, false);
                resp.error_message = "Unsupported HTTP Transfer-Encoding: " + codings.back();
                return resp;
            }
            is_chunked = true;
            if (resp.headers.find("content-length") != resp.headers.end()) {
                framing_clean_for_reuse = false;
            }
        }

        bool has_content_length = false;
        std::size_t declared_size = 0;
        if (!is_chunked) {
            if (const auto cl = resp.headers.find("content-length"); cl != resp.headers.end()) {
                const std::vector<std::string> cl_vals = split_http_comma_tokens(cl->second, false);
                if (cl_vals.empty()) {
                    close_http_connection(conn, false);
                    resp.error_message = "Malformed HTTP Content-Length";
                    return resp;
                }
                for (std::size_t i = 0; i < cl_vals.size(); ++i) {
                    std::size_t val = 0;
                    const std::string& sv = cl_vals[i];
                    const auto parsed = std::from_chars(sv.data(), sv.data() + sv.size(), val);
                    if (sv.empty() || parsed.ec != std::errc{} || parsed.ptr != sv.data() + sv.size()) {
                        close_http_connection(conn, false);
                        resp.error_message = "Malformed HTTP Content-Length";
                        return resp;
                    }
                    if (i == 0) {
                        declared_size = val;
                    } else if (val != declared_size) {
                        close_http_connection(conn, false);
                        resp.error_message = "Conflicting HTTP Content-Length headers";
                        return resp;
                    }
                }
                if (!bodyless_status &&
                    (declared_size > MaxHttpResponseBytes ||
                     declared_size > MaxHttpResponseBytes - header_total_bytes)) {
                    close_http_connection(conn, false);
                    resp.error_message = proto_label + " response exceeds 16 MiB size limit";
                    return resp;
                }
                has_content_length = true;
            }
        }

        if (bodyless_status) {
            if (!wire_body.empty()) {
                framing_clean_for_reuse = false;
                wire_body.clear();
            }
            body_part.clear();
        } else if (is_chunked) {
            IncrementalChunkDecoder chunk_decoder;
            std::size_t parse_pos = 0;
            while (true) {
                if (!chunk_decoder.feed(wire_body, parse_pos, MaxHttpResponseBytes, MaxHttpHeaderBytes)) {
                    close_http_connection(conn, false);
                    resp.error_message = "Malformed or oversized HTTP chunked response body";
                    return resp;
                }
                if (chunk_decoder.state == IncrementalChunkDecoder::State::Done) {
                    if (parse_pos != wire_body.size()) {
                        framing_clean_for_reuse = false;
                    }
                    break;
                }
                if (parse_pos >= 4096) {
                    wire_body.erase(0, parse_pos);
                    parse_pos = 0;
                }
                const ReadStatus st = read_some(conn, wire_body, total_wire_bytes);
                if (st == ReadStatus::Oversized) {
                    close_http_connection(conn, false);
                    resp.error_message = proto_label + " response exceeds 16 MiB size limit";
                    return resp;
                }
                if (st == ReadStatus::Eof || st == ReadStatus::Error) {
                    close_http_connection(conn, false);
                    resp.error_message = "Malformed or oversized HTTP chunked response body";
                    return resp;
                }
            }
            body_part = std::move(chunk_decoder.decoded);
        } else if (has_content_length) {
            while (wire_body.size() < declared_size) {
                const ReadStatus st = read_some(conn, wire_body, total_wire_bytes);
                if (st == ReadStatus::Oversized) {
                    close_http_connection(conn, false);
                    resp.error_message = proto_label + " response exceeds 16 MiB size limit";
                    return resp;
                }
                if (st == ReadStatus::Eof || st == ReadStatus::Error) {
                    close_http_connection(conn, false);
                    resp.error_message = (st == ReadStatus::Eof)
                        ? "Truncated HTTP response body"
                        : (use_tls ? "Failed while reading HTTPS response" : "Failed while reading HTTP response");
                    return resp;
                }
            }
            if (wire_body.size() > declared_size) {
                wire_body.resize(declared_size);
                framing_clean_for_reuse = false;
            }
            body_part = std::move(wire_body);
        } else {
            // Close-delimited HTTP response: drain until peer closes socket.
            framing_clean_for_reuse = false;
            while (true) {
                const ReadStatus st = read_some(conn, wire_body, total_wire_bytes);
                if (st == ReadStatus::Oversized) {
                    close_http_connection(conn, false);
                    resp.error_message = proto_label + " response exceeds 16 MiB size limit";
                    return resp;
                }
                if (st == ReadStatus::Error) {
                    close_http_connection(conn, false);
                    resp.error_message = use_tls ? "Failed while reading HTTPS response" : "Failed while reading HTTP response";
                    return resp;
                }
                if (st == ReadStatus::Eof) break;
            }
            body_part = std::move(wire_body);
        }

        // Persist or close socket according to RFC 9112 §9.3 & Keep-Alive directives.
        bool server_close = false;
        bool server_keep_alive = false;
        if (const auto ch = resp.headers.find("connection"); ch != resp.headers.end()) {
            for (const std::string& tok : split_http_comma_tokens(ch->second, true)) {
                if (tok == "close") server_close = true;
                else if (tok == "keep-alive") server_keep_alive = true;
            }
        }

        bool keep_alive = framing_clean_for_reuse && !server_close &&
                          (http_1_1_or_higher || server_keep_alive);
        int timeout_sec = kDefaultKeepAliveTimeoutSeconds;
        bool has_max = false;
        int parsed_max = -1;
        if (keep_alive) {
            if (const auto ka = resp.headers.find("keep-alive"); ka != resp.headers.end()) {
                for (const std::string& param : split_http_comma_tokens(ka->second, true)) {
                    const auto eq = param.find('=');
                    if (eq == std::string::npos) continue;
                    const std::string k = trim_http_ws(std::string_view(param).substr(0, eq));
                    std::string v = trim_http_ws(std::string_view(param).substr(eq + 1));
                    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                        v = v.substr(1, v.size() - 2);
                    }
                    int num = 0;
                    const auto pr = std::from_chars(v.data(), v.data() + v.size(), num);
                    if (v.empty() || pr.ec != std::errc{} || pr.ptr != v.data() + v.size()) continue;
                    if (k == "timeout") {
                        if (num <= 0) keep_alive = false;
                        else timeout_sec = std::min(num, kMaxKeepAliveTimeoutSeconds);
                    } else if (k == "max") {
                        has_max = true;
                        parsed_max = num;
                    }
                }
            }
        }

        ++conn.requests_served;
        if (has_max) {
            if (parsed_max <= 1) {
                keep_alive = false;
            } else {
                const int cap_after = parsed_max - 1;
                conn.max_remaining = (conn.max_remaining < 0)
                    ? cap_after
                    : std::min(conn.max_remaining - 1, cap_after);
                if (conn.max_remaining <= 0) keep_alive = false;
            }
        } else if (conn.max_remaining > 0) {
            --conn.max_remaining;
            if (conn.max_remaining == 0) keep_alive = false;
        }

        if (keep_alive) {
            conn.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
            http_connection_pool().release(pool_key, std::move(conn));
        } else {
            close_http_connection(conn, /*graceful_tls_shutdown=*/true);
        }
        break;
    }

    if (!bodyless_status && !body_part.empty()) {
        if (const auto ce = resp.headers.find("content-encoding"); ce != resp.headers.end()) {
            const std::vector<std::string> codings = split_http_comma_tokens(ce->second, true);
            for (auto it = codings.rbegin(); it != codings.rend(); ++it) {
                const std::string& encoding = *it;
                if (encoding.empty() || encoding == "identity") continue;
                if (encoding == "br") {
                    std::vector<uint8_t> decoded;
                    if (!Brotli::decompress(reinterpret_cast<const uint8_t*>(body_part.data()),
                                            body_part.size(), decoded, MaxHttpResponseBytes)) {
                        resp.error_message = "Malformed or oversized Brotli HTTP response body";
                        return resp;
                    }
                    body_part.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
                } else if (encoding == "gzip" || encoding == "x-gzip") {
                    std::vector<uint8_t> decoded;
                    if (!Deflate::inflate_gzip(reinterpret_cast<const uint8_t*>(body_part.data()),
                                               body_part.size(), decoded, MaxHttpResponseBytes)) {
                        resp.error_message = "Malformed or oversized gzip HTTP response body";
                        return resp;
                    }
                    body_part.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
                } else if (encoding == "deflate") {
                    std::vector<uint8_t> decoded;
                    bool ok = Deflate::inflate_zlib(reinterpret_cast<const uint8_t*>(body_part.data()),
                                                    body_part.size(), decoded, MaxHttpResponseBytes);
                    if (!ok) {
                        decoded.clear();
                        ok = Deflate::inflate_raw(reinterpret_cast<const uint8_t*>(body_part.data()),
                                                  body_part.size(), decoded, MaxHttpResponseBytes);
                    }
                    if (!ok) {
                        resp.error_message = "Malformed or oversized deflate HTTP response body";
                        return resp;
                    }
                    body_part.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
                } else {
                    resp.error_message = "Unsupported HTTP Content-Encoding: " + encoding;
                    return resp;
                }
            }
        }
    }

    resp.body = std::move(body_part);
    resp.url = parsed_url.href(true);
    resp.network_ok = (resp.status_code >= 100 && resp.status_code <= 599);
    resp.success = resp.network_ok && (resp.status_code >= 200 && resp.status_code < 400);
    return resp;
}

void ResourceLoader::set_origin_root_for_testing(const std::string& origin, std::string directory_path) {
    Url u;
    const std::string key = Url::parse(origin, nullptr, u) ? u.origin() : origin;
    {
        std::lock_guard<std::mutex> lock(g_origin_map_mutex);
        if (directory_path.empty()) g_origin_roots.erase(key);
        else g_origin_roots[key] = std::move(directory_path);
    }
    clear_http_cache();
}

void ResourceLoader::clear_origin_roots_for_testing() {
    {
        std::lock_guard<std::mutex> lock(g_origin_map_mutex);
        g_origin_roots.clear();
    }
    clear_http_cache();
}

} // namespace litebird
