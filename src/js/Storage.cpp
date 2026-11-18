#include "litebird/js/Storage.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/net/UrlParser.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace litebird {

Storage::Storage(std::string persist_path)
    : m_persist_path(std::move(persist_path)) {
    if (!m_persist_path.empty()) {
        load_from_disk();
    }
}

void Storage::update_keys() {
    m_keys.clear();
    for (const auto& [k, _] : m_data) {
        m_keys.push_back(k);
    }
    std::sort(m_keys.begin(), m_keys.end());
}

void Storage::set_item(const std::string& key, const std::string& value) {
    m_data[key] = value;
    update_keys();
    if (!m_persist_path.empty()) {
        save_to_disk();
    }
}

std::string Storage::get_item(const std::string& key) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        return it->second;
    }
    return "";
}

bool Storage::has_item(const std::string& key) const {
    return m_data.find(key) != m_data.end();
}

void Storage::remove_item(const std::string& key) {
    m_data.erase(key);
    update_keys();
    if (!m_persist_path.empty()) {
        save_to_disk();
    }
}

void Storage::clear() {
    m_data.clear();
    m_keys.clear();
    if (!m_persist_path.empty()) {
        save_to_disk();
    }
}

std::string Storage::key(size_t index) const {
    if (index < m_keys.size()) {
        return m_keys[index];
    }
    return "";
}

size_t Storage::length() const {
    return m_data.size();
}

void Storage::save_to_disk() {
    if (m_persist_path.empty()) return;
    std::ofstream out(m_persist_path);
    if (!out.is_open()) return;

    for (const auto& [k, v] : m_data) {
        out << k << "=" << v << "\n";
    }
}

void Storage::load_from_disk() {
    if (m_persist_path.empty()) return;
    std::ifstream in(m_persist_path);
    if (!in.is_open()) return;

    m_data.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            m_data[k] = v;
        }
    }
    update_keys();
}

JsValue Storage::to_js_value() {
    JsValue obj = JsValue::make_object();

    // The methods update the object they are called on (`this`) rather than
    // capturing `obj`: an object whose own methods hold a copy of it can never
    // be collected.
    obj.set_property("setItem", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            std::string k = args[0].to_string();
            std::string v = args[1].to_string();
            set_item(k, v);
            JsValue self = JsValue::current_this();
            if (self.is_object()) {
                self.set_property("length", JsValue::make_number(static_cast<double>(length())));
                self.set_property(k, JsValue::make_string(v));
            }
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("getItem", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_null();
        std::string k = args[0].to_string();
        if (has_item(k)) {
            return JsValue::make_string(get_item(k));
        }
        return JsValue::make_null();
    }));

    obj.set_property("removeItem", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            std::string k = args[0].to_string();
            remove_item(k);
            JsValue self = JsValue::current_this();
            if (self.is_object()) {
                self.set_property("length", JsValue::make_number(static_cast<double>(length())));
                self.set_property(k, JsValue::make_undefined());
            }
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("clear", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        clear();
        JsValue self = JsValue::current_this();
        if (self.is_object()) self.set_property("length", JsValue::make_number(0));
        return JsValue::make_undefined();
    }));

    obj.set_property("key", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_null();
        size_t idx = static_cast<size_t>(args[0].to_number());
        if (idx < length()) {
            return JsValue::make_string(key(idx));
        }
        return JsValue::make_null();
    }));

    obj.set_property("length", JsValue::make_number(static_cast<double>(length())));

    for (const auto& [k, v] : m_data) {
        obj.set_property(k, JsValue::make_string(v));
    }

    return obj;
}

// In memory unless the host asks for a file (the interactive shell does). It
// used to be /tmp/litebird_localstorage.db for every process: two runs at
// once shared it, and a page that stored a key called "getItem" broke
// localStorage for the next program that started, the unit tests included.
Storage& Storage::local_storage() {
    static Storage s_local("");
    return s_local;
}

void Storage::persist_to(const std::string& path) {
    m_persist_path = path;
    if (!m_persist_path.empty()) load_from_disk();
}

Storage& Storage::session_storage() {
    static Storage s_session("");
    return s_session;
}

// --- CookieJar Implementation ---

namespace {

double current_epoch_ms() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string cookie_ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') c = static_cast<char>(u + ('a' - 'A'));
    }
    return out;
}

std::string trim_cookie_ows(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return std::string(sv);
}

bool has_forbidden_cookie_ctl(std::string_view sv) {
    for (unsigned char c : sv) {
        if ((c < 0x20 && c != '\t') || c == 0x7f) return true;
    }
    return false;
}

