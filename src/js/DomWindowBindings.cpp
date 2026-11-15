// The window: the global object as a Window, with location, history,
// navigator, screen and storage; the load events; scripts a script inserts;
// and the handful of interfaces that hang off the window and nothing else.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/Selector.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/net/UrlParser.hpp"
#include "litebird/xml/XmlDom.hpp"
#include "litebird/xml/XmlParser.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/Storage.hpp"

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

    // Global variables are properties of the window: those declared with
    // `var` or `function`, and those made by assignment. `let`, `const` and
    // `class` declare a name every script sees that is not a property.
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        auto scope = realm->global_scope.lock();
        if (!scope) return false;
        auto* binding = scope->find_own(name);
        if (!binding || binding->is_lexical) return false;
        out = binding->value;
        return true;
    }
    bool set(JsObject*, const std::string& name, const JsValue& value) override {
        auto scope = realm->global_scope.lock();
        if (!scope) return false;
        auto* binding = scope->find_own(name);
        if (!binding || binding->is_lexical) return false;
        if (!binding->is_const && !binding->is_readonly) binding->value = value;
        return true;
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
            auto* binding = scope->find_own(name);
            if (binding && !binding->is_lexical) return true;
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
                    auto& element = static_cast<Element&>(node);
                    if (const std::string* id = element.attribute("id")) {
                        if (!id->empty()) part.ids.emplace(*id, shared(element));
                    }
                    // A form, an image, an embed or an object is found by its name too.
                    if (!element.is_foreign()) {
                        const std::string& tag = element.tag_name();
                        if (tag == "form" || tag == "img" || tag == "embed" || tag == "object") {
                            const std::string* name = element.attribute("name");
                            if (name && !name->empty()) part.ids.emplace(*name, shared(element));
                        }
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

// The address of the window's document, parsed. False when there is no
// document or its address is not a URL.
bool current_url(Realm& realm, Url& url) {
    return realm.document && Url::parse(realm.document->url(), nullptr, url);
}

// True when the two addresses differ in their fragment: "#a" and "#b", or
// none and "#".
bool fragment_changed(const std::string& old_url, const std::string& new_url) {
    Url before, after;
    if (!Url::parse(old_url, nullptr, before) || !Url::parse(new_url, nullptr, after)) return false;
    return before.has_fragment != after.has_fragment || before.fragment != after.fragment;
}

void fire_history_events(Realm& realm, const JsValue& state, bool state_changed, const std::string& old_url,
                         const std::string& new_url) {
    if (!realm.alive || !realm.window.is_object()) return;
    if (state_changed) {
        dispatch(realm, realm.window, make_event(realm, "PopStateEvent", "popstate", false, false, {{"state", state}}), true);
    }
    if (fragment_changed(old_url, new_url)) {
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

// May a page at `current` say its address is `wanted`? Never another site.
// On the web any path of the same site will do; a file may only change its
// query and fragment, and anything else only its fragment.
bool can_rewrite_url(const Url& current, const Url& wanted) {
    if (!current.same_site_parts(wanted)) return false;
    if (wanted.scheme == "http" || wanted.scheme == "https") return true;
    if (wanted.pathname() != current.pathname()) return false;
    if (wanted.scheme == "file") return true;
    return wanted.has_query == current.has_query && wanted.query == current.query;
}

void set_history_state(Realm& realm, Args args, bool replace, const char* what) {
    require_args(args, 2, what);
    if (!realm.document) return;
    WindowPart& part = realm.part<WindowPart>();
    std::string url = realm.document->url();
    // No address, or an empty one, keeps the page where it is.
    const std::string given = js_arg(args, 2).is_nullish() ? std::string() : to_str(args[2]);
    if (!given.empty()) {
        Url current, wanted;
        const bool known = current_url(realm, current);
        if (!known || !parse_url(realm, *realm.document, given, wanted) || !can_rewrite_url(current, wanted)) {
            throw_dom(realm, "SecurityError", std::string("Failed to execute '") + what + "' on 'History': A history state object with URL '" +
                                              given + "' cannot be created in a document with origin '" +
                                              (known ? current.origin() : std::string("null")) + "' and URL '" + url + "'.");
        }
        url = wanted.href();
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

void define_location_part(Iface& I, const char* name, std::string (Url::*get)() const) {
    const std::string which = name;
    I.accessor(name,
        [get](Realm& r) {
            require_brand("Location");
            Url url;
            return str(current_url(r, url) ? (url.*get)() : std::string());
        },
        [which](Realm& r, const JsValue& value) {
            require_brand("Location");
            if (which != "hash") return;        // anything else would load another page
            Url url;
            if (!current_url(r, url)) return;
            // "#" alone is a fragment too, an empty one: the address then
            // ends in "#".
            std::string fragment = to_str(value);
            if (fragment.empty() || fragment[0] != '#') fragment.insert(0, "#");
            url.set_hash(fragment);
            navigate(r, url);
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
            navigate_to(r, value, false);
        });
    I.getter("origin", [](Realm& r) {
        require_brand("Location");
        Url url;
        return str(current_url(r, url) ? url.origin() : std::string("null"));
    });
    define_location_part(I, "protocol", &Url::protocol);
    define_location_part(I, "host", &Url::host_and_port);
    define_location_part(I, "hostname", &Url::hostname);
    define_location_part(I, "port", &Url::port_text);
    define_location_part(I, "pathname", &Url::pathname);
    define_location_part(I, "search", &Url::search);
    define_location_part(I, "hash", &Url::hash);
    I.method("assign", 1, [](Realm& r, Args args) {
        require_brand("Location");
        require_args(args, 1, "Location.assign");
        navigate_to(r, args[0], false);
        return undefined();
    });
    I.method("replace", 1, [](Realm& r, Args args) {
        require_brand("Location");
        require_args(args, 1, "Location.replace");
        navigate_to(r, args[0], true);
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
// matchMedia
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

// ---------------------------------------------------------------------------
// XMLHttpRequest
// ---------------------------------------------------------------------------

// What came back. `ok` false is the standard's "network error": no status,
// no headers, no bytes.
struct RequestResponse {
    bool ok{false};
    int status{0};
    std::string status_text;
    /// Names in lower case.
    std::vector<std::pair<std::string, std::string>> headers;
    /// The address that answered, with its fragment and without.
    std::string url;
    std::string url_without_fragment;
    std::string bytes;
};

struct RequestState : JsExotic {
    int ready_state{0};
    bool send_flag{false};
    bool synchronous{false};
    std::string method;
    Url url;
    std::string response_type;
    double timeout{0};
    bool with_credentials{false};
    std::vector<std::pair<std::string, std::string>> request_headers;
    std::string request_body;
    bool has_override{false};
    MimeType override_type;
    RequestResponse response;
    /// The response as text, once it has been asked for.
    bool has_text{false};
    std::string text;
    /// What `response` and `responseXML` made of the bytes (a document, a
    /// JSON value, a Blob; kept in the object's slot "response"): nothing
    /// yet, something, or nothing and never will.
    enum class Made { NotYet, Yes, Failed } made{Made::NotYet};
    /// open() and abort() end the fetch that is under way. A task that
    /// belongs to an ended one finds another number here and does nothing.
    unsigned generation{0};
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

void forget_response(const JsValue& self, RequestState& state) {
    state.response = RequestResponse{};
    state.has_text = false;
    state.text.clear();
    state.made = RequestState::Made::NotYet;
    clear_slot(self, "response");
}

// A progress event of XMLHttpRequest. No response here has a Content-Length
// to compare with, so the total is never known.
void fire_progress(Realm& r, const JsValue& self, const char* type, std::size_t loaded) {
    dispatch(r, self,
             make_event(r, "ProgressEvent", type, false, false,
                        {{"lengthComputable", boolean(false)}, {"loaded", num(static_cast<double>(loaded))}, {"total", num(0)}}),
             true);
}

bool is_cors_safelisted_response_header(std::string_view lower_name) {
    return lower_name == "cache-control" || lower_name == "content-language" ||
           lower_name == "content-length" || lower_name == "content-type" ||
           lower_name == "expires" || lower_name == "last-modified" || lower_name == "pragma";
}

bool header_exposed_by_cors(
    std::string_view lower_name,
    const std::unordered_map<std::string, std::string>& resp_headers,
    bool with_credentials
) {
    if (is_cors_safelisted_response_header(lower_name)) return true;
    auto it = resp_headers.find("access-control-expose-headers");
    if (it == resp_headers.end()) return false;
    std::string_view list = it->second;
    std::size_t pos = 0;
    while (pos < list.size()) {
        auto comma = list.find(',', pos);
        std::string_view item = (comma == std::string_view::npos) ? list.substr(pos) : list.substr(pos, comma - pos);
        const std::string token = ascii_lower(strip_ascii_space(std::string(item)));
        if (!with_credentials && token == "*") return true;
        if (token == lower_name) return true;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return false;
}

// Performs an XMLHttpRequest load: `data:` URLs directly, and `http:` /
// `https:` URLs through `ResourceLoader::load_for_page` with `RequestMode::Cors`
// and `allow_local_files = false` (so XMLHttpRequest never reads local files).
void perform(Realm& r, const RequestState& state, RequestResponse& out) {
    out = RequestResponse{};
    if (state.url.protocol() == "data:") {
        MimeType type;
        std::string body;
        const std::string address = state.url.href(true);
        if (!process_data_url(address, type, body)) return;
        out.ok = true;
        out.status = 200;
        out.status_text = "OK";
        out.headers.emplace_back("content-type", type.serialize());
        out.url = state.url.href();
        out.url_without_fragment = address;
        out.bytes = std::move(body);
        return;
    }

    if (state.url.protocol() != "http:" && state.url.protocol() != "https:") {
        return;
    }

    ResourceRequestContext context;
    context.document_url = r.document ? r.document->url() : "about:blank";
    context.base_url = context.document_url;
    if (r.document) {
        Url base;
        if (document_base_url(r, *r.document, base)) {
            context.base_url = base.href();
        }
    }
    context.mode = RequestMode::Cors;
    context.include_credentials = state.with_credentials;
    context.allow_local_files = false;

    std::unordered_map<std::string, std::string> combined_headers;
    for (const auto& [name, val] : state.request_headers) {
        const std::string lower = ascii_lower(name);
        if (lower == "host" || lower == "connection" || lower == "transfer-encoding" || lower == "content-length") {
            continue;
        }
        auto existing = combined_headers.find(name);
        if (existing != combined_headers.end()) {
            existing->second += ", " + val;
        } else {
            combined_headers[name] = val;
        }
    }

    const std::string upper_method = ascii_upper(state.method);
    const std::string body = (upper_method == "GET" || upper_method == "HEAD") ? std::string() : state.request_body;
    ResourceResponse net = ResourceLoader::load_for_page(
        state.url.href(true), context, state.method, combined_headers, body
    );
    if (!net.network_ok) return;

    out.ok = true;
    out.status = net.status_code;
    out.status_text = net.status_text;
    for (const auto& [hname, hval] : net.header_list) {
        if (hname == "set-cookie" || hname == "set-cookie2") continue;
        if (net.cross_origin && !header_exposed_by_cors(hname, net.headers, state.with_credentials)) {
            continue;
        }
        out.headers.emplace_back(hname, hval);
    }
    out.url_without_fragment = net.url.empty() ? state.url.href(true) : net.url;
    out.url = out.url_without_fragment;
    if (!state.url.hash().empty() && out.url_without_fragment == state.url.href(true)) {
        out.url += state.url.hash();
    }
    out.bytes = std::move(net.body);
}

// "Get a response MIME type": what Content-Type says, and text/xml when it
// says nothing that can be read.
MimeType response_mime(const RequestState& state) {
    MimeType type;
    for (const auto& [name, value] : state.response.headers) {
        if (name == "content-type" && MimeType::parse(value, type)) return type;
    }
    type.type = "text";
    type.subtype = "xml";
    return type;
}

MimeType final_mime(const RequestState& state) {
    return state.has_override ? state.override_type : response_mime(state);
}

// "Get a final encoding": the charset of the response's type, or of the type
// the script put in its place, as the name of an encoding. Empty when there
// is none, and when it names no encoding this build reads.
std::string final_charset(const RequestState& state) {
    std::string label;
    // The type is kept in a name: parameter() gives a pointer into it.
    const MimeType response = response_mime(state);
    if (const std::string* charset = response.parameter("charset")) label = *charset;
    if (state.has_override) {
        if (const std::string* charset = state.override_type.parameter("charset")) label = *charset;
    }
    return encoding_name_for_label(label);
}

// "Get a text response".
const std::string& text_response(RequestState& state) {
    if (state.has_text) return state.text;
    std::string charset = final_charset(state);
    if (state.response_type.empty() && charset.empty() && final_mime(state).is_xml()) {
        // An XML document says what it is encoded in, and responseText
        // listens to it (responseType "text" does not). When what it says
        // cannot be followed, the text is read as UTF-8.
        std::string encoding;
        xml::XmlError error;
        if (xml::xml_encoding(state.response.bytes, encoding, error)) charset = encoding;
    }
    // Whatever the encoding, bytes that are no text in it become U+FFFD.
    state.text = decode_text(state.response.bytes, charset);
    state.has_text = true;
    return state.text;
}

// "Set a document response". Null for a type that is neither HTML nor XML,
// for HTML when the script did not ask for a document, and for XML that is
// not well formed: there is no `parsererror` document here, unlike DOMParser.
JsValue document_response(Realm& r, const RequestState& state) {
    const MimeType type = final_mime(state);
    const bool html = type.is_html();
    if (!html && !type.is_xml()) return null();
    if (html && state.response_type.empty()) return null();
    const std::string charset = final_charset(state);
    std::shared_ptr<Document> document;
    if (html) {
        // Scripts in a document made this way never run. The encoding is
        // the type's, or UTF-8: a <meta charset> is not looked for, here as
        // for a page.
        document = HtmlParser::parse(decode_text(state.response.bytes, charset), false);
        for (const auto& child : document->children()) child->set_owner_document(document);
    } else {
        std::string utf8, encoding;
        xml::XmlError error;
        if (!xml::decode_xml(state.response.bytes, utf8, encoding, error, charset)) return null();
        document = std::make_shared<Document>();
        document->set_content_type(type.essence());
        if (!xml::parse_xml_document(document, utf8, error)) return null();
    }
    document->set_url(state.response.url);
    document->set_ready_state("complete");
    return wrap(r, document);
}

// "Parse JSON from bytes": the bytes are UTF-8 whatever the type says.
JsValue json_response(Realm& r, const RequestState& state) {
    const JsValue json = r.window.is_object() ? r.window.get_property("JSON") : undefined();
    const JsValue parse = json.is_object() ? json.get_property("parse") : undefined();
    if (!parse.is_callable()) return null();
    JsValue value = null(), thrown;
    const std::string text = utf8_decode(state.response.bytes);
    if (!js_capture_throw([&]() { value = js_call(parse, json, {str(text)}); }, thrown)) return null();
    return value;
}

// What `response` is for the types that are objects, and `responseXML`:
// made the first time it is asked for, and the same object after that.
JsValue response_object(Realm& r, const JsValue& self, RequestState& state) {
    if (state.made == RequestState::Made::Failed || !state.response.ok) return null();
    if (state.made == RequestState::Made::Yes) {
        const JsValue* kept = slot(self, "response");
        return kept ? *kept : null();
    }
    const std::string& type = state.response_type;
    JsValue made = null();
    if (type.empty() || type == "document") {
        made = document_response(r, state);
    } else if (type == "json") {
        // Text that is not JSON is null, and is read again the next time.
        made = json_response(r, state);
        if (made.type == JsType::Null) return made;
    } else if (type == "blob") {
        made = make_blob(r, state.response.bytes, final_mime(state).serialize());
    }
    // "arraybuffer": the engine has no ArrayBuffer to give, and says null.
    if (made.type == JsType::Null) {
        state.made = RequestState::Made::Failed;
        return made;
    }
    set_slot(self, "response", made);
    state.made = RequestState::Made::Yes;
    return made;
}

// The response arrives: all of it in one task, since nothing here comes in
// pieces. While it is arriving, a listener may abort the request or open
// another, and what was to follow is then not fired. Once it has arrived the
// last three events are fired whatever their listeners do, as the standard
// has it ("handle response end-of-body" checks nothing between them).
void deliver(Realm& r, const JsValue& self, unsigned generation) {
    RequestState* state = state_of<RequestState>(self);
    if (!r.alive || !state || state->generation != generation || !state->send_flag) return;
    const auto current = [&]() { return state->generation == generation; };
    RequestResponse response;
    perform(r, *state, response);
    if (!response.ok) {
        state->ready_state = 4;
        state->send_flag = false;
        fire(r, self, "readystatechange");
        fire_progress(r, self, "error", 0);
        fire_progress(r, self, "loadend", 0);
        return;
    }
    state->response = std::move(response);
    const std::size_t length = state->response.bytes.size();
    state->ready_state = 2;
    fire(r, self, "readystatechange");
    if (!current()) return;
    if (length != 0) {
        state->ready_state = 3;
        fire(r, self, "readystatechange");
        if (!current()) return;
    }
    fire_progress(r, self, "progress", length);
    if (!current()) return;
    state->ready_state = 4;
    state->send_flag = false;
    fire(r, self, "readystatechange");
    fire_progress(r, self, "load", length);
    fire_progress(r, self, "loadend", length);
}

void setup_request(Iface& I) {
    static const std::pair<const char*, int> kStates[] = {
        {"UNSENT", 0}, {"OPENED", 1}, {"HEADERS_RECEIVED", 2}, {"LOADING", 3}, {"DONE", 4},
    };
    for (const auto& entry : kStates) I.constant(entry.first, entry.second);
    I.getter("readyState", [](Realm&) { return num(this_request().ready_state); });
    I.getter("status", [](Realm&) { return num(this_request().response.status); });
    I.getter("statusText", [](Realm&) { return str(this_request().response.status_text); });
    I.getter("responseText", [](Realm& r) {
        RequestState& state = this_request();
        if (!state.response_type.empty() && state.response_type != "text") {
            throw_dom(r, "InvalidStateError",
                      "The value is only accessible if the object's 'responseType' is '' or 'text' (was '" + state.response_type + "').");
        }
        if (state.ready_state != 3 && state.ready_state != 4) return str("");
        return str(text_response(state));
    });
    I.getter("response", [](Realm& r) {
        const JsValue self = JsValue::current_this();
        RequestState& state = this_request();
        if (state.response_type.empty() || state.response_type == "text") {
            return state.ready_state == 3 || state.ready_state == 4 ? str(text_response(state)) : str("");
        }
        if (state.ready_state != 4) return null();
        return response_object(r, self, state);
    });
    I.getter("responseURL", [](Realm&) { return str(this_request().response.url_without_fragment); });
    I.getter("responseXML", [](Realm& r) {
        const JsValue self = JsValue::current_this();
        RequestState& state = this_request();
        if (!state.response_type.empty() && state.response_type != "document") {
            throw_dom(r, "InvalidStateError",
                      "The value is only accessible if the object's 'responseType' is '' or 'document' (was '" + state.response_type + "').");
        }
        if (state.ready_state != 4) return null();
        return response_object(r, self, state);
    });
    I.accessor("responseType",
        [](Realm&) { return str(this_request().response_type); },
        [](Realm& r, const JsValue& value) {
            RequestState& state = this_request();
            const std::string type = to_str(value);
            bool known = false;
            for (const char* candidate : {"", "arraybuffer", "blob", "document", "json", "text"}) known = known || type == candidate;
            if (!known) return;                 // an enumeration: any other word is not assigned
            if (state.ready_state == 3 || state.ready_state == 4) {
                throw_dom(r, "InvalidStateError", "The response type cannot be set if the object's state is LOADING or DONE.");
            }
            if (state.synchronous) {
                throw_dom(r, "InvalidAccessError", "The response type cannot be changed for synchronous requests made from a document.");
            }
            state.response_type = type;
        });
    I.accessor("timeout",
        [](Realm&) { return num(this_request().timeout); },
        [](Realm& r, const JsValue& value) {
            RequestState& state = this_request();
            if (state.synchronous) {
                throw_dom(r, "InvalidAccessError", "Timeouts cannot be set for synchronous requests made from a document.");
            }
            double number = to_double(value);
            if (!std::isfinite(number)) number = 0;
            number = std::fmod(std::trunc(number), 4294967296.0);
            state.timeout = number < 0 ? number + 4294967296.0 : number;
        });
    I.accessor("withCredentials",
        [](Realm&) { return boolean(this_request().with_credentials); },
        [](Realm& r, const JsValue& value) {
            RequestState& state = this_request();
            if ((state.ready_state != 0 && state.ready_state != 1) || state.send_flag) {
                throw_dom(r, "InvalidStateError", "The value may only be set if the object's state is UNSENT or OPENED.");
            }
            state.with_credentials = to_bool(value);
        });
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
        std::string method = to_str(args[0]);
        if (!is_http_token(method)) throw_dom(r, "SyntaxError", "'" + method + "' is not a valid HTTP method.");
        const std::string upper = ascii_upper(method);
        if (upper == "CONNECT" || upper == "TRACE" || upper == "TRACK") {
            throw_dom(r, "SecurityError", "'" + method + "' HTTP method is unsupported.");
        }
        for (const char* known : {"DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT"}) {
            if (upper == known) method = upper;
        }
        const std::string reference = to_str(args[1]);
        Url url;
        const bool parsed = r.document ? parse_url(r, *r.document, reference, url)
                                       : Url::parse(reference, static_cast<const Url*>(nullptr), url);
        if (!parsed) throw_dom(r, "SyntaxError", "Invalid URL: " + reference);
        const bool synchronous = args.size() >= 3 && !to_bool(args[2]);
        if (synchronous && (state.timeout != 0 || !state.response_type.empty())) {
            throw_dom(r, "InvalidAccessError",
                      "Synchronous requests from a document must not set a response type or a timeout.");
        }
        ++state.generation;
        state.send_flag = false;
        state.method = method;
        state.url = url;
        state.synchronous = synchronous;
        state.request_headers.clear();
        state.request_body.clear();
        forget_response(self, state);
        if (state.ready_state != 1) {
            state.ready_state = 1;
            fire(r, self, "readystatechange");
        }
        return undefined();
    });
    I.method("setRequestHeader", 2, [](Realm& r, Args args) {
        require_args(args, 2, "XMLHttpRequest.setRequestHeader");
        RequestState& state = this_request();
        if (state.ready_state != 1 || state.send_flag) throw_dom(r, "InvalidStateError", "The object's state must be OPENED.");
        const std::string name = to_str(args[0]);
        std::string value = to_str(args[1]);
        const auto is_blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        while (!value.empty() && is_blank(value.back())) value.pop_back();
        std::size_t start = 0;
        while (start < value.size() && is_blank(value[start])) ++start;
        value.erase(0, start);
        if (!is_http_token(name)) throw_dom(r, "SyntaxError", "'" + name + "' is not a valid HTTP header field name.");
        if (value.find_first_of(std::string("\0\n\r", 3)) != std::string::npos) {
            throw_dom(r, "SyntaxError", "The value given is not a valid HTTP header field value.");
        }
        state.request_headers.emplace_back(name, std::move(value));
        return undefined();
    });
    I.method("send", 0, [](Realm& r, Args args) {
        const JsValue self = JsValue::current_this();
        RequestState& state = this_request();
        if (state.ready_state != 1 || state.send_flag) throw_dom(r, "InvalidStateError", "The object's state must be OPENED.");
        const std::string upper_method = ascii_upper(state.method);
        if (!args.empty() && !args[0].is_nullish() && upper_method != "GET" && upper_method != "HEAD") {
            state.request_body = to_str(args[0]);
            bool has_ct = false;
            for (const auto& [hn, _] : state.request_headers) {
                if (ascii_lower(hn) == "content-type") {
                    has_ct = true;
                    break;
                }
            }
            if (!has_ct) {
                state.request_headers.emplace_back("Content-Type", "text/plain;charset=UTF-8");
            }
        } else {
            state.request_body.clear();
        }
        state.send_flag = true;
        const unsigned generation = state.generation;
        if (state.synchronous) {
            RequestResponse response;
            perform(r, state, response);
            state.ready_state = 4;
            state.send_flag = false;
            if (!response.ok) {
                throw_dom(r, "NetworkError", "Failed to execute 'send' on 'XMLHttpRequest': Failed to load '" + state.url.href() + "'.");
            }
            state.response = std::move(response);
            const std::size_t length = state.response.bytes.size();
            fire(r, self, "readystatechange");
            fire_progress(r, self, "load", length);
            fire_progress(r, self, "loadend", length);
            return undefined();
        }
        fire_progress(r, self, "loadstart", 0);
        // A listener may have aborted the request or opened another.
        if (state.generation != generation || state.ready_state != 1 || !state.send_flag) return undefined();
        Realm* realm = &r;
        EventLoop::default_loop().post_task([realm, self, generation]() { deliver(*realm, self, generation); });
        return undefined();
    });
    I.method("abort", 0, [](Realm& r, Args) {
        const JsValue self = JsValue::current_this();
        RequestState& state = this_request();
        ++state.generation;
        if ((state.ready_state == 1 && state.send_flag) || state.ready_state == 2 || state.ready_state == 3) {
            state.ready_state = 4;
            state.send_flag = false;
            forget_response(self, state);
            fire(r, self, "readystatechange");
            fire_progress(r, self, "abort", 0);
            fire_progress(r, self, "loadend", 0);
        }
        // A request that is done goes back to the start, and says so to nobody.
        if (state.ready_state == 4) {
            state.ready_state = 0;
            forget_response(self, state);
        }
        return undefined();
    });
    I.method("getResponseHeader", 1, [](Realm&, Args args) {
        require_args(args, 1, "XMLHttpRequest.getResponseHeader");
        const RequestState& state = this_request();
        const std::string name = ascii_lower(to_str(args[0]));
        std::string value;
        bool found = false;
        for (const auto& header : state.response.headers) {
            if (header.first != name) continue;
            if (found) value += ", ";
            value += header.second;
            found = true;
        }
        return found ? str(value) : null();
    });
    I.method("getAllResponseHeaders", 0, [](Realm&, Args) {
        const RequestState& state = this_request();
        auto headers = state.response.headers;
        std::stable_sort(headers.begin(), headers.end(),
                         [](const auto& a, const auto& b) { return ascii_upper(a.first) < ascii_upper(b.first); });
        std::string out;
        for (const auto& [name, value] : headers) out += name + ": " + value + "\r\n";
        return str(out);
    });
    I.method("overrideMimeType", 1, [](Realm& r, Args args) {
        require_args(args, 1, "XMLHttpRequest.overrideMimeType");
        RequestState& state = this_request();
        if (state.ready_state == 3 || state.ready_state == 4) {
            throw_dom(r, "InvalidStateError", "The MIME type cannot be overridden when the object's state is LOADING or DONE.");
        }
        state.has_override = true;
        if (!MimeType::parse(to_str(args[0]), state.override_type)) {
            state.override_type = MimeType{};
            state.override_type.type = "application";
            state.override_type.subtype = "octet-stream";
        }
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
            navigate_to(r, value);
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
        Url url;
        return boolean(current_url(r, url) && (url.scheme == "https" || url.host == "localhost"));
    });
    I.getter("origin", [](Realm& r) {
        Url url;
        return str(current_url(r, url) ? url.origin() : std::string("null"));
    });
    I.getter("event", [](Realm& r) {
        require_window(r);
        return current_event(r);
    });
    auto viewport_w_getter = [](Realm& r) {
        require_window(r);
        return num(r.document ? std::round(r.document->viewport_width()) : 1024.0);
    };
    auto viewport_h_getter = [](Realm& r) {
        require_window(r);
        return num(r.document ? std::round(r.document->viewport_height()) : 768.0);
    };
    I.getter("innerWidth", viewport_w_getter);
    I.getter("outerWidth", viewport_w_getter);
    I.getter("innerHeight", viewport_h_getter);
    I.getter("outerHeight", viewport_h_getter);
    static const std::pair<const char*, double> kNumbers[] = {
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
            Url url;
            const std::string origin = current_url(*realm, url) ? url.origin() : std::string("null");
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

void navigate(Realm& realm, const Url& target, bool replace) {
    if (!realm.document || !realm.window.is_object()) return;
    const std::string old_url = realm.document->url();
    Url current;
    if (!Url::parse(old_url, nullptr, current)) return;
    // Only a move within the page is carried out: the same address with a
    // fragment. Anything else would be loading another page.
    if (!target.has_fragment || target.href(true) != current.href(true)) return;
    const std::string new_url = target.href();
    if (new_url == current.href()) return;
    WindowPart& part = realm.part<WindowPart>();
    if (part.history.empty()) part.history.push_back({old_url, null()});
    if (replace) {
        part.history[part.history_index] = {new_url, null()};
    } else {
        part.history.resize(part.history_index + 1);
        part.history.push_back({new_url, null()});
        part.history_index = part.history.size() - 1;
    }
    realm.document->set_url(new_url);
    Realm* r = &realm;
    EventLoop::default_loop().post_task([r, old_url, new_url]() { fire_history_events(*r, null(), true, old_url, new_url); });
}

// `location.href = x`, assign(x), replace(x): the address is read against
// the page's own. Only a move within the page is then carried out.
void navigate_to(Realm& r, const JsValue& value, bool replace) {
    if (!r.document || !r.window.is_object()) return;
    const std::string reference = to_str(value);
    Url target;
    if (!parse_url(r, *r.document, reference, target)) {
        throw_dom(r, "SyntaxError", "'" + reference + "' is not a valid URL.");
    }
    navigate(r, target, replace);
}

bool is_classic_script(const Element& script) {
    const std::string* type = script.attribute("type");
    const std::string* language = script.attribute("language");
    std::string name;
    if (type) name = type->empty() ? "text/javascript" : ascii_lower(strip_ascii_space(*type));
    else if (language && !language->empty()) name = "text/" + ascii_lower(*language);
    else name = "text/javascript";
    static const char* const kTypes[] = {
        "application/ecmascript", "application/javascript", "application/x-ecmascript", "application/x-javascript",
        "text/ecmascript", "text/javascript", "text/javascript1.0", "text/javascript1.1", "text/javascript1.2",
        "text/javascript1.3", "text/javascript1.4", "text/javascript1.5", "text/jscript", "text/livescript",
        "text/x-ecmascript", "text/x-javascript"};
    for (const char* known : kTypes) {
        if (name == known) return true;
    }
    return false;
}

void run_inserted_script(Realm& realm, Element& script) {
    if (!script.script_object() || !realm.document) return;
    JsValue object = wrap(realm, script);
    if (!slot(object, "runnable")) return;          // from markup, or it has run already
    if (!is_classic_script(script)) return;
    if (script.has_attribute("src")) {
        clear_slot(object, "runnable");
        const std::string raw_src = strip_ascii_space(script.get_attribute("src"));
        Realm* r = &realm;
        std::shared_ptr<Element> script_ref = shared(script);
        if (raw_src.empty()) {
            EventLoop::default_loop().post_task([r, object]() {
                if (r->alive) fire(*r, object, "error");
            });
            return;
        }
        ResourceRequestContext context;
        context.document_url = realm.document->url();
        context.base_url = context.document_url;
        Url base;
        if (document_base_url(realm, *realm.document, base)) {
            context.base_url = base.href();
        }
        context.mode = script.has_attribute("crossorigin") ? RequestMode::Cors : RequestMode::NoCors;
        if (context.mode == RequestMode::Cors &&
            ascii_lower(strip_ascii_space(script.get_attribute("crossorigin"))) == "use-credentials") {
            context.include_credentials = true;
        }
        const std::string attr_charset = script.get_attribute("charset");
        EventLoop::default_loop().post_task([r, object, script_ref, raw_src, context, attr_charset]() {
            if (!r->alive || !r->document) return;
            ResourceResponse resp = ResourceLoader::load_for_page(raw_src, context);
            if (!resp.success) {
                if (r->alive) fire(*r, object, "error");
                return;
            }
            std::string charset_label = attr_charset;
            MimeType mime;
            if (MimeType::parse(resp.content_type, mime)) {
                if (const std::string* cs = mime.parameter("charset")) {
                    charset_label = *cs;
                }
            }
            const std::string source = decode_text(resp.body, encoding_name_for_label(charset_label));
            WindowPart& part = r->part<WindowPart>();
            if (part.run_script && document_of(*r, *script_ref) == r->document) {
                auto previous = r->document->current_script();
                r->document->set_current_script(script_ref);
                part.run_script(source);
                if (r->alive && r->document) r->document->set_current_script(previous);
            }
            if (r->alive) fire(*r, object, "load");
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
        // From here on the address is kept as a URL writes itself:
        // "HTTP://Example.test" is "http://example.test/".
        Url address;
        if (Url::parse(document->url(), nullptr, address)) document->set_url(address.href());
        part.history.push_back({document->url(), null()});
        // `location` belongs to the document object itself, where a page
        // cannot replace it.
        JsValue object = wrap(realm, document);
        define_accessor_value(object.object_val, "location",
            make_function(realm, "location", 0, [](Realm& r, Args) -> JsValue {
                return r.window.is_object() ? r.window.get_property("location") : null();
            }),
            make_function(realm, "location", 1, [](Realm& r, Args args) -> JsValue {
                navigate_to(r, js_arg(args, 0), false);
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
