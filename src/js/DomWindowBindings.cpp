// The window: the global object as a Window, with location, history,
// navigator, screen and storage; the load events; scripts a script inserts;
// and the handful of interfaces that hang off the window and nothing else.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/Selector.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/URL.hpp"

#include <thread>

namespace litebird {
namespace dom {

namespace {

struct HistoryEntry {
    std::string url;
    JsValue state;
};

struct WindowPart : RealmPart {
    std::vector<HistoryEntry> history;
    std::size_t history_index{0};
    ScriptRunner run_script;
    std::string name;
    /// Elements by id, for `window.someId`: built when first asked after the
    /// tree changed.
    std::unordered_map<std::string, std::weak_ptr<Element>> ids;
    uint64_t ids_version{0};
    bool load_fired{false};
};

// ---------------------------------------------------------------------------
// The global object as a host object
// ---------------------------------------------------------------------------

JsValue named_constructor(Realm& realm, const std::string& name) {
    const char* iface = name == "Option" ? "HTMLOptionElement" : name == "Image" ? "HTMLImageElement"
                      : name == "Audio" ? "HTMLAudioElement" : nullptr;
    if (!iface) return undefined();
    Realm* r = &realm;
    const std::string which = name;
    JsValue ctor = js_make_native(name.c_str(), 0, [r, which](Args args) -> JsValue {
        if (!r->alive) return undefined();
        if (!JsValue::current_is_construct()) {
            js_throw_type_error("Class constructor " + which + " cannot be invoked without 'new'");
        }
        if (which == "Option") return wrap(*r, make_option(*r, args));
        auto element = std::make_shared<Element>(which == "Image" ? "img" : "audio");
        if (r->document) element->set_owner_document(r->document);
        if (which == "Image") {
            if (!js_arg(args, 0).is_undefined()) element->set_attribute("width", std::to_string(to_ulong(args[0])));
            if (!js_arg(args, 1).is_undefined()) element->set_attribute("height", std::to_string(to_ulong(args[1])));
        } else {
            element->set_attribute("preload", "auto");
            if (!js_arg(args, 0).is_undefined()) element->set_attribute("src", to_str(args[0]));
        }
        return wrap(*r, element);
    });
    ctor.define_property("prototype", JsValue::make_object(realm.proto(iface)), 0);
    return ctor;
}

JsValue css_namespace(Realm& realm) {
    JsValue css = JsValue::make_object();
    css.define_property("escape", make_function(realm, "escape", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSS.escape");
        return str(css_escape_identifier(to_str(args[0])));
    }), kMemberFlags);
    css.define_property("supports", make_function(realm, "supports", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSS.supports");
        return boolean(args.size() >= 2 && !to_str(args[1]).empty());
    }), kMemberFlags);
    css.define_property("@@toStringTag", str("CSS"), PropertyMap::Configurable);
    return css;
}

struct WindowExotic : JsExotic {
    Realm* realm{nullptr};

    // Global variables are properties of the window.
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        auto scope = realm->global_scope.lock();
        if (!scope) return false;
        if (auto* binding = scope->find_own(name)) {
            out = binding->value;
            return true;
        }
        return false;
    }
    bool set(JsObject*, const std::string& name, const JsValue& value) override {
        auto scope = realm->global_scope.lock();
        if (!scope) return false;
        if (auto* binding = scope->find_own(name)) {
            if (!binding->is_const && !binding->is_readonly) binding->value = value;
            return true;
        }
        return false;
    }
    // Asked when nothing else has the name: an interface, or an element.
    bool get_last(JsObject* self, const std::string& name, JsValue& out) override {
        if (!realm->alive || name.empty() || name[0] == '%' || name[0] == '@') return false;
        if (const InterfaceDef* def = find_interface_or_alias(name)) {
            out = realm->ctor(def->name);
            remember(self, name, out);
            return true;
        }
        JsValue named = named_constructor(*realm, name);
        if (named.is_undefined() && name == "CSS") named = css_namespace(*realm);
        if (!named.is_undefined()) {
            out = named;
            remember(self, name, out);
            return true;
        }
        if (auto element = element_with_id(name)) {
            out = wrap(*realm, element);
            return true;
        }
        return false;
    }
    bool has(JsObject*, const std::string& name) override {
        if (!realm->alive || name.empty() || name[0] == '%' || name[0] == '@') return false;
        if (auto scope = realm->global_scope.lock()) {
            if (scope->find_own(name)) return true;
        }
        if (find_interface_or_alias(name)) return true;
        if (name == "Option" || name == "Image" || name == "Audio" || name == "CSS") return true;
        return element_with_id(name) != nullptr;
    }

    // An interface that has been asked for once is an ordinary property from then on.
    static void remember(JsObject* self, const std::string& name, const JsValue& value) {
        auto& entry = self->properties.entry(name);
        entry.second = value;
        entry.flags = PropertyMap::Hidden;
    }