bool is_ip_host_literal(std::string_view host) {
    if (host.empty()) return false;
    if (host.front() == '[' && host.back() == ']') return true;
    bool saw_digit = false;
    bool saw_dot = false;
    for (unsigned char c : host) {
        if (c >= '0' && c <= '9') {
            saw_digit = true;
        } else if (c == '.') {
            saw_dot = true;
        } else {
            return false;
        }
    }
    return saw_digit && saw_dot;
}

// RFC 6265 §5.1.3 domain matching.
bool cookie_domain_matches(std::string_view req_host, std::string_view cookie_domain) {
    if (req_host.empty() || cookie_domain.empty()) return false;
    if (req_host == cookie_domain) return true;
    if (is_ip_host_literal(req_host)) return false;
    if (req_host.size() <= cookie_domain.size() + 1) return false;
    const std::size_t suffix_pos = req_host.size() - cookie_domain.size();
    return req_host[suffix_pos - 1] == '.' &&
           req_host.substr(suffix_pos) == cookie_domain;
}

// RFC 6265 §5.1.4 default-path computation.
std::string default_cookie_path(std::string_view uri_path) {
    if (uri_path.empty() || uri_path.front() != '/') return "/";
    const std::size_t last_slash = uri_path.rfind('/');
    if (last_slash == 0 || last_slash == std::string_view::npos) return "/";
    return std::string(uri_path.substr(0, last_slash));
}

// RFC 6265 §5.1.4 path-matching.
bool cookie_path_matches(std::string_view req_path, std::string_view cookie_path) {
    const std::string_view normalized_req = req_path.empty() ? std::string_view("/") : req_path;
    if (normalized_req == cookie_path) return true;
    if (normalized_req.size() > cookie_path.size() &&
        normalized_req.substr(0, cookie_path.size()) == cookie_path) {
        if (!cookie_path.empty() && cookie_path.back() == '/') return true;
        if (normalized_req[cookie_path.size()] == '/') return true;
    }
    return false;
}

std::string registrable_site_key(const Url& url) {
    if (url.scheme != "http" && url.scheme != "https") return {};
    const std::string host = cookie_ascii_lower(url.host);
    if (host.empty() || is_ip_host_literal(host)) return host;

    std::vector<std::string_view> labels;
    std::string_view rem(host);
    while (!rem.empty()) {
        const auto dot = rem.find('.');
        if (dot == std::string_view::npos) {
            labels.push_back(rem);
            break;
        }
        labels.push_back(rem.substr(0, dot));
        rem.remove_prefix(dot + 1);
    }
    if (labels.size() <= 2) return host;

    std::size_t keep = 2;
    const std::string_view sld = labels[labels.size() - 2];
    const std::string_view tld = labels.back();
    if (tld.size() == 2 &&
        (sld == "co" || sld == "com" || sld == "org" || sld == "net" || sld == "gov" || sld == "ac")) {
        keep = std::min<std::size_t>(3, labels.size());
    }

    std::string site;
    for (std::size_t i = labels.size() - keep; i < labels.size(); ++i) {
        if (!site.empty()) site.push_back('.');
        site.append(labels[i]);
    }
    return site;
}

bool are_same_site_urls(const Url& req_url, const Url& site_url) {
    const std::string r_site = registrable_site_key(req_url);
    const std::string s_site = registrable_site_key(site_url);
    return !r_site.empty() && r_site == s_site;
}

} // namespace

CookieJar& CookieJar::default_jar() {
    static CookieJar s_jar;
    return s_jar;
}

void CookieJar::prune_expired_locked(double now_ms) const {
    m_cookies.erase(
        std::remove_if(m_cookies.begin(), m_cookies.end(), [now_ms](const Cookie& c) {
            return c.has_expiry && c.expires_ms <= now_ms;
        }),
        m_cookies.end()
    );
}

