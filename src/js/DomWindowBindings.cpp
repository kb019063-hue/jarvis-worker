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
#include "litebird/js/TypedArray.hpp"

#include <cstdlib>
#include <deque>
#include <thread>

namespace litebird {
namespace dom {

namespace {

struct HistoryEntry {
    std::string url;
    JsValue state;
};

struct ChildFrameEntry {
    std::weak_ptr<Element> element;
    std::shared_ptr<JsInterpreter> interpreter;
    uint64_t navigation_generation{0};
    bool delaying_parent_load{false};
};

struct WindowPart : RealmPart {
    std::vector<HistoryEntry> history;
    std::size_t history_index{0};
    ScriptRunner run_script;
    std::string name;
    bool name_explicitly_set{false};
    /// Elements by id, for `window.someId`: built when first asked after the
    /// tree changed.
    std::unordered_map<std::string, std::weak_ptr<Element>> ids;
    uint64_t ids_version{0};
    bool dcl_fired{false};
    bool load_fired{false};
    bool pending_window_load{false};
    int delaying_load_count{0};
    int frame_depth{0};
    uint64_t parent_realm_id{0};
    std::weak_ptr<Element> frame_element;
    std::function<void()> on_window_load_complete;
    std::unordered_map<const Element*, ChildFrameEntry> child_frames;
};

static constexpr int kMaxNestedFrameDepth = 16;

std::vector<JsValue> collect_child_frame_windows(Realm& realm) {
    std::vector<JsValue> out;
    if (!realm.alive || !realm.document) return out;
    WindowPart* part = realm.find_part<WindowPart>();
    if (!part || part->child_frames.empty()) return out;
    for_each_descendant(*realm.document, [&](Node& node) {
        if (is_html_element(node, "iframe")) {
            auto& el = static_cast<Element&>(node);
            auto it = part->child_frames.find(&el);
            if (it != part->child_frames.end() && it->second.interpreter) {
                const JsValue& w = it->second.interpreter->global_object();
                if (w.is_object()) out.push_back(w);
            }
        }
        return true;
    });
    return out;
}

JsValue find_named_child_frame_window(Realm& realm, const std::string& target_name) {
    if (target_name.empty() || !realm.alive || !realm.document) return undefined();
    WindowPart* part = realm.find_part<WindowPart>();
    if (!part || part->child_frames.empty()) return undefined();
    JsValue match = undefined();
    for_each_descendant(*realm.document, [&](Node& node) {
        if (!is_html_element(node, "iframe")) return true;
        auto& el = static_cast<Element&>(node);
        auto it = part->child_frames.find(&el);
        if (it == part->child_frames.end() || !it->second.interpreter) return true;
        Realm& child_r = it->second.interpreter->dom_realm();
        const std::string& win_name = child_r.part<WindowPart>().name;
        const std::string* attr_name = el.attribute("name");
        const std::string effective_name = !win_name.empty() ? win_name : (attr_name ? *attr_name : std::string());
        if (effective_name == target_name) {
            match = it->second.interpreter->global_object();
            return false;
        }
        return true;
    });
    return match;
}

bool effective_origin_url(const Document& doc, Url& out) {
    if (doc.has_opaque_origin()) return false;
    const std::string& u = doc.url();
    if ((u == "about:blank" || u == "about:srcdoc" || u.rfind("data:", 0) == 0) &&
        !doc.inherited_origin_url().empty()) {
        return Url::parse(doc.inherited_origin_url(), nullptr, out);
    }
    return Url::parse(u, nullptr, out);
}

std::string effective_origin_string(const Realm& realm) {
    if (!realm.document || realm.document->has_opaque_origin()) return "null";
    Url u;
    return effective_origin_url(*realm.document, u) ? u.origin() : std::string("null");
}

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

    static bool is_cross_origin_safelisted(const std::string& name) {
        return name == "window" || name == "self" || name == "frames" ||
               name == "close" || name == "closed" || name == "focus" || name == "blur" ||
               name == "postMessage" || name == "length" || name == "top" ||
               name == "opener" || name == "parent" || name == "location";
    }

