#include "litebird/net/ResourceLoader.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/Deflate.hpp"
#include "litebird/net/UrlParser.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
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
    int (*SSL_connect_fn)(SSL*) = nullptr;
    int (*SSL_read_fn)(SSL*, void*, int) = nullptr;
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
                load(out.SSL_connect_fn, "SSL_connect") &&
                load(out.SSL_read_fn, "SSL_read") &&
                load(out.SSL_write_fn, "SSL_write") &&
                load(out.SSL_shutdown_fn, "SSL_shutdown") &&
                load(out.SSL_get_error_fn, "SSL_get_error") &&
                load(out.SSL_get_verify_result_fn, "SSL_get_verify_result");

            out.available = ok;
            return out;
        }();
        return api;
    }
};

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

bool decode_chunked_body(std::string_view raw, std::string& out, std::size_t max_bytes) {
    out.clear();
    std::size_t pos = 0;
    while (true) {
        const auto line_end = raw.find("\r\n", pos);
        if (line_end == std::string_view::npos) return false;
        std::string_view size_line = raw.substr(pos, line_end - pos);
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
            if (chunk_size > (max_bytes - static_cast<std::size_t>(d)) / 16u) return false;
            chunk_size = chunk_size * 16u + static_cast<std::size_t>(d);
        }
        pos = line_end + 2;

        if (chunk_size == 0) {
            // Optional trailer fields terminated by an empty CRLF line.
            if (pos + 2 <= raw.size() && raw.substr(pos, 2) == "\r\n") {
                return true;
            }
            const auto trailer_end = raw.find("\r\n\r\n", pos);
            return trailer_end != std::string_view::npos;
        }

        if (chunk_size > max_bytes - out.size()) return false;
        if (chunk_size + 2 > raw.size() - pos) return false;
        if (raw[pos + chunk_size] != '\r' || raw[pos + chunk_size + 1] != '\n') return false;

        out.append(raw.data() + pos, chunk_size);
        pos += chunk_size + 2;
    }
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

} // namespace

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