    std::shared_ptr<Element> element_with_id(const std::string& name) {
        if (!realm->document) return nullptr;
        WindowPart& part = realm->part<WindowPart>();
        if (part.ids_version != Node::tree_version()) {
            part.ids.clear();
            for_each_descendant(*realm->document, [&](Node& node) {
                if (node.is_element()) {
                    if (const std::string* id = static_cast<Element&>(node).attribute("id")) {
                        if (!id->empty()) part.ids.emplace(*id, shared(static_cast<Element&>(node)));
                    }
                }
                return true;
            });
            part.ids_version = Node::tree_version();
        }
        auto found = part.ids.find(name);
        return found == part.ids.end() ? nullptr : found->second.lock();
    }
};

void require_window(Realm& realm) {
    const JsValue& self = JsValue::current_this();
    if (self.is_nullish()) return;
    if (!realm.window.is_object() || self.object_val != realm.window.object_val) throw_illegal_invocation("Window");
}

// ---------------------------------------------------------------------------
// Location and history
// ---------------------------------------------------------------------------

std::string without_fragment(const std::string& url) {
    const auto hash = url.find('#');
    return hash == std::string::npos ? url : url.substr(0, hash);
}

bool current_url(Realm& realm, URLObject& url) {
    return realm.document && url.parse(realm.document->url());
}

void fire_history_events(Realm& realm, const JsValue& state, bool state_changed, const std::string& old_url,
                         const std::string& new_url) {
    if (!realm.alive || !realm.window.is_object()) return;
    if (state_changed) {
        dispatch(realm, realm.window, make_event(realm, "PopStateEvent", "popstate", false, false, {{"state", state}}), true);
    }
    if (without_fragment(old_url) == without_fragment(new_url) && old_url != new_url) {
        dispatch(realm, realm.window,
                 make_event(realm, "HashChangeEvent", "hashchange", false, false,
                            {{"oldURL", str(old_url)}, {"newURL", str(new_url)}}),
                 true);
    }
}

// Moves to another entry of the session history, as a task.
void traverse_history(Realm& realm, long delta) {
    Realm* r = &realm;
    EventLoop::default_loop().post_task([r, delta]() {
        if (!r->alive || !r->document) return;
        WindowPart& part = r->part<WindowPart>();
        const long target = static_cast<long>(part.history_index) + delta;
        if (delta == 0 || target < 0 || target >= static_cast<long>(part.history.size())) return;
        const std::string old_url = r->document->url();
        part.history_index = static_cast<std::size_t>(target);
        const HistoryEntry entry = part.history[part.history_index];
        r->document->set_url(entry.url);
        fire_history_events(*r, entry.state, true, old_url, entry.url);
    });
}

void set_history_state(Realm& realm, Args args, bool replace, const char* what) {
    require_args(args, 2, what);
    if (!realm.document) return;
    WindowPart& part = realm.part<WindowPart>();
    std::string url = realm.document->url();
    if (!js_arg(args, 2).is_nullish()) {
        URLObject current, wanted;
        const bool ok = wanted.parse(to_str(args[2]), url) && current.parse(url);
        if (!ok || wanted.origin != current.origin) {
            throw_dom(realm, "SecurityError", std::string("Failed to execute '") + what + "' on 'History': A history state object with URL '" +
                                              to_str(args[2]) + "' cannot be created in a document with origin '" + current.origin + "'.");
        }
        url = wanted.href;
    }
    if (part.history.empty()) part.history.push_back({realm.document->url(), null()});
    if (replace) {
        part.history[part.history_index] = {url, args[0]};
    } else {
        part.history.resize(part.history_index + 1);
        part.history.push_back({url, args[0]});
        part.history_index = part.history.size() - 1;
    }
    realm.document->set_url(url);
}

void require_brand(const char* name) {
    const JsValue* brand = slot(JsValue::current_this(), "brand");
    if (!brand || brand->to_string() != name) throw_illegal_invocation(name);
}

JsValue branded(Realm& realm, const char* iface) {
    JsValue object = realm.make(iface);
    set_slot(object, "brand", str(iface));
    return object;
}

void setup_history(Iface& I) {
    I.getter("length", [](Realm& r) {
        require_brand("History");
        return num(static_cast<double>(std::max<std::size_t>(r.part<WindowPart>().history.size(), 1)));
    });
    I.getter("state", [](Realm& r) {
        require_brand("History");
        WindowPart& part = r.part<WindowPart>();
        return part.history.empty() ? null() : part.history[part.history_index].state;
    });
    I.accessor("scrollRestoration",
        [](Realm&) {
            require_brand("History");
            return str("auto");
        },
        [](Realm&, const JsValue&) { require_brand("History"); });
    I.method("pushState", 2, [](Realm& r, Args args) {
        require_brand("History");
        set_history_state(r, args, false, "pushState");
        return undefined();
    });
    I.method("replaceState", 2, [](Realm& r, Args args) {
        require_brand("History");
        set_history_state(r, args, true, "replaceState");
        return undefined();
    });
    I.method("back", 0, [](Realm& r, Args) {
        require_brand("History");
        traverse_history(r, -1);
        return undefined();
    });
    I.method("forward", 0, [](Realm& r, Args) {
        require_brand("History");
        traverse_history(r, 1);
        return undefined();
    });
    I.method("go", 0, [](Realm& r, Args args) {
        require_brand("History");
        traverse_history(r, js_arg(args, 0).is_undefined() ? 0 : to_long(args[0]));
        return undefined();
    });
}

