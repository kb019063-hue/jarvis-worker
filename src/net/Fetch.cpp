#include "litebird/net/Fetch.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/TypedArray.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace litebird {

namespace {

std::string to_lower_str(std::string_view s) {
    std::string res(s);
    for (char& c : res) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') c = static_cast<char>(u + ('a' - 'A'));
    }
    return res;
}

std::string trim_fetch_ows(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return std::string(sv);
}

class HeadersExotic final : public JsExotic {
public:
    explicit HeadersExotic(std::shared_ptr<HeadersObject> h) : headers(std::move(h)) {}
    std::shared_ptr<HeadersObject> headers;
};

bool is_forbidden_fetch_method(std::string_view method) {
    const std::string lower = to_lower_str(method);
    return lower == "connect" || lower == "trace" || lower == "track";
}

std::string normalize_fetch_method(std::string method) {
    const std::string lower = to_lower_str(method);
    if (lower == "delete") return "DELETE";
    if (lower == "get") return "GET";
    if (lower == "head") return "HEAD";
    if (lower == "options") return "OPTIONS";
    if (lower == "post") return "POST";
    if (lower == "put") return "PUT";
    return method;
}

std::unordered_map<std::string, std::string> filter_response_headers_for_script(
    const ResourceResponse& resp,
    RequestMode mode,
    bool include_credentials
) {
    std::unordered_map<std::string, std::string> out;
    if (!resp.cross_origin || mode != RequestMode::Cors) {
        for (const auto& [k, v] : resp.headers) {
            const std::string lower = to_lower_str(k);
            if (lower == "set-cookie" || lower == "set-cookie2") continue;
            out[lower] = v;
        }
        return out;
    }

    static const std::unordered_set<std::string> kCorsSafelistedResponseHeaders = {
        "cache-control",
        "content-language",
        "content-length",
        "content-type",
        "expires",
        "last-modified",
        "pragma"
    };

    std::unordered_set<std::string> exposed;
    bool expose_wildcard = false;
    if (auto it = resp.headers.find("access-control-expose-headers"); it != resp.headers.end()) {
        std::string_view rem(it->second);
        while (!rem.empty()) {
            const auto comma = rem.find(',');
            const std::string token = to_lower_str(trim_fetch_ows(
                comma == std::string_view::npos ? rem : rem.substr(0, comma)
            ));
            if (!token.empty()) {
                if (token == "*" && !include_credentials) expose_wildcard = true;
                else exposed.insert(token);
            }
            if (comma == std::string_view::npos) break;
            rem.remove_prefix(comma + 1);
        }
    }

    for (const auto& [k, v] : resp.headers) {
        const std::string lower = to_lower_str(k);
        if (lower == "set-cookie" || lower == "set-cookie2") continue;
        if (expose_wildcard || kCorsSafelistedResponseHeaders.count(lower) > 0 || exposed.count(lower) > 0) {
            out[lower] = v;
        }
    }
    return out;
}

} // namespace

void HeadersObject::set(std::string name, std::string val) {
    const std::string key = to_lower_str(name);
    if (key == "set-cookie" || key == "set-cookie2") return;
    map[key] = std::move(val);
}

void HeadersObject::append(std::string name, std::string val) {
    const std::string key = to_lower_str(name);
    if (key == "set-cookie" || key == "set-cookie2") return;
    auto it = map.find(key);
    if (it != map.end()) {
        it->second += ", " + val;
    } else {
        map[key] = std::move(val);
    }
}

bool HeadersObject::has(std::string name) const {
    return map.find(to_lower_str(name)) != map.end();
}

std::string HeadersObject::get(std::string name) const {
    auto it = map.find(to_lower_str(name));
    if (it != map.end()) return it->second;
    return "";
}

void HeadersObject::remove(std::string name) {
    map.erase(to_lower_str(name));
}

void HeadersObject::extract_headers_init(
    const JsValue& init,
    std::unordered_map<std::string, std::string>& out
) {
    if (init.type != JsType::Object) return;

    if (init.object_val) {
        if (auto* hex = dynamic_cast<HeadersExotic*>(init.object_val->exotic.get())) {
            if (hex->headers) {
                for (const auto& [k, v] : hex->headers->map) {
                    out[k] = v;
                }
            }
            return;
        }
        if (init.object_val->is_array) {
            HeadersObject acc;
            for (const JsValue& pair : init.object_val->array_elements) {
                if (pair.type == JsType::Object && pair.object_val && pair.object_val->is_array &&
                    pair.object_val->array_elements.size() >= 2) {
                    acc.append(
                        pair.object_val->array_elements[0].to_string(),
                        pair.object_val->array_elements[1].to_string()
                    );
                }
            }
            for (const auto& [k, v] : acc.map) {
                out[k] = v;
            }
            return;
        }
        for (const auto& [k, v] : init.object_val->properties) {
            out[k] = v.to_string();
        }
        return;
    }

    if (init.properties) {
        for (const auto& [k, v] : *init.properties) {
            out[k] = v.to_string();
        }
    }
}

