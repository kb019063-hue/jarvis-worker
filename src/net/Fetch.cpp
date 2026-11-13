#include "litebird/net/Fetch.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/EventLoop.hpp"
#include <algorithm>
#include <cctype>

namespace litebird {

static std::string to_lower_str(std::string_view s) {
    std::string res(s);
    std::transform(res.begin(), res.end(), res.begin(), ::tolower);
    return res;
}

void HeadersObject::set(std::string name, std::string val) {
    map[to_lower_str(name)] = std::move(val);
}

void HeadersObject::append(std::string name, std::string val) {
    std::string key = to_lower_str(name);
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

JsValue HeadersObject::create_js_instance(const std::unordered_map<std::string, std::string>& init) {
    auto self = std::make_shared<HeadersObject>();
    for (const auto& [k, v] : init) {
        self->set(k, v);
    }

    JsValue inst = JsValue::make_object();

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

    return inst;
}

JsValue HeadersObject::create_constructor() {
    return JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::unordered_map<std::string, std::string> init_map;
        if (!args.empty()) {
            JsValue arg0 = args[0];
            if (arg0.type == JsType::Object) {
                if (arg0.object_val) {
                    for (const auto& [k, v] : arg0.object_val->properties) {
                        init_map[k] = v.to_string();
                    }
                } else if (arg0.properties) {
                    for (const auto& [k, v] : *arg0.properties) {
                        init_map[k] = v.to_string();
                    }
                }
            }
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
            if (h.type == JsType::Object) {
                if (h.object_val) {
                    for (const auto& [k, v] : h.object_val->properties) headers[k] = v.to_string();
                } else if (h.properties) {
                    for (const auto& [k, v] : *h.properties) headers[k] = v.to_string();
                }
            }
        }

        return ResponseObject::create_js_instance(body, status, status_text, headers, "");
    });
}

// -------------------------------------------------------------
// FetchAPI implementation
// -------------------------------------------------------------

JsValue FetchAPI::fetch(const std::vector<JsValue>& args) {
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

    if (args.size() > 1 && args[1].type == JsType::Object) {
        JsValue opts = args[1];
        JsValue m = opts.get_property("method");
        if (m.is_truthy()) method = m.to_string();

        JsValue b = opts.get_property("body");
        if (b.is_truthy()) req_body = b.to_string();

        signal_val = opts.get_property("signal");

        JsValue h = opts.get_property("headers");
        if (h.type == JsType::Object) {
            if (h.object_val) {
                for (const auto& [k, v] : h.object_val->properties) req_headers[k] = v.to_string();
            } else if (h.properties) {
                for (const auto& [k, v] : *h.properties) req_headers[k] = v.to_string();
            }
        }
    }

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

    EventLoop::default_loop().post_task([url, method, req_headers, req_body, pdata, signal_val]() {
        if (signal_val.is_object() && signal_val.get_property("aborted").is_truthy()) {
            JsValue reason = signal_val.get_property("reason");
            Promise::reject(pdata, reason.is_truthy() ? reason : JsValue::make_string("AbortError: The operation was aborted"));
            return;
        }

        ResourceResponse resp = ResourceLoader::load(url, method, req_headers, req_body);
        if (signal_val.is_object() && signal_val.get_property("aborted").is_truthy()) {
            JsValue reason = signal_val.get_property("reason");
            Promise::reject(pdata, reason.is_truthy() ? reason : JsValue::make_string("AbortError: The operation was aborted"));
            return;
        }

        if (resp.success) {
            std::string st = (resp.status_code == 200) ? "OK" : ("Status " + std::to_string(resp.status_code));
            JsValue res_obj = ResponseObject::create_js_instance(
                resp.body, resp.status_code, st, resp.headers, url
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