void define_location_part(Iface& I, const char* name, std::string URLObject::*part) {
    const std::string which = name;
    I.accessor(name,
        [part](Realm& r) {
            require_brand("Location");
            URLObject url;
            return str(current_url(r, url) ? url.*part : std::string());
        },
        [which](Realm& r, const JsValue& value) {
            require_brand("Location");
            if (which != "hash") return;        // anything else would load another page
            std::string fragment = to_str(value);
            if (!fragment.empty() && fragment[0] == '#') fragment.erase(0, 1);
            if (r.document) navigate(r, without_fragment(r.document->url()) + "#" + fragment);
        });
}

void setup_location(Iface& I) {
    I.accessor("href",
        [](Realm& r) {
            require_brand("Location");
            return str(document_url(r));
        },
        [](Realm& r, const JsValue& value) {
            require_brand("Location");
            navigate(r, to_str(value));
        });
    I.getter("origin", [](Realm& r) {
        require_brand("Location");
        URLObject url;
        return str(current_url(r, url) ? url.origin : std::string("null"));
    });
    define_location_part(I, "protocol", &URLObject::protocol);
    define_location_part(I, "host", &URLObject::host);
    define_location_part(I, "hostname", &URLObject::hostname);
    define_location_part(I, "port", &URLObject::port);
    define_location_part(I, "pathname", &URLObject::pathname);
    define_location_part(I, "search", &URLObject::search);
    define_location_part(I, "hash", &URLObject::hash);
    I.method("assign", 1, [](Realm& r, Args args) {
        require_brand("Location");
        require_args(args, 1, "Location.assign");
        navigate(r, to_str(args[0]));
        return undefined();
    });
    I.method("replace", 1, [](Realm& r, Args args) {
        require_brand("Location");
        require_args(args, 1, "Location.replace");
        navigate(r, to_str(args[0]));
        return undefined();
    });
    I.method("reload", 0, [](Realm&, Args) {
        require_brand("Location");
        return undefined();
    });
    I.method("toString", 0, [](Realm& r, Args) {
        require_brand("Location");
        return str(document_url(r));
    });
}

// ---------------------------------------------------------------------------
// Navigator, screen, storage
// ---------------------------------------------------------------------------

void setup_navigator(Iface& I) {
    static const std::pair<const char*, const char*> kStrings[] = {
        {"userAgent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Litebird/1.0"},
        {"appCodeName", "Mozilla"}, {"appName", "Netscape"},
        {"appVersion", "5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Litebird/1.0"},
        {"platform", "Linux x86_64"}, {"product", "Gecko"}, {"productSub", "20030107"},
        {"vendor", "Litebird"}, {"vendorSub", ""}, {"language", "en-US"},
    };
    for (const auto& entry : kStrings) {
        const char* value = entry.second;
        I.getter(entry.first, [value](Realm&) {
            require_brand("Navigator");
            return str(value);
        });
    }
    I.getter("languages", [](Realm&) {
        require_brand("Navigator");
        return cached(JsValue::current_this(), "languages", []() {
            JsValue list = JsValue::make_array({str("en-US"), str("en")});
            list.object_val->frozen = true;
            list.object_val->extensible = false;
            return list;
        });
    });
    I.getter("onLine", [](Realm&) { return boolean(true); });
    I.getter("cookieEnabled", [](Realm&) { return boolean(true); });
    I.getter("webdriver", [](Realm&) { return boolean(false); });
    I.getter("maxTouchPoints", [](Realm&) { return num(0); });
    I.getter("hardwareConcurrency", [](Realm&) {
        return num(std::max(1u, std::thread::hardware_concurrency()));
    });
    I.getter("plugins", [](Realm&) { return JsValue::make_array(); });
    I.getter("mimeTypes", [](Realm&) { return JsValue::make_array(); });
    I.method("javaEnabled", 0, [](Realm&, Args) { return boolean(false); });
    I.method("sendBeacon", 1, [](Realm&, Args) { return boolean(false); });
}

void setup_screen(Iface& I) {
    static const std::pair<const char*, double> kNumbers[] = {
        {"width", 1024}, {"height", 768}, {"availWidth", 1024}, {"availHeight", 768},
        {"availLeft", 0}, {"availTop", 0}, {"colorDepth", 24}, {"pixelDepth", 24},
    };
    for (const auto& entry : kNumbers) {
        const double value = entry.second;
        I.getter(entry.first, [value](Realm&) {
            require_brand("Screen");
            return num(value);
        });
    }
}

struct StorageExotic : JsExotic {
    Storage* storage{nullptr};