JsValue HeadersObject::create_js_instance(const std::unordered_map<std::string, std::string>& init) {
    auto self = std::make_shared<HeadersObject>();
    for (const auto& [k, v] : init) {
        self->set(k, v);
    }

    JsValue inst = JsValue::make_object();
    if (inst.object_val) {
        inst.object_val->exotic = std::make_shared<HeadersExotic>(self);
    }

    inst.set_property("get", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_null();
        std::string k = args[0].to_string();
        if (!self->has(k)) return JsValue::make_null();
        return JsValue::make_string(self->get(k));
    }));

    inst.set_property("has", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_boolean(false);
        return JsValue::make_boolean(self->has(args[0].to_string()));
    }));

    inst.set_property("set", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            self->set(args[0].to_string(), args[1].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("append", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            self->append(args[0].to_string(), args[1].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("delete", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            self->remove(args[0].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("entries", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        std::vector<JsValue> arr;
        for (const auto& [k, v] : self->map) {
            arr.push_back(JsValue::make_array({JsValue::make_string(k), JsValue::make_string(v)}));
        }
        return JsValue::make_array(arr);
    }));

    inst.set_property("forEach", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.empty() || !args[0].is_callable()) return JsValue::make_undefined();
        JsValue cb = args[0];
        JsValue this_arg = args.size() >= 2 ? args[1] : JsValue::make_undefined();
        std::vector<std::pair<std::string, std::string>> sorted(self->map.begin(), self->map.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        for (const auto& [k, v] : sorted) {
            cb.call_with_this(this_arg, {JsValue::make_string(v), JsValue::make_string(k)});
        }
        return JsValue::make_undefined();
    }));

    return inst;
}

JsValue HeadersObject::create_constructor() {
    return JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::unordered_map<std::string, std::string> init_map;
        if (!args.empty()) {
            HeadersObject::extract_headers_init(args[0], init_map);
        }
        return HeadersObject::create_js_instance(init_map);
    });
}

// -------------------------------------------------------------
// ResponseObject implementation
// -------------------------------------------------------------

JsValue ResponseObject::create_js_instance(
    const std::string& body,
    int status,
    const std::string& status_text,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& url
) {
    auto self = std::make_shared<ResponseObject>();
    self->body = body;
    self->status = status;
    self->status_text = status_text;
    self->ok = (status >= 200 && status < 300);
    self->url = url;
    self->headers = std::make_shared<HeadersObject>();
    for (const auto& [k, v] : headers) {
        self->headers->set(k, v);
    }

    JsValue inst = JsValue::make_object();
    inst.set_property("status", JsValue::make_number(self->status));
    inst.set_property("statusText", JsValue::make_string(self->status_text));
    inst.set_property("ok", JsValue::make_boolean(self->ok));
    inst.set_property("url", JsValue::make_string(self->url));
    inst.set_property("headers", HeadersObject::create_js_instance(self->headers->map));

    inst.set_property("text", JsValue::make_native_function([body](const std::vector<JsValue>&) {
        return Promise::make_resolved(JsValue::make_string(body));
    }));

    inst.set_property("arrayBuffer", JsValue::make_native_function([body](const std::vector<JsValue>&) {
        const JsValue view = typed_array_from_bytes(body);
        return Promise::make_resolved(view.get_property("buffer"));
    }));

    inst.set_property("json", JsValue::make_native_function([body](const std::vector<JsValue>&) {
        try {
            JsValue parsed = Json::parse(body);
            return Promise::make_resolved(parsed);
        } catch (const std::exception& ex) {
            return Promise::make_rejected(JsValue::make_string(ex.what()));
        }
    }));

    inst.set_property("clone", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        return ResponseObject::create_js_instance(
            self->body, self->status, self->status_text, self->headers->map, self->url
        );
    }));

    return inst;
}

JsValue ResponseObject::create_constructor() {
    return JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::string body = !args.empty() ? args[0].to_string() : "";
        int status = 200;
        std::string status_text = "OK";
        std::unordered_map<std::string, std::string> headers;

        if (args.size() > 1 && args[1].type == JsType::Object) {
            JsValue init = args[1];
            JsValue st = init.get_property("status");
            if (st.type == JsType::Number) status = static_cast<int>(st.number_val);

            JsValue st_txt = init.get_property("statusText");
            if (st_txt.type == JsType::String) status_text = st_txt.string_val.str();

            JsValue h = init.get_property("headers");
            HeadersObject::extract_headers_init(h, headers);
        }

        return ResponseObject::create_js_instance(body, status, status_text, headers, "");
    });
}

// -------------------------------------------------------------
// FetchAPI implementation
// -------------------------------------------------------------