    // Indexed child browsing contexts (`window[0]`, `frames[0]`) come first,
    // then HTML §7.2.3 cross-origin checks, then global variables declared
    // with `var` or `function` or assignment.
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        if (!realm || !realm->alive) return false;
        std::size_t index = 0;
        if (parse_index(name, index)) {
            const auto frames = collect_child_frame_windows(*realm);
            if (index < frames.size()) {
                out = frames[index];
                return true;
            }
        }
        Realm& caller = current_realm();
        if (!same_effective_origin(caller, *realm)) {
            if (!name.empty() && (name[0] == '%' || name[0] == '@')) return false;
            if (name == "then") {
                out = undefined();
                return true;
            }
            if (is_cross_origin_safelisted(name)) return false;
            JsValue child_win = find_named_child_frame_window(*realm, name);
            if (!child_win.is_undefined()) {
                out = child_win;
                return true;
            }
            throw_dom(caller, "SecurityError",
                      "Blocked a frame from accessing property '" + name + "' on a cross-origin Window.");
        }
        auto scope = realm->global_scope.lock();
        if (!scope) return false;
        auto* binding = scope->find_own(name);
        if (!binding || binding->is_lexical) return false;
        out = binding->value;
        return true;
    }
    JsExotic::SetResult set(JsObject*, const std::string& name, const JsValue& value) override {
        if (realm && realm->alive) {
            Realm& caller = current_realm();
            if (!same_effective_origin(caller, *realm)) {
                if (name == "location") return JsExotic::SetResult::NotHandled;
                throw_dom(caller, "SecurityError",
                          "Blocked a frame from setting property '" + name + "' on a cross-origin Window.");
            }
        }
        auto scope = realm ? realm->global_scope.lock() : nullptr;
        if (!scope) return JsExotic::SetResult::NotHandled;
        auto* binding = scope->find_own(name);
        if (!binding || binding->is_lexical) return JsExotic::SetResult::NotHandled;
        if (!binding->is_const && !binding->is_readonly) binding->value = value;
        return JsExotic::SetResult::Handled;
    }
    // Asked when nothing else has the name: an interface, a named child frame, or an element.
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
        JsValue child_win = find_named_child_frame_window(*realm, name);
        if (child_win.is_object()) {
            out = child_win;
            return true;
        }
        if (auto element = element_with_id(name)) {
            out = wrap(*realm, element);
            return true;
        }
        return false;
    }
    bool remove(JsObject*, const std::string& name, bool& result) override {
        if (!realm || !realm->alive) return false;
        Realm& caller = current_realm();
        if (!same_effective_origin(caller, *realm)) {
            throw_dom(caller, "SecurityError",
                      "Blocked a frame from deleting property '" + name + "' on a cross-origin Window.");
        }
        (void)result;
        return false;
    }
    bool has(JsObject*, const std::string& name) override {
        if (!realm->alive || name.empty() || name[0] == '%' || name[0] == '@') return false;
        {
            Realm& caller = current_realm();
            if (!same_effective_origin(caller, *realm)) {
                // HTML §7.2.3.5: what exists is the indices, the cross-origin
                // table names (plus the "then" fallback) and the names of the
                // document-tree child navigables. Nothing else: in particular
                // no ids, no interfaces, no globals of the child.
                std::size_t index = 0;
                if (parse_index(name, index))
                    return index < collect_child_frame_windows(*realm).size();
                if (name == "then") return true;
                if (is_cross_origin_safelisted(name)) return true;
                return find_named_child_frame_window(*realm, name).is_object();
            }
        }
        std::size_t index = 0;
        if (parse_index(name, index)) {
            if (index < collect_child_frame_windows(*realm).size()) return true;
        }
        if (auto scope = realm->global_scope.lock()) {
            auto* binding = scope->find_own(name);
            if (binding && !binding->is_lexical) return true;
        }
        if (find_interface_or_alias(name)) return true;
        if (name == "Option" || name == "Image" || name == "Audio" || name == "CSS") return true;
        if (find_named_child_frame_window(*realm, name).is_object()) return true;
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

// The Location object, as a cross-origin object it sees only what the
// standard exposes of it (HTML §7.2.4: `href` and `replace`). Everything
// else throws, the same SecurityError the Window throws. A same-origin
// caller gets the ordinary object: no hook answers, and the ordinary
// accessors and methods do as they do today.
//
// What crosses the origin boundary is decided here, per HTML §7.2.1.3.4:
// `href` has no getter (reading it throws SecurityError) but is writable
// (a cross-document target is scheduled through the child iframe loader);
// `replace()` is exposed; everything else throws. All of those
// fall through to the child's own bindings when allowed, which is what the
// ordinary accessors and methods are.
// Nothing else can be reached: no attribute, no method, no property the
// child has of its own.
struct LocationExotic : JsExotic {
    Realm* owner{nullptr};

    bool cross_origin() const {
        if (!owner || !owner->alive) return false;
        Realm& caller = current_realm();
        return !same_effective_origin(caller, *owner);
    }
    static bool is_exposed(const std::string& name) {
        return name == "href" || name == "replace" || name == "then";
    }
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        if (!cross_origin()) return false;
        if (name == "then") {
            out = undefined();
            return true;
        }
        if (name == "replace") return false; // the ordinary method answers
        Realm& caller = current_realm();
        if (name == "href") {
            // The cross-origin href entry has no getter (HTML §7.2.1.3.4):
            // reading it is refused. Writing it is not (see `set`).
            throw_dom(caller, "SecurityError",
                      "Blocked a frame from reading 'location.href' on a cross-origin Window.");
        }
        throw_dom(caller, "SecurityError",
                  "Blocked a frame from reading 'location." + name + "' on a cross-origin Window.");
    }
    JsExotic::SetResult set(JsObject*, const std::string& name, const JsValue&) override {
        if (!cross_origin()) return JsExotic::SetResult::NotHandled;
        if (name == "href") return JsExotic::SetResult::NotHandled; // the ordinary href setter navigates the child
        throw_dom(current_realm(), "SecurityError",
                  "Blocked a frame from setting 'location." + name + "' on a cross-origin Window.");
    }
    bool has(JsObject*, const std::string& name) override {
        if (!cross_origin()) return false;
        return is_exposed(name);
    }
    bool remove(JsObject*, const std::string& name, bool& result) override {
        if (!cross_origin()) return false;
        (void)result;
        Realm& caller = current_realm();
        throw_dom(caller, "SecurityError",
                  "Blocked a frame from deleting property '" + name + "' on a cross-origin Location.");
    }
};

Realm& resolve_window_realm(Realm& fallback) {
    const JsValue& self = JsValue::current_this();
    if (self.is_nullish()) return fallback;
    if (auto* wx = state_of<WindowExotic>(self)) {
        if (wx->realm && wx->realm->alive && wx->realm->window.is_object() &&
            self.object_val == wx->realm->window.object_val) {
            return *wx->realm;
        }
    }
    throw_illegal_invocation("Window");
}

void require_window(Realm& realm) {
    (void)resolve_window_realm(realm);
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
// MessageChannel and MessagePort
// ---------------------------------------------------------------------------
//
// A port is an EventTarget with one peer. Its same-agent queue uses the same
// neutral packet form as worker messages, so the recipient observes an
// independent structured clone rather than the sender's object identity. The
// port object itself stays rooted by the task; ports themselves are not yet
// transferred across agents.

[[noreturn]] void throw_message_data_clone(Realm& realm, std::string_view receiver,
                                           const std::string& reason) {
    throw_dom(realm, "DataCloneError",
              "Failed to execute 'postMessage' on '" + std::string(receiver) + "': " + reason);
    std::abort(); // throw_dom always throws; makes the noreturn contract explicit.
}

void reject_nonempty_message_transfer_list(Realm& realm, const JsValue& transfer,
                                           std::string_view receiver) {
    if (transfer.is_undefined()) return;
    // The argument is a Web IDL sequence. Iterating it preserves TypeError for
    // a non-iterable value; transferables themselves are still deliberately
    // unsupported, so only an empty list is a no-op.
    if (!js_iterable_to_vector(transfer).empty()) {
        throw_message_data_clone(realm, receiver, "transfer lists are not supported.");
    }
}

std::shared_ptr<JsStructuredClone> clone_message_or_throw(Realm& realm, const JsValue& value,
                                                           std::string_view receiver) {
    auto packet = std::make_shared<JsStructuredClone>();
    std::string error;
    if (!js_write_structured_clone(value, *packet, error)) {
        throw_message_data_clone(realm, receiver,
                                 error.empty() ? "The value could not be cloned." : error);
    }
    return packet;
}

struct MessageChannelState : JsExotic {};

struct MessagePortState : JsExotic {
    uint64_t realm_id{0};
    std::weak_ptr<MessagePortState> peer;
    JsValue object;
    std::deque<std::shared_ptr<JsStructuredClone>> pending;
    bool started{false};
    bool closed{false};
    bool delivery_queued{false};

    void trace(Heap& heap) override {
        heap.mark_value(object);
    }
};

void queue_message_port_delivery(const std::shared_ptr<MessagePortState>& state) {
    Realm* owner = state ? find_realm(state->realm_id) : nullptr;
    if (!state || state->closed || !state->started || state->pending.empty() || state->delivery_queued ||
        !owner || !owner->alive || !state->object.is_object()) {
        return;
    }
    state->delivery_queued = true;
    const JsValue target = state->object;
    EventLoop::default_loop().post_task([state, target]() {
        state->delivery_queued = false;
        Realm* realm = find_realm(state->realm_id);
        if (!realm || !realm->alive || state->closed || !state->started || state->pending.empty() ||
            !target.is_object()) {
            return;
        }
        JsIntrinsics* previous = &JsIntrinsics::get();
        if (realm->intrinsics) JsIntrinsics::set_current(realm->intrinsics);
        struct Restore {
            JsIntrinsics* previous;
            ~Restore() { JsIntrinsics::set_current(previous); }
        } restore{previous};
        // One event per task gives a listener an observable task boundary: a
        // message posted by that listener, or a close(), cannot be folded
        // into the delivery that is already underway.
        std::shared_ptr<JsStructuredClone> packet = std::move(state->pending.front());
        state->pending.pop_front();
        const JsValue data = packet ? js_read_structured_clone(*packet) : undefined();
        dispatch(*realm, target,
                 make_event(*realm, "MessageEvent", "message", false, false, {{"data", data}}), true);
        queue_message_port_delivery(state);
    }, {js_roots({target})});
}

std::shared_ptr<MessagePortState> message_port_ptr() {
    const JsValue& self = JsValue::current_this();
    if (self.type != JsType::Object || !self.object_val) throw_illegal_invocation("MessagePort");
    auto state = std::dynamic_pointer_cast<MessagePortState>(self.object_val->exotic);
    if (!state) throw_illegal_invocation("MessagePort");
    return state;
}

MessagePortState& message_port_state() {
    return *message_port_ptr();
}

JsValue construct_message_channel(Realm& realm, const InterfaceDef&, Args, const JsValue* derived) {
    JsValue channel = realm.make("MessageChannel", std::make_shared<MessageChannelState>(), derived);
    auto first = std::make_shared<MessagePortState>();
    auto second = std::make_shared<MessagePortState>();
    first->realm_id = realm.id;
    second->realm_id = realm.id;
    first->peer = second;
    second->peer = first;
    JsValue port1 = realm.make("MessagePort", first);
    JsValue port2 = realm.make("MessagePort", second);
    first->object = port1;
    second->object = port2;
    set_slot(channel, "port1", port1);
    set_slot(channel, "port2", port2);
    return channel;
}

void setup_message_channel(Iface& I) {
    I.getter("port1", [](Realm&) {
        (void)this_state<MessageChannelState>("MessageChannel");
        const JsValue* port = slot(JsValue::current_this(), "port1");
        if (!port) throw_illegal_invocation("MessageChannel");
        return *port;
    });
    I.getter("port2", [](Realm&) {
        (void)this_state<MessageChannelState>("MessageChannel");
        const JsValue* port = slot(JsValue::current_this(), "port2");
        if (!port) throw_illegal_invocation("MessageChannel");
        return *port;
    });
}

void setup_message_port(Iface& I) {
    I.method("postMessage", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "MessagePort.postMessage");
        reject_nonempty_message_transfer_list(realm, js_arg(args, 1), "MessagePort");
        MessagePortState& source = message_port_state();
        if (source.closed) return undefined();
        std::shared_ptr<MessagePortState> peer = source.peer.lock();
        if (!peer || peer->closed) return undefined();
        peer->pending.push_back(clone_message_or_throw(realm, args[0], "MessagePort"));
        // addEventListener follows MessagePort's explicit-start path:
        // onmessage assignment starts a port, while listeners need start().
        queue_message_port_delivery(peer);
        return undefined();
    });
    I.method("start", 0, [](Realm&, Args) {
        std::shared_ptr<MessagePortState> state = message_port_ptr();
        if (!state->closed) {
            state->started = true;
            queue_message_port_delivery(state);
        }
        return undefined();
    });
    I.method("close", 0, [](Realm&, Args) {
        MessagePortState& state = message_port_state();
        state.closed = true;
        state.pending.clear();
        state.peer.reset();
        return undefined();
    });
    I.accessor("onmessage",
        [](Realm& r) {
            (void)message_port_state();
            return get_event_handler(r, JsValue::current_this(), "message");
        },
        [](Realm& r, const JsValue& value) {
            std::shared_ptr<MessagePortState> state = message_port_ptr();
            if (!state->closed) {
                state->started = true;
                queue_message_port_delivery(state);
            }
            set_event_handler(r, JsValue::current_this(), "message", value);
        });
    I.accessor("onmessageerror",
        [](Realm& r) {
            (void)message_port_state();
            return get_event_handler(r, JsValue::current_this(), "messageerror");
        },
        [](Realm& r, const JsValue& value) {
            (void)message_port_state();
            set_event_handler(r, JsValue::current_this(), "messageerror", value);
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
    JsExotic::SetResult set(JsObject* self, const std::string& name, const JsValue& value) override {
        if (is_internal(name)) return JsExotic::SetResult::NotHandled;
        int guard = 0;
        for (JsObject* proto = self->prototype; proto && guard < kMaxPrototypeChain; proto = proto->prototype, ++guard) {
            if (proto->properties.find(name) != proto->properties.end()) return JsExotic::SetResult::NotHandled;
        }
        storage->set_item(name, to_str(value));
        return JsExotic::SetResult::Handled;
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
    if (state.response_type.empty() && charset.empty()) {
        const MimeType fm = final_mime(state);
        if (fm.is_html()) {
            charset = HtmlParser::prescan_meta_charset(state.response.bytes);
        } else if (fm.is_xml()) {
            // An XML document says what it is encoded in, and responseText
            // listens to it (responseType "text" does not). When what it says
            // cannot be followed, the text is read as UTF-8.
            std::string encoding;
            xml::XmlError error;
            if (xml::xml_encoding(state.response.bytes, encoding, error)) charset = encoding;
        }
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
        // Scripts in a document made this way never run.
        std::string resolved_cs;
        const std::string html_utf8 = HtmlParser::decode_bytes(state.response.bytes, charset, "UTF-8", &resolved_cs);
        document = HtmlParser::parse(html_utf8, false);
        document->set_character_set(resolved_cs);
        for (const auto& child : document->children()) child->set_owner_document(document);
    } else {
        std::string utf8, encoding;
        xml::XmlError error;
        if (!xml::decode_xml(state.response.bytes, utf8, encoding, error, charset)) return null();
        document = std::make_shared<Document>();
        document->set_content_type(type.essence());
        document->set_character_set(encoding);
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
    } else if (type == "arraybuffer") {
        const JsValue view = typed_array_from_bytes(state.response.bytes);
        made = view.get_property("buffer");
    }
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

struct ImageBitmapState : JsExotic {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    bool closed = false;
};

void setup_window(Iface& I) {
    for (const char* name : {"window", "self", "frames", "globalThis"}) {
        I.getter(name, [](Realm& r) {
            Realm& wr = resolve_window_realm(r);
            return wr.window;
        });
    }
    I.getter("parent", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        WindowPart& part = wr.part<WindowPart>();
        if (part.parent_realm_id != 0) {
            Realm* parent = find_realm(part.parent_realm_id);
            auto fe = part.frame_element.lock();
            if (!parent || !parent->alive || !fe || !fe->is_connected()) return null();
            return parent->window;
        }
        return wr.window;
    });
    I.getter("top", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        Realm* cur = &wr;
        for (int hops = 0; hops <= kMaxNestedFrameDepth; ++hops) {
            WindowPart& part = cur->part<WindowPart>();
            if (part.parent_realm_id == 0) return cur->window;
            Realm* parent = find_realm(part.parent_realm_id);
            auto fe = part.frame_element.lock();
            if (!parent || !parent->alive || !fe || !fe->is_connected()) return null();
            cur = parent;
        }
        return cur->window;
    });
    I.getter("document", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        Realm& caller = caller_realm_or(r);
        if (!same_effective_origin(caller, wr)) {
            throw_dom(caller, "SecurityError", "Blocked a frame from accessing a cross-origin frame's document.");
        }
        return wrap(wr, wr.document);
    });
    I.accessor("location",
        [](Realm& r) {
            Realm& wr = resolve_window_realm(r);
            return cached(wr.window, "location", [&]() {
                JsValue object = branded(wr, "Location");
                auto state = std::make_shared<LocationExotic>();
                state->owner = &wr;
                object.object_val->exotic = std::move(state);
                return object;
            });
        },
        [](Realm& r, const JsValue& value) {
            Realm& wr = resolve_window_realm(r);
            navigate_to(wr, value);
        });
    I.getter("history", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        return cached(wr.window, "history", [&]() { return branded(wr, "History"); });
    });
    I.getter("navigator", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        return cached(wr.window, "navigator", [&]() { return branded(wr, "Navigator"); });
    });
    I.alias("clientInformation", "navigator");
    I.getter("screen", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        return cached(wr.window, "screen", [&]() { return branded(wr, "Screen"); });
    });
    I.getter("customElements", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        return custom_elements_object(wr);
    });
    I.getter("localStorage", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        if (wr.document && wr.document->has_opaque_origin()) {
            throw_dom(wr, "SecurityError", "Failed to read the 'localStorage' property from 'Window': The document is sandboxed and lacks the 'allow-same-origin' flag.");
        }
        return storage_object(wr, "localStorage", Storage::local_storage());
    });
    I.getter("sessionStorage", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        if (wr.document && wr.document->has_opaque_origin()) {
            throw_dom(wr, "SecurityError", "Failed to read the 'sessionStorage' property from 'Window': The document is sandboxed and lacks the 'allow-same-origin' flag.");
        }
        return storage_object(wr, "sessionStorage", Storage::session_storage());
    });
    I.accessor("name",
        [](Realm& r) {
            Realm& wr = resolve_window_realm(r);
            return str(wr.part<WindowPart>().name);
        },
        [](Realm& r, const JsValue& value) {
            Realm& wr = resolve_window_realm(r);
            WindowPart& part = wr.part<WindowPart>();
            part.name = to_str(value);
            part.name_explicitly_set = true;
        });
    I.accessor("status", [](Realm&) { return str(""); }, [](Realm&, const JsValue&) {});
    I.getter("closed", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        WindowPart& part = wr.part<WindowPart>();
        if (part.parent_realm_id != 0) {
            Realm* parent = find_realm(part.parent_realm_id);
            auto fe = part.frame_element.lock();
            if (!parent || !parent->alive || !fe || !fe->is_connected()) return boolean(true);
        }
        return boolean(false);
    });
    I.getter("length", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        return num(static_cast<double>(collect_child_frame_windows(wr).size()));
    });
    I.accessor("opener", [](Realm&) { return null(); }, [](Realm&, const JsValue&) {});
    I.getter("frameElement", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        Realm& caller = caller_realm_or(r);
        if (!same_effective_origin(caller, wr)) {
            throw_dom(caller, "SecurityError", "Blocked a frame from accessing a cross-origin frameElement.");
        }
        WindowPart& part = wr.part<WindowPart>();
        if (part.parent_realm_id == 0) return null();
        Realm* parent = find_realm(part.parent_realm_id);
        auto fe = part.frame_element.lock();
        if (!parent || !parent->alive || !fe || !fe->is_connected()) return null();
        if (!same_effective_origin(*parent, wr)) return null();
        return wrap(*parent, fe);
    });
    I.getter("isSecureContext", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        Url url;
        return boolean(wr.document && effective_origin_url(*wr.document, url) &&
                       (url.scheme == "https" || url.host == "localhost"));
    });
    I.getter("origin", [](Realm& r) {
        Realm& wr = resolve_window_realm(r);
        return str(effective_origin_string(wr));
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
        Realm& target_wr = resolve_window_realm(r);
        Realm& sender_wr = caller_realm_or(r);
        require_args(args, 1, "postMessage");
        std::string target_origin_arg = "*";
        JsValue transfer = undefined();
        if (args.size() >= 2 && !args[1].is_undefined()) {
            if (args[1].is_object()) {
                JsValue opt = member(args[1], "targetOrigin");
                if (!opt.is_undefined()) target_origin_arg = to_str(opt);
                transfer = member(args[1], "transfer");
            } else {
                target_origin_arg = to_str(args[1]);
                transfer = js_arg(args, 2);
            }
        }
        std::string required_origin;
        if (target_origin_arg == "*") {
            required_origin.clear();
        } else if (target_origin_arg == "/") {
            required_origin = effective_origin_string(sender_wr);
        } else {
            Url parsed_target;
            if (!Url::parse(target_origin_arg, nullptr, parsed_target)) {
                throw_dom(sender_wr, "SyntaxError",
                          "Failed to execute 'postMessage' on 'Window': Invalid target origin '" + target_origin_arg + "'.");
            }
            required_origin = parsed_target.origin();
        }
        reject_nonempty_message_transfer_list(sender_wr, transfer, "Window");
        auto message = clone_message_or_throw(sender_wr, args[0], "Window");
        const uint64_t target_id = target_wr.id;
        const uint64_t sender_id = sender_wr.id;
        const std::string sender_origin = effective_origin_string(sender_wr);
        const bool same_sender_target = (&sender_wr == &target_wr);
        EventLoop::default_loop().post_task([target_id, sender_id, message = std::move(message), sender_origin, required_origin, same_sender_target]() {
            Realm* target = find_realm(target_id);
            if (!target || !target->alive || !target->window.is_object()) return;
            if (!required_origin.empty()) {
                const std::string actual = effective_origin_string(*target);
                if (required_origin == "null") {
                    if (!same_sender_target) return;
                } else if (actual != required_origin) {
                    return;
                }
            }
            Realm* sender = find_realm(sender_id);
            const JsValue source_win = (sender && sender->alive && sender->window.is_object()) ? sender->window : null();
            JsIntrinsics* prev = &JsIntrinsics::get();
            if (target->intrinsics) JsIntrinsics::set_current(target->intrinsics);
            struct Restore { JsIntrinsics* p; ~Restore() { JsIntrinsics::set_current(p); } } guard{prev};
            const JsValue data = message ? js_read_structured_clone(*message) : undefined();
            dispatch(*target, target->window,
                     make_event(*target, "MessageEvent", "message", false, false,
                                {{"data", data}, {"origin", str(sender_origin)}, {"source", source_win}}),
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
    I.method("createImageBitmap", 1, [](Realm& r, Args args) {
        JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
        auto reject_with = [&](const std::string& name, const std::string& msg) {
            JsValue err = make_dom_exception(r, name, msg);
            if (promise.is_function()) return js_call(promise.get_property("reject"), promise, {err});
            return undefined();
        };
        if (args.empty()) {
            return reject_with("TypeError", "createImageBitmap: 1 argument required.");
        }
        const JsValue& src = args[0];
        auto state = std::make_shared<ImageBitmapState>();
        if (ImageBitmapState* bm = state_of<ImageBitmapState>(src)) {
            if (bm->closed) {
                return reject_with("InvalidStateError", "createImageBitmap: source ImageBitmap is closed.");
            }
            state->width = bm->width;
            state->height = bm->height;
            state->pixels = bm->pixels;
        } else if (std::shared_ptr<Element> el = element_of(src); el && el->tag_name() == "canvas") {
            const std::string* w_attr = el->attribute("width");
            const std::string* h_attr = el->attribute("height");
            int w = w_attr ? std::atoi(w_attr->c_str()) : 300;
            int h = h_attr ? std::atoi(h_attr->c_str()) : 150;
            if (w <= 0 || h <= 0) {
                return reject_with("InvalidStateError", "createImageBitmap: canvas has zero dimensions.");
            }
            state->width = w;
            state->height = h;
            state->pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        } else if (src.is_object_like() && src.get_property("width").type == JsType::Number &&
                   src.get_property("height").type == JsType::Number && src.get_property("data").is_object_like()) {
            int w = static_cast<int>(to_double(src.get_property("width")));
            int h = static_cast<int>(to_double(src.get_property("height")));
            if (w <= 0 || h <= 0) {
                return reject_with("InvalidStateError", "createImageBitmap: ImageData has zero dimensions.");
            }
            state->width = w;
            state->height = h;
            state->pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        } else {
            return reject_with("TypeError", "createImageBitmap: unsupported image source.");
        }
        JsValue bitmap = r.make("ImageBitmap", state);
        if (promise.is_function()) return js_call(promise.get_property("resolve"), promise, {bitmap});
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

bool cross_origin_own_keys(const JsValue& object, bool include_non_enumerable,
                           std::vector<std::string>& out) {
    if (object.type != JsType::Object || !object.object_val) return false;
    Realm& caller = current_realm();
    static const char* const kWindowKeys[] = {"window", "self", "location", "close", "closed",
                                              "focus",  "blur",  "frames",  "length",
                                              "top",    "opener", "parent", "postMessage"};
    static const char* const kLocationKeys[] = {"href", "replace"};
    if (const auto* wx = state_of<WindowExotic>(object)) {
        if (!wx->realm || !wx->realm->alive) return false;
        if (same_effective_origin(caller, *wx->realm)) return false;
        // HTML §7.2.3.10 ([[OwnPropertyKeys]]): the indices ascending, then
        // (for the list, which is how the engine asks for non-enumerable
        // keys) CrossOriginOwnPropertyKeys: the cross-origin table names in
        // order, then "then". (The spec's three symbol keys are not reported:
        // no symbol key is, anywhere in this engine.) The named-property
        // set of the child navigables is deliberately NOT here: it takes
        // part in named access, not in [[OwnPropertyKeys]].
        const std::size_t n = collect_child_frame_windows(*wx->realm).size();
        for (std::size_t i = 0; i < n; ++i) out.push_back(std::to_string(i));
        if (include_non_enumerable) {
            for (const char* name : kWindowKeys) out.push_back(name);
            out.push_back("then");
        }
        return true;
    }
    if (const auto* lx = state_of<LocationExotic>(object)) {
        if (!lx->owner || !lx->owner->alive) return false;
        if (same_effective_origin(caller, *lx->owner)) return false;
        if (include_non_enumerable) {
            for (const char* name : kLocationKeys) out.push_back(name);
            out.push_back("then");
        }
        return true;
    }
    return false;
}

JsValue window_of(Realm& realm) {
    return realm.window.is_object() ? realm.window : null();
}

std::string document_url(Realm& realm) {
    return realm.document ? realm.document->url() : std::string("about:blank");
}

void set_script_runner(Realm& realm, ScriptRunner runner) {
    realm.part<WindowPart>().run_script = std::move(runner);
}

namespace {
// A full document navigation only has a loader for nested browsing contexts.
// Top-level navigation remains an embedder responsibility.
bool request_child_frame_navigation(Realm& child, const Url& target);
}

void navigate(Realm& realm, const Url& target, bool replace) {
    if (!realm.document || !realm.window.is_object()) return;
    const std::string old_url = realm.document->url();
    Url current;
    if (!Url::parse(old_url, nullptr, current)) return;
    // A same-document fragment move stays synchronous. For a nested browsing
    // context, every other Location navigation is handed to the iframe loader
    // below. This is what lets a cross-origin parent set child.location
    // without getting permission to inspect the child document.
    if (!target.has_fragment || target.href(true) != current.href(true)) {
        (void)request_child_frame_navigation(realm, target);
        return;
    }
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
// the page's own. Same-document fragments update history here; a child frame
// delegates every other target to its iframe loader.
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
            std::string chosen_charset;
            MimeType mime;
            if (MimeType::parse(resp.content_type, mime)) {
                if (const std::string* cs = mime.parameter("charset")) {
                    chosen_charset = encoding_name_for_label(*cs);
                }
            }
            if (chosen_charset.empty()) {
                chosen_charset = encoding_name_for_label(attr_charset);
            }
            if (chosen_charset.empty() && r->document) {
                chosen_charset = r->document->character_set();
            }
            const std::string source = decode_text(resp.body, chosen_charset);
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

namespace {

void finish_window_load_if_ready(Realm& realm) {
    if (!realm.alive || !realm.document || !realm.window.is_object()) return;
    WindowPart& part = realm.part<WindowPart>();
    if (part.load_fired || !part.pending_window_load || part.delaying_load_count > 0) return;
    part.pending_window_load = false;
    part.load_fired = true;

    JsIntrinsics* prev = &JsIntrinsics::get();
    if (realm.intrinsics) JsIntrinsics::set_current(realm.intrinsics);
    struct Restore { JsIntrinsics* p; ~Restore() { JsIntrinsics::set_current(p); } } guard{prev};

    JsValue document = wrap(realm, realm.document);
    if (realm.document->ready_state() != "complete") {
        realm.document->set_ready_state("complete");
        fire(realm, document, "readystatechange");
    }
    dispatch(realm, realm.window, make_event(realm, "Event", "load", false, false), true, &document);
    dispatch(realm, realm.window,
             make_event(realm, "PageTransitionEvent", "pageshow", false, false, {{"persisted", boolean(false)}}), true, &document);

    if (part.on_window_load_complete) {
        auto cb = std::move(part.on_window_load_complete);
        cb();
    }
}

void release_parent_load_delay(Realm& parent_realm, ChildFrameEntry& entry) {
    if (!entry.delaying_parent_load) return;
    entry.delaying_parent_load = false;
    WindowPart& parent_part = parent_realm.part<WindowPart>();
    if (parent_part.delaying_load_count > 0) --parent_part.delaying_load_count;
    finish_window_load_if_ready(parent_realm);
}

struct IframeSandboxFlags {
    bool sandboxed{false};
    bool allow_scripts{true};
    bool allow_same_origin{true};
};

IframeSandboxFlags parse_iframe_sandbox(const Element& iframe) {
    IframeSandboxFlags flags;
    const std::string* raw = iframe.attribute("sandbox");
    if (!raw) return flags;
    flags.sandboxed = true;
    flags.allow_scripts = false;
    flags.allow_same_origin = false;
    for (const std::string& tok : split_tokens(*raw)) {
        const std::string lower = ascii_lower(tok);
        if (lower == "allow-scripts") flags.allow_scripts = true;
        else if (lower == "allow-same-origin") flags.allow_same_origin = true;
    }
    return flags;
}

void init_iframe_document_context(Realm& parent_realm, const Element& iframe, Document& child_doc,
                                  const IframeSandboxFlags& sandbox) {
    float vw = 300.0f;
    float vh = 150.0f;
    long parsed_dim = 0;
    if (const std::string* w = iframe.attribute("width"); w && parse_html_integer(*w, parsed_dim) && parsed_dim > 0) {
        vw = static_cast<float>(parsed_dim);
    }
    if (const std::string* h = iframe.attribute("height"); h && parse_html_integer(*h, parsed_dim) && parsed_dim > 0) {
        vh = static_cast<float>(parsed_dim);
    }
    child_doc.set_viewport_size(vw, vh);

    if (!sandbox.allow_same_origin || (parent_realm.document && parent_realm.document->has_opaque_origin())) {
        child_doc.set_opaque_origin(true);
    }
    if (parent_realm.document) {
        const std::string& pu = parent_realm.document->url();
        const std::string origin_src =
            ((pu == "about:blank" || pu == "about:srcdoc" || pu.rfind("data:", 0) == 0) &&
             !parent_realm.document->inherited_origin_url().empty())
                ? parent_realm.document->inherited_origin_url()
                : pu;
        if (!child_doc.has_opaque_origin()) {
            child_doc.set_inherited_origin_url(origin_src);
        }
        Url base;
        if (document_base_url(parent_realm, iframe, base)) {
            child_doc.set_fallback_base_url(base.href());
        }
    }
}

std::shared_ptr<Document> create_blank_frame_document(Realm& parent_realm, const Element& iframe,
                                                      const IframeSandboxFlags& sandbox,
                                                      const std::string& blank_url = "about:blank") {
    auto doc = std::make_shared<Document>();
    doc->set_url(blank_url);
    init_iframe_document_context(parent_realm, iframe, *doc, sandbox);
    HtmlParser::parse_into(doc, "", false, nullptr);
    for (const auto& ch : doc->children()) ch->set_owner_document(doc);
    doc->install_default_layout_updater();
    return doc;
}

// `navigation_url` is an already-resolved Location navigation. It deliberately
// does not write the iframe's `src` attribute: an iframe can be navigated by
// its WindowProxy without changing the markup that created it.
void schedule_iframe_navigation(Realm& parent_realm, Element& iframe, bool initial_insertion,
                                std::string navigation_url = {}) {
    WindowPart& parent_part = parent_realm.part<WindowPart>();
    auto it = parent_part.child_frames.find(&iframe);
    if (it == parent_part.child_frames.end() || !it->second.interpreter) return;
    ChildFrameEntry& entry = it->second;

    const uint64_t gen = ++entry.navigation_generation;
    if (!entry.delaying_parent_load) {
        entry.delaying_parent_load = true;
        ++parent_part.delaying_load_count;
    }

    const uint64_t parent_id = parent_realm.id;
    std::weak_ptr<Element> weak_iframe = shared(iframe);

    EventLoop::default_loop().post_task([parent_id, weak_iframe, gen, initial_insertion,
                                         navigation_url = std::move(navigation_url)]() {
        Realm* pr = find_realm(parent_id);
        if (!pr || !pr->alive || !pr->document) return;
        auto iframe_ptr = weak_iframe.lock();
        if (!iframe_ptr) return;
        WindowPart& ppart = pr->part<WindowPart>();
        auto found = ppart.child_frames.find(iframe_ptr.get());
        if (found == ppart.child_frames.end() || !found->second.interpreter || found->second.navigation_generation != gen) {
            return;
        }
        if (!iframe_ptr->is_connected() || document_of(*pr, *iframe_ptr) != pr->document) {
            release_parent_load_delay(*pr, found->second);
            return;
        }

        const IframeSandboxFlags sandbox = parse_iframe_sandbox(*iframe_ptr);
        std::shared_ptr<JsInterpreter> child_interp = found->second.interpreter;

        // Install completion hook so child window load fires BEFORE iframe load,
        // and iframe load fires BEFORE parent window load.
        Realm& child_realm = child_interp->dom_realm();
        child_realm.part<WindowPart>().on_window_load_complete = [parent_id, weak_iframe, gen]() {
            Realm* pr_inner = find_realm(parent_id);
            if (!pr_inner || !pr_inner->alive) return;
            auto el = weak_iframe.lock();
            if (!el) return;
            WindowPart& pp = pr_inner->part<WindowPart>();
            auto f = pp.child_frames.find(el.get());
            if (f == pp.child_frames.end() || f->second.navigation_generation != gen) return;

            if (el->is_connected() && document_of(*pr_inner, *el) == pr_inner->document) {
                JsIntrinsics* prev = &JsIntrinsics::get();
                if (pr_inner->intrinsics) JsIntrinsics::set_current(pr_inner->intrinsics);
                struct Restore { JsIntrinsics* p; ~Restore() { JsIntrinsics::set_current(p); } } guard{prev};
                JsScriptScope scope;
                fire(*pr_inner, *el, "load");
            }
            // Re-lookup in case `iframe.onload` removed or re-navigated `el`.
            if (!pr_inner->alive) return;
            auto f_after = pr_inner->part<WindowPart>().child_frames.find(el.get());
            if (f_after != pr_inner->part<WindowPart>().child_frames.end() &&
                f_after->second.navigation_generation == gen) {
                release_parent_load_delay(*pr_inner, f_after->second);
            }
        };

        // 1. A direct Location navigation wins over the markup's srcdoc/src.
        // Otherwise `srcdoc` takes precedence over `src` whenever present.
        const std::string* srcdoc = navigation_url.empty() ? iframe_ptr->attribute("srcdoc") : nullptr;
        if (srcdoc) {
            const std::string markup = *srcdoc;
            auto new_doc = std::make_shared<Document>();
            new_doc->set_url("about:srcdoc");
            init_iframe_document_context(*pr, *iframe_ptr, *new_doc, sandbox);
            iframe_ptr->set_content_document(new_doc);
            // `navigate_document()` resets per-document load state. Preserve
            // the completion hook installed above across that reset so the
            // iframe and parent load ordering remains the same as other frame
            // navigation paths.
            auto saved_cb = std::move(child_realm.part<WindowPart>().on_window_load_complete);
            child_interp->navigate_document(new_doc);
            child_interp->dom_realm().part<WindowPart>().on_window_load_complete = std::move(saved_cb);
            child_interp->load_page(markup, sandbox.allow_scripts);
            child_interp->notify_document_loaded();
            return;
        }

        // 2. Determine the target URL. A direct Location navigation is already
        // absolute; markup uses `src` (missing or empty `src=""` is about:blank).
        const std::string* src_attr = iframe_ptr->attribute("src");
        const std::string raw_src = !navigation_url.empty() ? navigation_url
            : (src_attr ? strip_ascii_space(*src_attr) : std::string());
        Url target_url;
        bool is_about_blank = raw_src.empty();
        if (!is_about_blank) {
            if (!parse_url(*pr, *iframe_ptr, raw_src, target_url)) {
                is_about_blank = true;
            } else if (target_url.scheme == "about" && target_url.pathname() == "blank") {
                is_about_blank = true;
            }
        }

        if (is_about_blank) {
            const std::string blank_href = (!raw_src.empty() && target_url.scheme == "about")
                                               ? target_url.href()
                                               : std::string("about:blank");
            if (initial_insertion) {
                if (child_interp->document() && child_interp->document()->url() != blank_href) {
                    child_interp->document()->set_url(blank_href);
                }
                // HTML Standard §4.8.5: initial insertion with no `src` or `src=""`
                // completes synchronously without firing a redundant navigation,
                // EXCEPT dynamic `appendChild(iframe)` still fires `load` on `iframe`
                // in browsers (`Comment-Text-constructor.js`)! Firing `notify_document_loaded()`
                // triggers `on_window_load_complete` -> `iframe` `load` -> releases parent delay!
                child_interp->notify_document_loaded();
                return;
            }
            auto blank_doc = create_blank_frame_document(*pr, *iframe_ptr, sandbox, blank_href);
            iframe_ptr->set_content_document(blank_doc);
            auto saved_cb = std::move(child_realm.part<WindowPart>().on_window_load_complete);
            child_interp->navigate_document(blank_doc);
            child_interp->dom_realm().part<WindowPart>().on_window_load_complete = std::move(saved_cb);
            child_interp->notify_document_loaded();
            return;
        }

        if (target_url.scheme == "javascript") {
            if (sandbox.allow_scripts && same_effective_origin(*pr, child_realm)) {
                std::string code_pct = target_url.pathname() + target_url.search();
                // Evaluate javascript: URL in child realm; if result is string, replace document.
                JsIntrinsics* prev = &JsIntrinsics::get();
                JsIntrinsics::set_current(&child_interp->intrinsics());
                auto hex_val = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                    return -1;
                };
                std::string decoded_js;
                decoded_js.reserve(code_pct.size());
                for (std::size_t i = 0; i < code_pct.size(); ++i) {
                    if (code_pct[i] == '%' && i + 2 < code_pct.size()) {
                        const int hi = hex_val(code_pct[i + 1]);
                        const int lo = hex_val(code_pct[i + 2]);
                        if (hi >= 0 && lo >= 0) {
                            decoded_js.push_back(static_cast<char>((hi << 4) | lo));
                            i += 2;
                            continue;
                        }
                    }
                    decoded_js.push_back(code_pct[i]);
                }
                JsValue res = child_interp->eval_ast(decoded_js);
                JsIntrinsics::set_current(prev);
                if (res.type == JsType::String) {
                    auto js_doc = std::make_shared<Document>();
                    js_doc->set_url(child_interp->document() ? child_interp->document()->url() : "about:blank");
                    init_iframe_document_context(*pr, *iframe_ptr, *js_doc, sandbox);
                    iframe_ptr->set_content_document(js_doc);
                    auto saved_cb = std::move(child_realm.part<WindowPart>().on_window_load_complete);
                    child_interp->navigate_document(js_doc);
                    child_interp->dom_realm().part<WindowPart>().on_window_load_complete = std::move(saved_cb);
                    child_interp->load_page(res.string_val.str(), sandbox.allow_scripts);
                }
            }
            child_interp->notify_document_loaded();
            return;
        }

        // 3. Fetch HTTP(S), data:, or allowed local file: document.
        ResourceRequestContext ctx;
        if (pr->document && !pr->document->has_opaque_origin()) {
            const std::string& pu = pr->document->url();
            ctx.document_url = ((pu == "about:blank" || pu == "about:srcdoc" || pu.rfind("data:", 0) == 0) &&
                                !pr->document->inherited_origin_url().empty())
                                   ? pr->document->inherited_origin_url()
                                   : pu;
        }
        ctx.base_url = ctx.document_url;
        ctx.mode = RequestMode::NoCors;

        const std::string fetch_href = target_url.href(true);
        ResourceResponse resp = ResourceLoader::load_for_page(fetch_href, ctx);
        auto saved_cb = std::move(child_realm.part<WindowPart>().on_window_load_complete);

        if (!resp.network_ok) {
            auto err_doc = create_blank_frame_document(*pr, *iframe_ptr, sandbox, "about:blank");
            err_doc->set_opaque_origin(true);
            iframe_ptr->set_content_document(err_doc);
            child_interp->navigate_document(err_doc);
            child_interp->dom_realm().part<WindowPart>().on_window_load_complete = std::move(saved_cb);
            child_interp->notify_document_loaded();
            return;
        }

        std::string final_url = resp.url.empty() ? target_url.href() : resp.url;
        if (target_url.has_fragment && resp.url == target_url.href(true)) {
            final_url += "#" + target_url.fragment;
        }

        auto new_doc = std::make_shared<Document>();
        new_doc->set_url(final_url);
        init_iframe_document_context(*pr, *iframe_ptr, *new_doc, sandbox);
        iframe_ptr->set_content_document(new_doc);
        child_interp->navigate_document(new_doc);
        child_interp->dom_realm().part<WindowPart>().on_window_load_complete = std::move(saved_cb);

        MimeType mime;
        const bool parsed_mime = MimeType::parse(resp.content_type, mime);
        std::string charset_label;
        if (parsed_mime) {
            if (const std::string* cs = mime.parameter("charset")) charset_label = *cs;
        }
        if (parsed_mime && mime.is_xml()) {
            child_interp->load_xml_page(resp.body, mime.essence(), sandbox.allow_scripts, nullptr, charset_label);
        } else {
            std::string fallback_cs = "UTF-8";
            if (pr->document && same_effective_origin(*pr, child_interp->dom_realm())) {
                const std::string& parent_cs = pr->document->character_set();
                if (!parent_cs.empty() &&
                    parent_cs != "UTF-16BE" &&
                    parent_cs != "UTF-16LE" &&
                    parent_cs != "replacement") {
                    fallback_cs = parent_cs;
                }
            }
            std::string resolved_cs;
            const std::string html_text = HtmlParser::decode_bytes(resp.body, charset_label, fallback_cs, &resolved_cs);
            new_doc->set_character_set(resolved_cs);
            child_interp->load_page(html_text, sandbox.allow_scripts);
        }
        child_interp->notify_document_loaded();
    });
}

bool request_child_frame_navigation(Realm& child, const Url& target) {
    WindowPart* child_part = child.find_part<WindowPart>();
    if (!child_part || child_part->parent_realm_id == 0) return false;
    Realm* parent = find_realm(child_part->parent_realm_id);
    auto iframe = child_part->frame_element.lock();
    if (!parent || !parent->alive || !parent->document || !iframe || !iframe->is_connected() ||
        document_of(*parent, *iframe) != parent->document) {
        return false;
    }
    WindowPart* parent_part = parent->find_part<WindowPart>();
    if (!parent_part) return false;
    auto found = parent_part->child_frames.find(iframe.get());
    if (found == parent_part->child_frames.end() || !found->second.interpreter ||
        &found->second.interpreter->dom_realm() != &child) {
        return false;
    }
    schedule_iframe_navigation(*parent, *iframe, false, target.href());
    return true;
}

} // namespace

void fire_load_events(Realm& realm) {
    if (!realm.alive || !realm.document || !realm.window.is_object()) return;
    WindowPart& part = realm.part<WindowPart>();
    if (part.load_fired) return;

    JsIntrinsics* prev = &JsIntrinsics::get();
    if (realm.intrinsics) JsIntrinsics::set_current(realm.intrinsics);
    struct Restore { JsIntrinsics* p; ~Restore() { JsIntrinsics::set_current(p); } } guard{prev};

    if (!part.dcl_fired) {
        part.dcl_fired = true;
        JsValue document = wrap(realm, realm.document);
        if (realm.document->ready_state() != "interactive" && realm.document->ready_state() != "complete") {
            realm.document->set_ready_state("interactive");
            fire(realm, document, "readystatechange");
        }
        fire(realm, document, "DOMContentLoaded", true, false);
    }

    part.pending_window_load = true;
    finish_window_load_if_ready(realm);
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
    part.ids.clear();
    part.ids_version = 0;
    part.dcl_fired = false;
    part.load_fired = false;
    part.pending_window_load = false;
    part.delaying_load_count = 0;
    part.on_window_load_complete = nullptr;
    part.child_frames.clear();
    if (document) {
        document->set_has_window(true);
        document->set_browsing_realm_id(realm.id);
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

Realm* browsing_realm_of(const Document& document) {
    Realm* r = find_realm(document.browsing_realm_id());
    if (!r || !r->alive || r->document.get() != &document || !r->window.is_object()) return nullptr;
    return r;
}

bool same_effective_origin(const Realm& a, const Realm& b) {
    if (&a == &b) return true;
    if (!a.document || !b.document) return false;
    if (a.document->has_opaque_origin() || b.document->has_opaque_origin()) return false;
    auto eff_str = [](const Document& d) -> const std::string& {
        const std::string& u = d.url();
        if ((u == "about:blank" || u == "about:srcdoc" || u.rfind("data:", 0) == 0) &&
            !d.inherited_origin_url().empty()) {
            return d.inherited_origin_url();
        }
        return u;
    };
    const std::string& ua = eff_str(*a.document);
    const std::string& ub = eff_str(*b.document);
    if (ua == ub) return true;
    Url pa, pb;
    if (!Url::parse(ua, nullptr, pa) || !Url::parse(ub, nullptr, pb)) return false;
    if (pa.scheme == "file" && pb.scheme == "file") return true;
    const std::string oa = pa.origin();
    return oa != "null" && oa == pb.origin();
}

void note_iframe_connected(Realm& realm, Element& iframe, bool connected) {
    if (!realm.alive) return;
    WindowPart& part = realm.part<WindowPart>();

    if (!connected) {
        auto it = part.child_frames.find(&iframe);
        if (it == part.child_frames.end()) return;
        ++it->second.navigation_generation;
        if (it->second.interpreter) {
            const JsValue& w = it->second.interpreter->global_object();
            if (w.is_object() && w.object_val) {
                w.object_val->properties["closed"] = boolean(true);
            }
        }
        iframe.set_content_document(nullptr);
        const bool was_delaying = it->second.delaying_parent_load;
        part.child_frames.erase(it);
        if (was_delaying) {
            if (part.delaying_load_count > 0) --part.delaying_load_count;
            finish_window_load_if_ready(realm);
        }
        return;
    }

    // Only an `<iframe>` connected to an active browsing-context Document gets a
    // nested browsing context (never inside `<template>` or `DOMParser`).
    if (!realm.window.is_object() || !realm.document || !iframe.is_connected()) return;
    if (document_of(realm, iframe) != realm.document) return;
    if (part.frame_depth >= kMaxNestedFrameDepth) return;
    if (part.child_frames.count(&iframe) != 0) return;

    const IframeSandboxFlags sandbox = parse_iframe_sandbox(iframe);
    auto init_doc = create_blank_frame_document(realm, iframe, sandbox, "about:blank");

    JsIntrinsics* prev_intrinsics = &JsIntrinsics::get();
    auto child_interp = std::make_shared<JsInterpreter>(init_doc);
    JsIntrinsics::set_current(prev_intrinsics);

    Realm& child_realm = child_interp->dom_realm();
    WindowPart& child_part = child_realm.part<WindowPart>();
    child_part.parent_realm_id = realm.id;
    child_part.frame_element = shared(iframe);
    child_part.frame_depth = part.frame_depth + 1;
    if (const std::string* nm = iframe.attribute("name"); nm && !nm->empty()) {
        child_part.name = *nm;
    }

    iframe.set_content_document(init_doc);

    ChildFrameEntry entry;
    entry.element = shared(iframe);
    entry.interpreter = std::move(child_interp);
    part.child_frames.emplace(&iframe, std::move(entry));

    schedule_iframe_navigation(realm, iframe, true);
}

void note_iframe_attribute_changed(Realm& realm, Element& iframe, const std::string& name) {
    if (!realm.alive || !realm.window.is_object() || !realm.document || !iframe.is_connected()) return;
    WindowPart& part = realm.part<WindowPart>();
    auto it = part.child_frames.find(&iframe);
    if (it == part.child_frames.end() || !it->second.interpreter) return;

    if (name == "name") {
        WindowPart& child_part = it->second.interpreter->dom_realm().part<WindowPart>();
        if (!child_part.name_explicitly_set) {
            const std::string* nm = iframe.attribute("name");
            child_part.name = nm ? *nm : std::string();
        }
        return;
    }

    if (name == "srcdoc") {
        schedule_iframe_navigation(realm, iframe, false);
        return;
    }

    if (name == "src") {
        // When `srcdoc` is present, `src` changes are ignored until `srcdoc` is removed.
        if (iframe.has_attribute("srcdoc")) return;
        schedule_iframe_navigation(realm, iframe, false);
    }
}

JsValue iframe_content_window(Realm& caller_realm, Element& iframe) {
    if (!iframe.is_connected()) return null();
    Realm* owner_realm = realm_if_any(iframe);
    if (!owner_realm || !owner_realm->alive) owner_realm = &caller_realm;
    WindowPart* part = owner_realm->find_part<WindowPart>();
    if (!part) return null();
    auto it = part->child_frames.find(&iframe);
    if (it == part->child_frames.end() || !it->second.interpreter) return null();
    const JsValue& w = it->second.interpreter->global_object();
    return w.is_object() ? w : null();
}

JsValue iframe_content_document(Realm& caller_realm, Element& iframe) {
    if (!iframe.is_connected()) return null();
    Realm* owner_realm = realm_if_any(iframe);
    if (!owner_realm || !owner_realm->alive) owner_realm = &caller_realm;
    WindowPart* part = owner_realm->find_part<WindowPart>();
    if (!part) return null();
    auto it = part->child_frames.find(&iframe);
    if (it == part->child_frames.end() || !it->second.interpreter) return null();
    Realm& child_realm = it->second.interpreter->dom_realm();
    Realm& effective_caller = caller_realm_or(caller_realm);
    if (!same_effective_origin(effective_caller, child_realm) || !child_realm.document) return null();
    return wrap(child_realm, child_realm.document);
}

void setup_image_bitmap(Iface& I) {
    I.getter("width", [](Realm&) {
        auto& st = this_state<ImageBitmapState>("ImageBitmap");
        return num(static_cast<double>(st.closed ? 0 : st.width));
    });
    I.getter("height", [](Realm&) {
        auto& st = this_state<ImageBitmapState>("ImageBitmap");
        return num(static_cast<double>(st.closed ? 0 : st.height));
    });
    I.method("close", 0, [](Realm&, Args) {
        auto& st = this_state<ImageBitmapState>("ImageBitmap");
        st.closed = true;
        st.width = 0;
        st.height = 0;
        st.pixels.clear();
        return undefined();
    });
}

void register_window_interfaces() {
    register_interface({"Window", "EventTarget", setup_window, nullptr, 0, nullptr});
    register_interface({"ImageBitmap", nullptr, setup_image_bitmap, nullptr, 0, nullptr});
    register_interface({"Location", nullptr, setup_location, nullptr, 0, nullptr});
    register_interface({"MessageChannel", nullptr, setup_message_channel, construct_message_channel, 0, nullptr});
    register_interface({"MessagePort", "EventTarget", setup_message_port, nullptr, 0, nullptr});
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