    static bool is_internal(const std::string& name) {
        return name.empty() || name[0] == '%' || (name.size() > 1 && name[0] == '@' && name[1] == '@');
    }
    // A stored key is a property unless the interface has a member of that name.
    bool get_last(JsObject*, const std::string& name, JsValue& out) override {
        if (is_internal(name) || !storage->has_item(name)) return false;
        out = str(storage->get_item(name));
        return true;
    }
    bool has(JsObject*, const std::string& name) override {
        return !is_internal(name) && storage->has_item(name);
    }
    bool set(JsObject* self, const std::string& name, const JsValue& value) override {
        if (is_internal(name)) return false;
        int guard = 0;
        for (JsObject* proto = self->prototype; proto && guard < kMaxPrototypeChain; proto = proto->prototype, ++guard) {
            if (proto->properties.find(name) != proto->properties.end()) return false;
        }
        storage->set_item(name, to_str(value));
        return true;
    }
    bool remove(JsObject*, const std::string& name, bool& result) override {
        if (is_internal(name) || !storage->has_item(name)) return false;
        storage->remove_item(name);
        result = true;
        return true;
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        for (std::size_t i = 0; i < storage->length(); ++i) out.push_back(storage->key(i));
    }
};

Storage& this_storage() {
    return *this_state<StorageExotic>("Storage").storage;
}

void setup_storage(Iface& I) {
    I.getter("length", [](Realm&) { return num(static_cast<double>(this_storage().length())); });
    I.method("key", 1, [](Realm&, Args args) {
        require_args(args, 1, "Storage.key");
        Storage& storage = this_storage();
        const std::size_t index = to_ulong(args[0]);
        return index < storage.length() ? str(storage.key(index)) : null();
    });
    I.method("getItem", 1, [](Realm&, Args args) {
        require_args(args, 1, "Storage.getItem");
        Storage& storage = this_storage();
        const std::string key = to_str(args[0]);
        return storage.has_item(key) ? str(storage.get_item(key)) : null();
    });
    I.method("setItem", 2, [](Realm&, Args args) {
        require_args(args, 2, "Storage.setItem");
        this_storage().set_item(to_str(args[0]), to_str(args[1]));
        return undefined();
    });
    I.method("removeItem", 1, [](Realm&, Args args) {
        require_args(args, 1, "Storage.removeItem");
        this_storage().remove_item(to_str(args[0]));
        return undefined();
    });
    I.method("clear", 0, [](Realm&, Args) {
        this_storage().clear();
        return undefined();
    });
}

JsValue storage_object(Realm& realm, const char* slot_name, Storage& storage) {
    return cached(realm.window, slot_name, [&]() {
        auto exotic = std::make_shared<StorageExotic>();
        exotic->storage = &storage;
        return realm.make("Storage", exotic);
    });
}

// ---------------------------------------------------------------------------
// matchMedia, XMLHttpRequest
// ---------------------------------------------------------------------------

struct MediaQueryState : JsExotic {
    std::string media;
    bool matches{false};
};

// Enough of media queries for the questions scripts ask at start-up: the
// window is a 1024x768 screen in light mode with a mouse.
bool evaluate_media_query(const std::string& query) {
    const std::string text = ascii_lower(strip_ascii_space(query));
    if (text.empty() || text == "all" || text == "screen") return true;
    if (text.find("print") != std::string::npos) return false;
    auto feature = [&](const char* name, double& out) {
        const auto at = text.find(name);
        if (at == std::string::npos) return false;
        const auto colon = text.find(':', at);
        if (colon == std::string::npos) return false;
        out = std::atof(text.c_str() + colon + 1);
        return true;
    };
    double value = 0;
    if (feature("min-width", value) && 1024 < value) return false;
    if (feature("max-width", value) && 1024 > value) return false;
    if (feature("min-height", value) && 768 < value) return false;
    if (feature("max-height", value) && 768 > value) return false;
    if (text.find("prefers-color-scheme: dark") != std::string::npos) return false;
    if (text.find("prefers-reduced-motion: reduce") != std::string::npos) return false;
    if (text.find("hover: none") != std::string::npos || text.find("pointer: coarse") != std::string::npos) return false;
    if (text.find("orientation: portrait") != std::string::npos) return false;
    return true;
}

void setup_media_query_list(Iface& I) {
    I.getter("media", [](Realm&) { return str(this_state<MediaQueryState>("MediaQueryList").media); });
    I.getter("matches", [](Realm&) { return boolean(this_state<MediaQueryState>("MediaQueryList").matches); });
    I.accessor("onchange",
        [](Realm& r) { return get_event_handler(r, JsValue::current_this(), "change"); },
        [](Realm& r, const JsValue& value) { set_event_handler(r, JsValue::current_this(), "change", value); });
    // The older names for adding a "change" listener.
    I.method("addListener", 1, [](Realm&, Args args) {
        const JsValue self = JsValue::current_this();
        this_state<MediaQueryState>("MediaQueryList");
        if (!js_arg(args, 0).is_nullish()) js_call(self.get_property("addEventListener"), self, {str("change"), args[0]});
        return undefined();
    });
    I.method("removeListener", 1, [](Realm&, Args args) {
        const JsValue self = JsValue::current_this();
        this_state<MediaQueryState>("MediaQueryList");
        if (!js_arg(args, 0).is_nullish()) js_call(self.get_property("removeEventListener"), self, {str("change"), args[0]});
        return undefined();
    });
}

