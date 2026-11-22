#include "litebird/net/Fetch.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/TypedArray.hpp"
#include "litebird/net/AbortController.hpp"
#include "litebird/net/UrlParser.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string_view>
#include <unordered_map>
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

std::string normalize_header_value(std::string_view sv) {
    while (!sv.empty() &&
           (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() &&
           (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

bool is_http_token_char(unsigned char c) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

bool is_valid_http_token(std::string_view s) {
    if (s.empty()) return false;
    for (char ch : s) {
        if (!is_http_token_char(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

bool is_valid_header_value(std::string_view s) {
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == 0x00 || c == '\r' || c == '\n') return false;
    }
    return true;
}

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

bool is_no_cors_safelisted_method(std::string_view method) {
    return method == "GET" || method == "HEAD" || method == "POST";
}

bool contains_forbidden_method_token(std::string_view value) {
    std::string_view rem = value;
    while (!rem.empty()) {
        const size_t comma = rem.find(',');
        const std::string tok = trim_fetch_ows(comma == std::string_view::npos ? rem : rem.substr(0, comma));
        if (is_forbidden_fetch_method(tok)) return true;
        if (comma == std::string_view::npos) break;
        rem.remove_prefix(comma + 1);
    }
    return false;
}

// Per-realm prototype registry (non-owning pointers rooted by each realm's
// constructor.prototype in the global environment).
struct FetchRealmProtos {
    JsObject* headers_proto{nullptr};
    JsObject* response_proto{nullptr};
    JsObject* request_proto{nullptr};
};

std::mutex& fetch_protos_mutex() {
    static std::mutex mu;
    return mu;
}

std::unordered_map<const JsIntrinsics*, FetchRealmProtos>& fetch_protos_map() {
    static std::unordered_map<const JsIntrinsics*, FetchRealmProtos> map;
    return map;
}

FetchRealmProtos current_fetch_protos() {
    const JsIntrinsics* intr = &JsIntrinsics::get();
    std::lock_guard<std::mutex> lock(fetch_protos_mutex());
    auto it = fetch_protos_map().find(intr);
    return it != fetch_protos_map().end() ? it->second : FetchRealmProtos{};
}

void register_fetch_proto(JsObject* FetchRealmProtos::* member, JsObject* proto) {
    const JsIntrinsics* intr = &JsIntrinsics::get();
    std::lock_guard<std::mutex> lock(fetch_protos_mutex());
    fetch_protos_map()[intr].*member = proto;
}

void set_to_string_tag(JsValue& proto, const char* tag) {
    if (!proto.object_val) return;
    auto& slot = proto.object_val->properties.entry("@@toStringTag");
    slot.second = JsValue::make_string(tag);
    slot.flags = PropertyMap::Configurable;
}

void define_proto_getter(JsObject* proto, const char* name, JsValue::NativeFn getter) {
    const std::string gname = std::string("get ") + name;
    JsValue fn = js_make_native(gname.c_str(), 0, std::move(getter));
    if (JsObject* cell = fn.function_cell()) cell->not_constructor = true;
    JsValue::make_object(proto).define_accessor(
        name, std::move(fn), JsValue::make_undefined(),
        PropertyMap::Configurable | PropertyMap::Enumerable
    );
}

JsValue make_type_error_value(const std::string& msg) {
    return js_make_error(JsErrorKind::TypeError, msg);
}

JsValue make_syntax_error_value(const std::string& msg) {
    return js_make_error(JsErrorKind::SyntaxError, msg);
}

class HeadersExotic final : public JsExotic {
public:
    explicit HeadersExotic(std::shared_ptr<HeadersObject> h) : headers(std::move(h)) {}
    std::shared_ptr<HeadersObject> headers;
};

class RequestExotic final : public JsExotic {
public:
    explicit RequestExotic(std::shared_ptr<RequestObject> r) : request(std::move(r)) {}
    void trace(Heap& heap) override {
        if (!request) return;
        heap.mark_value(request->headers_js);
        heap.mark_value(request->signal_js);
    }
    std::shared_ptr<RequestObject> request;
};

bool is_abort_signal_js(const JsValue& val) {
    if (!val.is_object()) return false;
    bool aborted = false;
    if (dom::on_abort(val, nullptr, aborted)) return true;
    return val.has_own_property("__get_aborted__") &&
           val.has_own_property("throwIfAborted") &&
           val.get_property("addEventListener").is_callable();
}

void link_legacy_abort_signal(const std::shared_ptr<AbortSignal>& dst, const JsValue& src_signal_js) {
    if (!dst || !src_signal_js.is_object()) return;
    if (src_signal_js.get_property("aborted").is_truthy()) {
        dst->signal_abort(src_signal_js.get_property("reason"));
        return;
    }
    JsValue add_el = src_signal_js.get_property("addEventListener");
    if (!add_el.is_callable()) return;
    std::weak_ptr<AbortSignal> weak_dst = dst;
    JsValue cb = JsValue::make_native_function([weak_dst](const std::vector<JsValue>&) {
        auto locked = weak_dst.lock();
        if (!locked) return JsValue::make_undefined();
        const auto& caps = JsValue::current_captured();
        JsValue reason = (!caps.empty() && caps[0].is_object())
            ? caps[0].get_property("reason")
            : JsValue::make_undefined();
        locked->signal_abort(reason);
        return JsValue::make_undefined();
    });
    cb.set_payload_captured({src_signal_js});
    add_el.call_with_this(src_signal_js, {JsValue::make_string("abort"), cb});
}

void create_dependent_signal(RequestObject& req, const JsValue& source_signal) {
    // Prefer the DOM realm's AbortSignal.any() so `req.signal instanceof AbortSignal`
    // is true and abort propagation follows the DOM standard.
    bool src_is_dom = false;
    if (source_signal.is_object()) {
        bool dummy = false;
        src_is_dom = dom::on_abort(source_signal, nullptr, dummy);
    }

    JsValue abort_sig_ctor = dom::current_realm().ctor("AbortSignal");
    JsValue any_fn = abort_sig_ctor.is_object_like()
        ? abort_sig_ctor.get_property("any")
        : JsValue::make_undefined();

    if (any_fn.is_callable() && (!source_signal.is_object() || src_is_dom)) {
        std::vector<JsValue> sources;
        if (src_is_dom) sources.push_back(source_signal);
        req.signal_js = any_fn.call_with_this(abort_sig_ctor, {JsValue::make_array(sources)});
        req.signal = nullptr;
        return;
    }

    // Fallback for document-less interpreters using legacy AbortSignal.
    req.signal = std::make_shared<AbortSignal>();
    if (source_signal.is_object()) {
        link_legacy_abort_signal(req.signal, source_signal);
    }
    req.signal_js = req.signal->to_js_value();
}

bool parse_url_with_optional_base(std::string_view raw, std::string_view base_str, Url& out) {
    if (!base_str.empty() && base_str != "about:blank") {
        Url base_url;
        if (Url::parse(base_str, nullptr, base_url)) {
            return Url::parse(raw, &base_url, out);
        }
    }
    return Url::parse(raw, nullptr, out);
}

bool is_valid_referrer_policy(std::string_view p) {
    return p.empty() ||
           p == "no-referrer" ||
           p == "no-referrer-when-downgrade" ||
           p == "same-origin" ||
           p == "origin" ||
           p == "strict-origin" ||
           p == "origin-when-cross-origin" ||
           p == "strict-origin-when-cross-origin" ||
           p == "unsafe-url";
}

bool is_url_search_params_js(const JsValue& val) {
    if (!val.is_object()) return false;
    JsValue tag = val.get_property("@@toStringTag");
    return tag.type == JsType::String && tag.to_string() == "URLSearchParams";
}

void extract_body_init(const JsValue& body_val, std::string& bytes_out, std::string& mime_out) {
    bytes_out.clear();
    mime_out.clear();

    if (is_buffer_source(body_val)) {
        if (!typed_array_bytes(body_val, bytes_out)) {
            js_throw_type_error("Failed to read Request body: ArrayBufferView or ArrayBuffer is detached or out of bounds.");
        }
        return;
    }

    if (dom::blob_bytes(body_val, bytes_out)) {
        JsValue type_val = body_val.get_property("type");
        if (type_val.type == JsType::String) {
            mime_out = type_val.to_string();
        }
        return;
    }

    if (is_url_search_params_js(body_val)) {
        bytes_out = js_to_string(body_val);
        mime_out = "application/x-www-form-urlencoded;charset=UTF-8";
        return;
    }

    bytes_out = js_to_string(body_val);
    mime_out = "text/plain;charset=UTF-8";
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

std::shared_ptr<RequestObject> require_request_this(const char* what) {
    auto req = RequestObject::from_js_value(JsValue::current_this());
    if (!req) {
        js_throw_type_error(std::string("Illegal invocation: ") + what + " requires a Request receiver.");
    }
    return req;
}

bool consume_request_body_for_promise(const std::shared_ptr<RequestObject>& req, JsValue& rejection_out) {
    if (!req) {
        rejection_out = make_type_error_value("Illegal invocation: receiver is not a Request.");
        return false;
    }
    if (req->has_body) {
        if (req->body_used) {
            rejection_out = make_type_error_value("Body is unusable: Body has already been read.");
            return false;
        }
        req->body_used = true;
    }
    return true;
}

} // namespace

// -------------------------------------------------------------
// HeadersObject implementation
// -------------------------------------------------------------

bool HeadersObject::is_valid_header_name(std::string_view name) {
    return is_valid_http_token(name);
}

bool HeadersObject::is_forbidden_request_header(std::string_view name, std::string_view value) {
    const std::string lower = to_lower_str(name);
    if (lower.rfind("proxy-", 0) == 0 || lower.rfind("sec-", 0) == 0) return true;
    static const std::unordered_set<std::string> kForbidden = {
        "accept-charset",
        "accept-encoding",
        "access-control-request-headers",
        "access-control-request-method",
        "connection",
        "content-length",
        "cookie",
        "cookie2",
        "date",
        "dnt",
        "expect",
        "host",
        "keep-alive",
        "origin",
        "referer",
        "set-cookie",
        "set-cookie2",
        "te",
        "trailer",
        "transfer-encoding",
        "upgrade",
        "via"
    };
    if (kForbidden.count(lower) > 0) return true;
    if ((lower == "x-http-method" || lower == "x-http-method-override" || lower == "x-method-override") &&
        contains_forbidden_method_token(value)) {
        return true;
    }
    return false;
}

bool HeadersObject::is_no_cors_safelisted_request_header(std::string_view name, std::string_view value) {
    const std::string lower = to_lower_str(name);
    if (value.size() > 128) return false;
    for (char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c < 0x20 && c != '\t') || c == 0x7F) return false;
    }
    if (lower == "accept" || lower == "accept-language" || lower == "content-language") {
        return true;
    }
    if (lower == "content-type") {
        const size_t semi = value.find(';');
        const std::string essence = to_lower_str(trim_fetch_ows(
            semi == std::string_view::npos ? value : value.substr(0, semi)
        ));
        return essence == "application/x-www-form-urlencoded" ||
               essence == "multipart/form-data" ||
               essence == "text/plain";
    }
    return false;
}

void HeadersObject::set(std::string name, std::string val) {
    const std::string key = to_lower_str(name);
    std::string norm = normalize_header_value(val);
    if (key == "set-cookie" || key == "set-cookie2") return;
    if (guard == HeadersGuard::Request && is_forbidden_request_header(key, norm)) return;
    if (guard == HeadersGuard::RequestNoCors && !is_no_cors_safelisted_request_header(key, norm)) return;
    map[key] = std::move(norm);
}

void HeadersObject::append(std::string name, std::string val) {
    const std::string key = to_lower_str(name);
    std::string norm = normalize_header_value(val);
    if (key == "set-cookie" || key == "set-cookie2") return;
    if (guard == HeadersGuard::Request && is_forbidden_request_header(key, norm)) return;
    if (guard == HeadersGuard::RequestNoCors) {
        auto it = map.find(key);
        const std::string combined = (it != map.end()) ? (it->second + ", " + norm) : norm;
        if (!is_no_cors_safelisted_request_header(key, combined)) return;
        map[key] = combined;
        return;
    }
    auto it = map.find(key);
    if (it != map.end()) {
        it->second += ", " + norm;
    } else {
        map[key] = std::move(norm);
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
    const std::string key = to_lower_str(name);
    if (guard == HeadersGuard::Request && is_forbidden_request_header(key)) return;
    if (guard == HeadersGuard::RequestNoCors &&
        key != "accept" && key != "accept-language" && key != "content-language" && key != "content-type") {
        return;
    }
    map.erase(key);
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

void HeadersObject::fill_headers_init(const JsValue& init, HeadersObject& target) {
    if (init.type != JsType::Object) {
        js_throw_type_error("Failed to parse headers: HeadersInit must be an object or sequence.");
    }

    auto append_checked = [&](const std::string& name, const std::string& val) {
        if (!is_valid_header_name(name)) {
            js_throw_type_error("Invalid header name: '" + name + "'.");
        }
        if (!is_valid_header_value(val)) {
            js_throw_type_error("Invalid header value for '" + name + "'.");
        }
        target.append(name, val);
    };

    if (init.object_val) {
        if (auto* hex = dynamic_cast<HeadersExotic*>(init.object_val->exotic.get())) {
            if (hex->headers) {
                for (const auto& [k, v] : hex->headers->map) {
                    append_checked(k, v);
                }
            }
            return;
        }
        if (init.object_val->is_array) {
            for (const JsValue& pair : init.object_val->array_elements) {
                if (pair.type != JsType::Object || !pair.object_val || !pair.object_val->is_array ||
                    pair.object_val->array_elements.size() != 2) {
                    js_throw_type_error("Failed to parse headers: each header pair must be a sequence of length 2.");
                }
                append_checked(
                    js_to_string(pair.object_val->array_elements[0]),
                    js_to_string(pair.object_val->array_elements[1])
                );
            }
            return;
        }
        for (const std::string& key : init.own_keys()) {
            append_checked(key, js_to_string(init.get_property(key)));
        }
        return;
    }

    if (init.properties) {
        for (const auto& [k, v] : *init.properties) {
            append_checked(k, js_to_string(v));
        }
    }
}

JsValue HeadersObject::create_js_instance(std::shared_ptr<HeadersObject> self) {
    if (!self) self = std::make_shared<HeadersObject>();

    JsValue inst = JsValue::make_object();
    if (inst.object_val) {
        inst.object_val->exotic = std::make_shared<HeadersExotic>(self);
        if (JsObject* proto = current_fetch_protos().headers_proto) {
            inst.object_val->prototype = proto;
        }
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
            const std::string name = args[0].to_string();
            const std::string val = args[1].to_string();
            if (!HeadersObject::is_valid_header_name(name)) {
                js_throw_type_error("Failed to execute 'set' on 'Headers': Invalid header name.");
            }
            if (!is_valid_header_value(val)) {
                js_throw_type_error("Failed to execute 'set' on 'Headers': Invalid header value.");
            }
            self->set(name, val);
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("append", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            const std::string name = args[0].to_string();
            const std::string val = args[1].to_string();
            if (!HeadersObject::is_valid_header_name(name)) {
                js_throw_type_error("Failed to execute 'append' on 'Headers': Invalid header name.");
            }
            if (!is_valid_header_value(val)) {
                js_throw_type_error("Failed to execute 'append' on 'Headers': Invalid header value.");
            }
            self->append(name, val);
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("delete", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            const std::string name = args[0].to_string();
            if (!HeadersObject::is_valid_header_name(name)) {
                js_throw_type_error("Failed to execute 'delete' on 'Headers': Invalid header name.");
            }
            self->remove(name);
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("entries", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        std::vector<std::pair<std::string, std::string>> sorted(self->map.begin(), self->map.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        std::vector<JsValue> arr;
        arr.reserve(sorted.size());
        for (const auto& [k, v] : sorted) {
            arr.push_back(JsValue::make_array({JsValue::make_string(k), JsValue::make_string(v)}));
        }
        return JsValue::make_array(arr);
    }));

    inst.set_property("keys", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        std::vector<std::string> sorted;
        sorted.reserve(self->map.size());
        for (const auto& [k, _] : self->map) sorted.push_back(k);
        std::sort(sorted.begin(), sorted.end());
        std::vector<JsValue> arr;
        arr.reserve(sorted.size());
        for (const auto& k : sorted) arr.push_back(JsValue::make_string(k));
        return JsValue::make_array(arr);
    }));

    inst.set_property("values", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        std::vector<std::pair<std::string, std::string>> sorted(self->map.begin(), self->map.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        std::vector<JsValue> arr;
        arr.reserve(sorted.size());
        for (const auto& [_, v] : sorted) arr.push_back(JsValue::make_string(v));
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

JsValue HeadersObject::create_js_instance(
    const std::unordered_map<std::string, std::string>& init,
    HeadersGuard guard
) {
    auto self = std::make_shared<HeadersObject>();
    self->guard = guard;
    for (const auto& [k, v] : init) {
        self->set(k, v);
    }
    return create_js_instance(std::move(self));
}

JsValue HeadersObject::create_constructor() {
    JsValue proto = JsValue::make_object();
    set_to_string_tag(proto, "Headers");

    JsValue ctor = js_make_native("Headers", 0, [](const std::vector<JsValue>& args) {
        std::unordered_map<std::string, std::string> init_map;
        if (!args.empty() && !args[0].is_undefined()) {
            HeadersObject::extract_headers_init(args[0], init_map);
        }
        return HeadersObject::create_js_instance(init_map);
    });
    ctor.set_property("prototype", proto);
    ctor.set_payload_captured({proto});
    if (proto.object_val) {
        js_define_hidden(proto.object_val, "constructor", ctor);
        register_fetch_proto(&FetchRealmProtos::headers_proto, proto.object_val);
    }
    return ctor;
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
    if (inst.object_val) {
        if (JsObject* proto = current_fetch_protos().response_proto) {
            inst.object_val->prototype = proto;
        }
    }
    inst.set_property("status", JsValue::make_number(self->status));
    inst.set_property("statusText", JsValue::make_string(self->status_text));
    inst.set_property("ok", JsValue::make_boolean(self->ok));
    inst.set_property("url", JsValue::make_string(self->url));
    inst.set_property("headers", HeadersObject::create_js_instance(self->headers));

    inst.set_property("text", JsValue::make_native_function([body](const std::vector<JsValue>&) {
        return Promise::make_resolved(JsValue::make_string(body));
    }));

    inst.set_property("arrayBuffer", JsValue::make_native_function([body](const std::vector<JsValue>&) {
        const JsValue view = typed_array_from_bytes(body);
        return Promise::make_resolved(view.get_property("buffer"));
    }));

    inst.set_property("bytes", JsValue::make_native_function([body](const std::vector<JsValue>&) {
        return Promise::make_resolved(typed_array_from_bytes(body));
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
    JsValue proto = JsValue::make_object();
    set_to_string_tag(proto, "Response");

    JsValue ctor = js_make_native("Response", 0, [](const std::vector<JsValue>& args) {
        std::string body = (!args.empty() && !args[0].is_nullish())
            ? args[0].to_string()
            : "";
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
    ctor.set_property("prototype", proto);
    ctor.set_payload_captured({proto});
    if (proto.object_val) {
        js_define_hidden(proto.object_val, "constructor", ctor);
        register_fetch_proto(&FetchRealmProtos::response_proto, proto.object_val);
    }
    return ctor;
}

// -------------------------------------------------------------
// RequestObject implementation (WHATWG Fetch Standard §5.4)
// -------------------------------------------------------------

std::shared_ptr<RequestObject> RequestObject::from_js_value(const JsValue& val) {
    if (val.type != JsType::Object || !val.object_val || !val.object_val->exotic) {
        return nullptr;
    }
    if (auto* rex = dynamic_cast<RequestExotic*>(val.object_val->exotic.get())) {
        return rex->request;
    }
    return nullptr;
}

std::shared_ptr<RequestObject> RequestObject::construct(
    const std::vector<JsValue>& args,
    const ResourceRequestContext& context
) {
    if (args.empty()) {
        js_throw_type_error("Failed to construct 'Request': 1 argument required, but only 0 present.");
    }

    const bool has_init_arg = args.size() >= 2 && !args[1].is_nullish();
    if (has_init_arg && !args[1].is_object()) {
        js_throw_type_error("Failed to construct 'Request': parameter 2 is not of type 'RequestInit'.");
    }

    auto req = std::make_shared<RequestObject>();
    req->headers = std::make_shared<HeadersObject>();

    std::shared_ptr<RequestObject> input_req = from_js_value(args[0]);
    JsValue source_signal = JsValue::make_undefined();
    std::unordered_map<std::string, std::string> inherited_headers;

    if (input_req) {
        req->url = input_req->url;
        req->method = input_req->method;
        req->mode = input_req->mode;
        req->credentials = input_req->credentials;
        req->cache = input_req->cache;
        req->redirect = input_req->redirect;
        req->referrer = input_req->referrer;
        req->referrer_policy = input_req->referrer_policy;
        req->integrity = input_req->integrity;
        req->keepalive = input_req->keepalive;
        req->destination = input_req->destination;
        if (input_req->headers) {
            inherited_headers = input_req->headers->map;
        }
        source_signal = input_req->signal_js;
    } else {
        std::string raw_url;
        if (args[0].is_object()) {
            JsValue href = args[0].get_property("href");
            if (href.type == JsType::String) {
                raw_url = href.to_string();
            } else {
                raw_url = js_to_string(args[0]);
            }
        } else {
            raw_url = js_to_string(args[0]);
        }

        Url parsed;
        if (!parse_url_with_optional_base(raw_url, context.base_url, parsed)) {
            js_throw_type_error("Failed to construct 'Request': Failed to parse URL from " + raw_url);
        }
        if (!parsed.username.empty() || !parsed.password.empty()) {
            js_throw_type_error("Failed to construct 'Request': Request cannot be constructed from a URL that includes credentials.");
        }
        req->url = parsed.href();
    }

    JsValue init = has_init_arg ? args[1] : JsValue::make_undefined();
    bool has_init_headers = false;
    JsValue init_headers_val = JsValue::make_undefined();
    bool has_init_body = false;
    JsValue init_body_val = JsValue::make_undefined();

    if (has_init_arg) {
        // WHATWG Fetch §5.4 step 16: window must be null if present.
        JsValue win_val = init.get_property("window");
        if (!win_val.is_nullish()) {
            js_throw_type_error("Failed to construct 'Request': 'window' option must be null.");
        }

        // §5.4 step 15: non-empty init on a navigate request switches mode to same-origin.
        if (input_req && req->mode == "navigate" && !init.own_keys().empty()) {
            req->mode = "same-origin";
        }

        // §5.4 step 20: referrer
        JsValue ref_val = init.get_property("referrer");
        if (!ref_val.is_undefined()) {
            const std::string ref_str = js_to_string(ref_val);
            if (ref_str.empty()) {
                req->referrer.clear();
            } else if (ref_str == "about:client") {
                req->referrer = "about:client";
            } else {
                Url parsed_ref;
                if (!parse_url_with_optional_base(ref_str, context.base_url, parsed_ref)) {
                    js_throw_type_error("Failed to construct 'Request': Invalid referrer URL '" + ref_str + "'.");
                }
                req->referrer = parsed_ref.href();
            }
        }

        // §5.4 step 21: referrerPolicy
        JsValue rp_val = init.get_property("referrerPolicy");
        if (!rp_val.is_undefined()) {
            const std::string rp_str = js_to_string(rp_val);
            if (!is_valid_referrer_policy(rp_str)) {
                js_throw_type_error("Failed to construct 'Request': Invalid referrerPolicy '" + rp_str + "'.");
            }
            req->referrer_policy = rp_str;
        }

        // §5.4 step 22: mode
        JsValue mode_val = init.get_property("mode");
        if (!mode_val.is_undefined()) {
            const std::string mode_str = js_to_string(mode_val);
            if (mode_str == "navigate") {
                js_throw_type_error("Failed to construct 'Request': Cannot construct a Request with mode 'navigate'.");
            }
            if (mode_str != "same-origin" && mode_str != "no-cors" && mode_str != "cors") {
                js_throw_type_error("Failed to construct 'Request': Invalid request mode '" + mode_str + "'.");
            }
            req->mode = mode_str;
        }

        // §5.4 step 23: credentials
        JsValue cred_val = init.get_property("credentials");
        if (!cred_val.is_undefined()) {
            const std::string cred_str = js_to_string(cred_val);
            if (cred_str != "omit" && cred_str != "same-origin" && cred_str != "include") {
                js_throw_type_error("Failed to construct 'Request': Invalid credentials mode '" + cred_str + "'.");
            }
            req->credentials = cred_str;
        }

        // §5.4 step 24: cache
        JsValue cache_val = init.get_property("cache");
        if (!cache_val.is_undefined()) {
            const std::string cache_str = js_to_string(cache_val);
            if (cache_str != "default" && cache_str != "no-store" && cache_str != "reload" &&
                cache_str != "no-cache" && cache_str != "force-cache" && cache_str != "only-if-cached") {
                js_throw_type_error("Failed to construct 'Request': Invalid cache mode '" + cache_str + "'.");
            }
            req->cache = cache_str;
        }

        // §5.4 step 25: only-if-cached requires same-origin mode
        if (req->cache == "only-if-cached" && req->mode != "same-origin") {
            js_throw_type_error("Failed to construct 'Request': 'only-if-cached' can be set only with 'same-origin' mode.");
        }

        // §5.4 step 26: redirect
        JsValue redir_val = init.get_property("redirect");
        if (!redir_val.is_undefined()) {
            const std::string redir_str = js_to_string(redir_val);
            if (redir_str != "follow" && redir_str != "error" && redir_str != "manual") {
                js_throw_type_error("Failed to construct 'Request': Invalid redirect mode '" + redir_str + "'.");
            }
            req->redirect = redir_str;
        }

        // §5.4 step 27: integrity
        JsValue int_val = init.get_property("integrity");
        if (!int_val.is_undefined()) {
            req->integrity = js_to_string(int_val);
        }

        // §5.4 step 28: keepalive
        JsValue ka_val = init.get_property("keepalive");
        if (!ka_val.is_undefined()) {
            req->keepalive = ka_val.is_truthy();
        }

        // §5.4 step 29: method
        JsValue method_val = init.get_property("method");
        if (!method_val.is_undefined()) {
            std::string m = js_to_string(method_val);
            if (!is_valid_http_token(m)) {
                js_throw_type_error("Failed to construct 'Request': '" + m + "' is not a valid HTTP method.");
            }
            if (is_forbidden_fetch_method(m)) {
                js_throw_type_error("Failed to construct 'Request': '" + m + "' HTTP method is unsupported.");
            }
            req->method = normalize_fetch_method(std::move(m));
        }

        // §5.4 step 30: signal
        JsValue sig_val = init.get_property("signal");
        if (!sig_val.is_undefined()) {
            if (sig_val.type == JsType::Null) {
                source_signal = JsValue::make_undefined();
            } else if (is_abort_signal_js(sig_val)) {
                source_signal = sig_val;
            } else {
                js_throw_type_error("Failed to construct 'Request': member signal is not of type AbortSignal.");
            }
        }

        init_headers_val = init.get_property("headers");
        has_init_headers = !init_headers_val.is_undefined();

        init_body_val = init.get_property("body");
        has_init_body = !init_body_val.is_nullish();
    }

    // §5.4 step 25 also applies when inheriting only-if-cached with non-same-origin
    if (req->cache == "only-if-cached" && req->mode != "same-origin") {
        js_throw_type_error("Failed to construct 'Request': 'only-if-cached' can be set only with 'same-origin' mode.");
    }

    // §5.4 step 34: no-cors restricts methods to GET/HEAD/POST and uses request-no-cors guard.
    if (req->mode == "no-cors") {
        if (!is_no_cors_safelisted_method(req->method)) {
            js_throw_type_error("Failed to construct 'Request': '" + req->method + "' is unsupported in no-cors mode.");
        }
        req->headers->guard = HeadersGuard::RequestNoCors;
    } else {
        req->headers->guard = HeadersGuard::Request;
    }

    // §5.4 step 35-36: fill headers from init.headers (replacing inherited) or filter inherited.
    if (has_init_headers) {
        HeadersObject::fill_headers_init(init_headers_val, *req->headers);
    } else {
        for (const auto& [k, v] : inherited_headers) {
            req->headers->set(k, v);
        }
    }

    // §5.4 step 37-42: body handling.
    if (has_init_body) {
        if (req->method == "GET" || req->method == "HEAD") {
            js_throw_type_error("Failed to construct 'Request': Request with GET/HEAD method cannot have body.");
        }
        std::string extracted_bytes;
        std::string extracted_mime;
        extract_body_init(init_body_val, extracted_bytes, extracted_mime);
        req->has_body = true;
        req->body = std::move(extracted_bytes);
        req->body_mime_type = extracted_mime;
        if (!extracted_mime.empty() && !req->headers->has("content-type")) {
            req->headers->append("content-type", extracted_mime);
        }
    } else if (input_req && input_req->has_body) {
        if (req->method == "GET" || req->method == "HEAD") {
            js_throw_type_error("Failed to construct 'Request': Request with GET/HEAD method cannot have body.");
        }
        if (input_req->body_used) {
            js_throw_type_error("Failed to construct 'Request': Cannot construct a Request with a Request object that has already been used.");
        }
        req->has_body = true;
        req->body = input_req->body;
        req->body_mime_type = input_req->body_mime_type;
        input_req->body_used = true;
    }

    // Create fresh linked AbortSignal and live Headers wrapper.
    create_dependent_signal(*req, source_signal);
    req->headers_js = HeadersObject::create_js_instance(req->headers);

    return req;
}

JsValue RequestObject::create_js_instance(
    std::shared_ptr<RequestObject> req,
    JsObject* proto_override
) {
    if (!req) req = std::make_shared<RequestObject>();
    if (!req->headers) req->headers = std::make_shared<HeadersObject>();
    if (!req->headers_js.is_object()) req->headers_js = HeadersObject::create_js_instance(req->headers);
    if (!req->signal_js.is_object()) create_dependent_signal(*req, JsValue::make_undefined());

    JsValue inst = JsValue::make_object();
    if (inst.object_val) {
        inst.object_val->exotic = std::make_shared<RequestExotic>(std::move(req));
        if (proto_override) {
            inst.object_val->prototype = proto_override;
        } else if (JsObject* proto = current_fetch_protos().request_proto) {
            inst.object_val->prototype = proto;
        }
    }
    return inst;
}

JsValue RequestObject::create_constructor(std::function<ResourceRequestContext()> context_fn) {
    JsValue proto = JsValue::make_object();
    set_to_string_tag(proto, "Request");

    if (JsObject* p = proto.object_val) {
        define_proto_getter(p, "url", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.url")->url);
        });
        define_proto_getter(p, "method", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.method")->method);
        });
        define_proto_getter(p, "headers", [](const std::vector<JsValue>&) {
            return require_request_this("get Request.prototype.headers")->headers_js;
        });
        define_proto_getter(p, "destination", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.destination")->destination);
        });
        define_proto_getter(p, "referrer", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.referrer")->referrer);
        });
        define_proto_getter(p, "referrerPolicy", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.referrerPolicy")->referrer_policy);
        });
        define_proto_getter(p, "mode", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.mode")->mode);
        });
        define_proto_getter(p, "credentials", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.credentials")->credentials);
        });
        define_proto_getter(p, "cache", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.cache")->cache);
        });
        define_proto_getter(p, "redirect", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.redirect")->redirect);
        });
        define_proto_getter(p, "integrity", [](const std::vector<JsValue>&) {
            return JsValue::make_string(require_request_this("get Request.prototype.integrity")->integrity);
        });
        define_proto_getter(p, "keepalive", [](const std::vector<JsValue>&) {
            return JsValue::make_boolean(require_request_this("get Request.prototype.keepalive")->keepalive);
        });
        define_proto_getter(p, "signal", [](const std::vector<JsValue>&) {
            return require_request_this("get Request.prototype.signal")->signal_js;
        });
        define_proto_getter(p, "bodyUsed", [](const std::vector<JsValue>&) {
            auto req = require_request_this("get Request.prototype.bodyUsed");
            return JsValue::make_boolean(req->has_body && req->body_used);
        });

        js_define_method(p, "clone", 0, [](const std::vector<JsValue>&) {
            JsValue this_val = JsValue::current_this();
            auto req = require_request_this("Request.prototype.clone");
            if (req->has_body && req->body_used) {
                js_throw_type_error("Failed to execute 'clone' on 'Request': Request body is already used.");
            }
            auto copy = std::make_shared<RequestObject>();
            copy->url = req->url;
            copy->method = req->method;
            copy->headers = std::make_shared<HeadersObject>();
            copy->headers->guard = req->headers ? req->headers->guard : HeadersGuard::Request;
            if (req->headers) copy->headers->map = req->headers->map;
            copy->has_body = req->has_body;
            copy->body = req->body;
            copy->body_mime_type = req->body_mime_type;
            copy->body_used = false;
            copy->mode = req->mode;
            copy->credentials = req->credentials;
            copy->cache = req->cache;
            copy->redirect = req->redirect;
            copy->referrer = req->referrer;
            copy->referrer_policy = req->referrer_policy;
            copy->integrity = req->integrity;
            copy->keepalive = req->keepalive;
            copy->destination = req->destination;
            create_dependent_signal(*copy, req->signal_js);
            copy->headers_js = HeadersObject::create_js_instance(copy->headers);
            JsObject* proto_ptr = this_val.object_val ? this_val.object_val->prototype : nullptr;
            return RequestObject::create_js_instance(std::move(copy), proto_ptr);
        });

        js_define_method(p, "text", 0, [](const std::vector<JsValue>&) {
            auto req = RequestObject::from_js_value(JsValue::current_this());
            JsValue err;
            if (!consume_request_body_for_promise(req, err)) {
                return Promise::make_rejected(err);
            }
            return Promise::make_resolved(JsValue::make_string(req->has_body ? req->body : std::string()));
        });

        js_define_method(p, "arrayBuffer", 0, [](const std::vector<JsValue>&) {
            auto req = RequestObject::from_js_value(JsValue::current_this());
            JsValue err;
            if (!consume_request_body_for_promise(req, err)) {
                return Promise::make_rejected(err);
            }
            return Promise::make_resolved(array_buffer_from_bytes(req->has_body ? req->body : std::string()));
        });

        js_define_method(p, "bytes", 0, [](const std::vector<JsValue>&) {
            auto req = RequestObject::from_js_value(JsValue::current_this());
            JsValue err;
            if (!consume_request_body_for_promise(req, err)) {
                return Promise::make_rejected(err);
            }
            return Promise::make_resolved(typed_array_from_bytes(req->has_body ? req->body : std::string()));
        });

        js_define_method(p, "json", 0, [](const std::vector<JsValue>&) {
            auto req = RequestObject::from_js_value(JsValue::current_this());
            JsValue err;
            if (!consume_request_body_for_promise(req, err)) {
                return Promise::make_rejected(err);
            }
            try {
                JsValue parsed = Json::parse(req->has_body ? req->body : std::string());
                return Promise::make_resolved(parsed);
            } catch (const std::exception& ex) {
                return Promise::make_rejected(make_syntax_error_value(ex.what()));
            }
        });

        js_define_method(p, "blob", 0, [](const std::vector<JsValue>&) {
            auto req = RequestObject::from_js_value(JsValue::current_this());
            JsValue err;
            if (!consume_request_body_for_promise(req, err)) {
                return Promise::make_rejected(err);
            }
            std::string mime = req->headers && req->headers->has("content-type")
                ? req->headers->get("content-type")
                : req->body_mime_type;
            mime = to_lower_str(mime);
            return Promise::make_resolved(
                dom::make_blob(dom::current_realm(), req->has_body ? req->body : std::string(), mime)
            );
        });
    }

    JsValue ctor = js_make_native("Request", 1, [context_fn](const std::vector<JsValue>& args) {
        if (!JsValue::current_is_construct()) {
            js_throw_type_error("Failed to construct 'Request': Please use the 'new' operator, this DOM object constructor cannot be called as a function.");
        }
        ResourceRequestContext ctx = context_fn ? context_fn() : ResourceRequestContext{};
        auto req = RequestObject::construct(args, ctx);
        const auto& caps = JsValue::current_captured();
        JsObject* proto_ptr = (!caps.empty() && caps[0].object_val) ? caps[0].object_val : nullptr;
        return RequestObject::create_js_instance(std::move(req), proto_ptr);
    });
    ctor.set_property("prototype", proto);
    ctor.set_payload_captured({proto});
    if (proto.object_val) {
        js_define_hidden(proto.object_val, "constructor", ctor);
        register_fetch_proto(&FetchRealmProtos::request_proto, proto.object_val);
    }
    return ctor;
}

// -------------------------------------------------------------
// FetchAPI implementation (WHATWG Fetch Standard §5.5)
// -------------------------------------------------------------

JsValue FetchAPI::fetch(const std::vector<JsValue>& args,
                        const ResourceRequestContext& context) {
    if (args.empty()) {
        return Promise::make_rejected(make_type_error_value("fetch requires at least 1 argument"));
    }

    std::shared_ptr<RequestObject> req;
    try {
        req = RequestObject::construct(args, context);
    } catch (const JsThrow& err) {
        return Promise::make_rejected(err.value);
    } catch (const std::exception& ex) {
        return Promise::make_rejected(make_type_error_value(ex.what()));
    }

    ResourceRequestContext req_context = context;
    if (req->mode == "same-origin") req_context.mode = RequestMode::SameOrigin;
    else if (req->mode == "no-cors") req_context.mode = RequestMode::NoCors;
    else req_context.mode = RequestMode::Cors;

    if (req->credentials == "include") {
        req_context.include_credentials = true;
        req_context.omit_credentials = false;
    } else if (req->credentials == "omit") {
        req_context.include_credentials = false;
        req_context.omit_credentials = true;
    } else {
        req_context.include_credentials = false;
        req_context.omit_credentials = false;
    }

    if (req->cache == "default") req_context.cache_mode = CacheMode::Default;
    else if (req->cache == "no-store") req_context.cache_mode = CacheMode::NoStore;
    else if (req->cache == "reload") req_context.cache_mode = CacheMode::Reload;
    else if (req->cache == "no-cache") req_context.cache_mode = CacheMode::NoCache;
    else if (req->cache == "force-cache" || req->cache == "only-if-cached") {
        req_context.cache_mode = CacheMode::ForceCache;
    }

    auto signal = req->signal;
    JsValue signal_js = req->signal_js;
    auto abort_state = std::make_shared<std::pair<bool, JsValue>>(false, JsValue::make_undefined());

    auto current_abort_reason = [&]() -> JsValue {
        if (signal && signal->aborted()) {
            JsValue r = signal->reason();
            return r.is_truthy() ? r : JsValue::make_string("AbortError: The operation was aborted");
        }
        if (signal_js.is_object() && signal_js.get_property("aborted").is_truthy()) {
            JsValue r = signal_js.get_property("reason");
            return !r.is_undefined() ? r : JsValue::make_string("AbortError: The operation was aborted");
        }
        return JsValue::make_string("AbortError: The operation was aborted");
    };

    bool dom_aborted = false;
    const bool has_dom_signal = signal_js.is_object() && dom::on_abort(signal_js, nullptr, dom_aborted);
    if ((signal && signal->aborted()) || (has_dom_signal && dom_aborted)) {
        return Promise::make_rejected(current_abort_reason());
    }

    auto pdata = std::make_shared<PromiseData>();
    JsValue pval = Promise::create_promise_instance(pdata);

    if (has_dom_signal) {
        dom::on_abort(signal_js, [pdata, abort_state, signal_js]() {
            JsValue r = signal_js.get_property("reason");
            JsValue reason = !r.is_undefined() ? r : JsValue::make_string("AbortError: The operation was aborted");
            abort_state->first = true;
            abort_state->second = reason;
            Promise::reject(pdata, reason);
        }, dom_aborted);
    } else if (signal) {
        signal->add_event_listener("abort", [pdata, abort_state, signal](const std::shared_ptr<Event>&) {
            JsValue r = signal->reason();
            JsValue reason = r.is_truthy() ? r : JsValue::make_string("AbortError: The operation was aborted");
            abort_state->first = true;
            abort_state->second = reason;
            Promise::reject(pdata, reason);
        });
    }

    const std::string url = req->url;
    const std::string method = req->method;
    const std::unordered_map<std::string, std::string> req_headers =
        req->headers ? req->headers->map : std::unordered_map<std::string, std::string>{};
    const std::string req_body = req->has_body ? req->body : std::string();

    EventLoop::default_loop().post_task([url, method, req_headers, req_body, pdata, abort_state, signal, req_context]() {
        if (abort_state->first || (signal && signal->aborted())) {
            JsValue reason = abort_state->first
                ? abort_state->second
                : (signal->reason().is_truthy() ? signal->reason()
                                                : JsValue::make_string("AbortError: The operation was aborted"));
            Promise::reject(pdata, reason);
            return;
        }

        ResourceResponse resp = ResourceLoader::load_for_page(url, req_context, method, req_headers, req_body);
        if (abort_state->first || (signal && signal->aborted())) {
            JsValue reason = abort_state->first
                ? abort_state->second
                : (signal->reason().is_truthy() ? signal->reason()
                                                : JsValue::make_string("AbortError: The operation was aborted"));
            Promise::reject(pdata, reason);
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
