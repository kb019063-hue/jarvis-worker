#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <string>
#include <unordered_map>
#include <memory>

namespace litebird {

class HeadersObject {
public:
    std::unordered_map<std::string, std::string> map;

    void set(std::string name, std::string val);
    void append(std::string name, std::string val);
    bool has(std::string name) const;
    std::string get(std::string name) const;
    void remove(std::string name);

    static JsValue create_js_instance(const std::unordered_map<std::string, std::string>& init = {});
    static JsValue create_constructor();
};

class ResponseObject {
public:
    int status{200};
    std::string status_text{"OK"};
    bool ok{true};
    std::string url;
    std::string body;
    std::shared_ptr<HeadersObject> headers;

    static JsValue create_js_instance(
        const std::string& body = "",
        int status = 200,
        const std::string& status_text = "OK",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& url = ""
    );
    static JsValue create_constructor();
};

class FetchAPI {
public:
    /// The context is the document's, supplied by the host when the global
    /// fetch is installed. There is no default that reads local paths.
    static JsValue fetch(const std::vector<JsValue>& args,
                         const ResourceRequestContext& context);
};

} // namespace litebird