struct RequestState : JsExotic {
    int ready_state{0};
    bool sent{false};
    std::string method;
    std::string url;
    std::string response_type;
};

RequestState& this_request() {
    return this_state<RequestState>("XMLHttpRequest");
}

JsValue construct_request(Realm& realm, const InterfaceDef&, Args, const JsValue* derived) {
    return realm.make("XMLHttpRequest", std::make_shared<RequestState>(), derived);
}

void setup_request_target(Iface& I) {
    for (const char* type : {"loadstart", "progress", "abort", "error", "load", "timeout", "loadend"}) {
        const std::string event_type = type;
        const std::string name = "on" + event_type;
        define_accessor_value(I.proto, name.c_str(),
            make_function(I.realm, name.c_str(), 0, [event_type](Realm& r, Args) -> JsValue {
                return get_event_handler(r, JsValue::current_this(), event_type);
            }),
            make_function(I.realm, name.c_str(), 1, [event_type](Realm& r, Args args) -> JsValue {
                set_event_handler(r, JsValue::current_this(), event_type, js_arg(args, 0));
                return undefined();
            }));
    }
}

// There is no network behind this: a request that is sent fails, the way a
// request does when the server cannot be reached.
void setup_request(Iface& I) {
    static const std::pair<const char*, int> kStates[] = {
        {"UNSENT", 0}, {"OPENED", 1}, {"HEADERS_RECEIVED", 2}, {"LOADING", 3}, {"DONE", 4},
    };
    for (const auto& entry : kStates) I.constant(entry.first, entry.second);
    I.getter("readyState", [](Realm&) { return num(this_request().ready_state); });
    I.getter("status", [](Realm&) {
        this_request();
        return num(0);
    });
    I.getter("statusText", [](Realm&) {
        this_request();
        return str("");
    });
    I.getter("responseText", [](Realm&) {
        this_request();
        return str("");
    });
    I.getter("response", [](Realm&) {
        const RequestState& state = this_request();
        return state.response_type.empty() || state.response_type == "text" ? str("") : null();
    });
    I.getter("responseURL", [](Realm&) {
        this_request();
        return str("");
    });
    I.getter("responseXML", [](Realm&) {
        this_request();
        return null();
    });
    I.accessor("responseType",
        [](Realm&) { return str(this_request().response_type); },
        [](Realm&, const JsValue& value) { this_request().response_type = to_str(value); });
    I.accessor("timeout", [](Realm&) { this_request(); return num(0); }, [](Realm&, const JsValue&) { this_request(); });
    I.accessor("withCredentials",
        [](Realm&) { this_request(); return boolean(false); },
        [](Realm&, const JsValue&) { this_request(); });
    I.getter("upload", [](Realm& r) {
        this_request();
        return cached(JsValue::current_this(), "upload", [&]() { return r.make("XMLHttpRequestUpload"); });
    });
    I.accessor("onreadystatechange",
        [](Realm& r) { return get_event_handler(r, JsValue::current_this(), "readystatechange"); },
        [](Realm& r, const JsValue& value) { set_event_handler(r, JsValue::current_this(), "readystatechange", value); });
    I.method("open", 2, [](Realm& r, Args args) {
        require_args(args, 2, "XMLHttpRequest.open");
        const JsValue self = JsValue::current_this();
        RequestState& state = this_request();
        state.method = ascii_upper(to_str(args[0]));
        state.url = to_str(args[1]);
        state.sent = false;
        state.ready_state = 1;
        fire(r, self, "readystatechange");
        return undefined();
    });
    I.method("setRequestHeader", 2, [](Realm& r, Args args) {
        require_args(args, 2, "XMLHttpRequest.setRequestHeader");
        if (this_request().ready_state != 1) throw_dom(r, "InvalidStateError", "The object's state must be OPENED.");
        return undefined();
    });
    I.method("send", 0, [](Realm& r, Args) {
        const JsValue self = JsValue::current_this();
        RequestState& state = this_request();
        if (state.ready_state != 1 || state.sent) throw_dom(r, "InvalidStateError", "The object's state must be OPENED.");
        state.sent = true;
        Realm* realm = &r;
        EventLoop::default_loop().post_task([realm, self]() {
            RequestState* request = state_of<RequestState>(self);
            if (!realm->alive || !request || !request->sent) return;
            request->ready_state = 4;
            request->sent = false;
            fire(*realm, self, "readystatechange");
            for (const char* type : {"error", "loadend"}) {
                dispatch(*realm, self, make_event(*realm, "ProgressEvent", type, false, false), true);
            }
        });
        return undefined();
    });
    I.method("abort", 0, [](Realm& r, Args) {
        const JsValue self = JsValue::current_this();
        RequestState& state = this_request();
        if (state.sent) {
            state.sent = false;
            state.ready_state = 4;
            fire(r, self, "readystatechange");
            for (const char* type : {"abort", "loadend"}) {
                dispatch(r, self, make_event(r, "ProgressEvent", type, false, false), true);
            }
        }
        state.ready_state = 0;
        return undefined();
    });
    I.method("getResponseHeader", 1, [](Realm&, Args) {
        this_request();
        return null();
    });
    I.method("getAllResponseHeaders", 0, [](Realm&, Args) {
        this_request();
        return str("");
    });
    I.method("overrideMimeType", 1, [](Realm&, Args) {
        this_request();
        return undefined();
    });
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void setup_window(Iface& I) {
    for (const char* name : {"window", "self", "frames", "parent", "top", "globalThis"}) {
        I.getter(name, [](Realm& r) {
            require_window(r);
            return r.window;
        });
    }
    I.getter("document", [](Realm& r) {
        require_window(r);
        return wrap(r, r.document);
    });
    I.accessor("location",
        [](Realm& r) {
            require_window(r);
            return cached(r.window, "location", [&]() { return branded(r, "Location"); });
        },
        [](Realm& r, const JsValue& value) {
            require_window(r);
            navigate(r, to_str(value));
        });
    I.getter("history", [](Realm& r) {
        require_window(r);
        return cached(r.window, "history", [&]() { return branded(r, "History"); });
    });
    I.getter("navigator", [](Realm& r) {
        require_window(r);
        return cached(r.window, "navigator", [&]() { return branded(r, "Navigator"); });
    });
    I.alias("clientInformation", "navigator");
    I.getter("screen", [](Realm& r) {
        require_window(r);
        return cached(r.window, "screen", [&]() { return branded(r, "Screen"); });
    });
    I.getter("customElements", [](Realm& r) {
        require_window(r);
        return custom_elements_object(r);
    });
    I.getter("localStorage", [](Realm& r) {
        require_window(r);
        return storage_object(r, "localStorage", Storage::local_storage());
    });
    I.getter("sessionStorage", [](Realm& r) {
        require_window(r);
        return storage_object(r, "sessionStorage", Storage::session_storage());
    });
    I.accessor("name",
        [](Realm& r) {
            require_window(r);
            return str(r.part<WindowPart>().name);
        },
        [](Realm& r, const JsValue& value) {
            require_window(r);
            r.part<WindowPart>().name = to_str(value);
        });
    I.accessor("status", [](Realm&) { return str(""); }, [](Realm&, const JsValue&) {});
    I.getter("closed", [](Realm&) { return boolean(false); });
    I.getter("length", [](Realm&) { return num(0); });
    I.accessor("opener", [](Realm&) { return null(); }, [](Realm&, const JsValue&) {});
    I.getter("frameElement", [](Realm&) { return null(); });
    I.getter("isSecureContext", [](Realm& r) {
        URLObject url;
        return boolean(current_url(r, url) && (url.protocol == "https:" || url.hostname == "localhost"));
    });
    I.getter("origin", [](Realm& r) {
        URLObject url;
        return str(current_url(r, url) ? url.origin : std::string("null"));
    });
    I.getter("event", [](Realm& r) {
        require_window(r);
        return current_event(r);
    });
    static const std::pair<const char*, double> kNumbers[] = {
        {"innerWidth", 1024}, {"innerHeight", 768}, {"outerWidth", 1024}, {"outerHeight", 768},
        {"devicePixelRatio", 1}, {"scrollX", 0}, {"scrollY", 0}, {"pageXOffset", 0}, {"pageYOffset", 0},
        {"screenX", 0}, {"screenY", 0}, {"screenLeft", 0}, {"screenTop", 0},
    };
    for (const auto& entry : kNumbers) {
        const double value = entry.second;
        I.getter(entry.first, [value](Realm&) { return num(value); });
    }

    I.method("getComputedStyle", 1, [](Realm& r, Args args) {
        require_window(r);
        require_args(args, 1, "getComputedStyle");
        Node* node = node_ptr(args[0]);
        if (!node || !node->is_element()) {
            js_throw_type_error("Failed to execute 'getComputedStyle' on 'Window': parameter 1 is not of type 'Element'.");
        }
        return computed_style(r, static_cast<Element&>(*node));
    });
    I.method("matchMedia", 1, [](Realm& r, Args args) {
        require_window(r);
        require_args(args, 1, "matchMedia");
        auto state = std::make_shared<MediaQueryState>();
        state->media = to_str(args[0]);
        state->matches = evaluate_media_query(state->media);
        return r.make("MediaQueryList", state);
    });
    I.method("getSelection", 0, [](Realm& r, Args) {
        require_window(r);
        return selection_object(r);
    });
    // Dialogs nobody is there to answer, and windows nobody is there to see.
    for (const char* name : {"alert", "print", "focus", "blur", "close", "stop", "scroll", "scrollTo", "scrollBy",
                             "moveTo", "moveBy", "resizeTo", "resizeBy", "captureEvents", "releaseEvents"}) {
        I.method(name, 0, [](Realm&, Args) { return undefined(); });
    }
    I.method("confirm", 0, [](Realm&, Args) { return boolean(false); });
    I.method("prompt", 0, [](Realm&, Args) { return null(); });
    I.method("open", 0, [](Realm&, Args) { return null(); });
    I.method("postMessage", 1, [](Realm& r, Args args) {
        require_window(r);
        require_args(args, 1, "postMessage");
        const JsValue data = args[0];
        Realm* realm = &r;
        EventLoop::default_loop().post_task([realm, data]() {
            if (!realm->alive || !realm->window.is_object()) return;
            URLObject url;
            const std::string origin = current_url(*realm, url) ? url.origin : std::string("null");
            dispatch(*realm, realm->window,
                     make_event(*realm, "MessageEvent", "message", false, false,
                                {{"data", data}, {"origin", str(origin)}, {"source", realm->window}}),
                     true);
        });
        return undefined();
    });
    I.method("requestIdleCallback", 1, [](Realm& r, Args args) {
        require_window(r);
        require_args(args, 1, "requestIdleCallback");
        const JsValue callback = args[0];
        if (!callback.is_function()) js_throw_type_error("requestIdleCallback: the callback is not a function");
        Realm* realm = &r;
        const uint32_t id = EventLoop::default_loop().set_timeout([realm, callback]() {
            if (!realm->alive) return;
            JsValue deadline = JsValue::make_object();
            deadline.set_property("didTimeout", boolean(false));
            deadline.define_property("timeRemaining", make_function(*realm, "timeRemaining", 0, [](Realm&, Args) {
                return num(50);
            }), kMemberFlags);
            call_reporting(*realm, callback, JsValue(), {deadline});
        }, 1);
        return num(id);
    });
    I.method("cancelIdleCallback", 1, [](Realm&, Args args) {
        const double id = to_double(js_arg(args, 0));
        if (id >= 1 && id <= 4294967295.0) EventLoop::default_loop().clear_timer(static_cast<uint32_t>(id));
        return undefined();
    });
    I.method("btoa", 1, [](Realm& r, Args args) {
        require_args(args, 1, "btoa");
        const std::u16string text = js_utf8_to_utf16(to_str(args[0]));
        std::string bytes;
        for (char16_t unit : text) {
            if (unit > 0xFF) throw_dom(r, "InvalidCharacterError", "The string to be encoded contains characters outside of the Latin1 range.");
            bytes += static_cast<char>(unit);
        }
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3) {
            const unsigned b0 = static_cast<unsigned char>(bytes[i]);
            const unsigned b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
            const unsigned b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
            out += kBase64[b0 >> 2];
            out += kBase64[((b0 & 3) << 4) | (b1 >> 4)];
            out += i + 1 < bytes.size() ? kBase64[((b1 & 15) << 2) | (b2 >> 6)] : '=';
            out += i + 2 < bytes.size() ? kBase64[b2 & 63] : '=';
        }
        return str(out);
    });
    I.method("atob", 1, [](Realm& r, Args args) {
        require_args(args, 1, "atob");
        std::string text;
        for (char c : to_str(args[0])) {
            if (!is_ascii_space(c)) text += c;
        }
        if (text.size() % 4 == 0) {
            while (!text.empty() && text.back() == '=' && text.size() % 4 != 2) text.pop_back();
            if (!text.empty() && text.back() == '=') text.pop_back();
        }
        if (text.size() % 4 == 1) throw_dom(r, "InvalidCharacterError", "The string to be decoded is not correctly encoded.");
        std::u16string out;
        unsigned buffer = 0;
        int bits = 0;
        for (char c : text) {
            const char* found = c ? std::char_traits<char>::find(kBase64, 64, c) : nullptr;
            if (!found) throw_dom(r, "InvalidCharacterError", "The string to be decoded is not correctly encoded.");
            buffer = (buffer << 6) | static_cast<unsigned>(found - kBase64);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += static_cast<char16_t>((buffer >> bits) & 0xFF);
            }
        }
        return str(js_utf16_to_utf8(out));
    });
    define_event_handlers(I, true);
}

} // namespace