// One cookie from the text of a Set-Cookie header or an assignment to
// document.cookie: "name=value; Path=/; Domain=...; Expires=...; Max-Age=...;
// Secure; HttpOnly; SameSite=...".
//
// A cookie keeps its place when its value changes, and one that is set with
// a time in the past is deleted: that is how a page removes a cookie.
void CookieJar::store_cookie_internal(
    const std::string& set_cookie_str,
    const std::string& request_url,
    bool from_http
) {
    if (set_cookie_str.empty() || has_forbidden_cookie_ctl(set_cookie_str)) return;

    Url parsed_url;
    const bool have_url = !request_url.empty() && Url::parse(request_url, nullptr, parsed_url) &&
                          (parsed_url.scheme == "http" || parsed_url.scheme == "https" || parsed_url.scheme == "file");
    const bool url_is_https = have_url && parsed_url.scheme == "https";
    const std::string req_host = (have_url && (parsed_url.scheme == "http" || parsed_url.scheme == "https"))
                                     ? cookie_ascii_lower(parsed_url.host)
                                     : std::string();

    std::stringstream ss(set_cookie_str);
    std::string part;
    std::string name;
    std::string val;
    std::string explicit_path;
    std::string explicit_domain;
    bool saw_domain_attr = false;
    bool saw_explicit_slash_path = false;
    bool secure_flag = false;
    bool http_only_flag = false;
    SameSite same_site = SameSite::Lax;
    bool has_max_age = false;
    double max_age_expires_ms = 0.0;
    bool max_age_expired = false;
    bool has_expires = false;
    double expires_at_ms = 0.0;
    bool expires_expired = false;

    const double now_ms = current_epoch_ms();

    bool first = true;
    while (std::getline(ss, part, ';')) {
        part = trim_cookie_ows(part);
        if (part.empty() && !first) continue;
        const auto eq = part.find('=');
        if (first) {
            first = false;
            if (eq != std::string::npos) {
                name = trim_cookie_ows(std::string_view(part).substr(0, eq));
                val = trim_cookie_ows(std::string_view(part).substr(eq + 1));
            } else {
                // "value" alone is a cookie with an empty name.
                val = part;
            }
            continue;
        }
        std::string attr = cookie_ascii_lower(
            trim_cookie_ows(eq == std::string_view::npos ? std::string_view(part) : std::string_view(part).substr(0, eq))
        );
        const std::string attr_val = eq == std::string::npos
                                         ? std::string()
                                         : trim_cookie_ows(std::string_view(part).substr(eq + 1));
        if (attr == "path") {
            if (!attr_val.empty() && attr_val.front() == '/') {
                explicit_path = attr_val;
                saw_explicit_slash_path = (attr_val == "/");
            }
        } else if (attr == "domain") {
            if (!attr_val.empty()) {
                std::string d = cookie_ascii_lower(attr_val);
                if (!d.empty() && d.front() == '.') d.erase(d.begin());
                if (d.empty() || d.front() == '.' || d.back() == '.') return;
                explicit_domain = std::move(d);
                saw_domain_attr = true;
            }
        } else if (attr == "max-age") {
            if (!attr_val.empty()) {
                char* end = nullptr;
                const long long seconds = std::strtoll(attr_val.c_str(), &end, 10);
                if (end != attr_val.c_str() && *end == '\0') {
                    has_max_age = true;
                    if (seconds <= 0) {
                        max_age_expired = true;
                        max_age_expires_ms = 0.0;
                    } else {
                        const double clamped = std::min<double>(static_cast<double>(seconds), 34560000.0); // 400 days cap
                        max_age_expires_ms = now_ms + clamped * 1000.0;
                    }
                }
            }
        } else if (attr == "expires") {
            const double when = js_parse_date_string(attr_val);
            if (!std::isnan(when)) {
                has_expires = true;
                expires_at_ms = when;
                expires_expired = (when <= now_ms);
            }
        } else if (attr == "secure") {
            secure_flag = true;
        } else if (attr == "httponly") {
            http_only_flag = true;
        } else if (attr == "samesite") {
            const std::string ss_val = cookie_ascii_lower(attr_val);
            if (ss_val == "strict") same_site = SameSite::Strict;
            else if (ss_val == "none") same_site = SameSite::None;
            else if (ss_val == "lax") same_site = SameSite::Lax;
        }
    }

    if (name.empty() && val.empty()) return;
    if (name.size() + val.size() > kMaxCookieBytes) return;

    // RFC 6265 §5.3 step 10: non-HTTP APIs (`document.cookie`) cannot set HttpOnly cookies.
    if (!from_http && http_only_flag) return;

    // RFC 6265bis: `SameSite=None` requires `Secure`.
    if (same_site == SameSite::None && !secure_flag) return;

    // RFC 6265bis: `Secure` cookies cannot be set by insecure HTTP origins.
    if (secure_flag && have_url && parsed_url.scheme == "http") return;

    // RFC 6265bis §4.1.3 cookie name prefixes (`__Secure-`, `__Host-`).
    if (name.rfind("__Secure-", 0) == 0) {
        if (!secure_flag || (have_url && !url_is_https)) return;
    }
    if (name.rfind("__Host-", 0) == 0) {
        if (!secure_flag || (have_url && !url_is_https) || saw_domain_attr || !saw_explicit_slash_path) return;
    }

    bool host_only = true;
    std::string cookie_domain;
    if (!req_host.empty()) {
        if (saw_domain_attr) {
            // Refuse single-label public-suffix-style Domain attributes unless exact host match.
            if (explicit_domain.find('.') == std::string::npos && explicit_domain != req_host) {
                return;
            }
            if (!cookie_domain_matches(req_host, explicit_domain)) {
                return;
            }
            cookie_domain = explicit_domain;
            host_only = is_ip_host_literal(req_host);
        } else {
            cookie_domain = req_host;
            host_only = true;
        }
    } else if (saw_domain_attr) {
        cookie_domain = explicit_domain;
        host_only = false;
    }

    const std::string cookie_path = !explicit_path.empty()
                                        ? explicit_path
                                        : (have_url ? default_cookie_path(parsed_url.pathname()) : std::string("/"));

    // Max-Age wins over Expires, whichever comes first in the text.
    const bool expired = has_max_age ? max_age_expired : (has_expires && expires_expired);
    const bool has_expiry = has_max_age || has_expires;
    const double final_expires_ms = has_max_age ? max_age_expires_ms : expires_at_ms;

    std::lock_guard<std::mutex> lock(m_mutex);
    prune_expired_locked(now_ms);

    // RFC 6265 §5.3 step 11.2: a non-HTTP caller (`document.cookie`) must not
    // overwrite or delete an existing `HttpOnly` cookie that covers this name/path/domain.
    if (!from_http) {
        for (const auto& c : m_cookies) {
            if (!c.http_only || c.name != name) continue;
            const bool domain_overlap = cookie_domain.empty() || c.domain.empty() ||
                                        c.domain == cookie_domain ||
                                        cookie_domain_matches(cookie_domain, c.domain) ||
                                        cookie_domain_matches(c.domain, cookie_domain);
            const bool path_overlap = (c.path == cookie_path) ||
                                      cookie_path_matches(cookie_path, c.path);
            if (domain_overlap && path_overlap) {
                return;
            }
        }
    }

    auto existing = std::find_if(m_cookies.begin(), m_cookies.end(), [&](const Cookie& cookie) {
        return cookie.name == name &&
               cookie.domain == cookie_domain &&
               cookie.host_only == host_only &&
               cookie.path == cookie_path;
    });

    if (expired) {
        if (existing != m_cookies.end()) {
            m_cookies.erase(existing);
        }
        return;
    }

    Cookie updated{
        name,
        val,
        cookie_domain,
        cookie_path,
        host_only,
        secure_flag,
        http_only_flag,
        same_site,
        has_expiry,
        final_expires_ms
    };

    if (existing != m_cookies.end()) {
        *existing = std::move(updated);
        return;
    }

    // Bound per-domain and total jar size against hostile Set-Cookie floods.
    if (!cookie_domain.empty()) {
        std::size_t domain_count = 0;
        std::size_t oldest_idx = m_cookies.size();
        for (std::size_t i = 0; i < m_cookies.size(); ++i) {
            if (m_cookies[i].domain == cookie_domain) {
                if (oldest_idx == m_cookies.size()) oldest_idx = i;
                ++domain_count;
            }
        }
        if (domain_count >= kMaxCookiesPerDomain && oldest_idx < m_cookies.size()) {
            m_cookies.erase(m_cookies.begin() + static_cast<std::ptrdiff_t>(oldest_idx));
        }
    }
    if (m_cookies.size() >= kMaxTotalCookies && !m_cookies.empty()) {
        m_cookies.erase(m_cookies.begin());
    }
    m_cookies.push_back(std::move(updated));
}

