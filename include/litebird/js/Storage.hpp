#pragma once

#include "litebird/js/JsValue.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

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
    static CookieJar& default_jar();

    std::string get_cookie_header(const std::string& url = "") const;
    void set_cookie_from_header(const std::string& set_cookie_str, const std::string& url = "");

    std::string get_document_cookie() const;
    void set_document_cookie(const std::string& cookie_str);

    void clear();

private:
    struct Cookie {
        std::string name;
        std::string value;
        std::string path{"/"};
        std::string domain;
    };

    std::unordered_map<std::string, Cookie> m_cookies;
};

} // namespace litebird