JsValue window_of(Realm& realm) {
    return realm.window.is_object() ? realm.window : null();
}

std::string document_url(Realm& realm) {
    return realm.document ? realm.document->url() : std::string("about:blank");
}

void set_script_runner(Realm& realm, ScriptRunner runner) {
    realm.part<WindowPart>().run_script = std::move(runner);
}

void navigate(Realm& realm, const std::string& reference) {
    if (!realm.document || !realm.window.is_object()) return;
    const std::string old_url = realm.document->url();
    URLObject target;
    if (!target.parse(strip_ascii_space(reference), old_url)) return;
    // Only a move within the page is carried out: the same document, another
    // fragment. Anything else would be loading another page.
    if (without_fragment(target.href) != without_fragment(old_url)) return;
    if (target.href.find('#') == std::string::npos) return;
    WindowPart& part = realm.part<WindowPart>();
    if (part.history.empty()) part.history.push_back({old_url, null()});
    if (target.href == old_url) return;
    part.history.resize(part.history_index + 1);
    part.history.push_back({target.href, null()});
    part.history_index = part.history.size() - 1;
    realm.document->set_url(target.href);
    Realm* r = &realm;
    const std::string new_url = target.href;
    EventLoop::default_loop().post_task([r, old_url, new_url]() { fire_history_events(*r, null(), true, old_url, new_url); });
}

