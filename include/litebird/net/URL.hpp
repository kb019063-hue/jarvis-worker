#pragma once

#include "litebird/js/JsValue.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <utility>
#include <functional>

namespace litebird {

class URLSearchParamsObject {
public:
    std::vector<std::pair<std::string, std::string>> entries;
    std::function<void()> on_change;

    void parse(std::string_view query);
    std::string to_string() const;
    void set(const std::string& name, const std::string& value);
    void append(const std::string& name, const std::string& value);
    bool has(const std::string& name) const;
    std::string get(const std::string& name) const;
    std::vector<std::string> get_all(const std::string& name) const;
    void remove(const std::string& name);

    static JsValue wrap_existing(std::shared_ptr<URLSearchParamsObject> existing);
    static JsValue create_js_instance(
        const std::string& init = "",
        std::function<void()> change_listener = nullptr
    );
    static JsValue create_constructor();
};

class URLObject {
public:
    std::string href;
    std::string protocol;
    std::string host;
    std::string hostname;
    std::string port;
    std::string pathname{"/"};
    std::string search;
    std::string hash;
    std::string origin;

    std::shared_ptr<URLSearchParamsObject> search_params;

    bool parse(std::string_view url_str, std::string_view base_str = "");
    void update_from_search_params();
    void update_search(const std::string& new_search);

    static JsValue create_js_instance(std::string_view url_str, std::string_view base_str = "");
    static JsValue create_constructor();
};

} // namespace litebird