void CookieJar::set_cookie_from_header(const std::string& set_cookie_str, const std::string& request_url) {
    store_cookie_internal(set_cookie_str, request_url, /*from_http=*/true);
}

void CookieJar::set_document_cookie(const std::string& cookie_str, const std::string& document_url) {
    store_cookie_internal(cookie_str, document_url, /*from_http=*/false);
}

std::string CookieJar::get_cookie_header(
    const std::string& request_url,
    const std::string& site_for_cookies_url,
    bool is_top_level_navigation,
    const std::string& method
) const {
    const double now_ms = current_epoch_ms();
    std::lock_guard<std::mutex> lock(m_mutex);
    prune_expired_locked(now_ms);

    Url req_url;
    const bool have_req = !request_url.empty() && Url::parse(request_url, nullptr, req_url) &&
                          (req_url.scheme == "http" || req_url.scheme == "https");
    if (!request_url.empty() && !have_req) {
        return "";
    }

    bool is_same_site = true;
    if (have_req && !site_for_cookies_url.empty()) {
        Url site_url;
        if (Url::parse(site_for_cookies_url, nullptr, site_url)) {
            is_same_site = are_same_site_urls(req_url, site_url);
        } else {
            is_same_site = false;
        }
    }

    const std::string req_host = have_req ? cookie_ascii_lower(req_url.host) : std::string();
    const std::string req_path = have_req ? req_url.pathname() : std::string("/");
    const bool req_is_https = have_req && req_url.scheme == "https";
    const std::string lower_method = cookie_ascii_lower(method);
    const bool safe_nav_method = (lower_method.empty() || lower_method == "get" || lower_method == "head");

    std::vector<const Cookie*> order;
    order.reserve(m_cookies.size());
    for (const auto& cookie : m_cookies) {
        if (have_req) {
            if (cookie.secure && !req_is_https) continue;

            if (!cookie.domain.empty()) {
                if (cookie.host_only) {
                    if (req_host != cookie.domain) continue;
                } else {
                    if (!cookie_domain_matches(req_host, cookie.domain)) continue;
                }
            } else if (!is_same_site) {
                // URL-less document.cookie entries must never leak cross-site.
                continue;
            }

            if (!cookie_path_matches(req_path, cookie.path)) continue;

            if (!is_same_site) {
                if (cookie.same_site == SameSite::Strict) continue;
                if (cookie.same_site == SameSite::Lax &&
                    !(is_top_level_navigation && safe_nav_method)) {
                    continue;
                }
                if (cookie.same_site == SameSite::None && (!cookie.secure || !req_is_https)) {
                    continue;
                }
            }
        }
        order.push_back(&cookie);
    }

    // RFC 6265 §5.4 step 2: longer paths first, and among equals the older cookie first.
    std::stable_sort(order.begin(), order.end(), [](const Cookie* a, const Cookie* b) {
        return a->path.size() > b->path.size();
    });

    std::string result;
    for (const Cookie* cookie : order) {
        if (!result.empty()) result += "; ";
        if (!cookie->name.empty()) result += cookie->name + "=";
        result += cookie->value;
    }
    return result;
}

