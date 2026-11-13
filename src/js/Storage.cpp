#include "litebird/js/Storage.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>

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

CookieJar& CookieJar::default_jar() {
    static CookieJar s_jar;
    return s_jar;
}

std::string CookieJar::get_cookie_header(const std::string&) const {
    return get_document_cookie();
}

// One cookie from the text of a Set-Cookie header or an assignment to
// document.cookie: "name=value; Path=/; Expires=...; Max-Age=...".
//
// A cookie keeps its place when its value changes, and one that is set with
// a time in the past is deleted: that is how a page removes a cookie.
// (Cookies used to live in a hash table, so document.cookie listed them in
// no particular order, and deleting one left an empty cookie behind.)
void CookieJar::set_cookie_from_header(const std::string& set_cookie_str, const std::string&) {
    if (set_cookie_str.empty()) return;

    auto trim = [](std::string text) {
        const size_t start = text.find_first_not_of(" \t");
        if (start == std::string::npos) return std::string();
        const size_t end = text.find_last_not_of(" \t");
        return text.substr(start, end - start + 1);
    };

    std::stringstream ss(set_cookie_str);
    std::string part;
    std::string name, val, path = "/";
    bool has_max_age = false;
    bool max_age_expired = false;
    bool expires_expired = false;

    bool first = true;
    while (std::getline(ss, part, ';')) {
        part = trim(part);
        if (part.empty() && !first) continue;
        const auto eq = part.find('=');
        if (first) {
            first = false;
            if (eq != std::string::npos) {
                name = trim(part.substr(0, eq));
                val = trim(part.substr(eq + 1));
            } else {
                // "value" alone is a cookie with an empty name.
                val = part;
            }
            continue;
        }
        std::string attr = trim(eq == std::string::npos ? part : part.substr(0, eq));
        const std::string attr_val = eq == std::string::npos ? std::string() : trim(part.substr(eq + 1));
        std::transform(attr.begin(), attr.end(), attr.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (attr == "path") {
            if (!attr_val.empty() && attr_val[0] == '/') path = attr_val;
        } else if (attr == "max-age") {
            char* end = nullptr;
            const long seconds = std::strtol(attr_val.c_str(), &end, 10);
            if (end != attr_val.c_str()) {
                has_max_age = true;
                max_age_expired = seconds <= 0;
            }
        } else if (attr == "expires") {
            const double when = js_parse_date_string(attr_val);
            const double now = std::chrono::duration<double, std::milli>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (when == when) expires_expired = when <= now;       // a date that does not parse is ignored
        }
    }
    if (name.empty() && val.empty()) return;
    // Max-Age wins over Expires, whichever comes first in the text.
    const bool expired = has_max_age ? max_age_expired : expires_expired;

    auto existing = std::find_if(m_cookies.begin(), m_cookies.end(), [&](const Cookie& cookie) {
        return cookie.name == name && cookie.path == path;
    });
    if (expired) {
        if (existing != m_cookies.end()) m_cookies.erase(existing);
        return;
    }
    if (existing != m_cookies.end()) {
        existing->value = val;
    } else {
        m_cookies.push_back(Cookie{name, val, path, ""});
    }
}

std::string CookieJar::get_document_cookie() const {
    // Longer paths first, and among equals the older cookie first.
    std::vector<const Cookie*> order;
    order.reserve(m_cookies.size());
    for (const auto& cookie : m_cookies) order.push_back(&cookie);
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

void CookieJar::set_document_cookie(const std::string& cookie_str) {
    set_cookie_from_header(cookie_str);
}

void CookieJar::clear() {
    m_cookies.clear();
}

} // namespace litebird