void ResourceLoader::set_ca_bundle_for_testing(std::string ca_file_path) {
    std::lock_guard<std::mutex> lock(g_ca_mutex);
    g_test_ca_bundle = std::move(ca_file_path);
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
        request += "Accept-Encoding: gzip, deflate\r\n";
    }
    for (const auto& [hk, hv] : headers) {
        request += hk + ": " + hv + "\r\n";
    }
    if (!has_content_length && !body.empty()) {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "Connection: close\r\n";
    if (request.size() + 2 > MaxHttpHeaderBytes) {
        resp.error_message = "HTTP request headers exceed 64 KiB limit";
        return resp;
    }
    request += "\r\n";
    if (!body.empty()) request += body;

    const int sockfd = connect_with_timeout(connect_host, port, 1500, resp.error_message);
    if (sockfd < 0) {
        return resp;
    }

    std::string raw_response;

    if (use_tls) {
        const OpenSslApi& ssl_api = OpenSslApi::instance();
        if (!ssl_api.available) {
            ::close(sockfd);
            resp.error_message = "TLS library (libssl.so.3) is unavailable on this host";
            return resp;
        }

        OpenSslApi::SSL_CTX* ctx = ssl_api.SSL_CTX_new_fn(ssl_api.TLS_client_method_fn());
        if (!ctx) {
            ::close(sockfd);
            resp.error_message = "Failed to initialize TLS context";
            return resp;
        }

        const std::string custom_ca = get_test_ca_bundle();
        if (!custom_ca.empty()) {
            if (ssl_api.SSL_CTX_load_verify_locations_fn(ctx, custom_ca.c_str(), nullptr) != 1) {
                ssl_api.SSL_CTX_free_fn(ctx);
                ::close(sockfd);
                resp.error_message = "Failed to load custom TLS CA bundle";
                return resp;
            }
        } else {
            (void)ssl_api.SSL_CTX_set_default_verify_paths_fn(ctx);
        }
        // SSL_VERIFY_PEER = 0x01
        ssl_api.SSL_CTX_set_verify_fn(ctx, 0x01, nullptr);

        OpenSslApi::SSL* ssl = ssl_api.SSL_new_fn(ctx);
        if (!ssl) {
            ssl_api.SSL_CTX_free_fn(ctx);
            ::close(sockfd);
            resp.error_message = "Failed to allocate TLS session";
            return resp;
        }

        auto cleanup_tls = [&]() {
            ssl_api.SSL_free_fn(ssl);
            ssl_api.SSL_CTX_free_fn(ctx);
            ::close(sockfd);
        };

        (void)ssl_api.SSL_set_fd_fn(ssl, sockfd);
        if (!is_ip_literal(connect_host)) {
            // SSL_CTRL_SET_TLSEXT_HOSTNAME = 55, TLSEXT_NAMETYPE_host_name = 0
            (void)ssl_api.SSL_ctrl_fn(ssl, 55, 0, const_cast<char*>(connect_host.c_str()));
            (void)ssl_api.SSL_set1_host_fn(ssl, connect_host.c_str());
        }

        if (ssl_api.SSL_connect_fn(ssl) != 1 || ssl_api.SSL_get_verify_result_fn(ssl) != 0) {
            cleanup_tls();
            resp.error_message = "TLS handshake or certificate verification failed for " + connect_host;
            return resp;
        }

        std::size_t sent = 0;
        while (sent < request.size()) {
            const int chunk = static_cast<int>(std::min<std::size_t>(request.size() - sent, 65536u));
            const int written = ssl_api.SSL_write_fn(ssl, request.data() + sent, chunk);
            if (written <= 0) {
                cleanup_tls();
                resp.error_message = "Failed to write HTTPS request";
                return resp;
            }
            sent += static_cast<std::size_t>(written);
        }

        char buffer[8192];
        while (true) {
            const int n = ssl_api.SSL_read_fn(ssl, buffer, sizeof(buffer));
            if (n == 0) break;
            if (n < 0) {
                // SSL_ERROR_ZERO_RETURN = 6
                const int err = ssl_api.SSL_get_error_fn(ssl, n);
                if (err == 6) break;
                cleanup_tls();
                resp.error_message = "Failed while reading HTTPS response";
                return resp;
            }

            const auto received = static_cast<std::size_t>(n);
            if (received > MaxHttpResponseBytes - raw_response.size()) {
                cleanup_tls();
                resp.error_message = "HTTPS response exceeds 16 MiB size limit";
                return resp;
            }
            raw_response.append(buffer, received);

            std::size_t header_end = raw_response.find("\r\n\r\n");
            std::size_t sep_len = 4;
            if (header_end == std::string::npos) {
                header_end = raw_response.find("\n\n");
                sep_len = 2;
            }
            if ((header_end == std::string::npos && raw_response.size() > MaxHttpHeaderBytes) ||
                (header_end != std::string::npos && header_end + sep_len > MaxHttpHeaderBytes)) {
                cleanup_tls();
                resp.error_message = "HTTPS response headers exceed 64 KiB size limit";
                return resp;
            }
        }

        (void)ssl_api.SSL_shutdown_fn(ssl);
        cleanup_tls();
    } else {
        std::size_t sent = 0;
        while (sent < request.size()) {
            const ssize_t count = ::send(sockfd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) {
                ::close(sockfd);
                resp.error_message = "Failed to send HTTP request";
                return resp;
            }
            sent += static_cast<std::size_t>(count);
        }

        char buffer[8192];
        while (true) {
            const ssize_t bytes_read = ::recv(sockfd, buffer, sizeof(buffer), 0);
            if (bytes_read == 0) break;
            if (bytes_read < 0) {
                if (errno == EINTR) continue;
                ::close(sockfd);
                resp.error_message = "Failed while reading HTTP response";
                return resp;
            }

            const auto received = static_cast<std::size_t>(bytes_read);
            if (received > MaxHttpResponseBytes - raw_response.size()) {
                ::close(sockfd);
                resp.error_message = "HTTP response exceeds 16 MiB size limit";
                return resp;
            }
            raw_response.append(buffer, received);

            std::size_t header_end = raw_response.find("\r\n\r\n");
            std::size_t sep_len = 4;
            if (header_end == std::string::npos) {
                header_end = raw_response.find("\n\n");
                sep_len = 2;
            }
            if ((header_end == std::string::npos && raw_response.size() > MaxHttpHeaderBytes) ||
                (header_end != std::string::npos && header_end + sep_len > MaxHttpHeaderBytes)) {
                ::close(sockfd);
                resp.error_message = "HTTP response headers exceed 64 KiB size limit";
                return resp;
            }
        }
        ::close(sockfd);
    }

    if (raw_response.empty()) {
        resp.error_message = "Empty HTTP response";
        return resp;
    }

    std::size_t header_end = raw_response.find("\r\n\r\n");
    std::size_t header_separator_size = 4;
    if (header_end == std::string::npos) {
        header_end = raw_response.find("\n\n");
        header_separator_size = 2;
    }
    if (header_end == std::string::npos) {
        resp.error_message = "Malformed HTTP response: missing header terminator";
        return resp;
    }

    const std::string header_part = raw_response.substr(0, header_end);
    std::string body_part = raw_response.substr(header_end + header_separator_size);

    std::stringstream hss(header_part);
    std::string status_line;
    if (std::getline(hss, status_line)) {
        if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();
        std::stringstream sl(status_line);
        std::string http_ver;
        int code = 0;
        if (sl >> http_ver >> code && http_ver.rfind("HTTP/", 0) == 0 && code >= 100 && code <= 999) {
            resp.status_code = code;
            std::string rest;
            std::getline(sl, rest);
            resp.status_text = trim_http_ws(rest);
        } else {
            resp.error_message = "Malformed HTTP status line";
            return resp;
        }
    } else {
        resp.error_message = "Missing HTTP status line";
        return resp;
    }

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

    bool is_chunked = false;
    if (const auto te = resp.headers.find("transfer-encoding"); te != resp.headers.end()) {
        if (ascii_lower(te->second).find("chunked") != std::string::npos) {
            is_chunked = true;
        }
    }

    const bool bodyless_status =
        (resp.status_code >= 100 && resp.status_code < 200) ||
        resp.status_code == 204 ||
        resp.status_code == 304 ||
        ascii_lower(req_method) == "head";

    if (bodyless_status) {
        body_part.clear();
    } else if (is_chunked) {
        std::string unchunked;
        if (!decode_chunked_body(body_part, unchunked, MaxHttpResponseBytes)) {
            resp.error_message = "Malformed or oversized HTTP chunked response body";
            return resp;
        }
        body_part = std::move(unchunked);
    } else if (const auto content_length = resp.headers.find("content-length"); content_length != resp.headers.end()) {
        std::string_view value = content_length->second;
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
        std::size_t declared_size = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), declared_size);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
            resp.error_message = "Malformed HTTP Content-Length";
            return resp;
        }
        if (declared_size > MaxHttpResponseBytes) {
            resp.error_message = "HTTP response exceeds 16 MiB size limit";
            return resp;
        }
        if (body_part.size() > declared_size) {
            body_part.resize(declared_size);
        }
    }

    if (!bodyless_status && !body_part.empty()) {
        if (const auto ce = resp.headers.find("content-encoding"); ce != resp.headers.end()) {
            const std::string encoding = ascii_lower(trim_http_ws(ce->second));
            if (encoding == "gzip" || encoding == "x-gzip") {
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
            } else if (!encoding.empty() && encoding != "identity") {
                resp.error_message = "Unsupported HTTP Content-Encoding: " + encoding;
                return resp;
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