void run_inserted_script(Realm& realm, Element& script) {
    if (!script.script_object() || !realm.document) return;
    JsValue object = wrap(realm, script);
    if (!slot(object, "runnable")) return;          // from markup, or it has run already
    const std::string type = ascii_lower(strip_ascii_space(script.get_attribute("type")));
    static const char* const kScriptTypes[] = {"", "text/javascript", "application/javascript", "text/ecmascript",
                                               "application/ecmascript", "application/x-javascript", "text/jscript",
                                               "text/livescript", "text/x-javascript", "text/javascript1.5"};
    bool classic = false;
    for (const char* known : kScriptTypes) classic = classic || type == known;
    if (!classic) return;
    if (script.has_attribute("src")) {
        // Nothing is fetched, so an external script never arrives.
        clear_slot(object, "runnable");
        Realm* r = &realm;
        EventLoop::default_loop().post_task([r, object]() {
            if (r->alive) fire(*r, object, "error");
        });
        return;
    }
    std::string source;
    for (const auto& child : script.children()) {
        if (child->is_text()) source += character_data(*child);
    }
    if (source.empty()) return;                     // it may still be given text and run then
    clear_slot(object, "runnable");
    WindowPart& part = realm.part<WindowPart>();
    if (!part.run_script || document_of(realm, script) != realm.document) return;
    auto previous = realm.document->current_script();
    realm.document->set_current_script(shared(script));
    part.run_script(source);
    if (realm.alive && realm.document) realm.document->set_current_script(previous);
}

