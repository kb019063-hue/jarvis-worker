#pragma once

#include "litebird/js/JsValue.hpp"
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace litebird {

class Storage {
public:
    explicit Storage(std::string persist_path = "");

    void set_item(const std::string& key, const std::string& value);
    std::string get_item(const std::string& key) const;
    bool has_item(const std::string& key) const;
    void remove_item(const std::string& key);
    void clear();
    std::string key(size_t index) const;
    size_t length() const;

    void save_to_disk();
    void load_from_disk();
    /// Keeps the contents in `path` from now on, starting with what is there.
    void persist_to(const std::string& path);

    JsValue to_js_value();

    static Storage& local_storage();
    static Storage& session_storage();

private:
    std::unordered_map<std::string, std::string> m_data;
    std::vector<std::string> m_keys;
    std::string m_persist_path;

    void update_keys();
};

class CookieJar {
public:
    enum class SameSite {
        Lax,
        Strict,
        None
    };

    static constexpr std::size_t kMaxCookieBytes = 4096;
    static constexpr std::size_t kMaxCookiesPerDomain = 180;
    static constexpr std::size_t kMaxTotalCookies = 3000;

    static CookieJar& default_jar();

    /// Builds the outbound HTTP `Cookie:` header value for `request_url`
    /// enforcing RFC 6265 domain, path, expiry, `Secure`, and `SameSite` rules
    /// (including `HttpOnly` cookies). When `request_url` is empty, returns all
    /// non-expired cookies in RFC 6265 path/creation order.
    std::string get_cookie_header(
        const std::string& request_url = "",
        const std::string& site_for_cookies_url = "",
        bool is_top_level_navigation = true,
        const std::string& method = "GET"
    ) const;

    /// Parses and stores an HTTP `Set-Cookie` response header received from
    /// `request_url`. Enforces domain-matching, default-path computation,
    /// `Max-Age`/`Expires`, `Secure`, `HttpOnly`, `SameSite`, and `__Secure-` /
    /// `__Host-` prefix invariants.
    void set_cookie_from_header(const std::string& set_cookie_str, const std::string& request_url = "");

    /// Returns cookies visible to `document.cookie` (excludes `HttpOnly` and
    /// expired cookies; filters by `document_url` when non-empty).
    std::string get_document_cookie(const std::string& document_url = "") const;

    /// Stores a cookie assigned via `document.cookie = ...`. Rejects `HttpOnly`
    /// attributes and refuses to overwrite or expire an existing `HttpOnly` cookie.
    void set_document_cookie(const std::string& cookie_str, const std::string& document_url = "");

    void clear();
    std::size_t size() const;

private:
    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path{"/"};
        bool host_only{true};
        bool secure{false};
        bool http_only{false};
        SameSite same_site{SameSite::Lax};
        bool has_expiry{false};
        double expires_ms{0.0};
    };

    void store_cookie_internal(const std::string& raw_cookie, const std::string& request_url, bool from_http);
    void prune_expired_locked(double now_ms) const;

    mutable std::mutex m_mutex;
    /// In the order they were first set.
    mutable std::vector<Cookie> m_cookies;
};

} // namespace litebird
