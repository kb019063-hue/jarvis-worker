#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace litebird {

class AbortSignal;

enum class HeadersGuard {
    None,
    Request,
    RequestNoCors
};

class HeadersObject {
public:
    std::unordered_map<std::string, std::string> map;
    HeadersGuard guard{HeadersGuard::None};

    void set(std::string name, std::string val);
    void append(std::string name, std::string val);
    bool has(std::string name) const;
    std::string get(std::string name) const;
    void remove(std::string name);

    static bool is_valid_header_name(std::string_view name);
    static bool is_forbidden_request_header(std::string_view name, std::string_view value = "");
    static bool is_no_cors_safelisted_request_header(std::string_view name, std::string_view value);

    static void extract_headers_init(
        const JsValue& init,
        std::unordered_map<std::string, std::string>& out
    );
    static void fill_headers_init(const JsValue& init, HeadersObject& target);
    static JsValue create_js_instance(
        const std::unordered_map<std::string, std::string>& init = {},
        HeadersGuard guard = HeadersGuard::None
    );
    static JsValue create_js_instance(std::shared_ptr<HeadersObject> existing);
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

class RequestObject {
public:
    std::string url;
    std::string method{"GET"};
    std::shared_ptr<HeadersObject> headers;
    JsValue headers_js;
    bool has_body{false};
    std::string body;
    std::string body_mime_type;
    bool body_used{false};
    std::string mode{"cors"};
    std::string credentials{"same-origin"};
    std::string cache{"default"};
    std::string redirect{"follow"};
    std::string referrer{"about:client"};
    std::string referrer_policy;
    std::string integrity;
    bool keepalive{false};
    std::string destination;
    std::shared_ptr<AbortSignal> signal;
    JsValue signal_js;

    static std::shared_ptr<RequestObject> from_js_value(const JsValue& val);
    static std::shared_ptr<RequestObject> construct(
        const std::vector<JsValue>& args,
        const ResourceRequestContext& context
    );
    static JsValue create_js_instance(
        std::shared_ptr<RequestObject> req,
        JsObject* proto_override = nullptr
    );
    static JsValue create_constructor(
        std::function<ResourceRequestContext()> context_fn = {}
    );
};

class FetchAPI {
public:
    /// The context is the document's, supplied by the host when the global
    /// fetch is installed. There is no default that reads local paths.
    static JsValue fetch(const std::vector<JsValue>& args,
                         const ResourceRequestContext& context);
};

} // namespace litebird