void fire_load_events(Realm& realm) {
    if (!realm.alive || !realm.document || !realm.window.is_object()) return;
    WindowPart& part = realm.part<WindowPart>();
    if (part.load_fired) return;
    part.load_fired = true;
    JsValue document = wrap(realm, realm.document);
    auto set_ready_state = [&](const char* state) {
        if (realm.document->ready_state() == state) return;
        realm.document->set_ready_state(state);
        fire(realm, document, "readystatechange");
    };
    // The parser is done and every script has run.
    set_ready_state("interactive");
    fire(realm, document, "DOMContentLoaded", true, false);
    // Nothing is still arriving (no external style sheets, scripts or images
    // are fetched), so the page is complete as soon as it is interactive.
    set_ready_state("complete");
    dispatch(realm, realm.window, make_event(realm, "Event", "load", false, false), true, &document);
    dispatch(realm, realm.window,
             make_event(realm, "PageTransitionEvent", "pageshow", false, false, {{"persisted", boolean(false)}}), true, &document);
}

namespace {

bool handle_uncaught(const JsValue& value) {
    JsIntrinsics& intrinsics = JsIntrinsics::get();
    if (!intrinsics.dom_realm) return false;
    Realm& realm = *static_cast<Realm*>(intrinsics.dom_realm);
    return report_exception(realm, value);
}

} // namespace

void attach_window(Realm& realm, const JsValue& global, const std::shared_ptr<Document>& document,
                   const std::shared_ptr<JsEnvironment>& scope) {
    realm.window = global;
    realm.document = document;
    realm.global_scope = scope;
    JsObject* cell = global.object_val;
    cell->prototype = realm.proto("Window");
    auto exotic = std::make_shared<WindowExotic>();
    exotic->realm = &realm;
    cell->exotic = exotic;

    WindowPart& part = realm.part<WindowPart>();
    part.history.clear();
    part.history_index = 0;
    part.load_fired = false;
    if (document) {
        document->set_has_window(true);
        part.history.push_back({document->url(), null()});
        // `location` belongs to the document object itself, where a page
        // cannot replace it.
        JsValue object = wrap(realm, document);
        define_accessor_value(object.object_val, "location",
            make_function(realm, "location", 0, [](Realm& r, Args) -> JsValue {
                return r.window.is_object() ? r.window.get_property("location") : null();
            }),
            make_function(realm, "location", 1, [](Realm& r, Args args) -> JsValue {
                navigate(r, to_str(js_arg(args, 0)));
                return undefined();
            }));
    }
    js_uncaught_handler = &handle_uncaught;
}

void register_window_interfaces() {
    register_interface({"Window", "EventTarget", setup_window, nullptr, 0, nullptr});
    register_interface({"Location", nullptr, setup_location, nullptr, 0, nullptr});
    register_interface({"History", nullptr, setup_history, nullptr, 0, nullptr});
    register_interface({"Navigator", nullptr, setup_navigator, nullptr, 0, nullptr});
    register_interface({"Screen", nullptr, setup_screen, nullptr, 0, nullptr});
    register_interface({"Storage", nullptr, setup_storage, nullptr, 0, nullptr});
    register_interface({"MediaQueryList", "EventTarget", setup_media_query_list, nullptr, 0, nullptr});
    register_interface({"XMLHttpRequestEventTarget", "EventTarget", setup_request_target, nullptr, 0, nullptr});
    register_interface({"XMLHttpRequestUpload", "XMLHttpRequestEventTarget", nullptr, nullptr, 0, nullptr});
    register_interface({"XMLHttpRequest", "XMLHttpRequestEventTarget", setup_request, construct_request, 0, nullptr});
}

} // namespace dom
} // namespace litebird
