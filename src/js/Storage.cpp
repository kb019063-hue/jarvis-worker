#include "litebird/js/Storage.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

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

    obj.set_property("setItem", JsValue::make_native_function([this, obj](const std::vector<JsValue>& args) mutable {
        if (args.size() >= 2) {
            std::string k = args[0].to_string();
            std::string v = args[1].to_string();
            set_item(k, v);
            obj.set_property("length", JsValue::make_number(static_cast<double>(length())));
            obj.set_property(k, JsValue::make_string(v));
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

    obj.set_property("removeItem", JsValue::make_native_function([this, obj](const std::vector<JsValue>& args) mutable {
        if (!args.empty()) {
            std::string k = args[0].to_string();
            remove_item(k);
            obj.set_property("length", JsValue::make_number(static_cast<double>(length())));
            obj.set_property(k, JsValue::make_undefined());
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("clear", JsValue::make_native_function([this, obj](const std::vector<JsValue>&) mutable {
        clear();
        obj.set_property("length", JsValue::make_number(0));
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

Storage& Storage::local_storage() {
    static Storage s_local("/tmp/litebird_localstorage.db");
    return s_local;
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
    std::string result;
    for (const auto& [name, cookie] : m_cookies) {
        if (!result.empty()) result += "; ";
        result += cookie.name + "=" + cookie.value;
    }
    return result;
}

void CookieJar::set_cookie_from_header(const std::string& set_cookie_str, const std::string&) {
    if (set_cookie_str.empty()) return;

    // e.g. "sessionId=abc1234; Path=/; HttpOnly"
    std::stringstream ss(set_cookie_str);
    std::string part;
    std::string name, val, path = "/";

    bool first = true;
    while (std::getline(ss, part, ';')) {
        // Trim whitespace
        size_t start = part.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = part.find_last_not_of(" \t");
        part = part.substr(start, end - start + 1);

        auto eq = part.find('=');
        if (first) {
            first = false;
            if (eq != std::string::npos) {
                name = part.substr(0, eq);
                val = part.substr(eq + 1);
            } else {
                name = part;
                val = "";
            }
        } else {
            if (eq != std::string::npos) {
                std::string attr = part.substr(0, eq);
                std::string attr_val = part.substr(eq + 1);
                std::transform(attr.begin(), attr.end(), attr.begin(), ::tolower);
                if (attr == "path") {
                    path = attr_val;
                }
            }
        }
    }

    if (!name.empty()) {
        m_cookies[name] = Cookie{name, val, path, ""};
    }
}

std::string CookieJar::get_document_cookie() const {
    std::string result;
    for (const auto& [name, cookie] : m_cookies) {
        if (!result.empty()) result += "; ";
        result += cookie.name + "=" + cookie.value;
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