std::string CookieJar::get_document_cookie(const std::string& document_url) const {
    const double now_ms = current_epoch_ms();
    std::lock_guard<std::mutex> lock(m_mutex);
    prune_expired_locked(now_ms);

    Url doc_url;
    const bool have_doc = !document_url.empty() && Url::parse(document_url, nullptr, doc_url) &&
                          (doc_url.scheme == "http" || doc_url.scheme == "https");
    const std::string doc_host = have_doc ? cookie_ascii_lower(doc_url.host) : std::string();
    const std::string doc_path = have_doc ? doc_url.pathname() : std::string("/");
    const bool doc_is_https = have_doc && doc_url.scheme == "https";

    std::vector<const Cookie*> order;
    order.reserve(m_cookies.size());
    for (const auto& cookie : m_cookies) {
        if (cookie.http_only) continue;
        if (have_doc) {
            if (cookie.secure && !doc_is_https) continue;
            if (!cookie.domain.empty()) {
                if (cookie.host_only) {
                    if (doc_host != cookie.domain) continue;
                } else {
                    if (!cookie_domain_matches(doc_host, cookie.domain)) continue;
                }
            }
            if (!cookie_path_matches(doc_path, cookie.path)) continue;
        }
        order.push_back(&cookie);
    }

    std::stable_sort(order.begin(), order.end(), [](const Cookie* a, const Cookie* b) {
        return a->path.size() > b->path.size();
    });

    std::string result;
    for (const Cookie* cookie : order) {
        if (!result.empty()) result += "; ";
        if (!cookie->name.empty()) result += cookie->name + "=";
        result += cookie->value;
    }
    return result;
}

void CookieJar::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cookies.clear();
}

std::size_t CookieJar::size() const {
    const double now_ms = current_epoch_ms();
    std::lock_guard<std::mutex> lock(m_mutex);
    prune_expired_locked(now_ms);
    return m_cookies.size();
}

} // namespace litebird