JsValue FetchAPI::fetch(const std::vector<JsValue>& args,
                        const ResourceRequestContext& context) {
    if (args.empty()) {
        return Promise::make_rejected(JsValue::make_string("TypeError: fetch requires at least 1 argument"));
    }

    std::string url;
    if (args[0].type == JsType::Object) {
        JsValue href = args[0].get_property("href");
        url = href.is_truthy() ? href.to_string() : args[0].to_string();
    } else {
        url = args[0].to_string();
    }

    std::string method = "GET";
    std::unordered_map<std::string, std::string> req_headers;
    std::string req_body;
    JsValue signal_val = JsValue::make_undefined();
    ResourceRequestContext req_context = context;

    if (args.size() > 1 && args[1].type == JsType::Object) {
        JsValue opts = args[1];
        JsValue m = opts.get_property("method");
        if (m.type != JsType::Undefined && m.type != JsType::Null && m.is_truthy()) {
            method = m.to_string();
        }

        JsValue b = opts.get_property("body");
        if (b.type != JsType::Undefined && b.type != JsType::Null) {
            req_body = b.to_string();
        }

        JsValue mode_val = opts.get_property("mode");
        if (mode_val.type == JsType::String) {
            const std::string mode_str = mode_val.to_string();
            if (mode_str == "same-origin") req_context.mode = RequestMode::SameOrigin;
            else if (mode_str == "no-cors") req_context.mode = RequestMode::NoCors;
            else if (mode_str == "cors") req_context.mode = RequestMode::Cors;
        }

        JsValue cred_val = opts.get_property("credentials");
        if (cred_val.type == JsType::String) {
            const std::string cred_str = cred_val.to_string();
            if (cred_str == "include") {
                req_context.include_credentials = true;
                req_context.omit_credentials = false;
            } else if (cred_str == "omit") {
                req_context.include_credentials = false;
                req_context.omit_credentials = true;
            } else if (cred_str == "same-origin") {
                req_context.include_credentials = false;
                req_context.omit_credentials = false;
            }
        }

        JsValue cache_val = opts.get_property("cache");
        if (cache_val.type == JsType::String) {
            const std::string cache_str = cache_val.to_string();
            if (cache_str == "default") req_context.cache_mode = CacheMode::Default;
            else if (cache_str == "no-store") req_context.cache_mode = CacheMode::NoStore;
            else if (cache_str == "reload") req_context.cache_mode = CacheMode::Reload;
            else if (cache_str == "no-cache") req_context.cache_mode = CacheMode::NoCache;
            else if (cache_str == "force-cache") req_context.cache_mode = CacheMode::ForceCache;
        }

        signal_val = opts.get_property("signal");

        JsValue h = opts.get_property("headers");
        HeadersObject::extract_headers_init(h, req_headers);
    }

    if (is_forbidden_fetch_method(method)) {
        return Promise::make_rejected(JsValue::make_string("TypeError: Forbidden HTTP method: " + method));
    }
    method = normalize_fetch_method(std::move(method));

    if (signal_val.is_object() && signal_val.get_property("aborted").is_truthy()) {
        JsValue reason = signal_val.get_property("reason");
        return Promise::make_rejected(reason.is_truthy() ? reason : JsValue::make_string("AbortError: The operation was aborted"));
    }

    auto pdata = std::make_shared<PromiseData>();
    JsValue pval = Promise::create_promise_instance(pdata);

    if (signal_val.is_object()) {
        JsValue add_el = signal_val.get_property("addEventListener");
        if (add_el.is_callable()) {
            add_el.call_with_this(signal_val, {
                JsValue::make_string("abort"),
                JsValue::make_native_function([pdata, signal_val](const std::vector<JsValue>&) {
                    JsValue reason = signal_val.get_property("reason");
                    Promise::reject(pdata, reason.is_truthy() ? reason : JsValue::make_string("AbortError: The operation was aborted"));
                    return JsValue::make_undefined();
                })
            });
        }
    }

    EventLoop::default_loop().post_task([url, method, req_headers, req_body, pdata, signal_val, req_context]() {
        if (signal_val.is_object() && signal_val.get_property("aborted").is_truthy()) {
            JsValue reason = signal_val.get_property("reason");
            Promise::reject(pdata, reason.is_truthy() ? reason : JsValue::make_string("AbortError: The operation was aborted"));
            return;
        }

        ResourceResponse resp = ResourceLoader::load_for_page(url, req_context, method, req_headers, req_body);
        if (signal_val.is_object() && signal_val.get_property("aborted").is_truthy()) {
            JsValue reason = signal_val.get_property("reason");
            Promise::reject(pdata, reason.is_truthy() ? reason : JsValue::make_string("AbortError: The operation was aborted"));
            return;
        }

        if (resp.network_ok) {
            std::string st = !resp.status_text.empty()
                                 ? resp.status_text
                                 : ((resp.status_code == 200) ? "OK" : ("Status " + std::to_string(resp.status_code)));
            const auto visible_headers = filter_response_headers_for_script(
                resp, req_context.mode, req_context.include_credentials
            );
            JsValue res_obj = ResponseObject::create_js_instance(
                resp.body, resp.status_code, st, visible_headers,
                resp.url.empty() ? url : resp.url
            );
            Promise::resolve(pdata, res_obj);
        } else {
            std::string err = resp.error_message.empty() ? "Network request failed" : resp.error_message;
            Promise::reject(pdata, JsValue::make_string(err));
        }
    });

    return pval;
}

} // namespace litebird
