// Element: attributes, selectors, markup in both directions, shadow trees and
// geometry.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/Selector.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/html/HtmlParser.hpp"

#include <algorithm>
#include <cmath>

namespace litebird {
namespace dom {

namespace {

bool is_shadow_root(const Node& node) {
    return node.is_document_fragment() && dynamic_cast<const ShadowRoot*>(&node) != nullptr;
}

// ---------------------------------------------------------------------------
// Selectors
// ---------------------------------------------------------------------------

struct SelectorCache : RealmPart {
    std::unordered_map<std::string, std::shared_ptr<SelectorList>> parsed;
};

std::shared_ptr<SelectorList> parse_selector(Realm& realm, const std::string& text) {
    SelectorCache& cache = realm.part<SelectorCache>();
    auto found = cache.parsed.find(text);
    if (found != cache.parsed.end()) return found->second;
    auto list = std::make_shared<SelectorList>();
    if (!SelectorList::parse(text, *list)) {
        throw_dom(realm, "SyntaxError", "'" + text + "' is not a valid selector.");
    }
    if (cache.parsed.size() >= 512) cache.parsed.clear();
    cache.parsed[text] = list;
    return list;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

bool is_name_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' || c >= 0x80;
}

bool is_name_char(unsigned char c) {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

std::string attribute_name(const Element& element, const std::string& name) {
    return element.is_foreign() ? name : ascii_lower(name);
}

// The stored name of the attribute a namespaced lookup means. Namespaces of
// attributes are not kept; the local name decides.
const std::string* find_attribute_ns(const Element& element, const std::string& local, std::string& stored) {
    if (const std::string* value = element.attribute(local)) {
        stored = local;
        return value;
    }
    for (const auto& [name, value] : element.attributes()) {
        const auto colon = name.find(':');
        if (colon != std::string::npos && name.compare(colon + 1, std::string::npos, local) == 0) {
            stored = name;
            return &value;
        }
    }
    return nullptr;
}

std::string namespace_uri(const Element& element) {
    return namespace_uri_of(element);
}

// ---------------------------------------------------------------------------
// Markup
// ---------------------------------------------------------------------------

// The node `innerHTML` of `element` reads and writes: a template's contents.
Node& markup_target(Element& element) {
    if (!element.is_foreign() && element.tag_name() == "template") {
        if (!element.template_contents()) {
            element.set_template_contents(std::make_shared<Node>(NodeType::DocumentFragment));
        }
        return *element.template_contents();
    }
    return element;
}

enum class Where { BeforeBegin, AfterBegin, BeforeEnd, AfterEnd };

Where parse_where(Realm& realm, const std::string& text) {
    const std::string where = ascii_lower(text);
    if (where == "beforebegin") return Where::BeforeBegin;
    if (where == "afterbegin") return Where::AfterBegin;
    if (where == "beforeend") return Where::BeforeEnd;
    if (where == "afterend") return Where::AfterEnd;
    throw_dom(realm, "SyntaxError", "The value provided ('" + text + "') is not one of 'beforeBegin', 'afterBegin', 'beforeEnd', or 'afterEnd'.");
}

// Inserts `node` at `where` relative to `element`. False when there is
// nowhere to put it (no parent).
bool insert_adjacent(Realm& realm, Element& element, Where where, const std::shared_ptr<Node>& node) {
    switch (where) {
        case Where::BeforeBegin: {
            auto parent = element.parent();
            if (!parent) return false;
            pre_insert(realm, *parent, node, &element);
            return true;
        }
        case Where::AfterBegin: {
            auto first = element.first_child();
            pre_insert(realm, element, node, first.get());
            return true;
        }
        case Where::BeforeEnd:
            pre_insert(realm, element, node, nullptr);
            return true;
        case Where::AfterEnd: {
            auto parent = element.parent();
            if (!parent) return false;
            auto next = element.next_sibling();
            pre_insert(realm, *parent, node, next.get());
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// DOMRect
// ---------------------------------------------------------------------------

struct RectState : JsExotic {
    double x{0}, y{0}, width{0}, height{0};
};

RectState& this_rect() {
    return this_state<RectState>("DOMRect");
}

JsValue make_rect(Realm& realm, double x, double y, double width, double height) {
    auto state = std::make_shared<RectState>();
    state->x = x;
    state->y = y;
    state->width = width;
    state->height = height;
    return realm.make("DOMRect", state);
}

JsValue construct_rect(Realm& realm, const InterfaceDef& def, Args args, const JsValue* derived) {
    auto state = std::make_shared<RectState>();
    state->x = js_arg(args, 0).is_undefined() ? 0 : to_double(args[0]);
    state->y = js_arg(args, 1).is_undefined() ? 0 : to_double(args[1]);
    state->width = js_arg(args, 2).is_undefined() ? 0 : to_double(args[2]);
    state->height = js_arg(args, 3).is_undefined() ? 0 : to_double(args[3]);
    return realm.make(def.name, state, derived);
}

void setup_rect_read_only(Iface& I) {
    I.getter("x", [](Realm&) { return num(this_rect().x); });
    I.getter("y", [](Realm&) { return num(this_rect().y); });
    I.getter("width", [](Realm&) { return num(this_rect().width); });
    I.getter("height", [](Realm&) { return num(this_rect().height); });
    I.getter("top", [](Realm&) { const RectState& r = this_rect(); return num(std::min(r.y, r.y + r.height)); });
    I.getter("bottom", [](Realm&) { const RectState& r = this_rect(); return num(std::max(r.y, r.y + r.height)); });
    I.getter("left", [](Realm&) { const RectState& r = this_rect(); return num(std::min(r.x, r.x + r.width)); });
    I.getter("right", [](Realm&) { const RectState& r = this_rect(); return num(std::max(r.x, r.x + r.width)); });
    I.method("toJSON", 0, [](Realm&, Args) {
        const RectState& r = this_rect();
        JsValue out = JsValue::make_object();
        out.set_property("x", num(r.x));
        out.set_property("y", num(r.y));
        out.set_property("width", num(r.width));
        out.set_property("height", num(r.height));
        out.set_property("top", num(std::min(r.y, r.y + r.height)));
        out.set_property("right", num(std::max(r.x, r.x + r.width)));
        out.set_property("bottom", num(std::max(r.y, r.y + r.height)));
        out.set_property("left", num(std::min(r.x, r.x + r.width)));
        return out;
    });
}

void setup_rect(Iface& I) {
    I.accessor("x", [](Realm&) { return num(this_rect().x); },
               [](Realm&, const JsValue& v) { this_rect().x = to_double(v); });
    I.accessor("y", [](Realm&) { return num(this_rect().y); },
               [](Realm&, const JsValue& v) { this_rect().y = to_double(v); });
    I.accessor("width", [](Realm&) { return num(this_rect().width); },
               [](Realm&, const JsValue& v) { this_rect().width = to_double(v); });
    I.accessor("height", [](Realm&) { return num(this_rect().height); },
               [](Realm&, const JsValue& v) { this_rect().height = to_double(v); });
}

// ---------------------------------------------------------------------------
// Element
// ---------------------------------------------------------------------------

using El = ElementMembers<&is_element_node>;

bool can_host_shadow(const Element& element) {
    if (element.is_foreign()) return false;
    static const char* const kHosts[] = {"article", "aside", "blockquote", "body", "div", "footer", "h1", "h2",
                                         "h3", "h4", "h5", "h6", "header", "main", "nav", "p", "section", "span"};
    for (const char* tag : kHosts) {
        if (element.tag_name() == tag) return true;
    }
    return std::string(html_interface_for_tag(element.tag_name())) == "HTMLElement" &&
           element.tag_name().find('-') != std::string::npos;
}

void setup_element(Iface& I) {
    El E(I);
    E.getter("namespaceURI", [](Realm&, Element& el) {
        const std::string uri = namespace_uri_of(el);
        return uri.empty() ? null() : str(uri);
    });
    E.getter("prefix", [](Realm&, Element& el) { return el.prefix().empty() ? null() : str(el.prefix()); });
    E.getter("localName", [](Realm&, Element& el) { return str(el.tag_name()); });
    E.getter("tagName", [](Realm&, Element& el) {
        // An HTML element's name is given in upper case; any other as written.
        const std::string name = qualified_name(el);
        return str(el.is_foreign() ? name : ascii_upper(name));
    });
    E.string("id");
    E.string("className", "class");
    E.string("slot");
    E.token_list("classList", "class");
    E.getter("attributes", [](Realm& r, Element& el) {
        return cached(wrap(r, el), "attributes", [&]() { return make_attribute_map(r, el); });
    });
    E.method("hasAttributes", 0, [](Realm&, Element& el, Args) { return boolean(!el.attributes().empty()); });
    E.method("getAttributeNames", 0, [](Realm&, Element& el, Args) {
        std::vector<JsValue> names;
        for (const auto& [name, value] : el.attributes()) names.push_back(str(name));
        return JsValue::make_array(names);
    });
    E.method("getAttribute", 1, [](Realm&, Element& el, Args args) {
        require_args(args, 1, "Element.getAttribute");
        return nullable_str(el.attribute(attribute_name(el, to_str(args[0]))));
    });
    E.method("getAttributeNS", 2, [](Realm&, Element& el, Args args) {
        require_args(args, 2, "Element.getAttributeNS");
        std::string stored;
        return nullable_str(find_attribute_ns(el, to_str(args[1]), stored));
    });
    E.method("setAttribute", 2, [](Realm& r, Element& el, Args args) {
        require_args(args, 2, "Element.setAttribute");
        set_attribute_from_script(r, el, to_str(args[0]), to_str(args[1]));
        return undefined();
    });
    E.method("setAttributeNS", 3, [](Realm& r, Element& el, Args args) {
        require_args(args, 3, "Element.setAttributeNS");
        const std::string name = to_str(args[1]);
        validate_name(r, name);
        const auto colon = name.find(':');
        const bool no_namespace = args[0].is_nullish() || to_str(args[0]).empty();
        if (colon != std::string::npos && no_namespace) {
            throw_dom(r, "NamespaceError", "A prefix was given without a namespace.");
        }
        el.set_attribute(name, to_str(args[2]));
        return undefined();
    });
    E.method("removeAttribute", 1, [](Realm&, Element& el, Args args) {
        require_args(args, 1, "Element.removeAttribute");
        el.remove_attribute(attribute_name(el, to_str(args[0])));
        return undefined();
    });
    E.method("removeAttributeNS", 2, [](Realm&, Element& el, Args args) {
        require_args(args, 2, "Element.removeAttributeNS");
        std::string stored;
        if (find_attribute_ns(el, to_str(args[1]), stored)) el.remove_attribute(stored);
        return undefined();
    });
    E.method("toggleAttribute", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.toggleAttribute");
        std::string name = to_str(args[0]);
        validate_name(r, name);
        name = attribute_name(el, name);
        const bool has_force = args.size() > 1 && !args[1].is_undefined();
        const bool force = has_force && to_bool(args[1]);
        if (!el.has_attribute(name)) {
            if (has_force && !force) return boolean(false);
            el.set_attribute(name, "");
            return boolean(true);
        }
        if (has_force && force) return boolean(true);
        el.remove_attribute(name);
        return boolean(false);
    });
    E.method("hasAttribute", 1, [](Realm&, Element& el, Args args) {
        require_args(args, 1, "Element.hasAttribute");
        return boolean(el.has_attribute(attribute_name(el, to_str(args[0]))));
    });
    E.method("hasAttributeNS", 2, [](Realm&, Element& el, Args args) {
        require_args(args, 2, "Element.hasAttributeNS");
        std::string stored;
        return boolean(find_attribute_ns(el, to_str(args[1]), stored) != nullptr);
    });
    E.method("getAttributeNode", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.getAttributeNode");
        const std::string name = attribute_name(el, to_str(args[0]));
        return el.has_attribute(name) ? attr_object(r, el, name) : null();
    });
    E.method("getAttributeNodeNS", 2, [](Realm& r, Element& el, Args args) {
        require_args(args, 2, "Element.getAttributeNodeNS");
        std::string stored;
        return find_attribute_ns(el, to_str(args[1]), stored) ? attr_object(r, el, stored) : null();
    });
    E.method("setAttributeNode", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.setAttributeNode");
        return set_attribute_node(r, el, args[0]);
    });
    I.alias("setAttributeNodeNS", "setAttributeNode");
    E.method("removeAttributeNode", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.removeAttributeNode");
        return remove_attribute_node(r, el, args[0]);
    });

    // --- Shadow trees -------------------------------------------------------------
    E.method("attachShadow", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.attachShadow");
        if (!args[0].is_object_like()) js_throw_type_error("Failed to execute 'attachShadow' on 'Element': parameter 1 is not of type 'ShadowRootInit'.");
        const JsValue mode_value = args[0].get_property("mode");
        const std::string mode = mode_value.is_undefined() ? std::string() : to_str(mode_value);
        if (mode != "open" && mode != "closed") {
            js_throw_type_error("Failed to execute 'attachShadow' on 'Element': the provided mode is not 'open' or 'closed'.");
        }
        if (!can_host_shadow(el)) throw_dom(r, "NotSupportedError", "This element does not support attachShadow.");
        if (el.shadow_root()) throw_dom(r, "NotSupportedError", "Shadow root cannot be created on a host which already hosts a shadow tree.");
        auto shadow = el.attach_shadow(mode);
        if (auto document = document_of(r, el)) shadow->set_owner_document(document);
        return wrap(r, shadow);
    });
    E.getter("shadowRoot", [](Realm& r, Element& el) {
        auto shadow = el.shadow_root();
        return shadow && shadow->mode() == ShadowRootMode::Open ? wrap(r, shadow) : null();
    });
    E.getter("assignedSlot", [](Realm& r, Element& el) {
        auto parent = el.parent();
        if (!parent || !parent->is_element()) return null();
        auto shadow = static_cast<Element&>(*parent).shadow_root();
        if (!shadow || shadow->mode() != ShadowRootMode::Open) return null();
        const std::string wanted = el.get_attribute("slot");
        std::shared_ptr<Node> found;
        for_each_descendant(*shadow, [&](Node& node) {
            if (is_html_element(node, "slot") && static_cast<Element&>(node).get_attribute("name") == wanted) {
                found = node.shared_from_this();
                return false;
            }
            return true;
        });
        return wrap(r, found);
    });

    // --- Selectors ------------------------------------------------------------------
    E.method("matches", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.matches");
        return boolean(matches_selector(r, el, to_str(args[0])));
    });
    I.alias("webkitMatchesSelector", "matches");
    E.method("closest", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.closest");
        auto list = parse_selector(r, to_str(args[0]));
        for (std::shared_ptr<Node> at = el.shared_from_this(); at && at->is_element(); at = at->parent()) {
            if (list->matches(static_cast<Element&>(*at), &el)) return wrap(r, at);
        }
        return null();
    });
    E.method("getElementsByTagName", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.getElementsByTagName");
        const std::string name = to_str(args[0]);
        const std::string lower = ascii_lower(name);
        return make_live_list(r, "HTMLCollection", el.shared_from_this(), [name, lower](Node& root, NodeVector& out) {
            for_each_descendant(root, [&](Node& node) {
                if (!node.is_element()) return true;
                const auto& element = static_cast<Element&>(node);
                if (name == "*" || element.tag_name() == (element.is_foreign() ? name : lower)) out.push_back(node.shared_from_this());
                return true;
            });
        });
    });
    E.method("getElementsByTagNameNS", 2, [](Realm& r, Element& el, Args args) {
        require_args(args, 2, "Element.getElementsByTagNameNS");
        const std::string ns = args[0].is_nullish() ? std::string() : to_str(args[0]);
        const std::string name = to_str(args[1]);
        return make_live_list(r, "HTMLCollection", el.shared_from_this(), [ns, name](Node& root, NodeVector& out) {
            for_each_descendant(root, [&](Node& node) {
                if (!node.is_element()) return true;
                const auto& element = static_cast<Element&>(node);
                if ((ns == "*" || ns == namespace_uri(element)) && (name == "*" || element.tag_name() == name)) {
                    out.push_back(node.shared_from_this());
                }
                return true;
            });
        });
    });
    E.method("getElementsByClassName", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "Element.getElementsByClassName");
        const std::vector<std::string> wanted = split_tokens(to_str(args[0]));
        return make_live_list(r, "HTMLCollection", el.shared_from_this(), [wanted](Node& root, NodeVector& out) {
            if (wanted.empty()) return;
            for_each_descendant(root, [&](Node& node) {
                if (!node.is_element()) return true;
                const auto& element = static_cast<Element&>(node);
                for (const auto& name : wanted) {
                    if (!element.has_class(name)) return true;
                }
                out.push_back(node.shared_from_this());
                return true;
            });
        });
    });

    // --- Markup -------------------------------------------------------------------
    E.accessor("innerHTML",
        [](Realm&, Element& el) { return str(el.inner_markup()); },
        [](Realm& r, Element& el, const JsValue& value) { set_inner_markup(r, el, to_str_null_empty(value)); });
    E.accessor("outerHTML",
        [](Realm&, Element& el) { return str(el.outer_markup()); },
        [](Realm& r, Element& el, const JsValue& value) {
            const std::string markup = to_str_null_empty(value);
            auto parent = el.parent();
            if (!parent) return;
            if (parent->is_document()) {
                throw_dom(r, "NoModificationAllowedError", "This element's parent is of type '#document', which is not an element node.");
            }
            std::shared_ptr<Element> context;
            if (parent->is_element()) context = std::static_pointer_cast<Element>(parent);
            else context = std::make_shared<Element>("body");
            auto fragment = parse_fragment(r, *context, markup);
            auto keep = shared(el);
            auto next = el.next_sibling();
            auto previous = el.previous_sibling();
            const NodeVector added = fragment->children();
            ChildListBatch batch(r, *parent);
            parent->remove_child(keep);
            if (next) parent->insert_before(fragment, next);
            else parent->append_child(fragment);
            batch.commit(added, {keep}, previous, next);
        });
    E.method("insertAdjacentHTML", 2, [](Realm& r, Element& el, Args args) {
        require_args(args, 2, "Element.insertAdjacentHTML");
        const Where where = parse_where(r, to_str(args[0]));
        const std::string markup = to_str(args[1]);
        std::shared_ptr<Element> context;
        if (where == Where::BeforeBegin || where == Where::AfterEnd) {
            auto parent = el.parent();
            if (!parent || parent->is_document()) {
                throw_dom(r, "NoModificationAllowedError", "The element has no parent.");
            }
            if (parent->is_element()) context = std::static_pointer_cast<Element>(parent);
            else context = std::make_shared<Element>("body");
        } else {
            context = shared(el);
        }
        // In an <html> context the fragment is parsed as for a <body>.
        if (!context->is_foreign() && context->tag_name() == "html") context = std::make_shared<Element>("body");
        insert_adjacent(r, el, where, parse_fragment(r, *context, markup));
        return undefined();
    });
    E.method("insertAdjacentElement", 2, [](Realm& r, Element& el, Args args) {
        require_args(args, 2, "Element.insertAdjacentElement");
        const Where where = parse_where(r, to_str(args[0]));
        Node* node = node_ptr(args[1]);
        if (!node || !node->is_element()) {
            js_throw_type_error("Failed to execute 'insertAdjacentElement' on 'Element': parameter 2 is not of type 'Element'.");
        }
        return insert_adjacent(r, el, where, node->shared_from_this()) ? args[1] : null();
    });
    E.method("insertAdjacentText", 2, [](Realm& r, Element& el, Args args) {
        require_args(args, 2, "Element.insertAdjacentText");
        const Where where = parse_where(r, to_str(args[0]));
        auto text = std::make_shared<Text>(to_str(args[1]));
        if (auto document = document_of(r, el)) text->set_owner_document(document);
        insert_adjacent(r, el, where, text);
        return undefined();
    });

    // --- Geometry: the layout box, zero before there is one -------------------------
    E.method("getBoundingClientRect", 0, [](Realm& r, Element& el, Args) {
        const Rect box = el.bounding_box();
        return make_rect(r, box.x, box.y, box.width, box.height);
    });
    E.method("getClientRects", 0, [](Realm& r, Element& el, Args) {
        const Rect box = el.bounding_box();
        std::vector<JsValue> rects;
        if (box.width > 0 || box.height > 0) rects.push_back(make_rect(r, box.x, box.y, box.width, box.height));
        return JsValue::make_array(rects);
    });
    E.getter("clientWidth", [](Realm&, Element& el) { return num(std::round(el.bounding_box().width)); });
    E.getter("clientHeight", [](Realm&, Element& el) { return num(std::round(el.bounding_box().height)); });
    E.getter("clientTop", [](Realm&, Element&) { return num(0); });
    E.getter("clientLeft", [](Realm&, Element&) { return num(0); });
    E.getter("scrollWidth", [](Realm&, Element& el) { return num(std::round(el.bounding_box().width)); });
    E.getter("scrollHeight", [](Realm&, Element& el) { return num(std::round(el.bounding_box().height)); });
    // Nothing scrolls: the offsets stay zero whatever is assigned.
    E.accessor("scrollTop", [](Realm&, Element&) { return num(0); }, [](Realm&, Element&, const JsValue&) {});
    E.accessor("scrollLeft", [](Realm&, Element&) { return num(0); }, [](Realm&, Element&, const JsValue&) {});
    for (const char* name : {"scrollIntoView", "scroll", "scrollTo", "scrollBy", "setPointerCapture",
                             "releasePointerCapture", "requestPointerLock"}) {
        E.method(name, 0, [](Realm&, Element&, Args) { return undefined(); });
    }
    E.method("hasPointerCapture", 1, [](Realm&, Element&, Args) { return boolean(false); });

    define_parent_node(I, &is_element_node);
    define_child_node(I, &is_element_node);
    define_element_siblings(I, &is_element_node);
}

// ---------------------------------------------------------------------------
// ShadowRoot
// ---------------------------------------------------------------------------

void setup_shadow_root(Iface& I) {
    I.node_getter(&is_shadow_root, "mode", [](Realm&, Node& self) {
        return str(static_cast<ShadowRoot&>(self).mode_str());
    });
    I.node_getter(&is_shadow_root, "host", [](Realm& r, Node& self) {
        return wrap(r, static_cast<ShadowRoot&>(self).host());
    });
    I.node_getter(&is_shadow_root, "delegatesFocus", [](Realm&, Node&) { return boolean(false); });
    I.node_getter(&is_shadow_root, "slotAssignment", [](Realm&, Node&) { return str("named"); });
    I.node_accessor(&is_shadow_root, "innerHTML",
        [](Realm&, Node& self) { return str(self.inner_markup()); },
        [](Realm& r, Node& self, const JsValue& value) { set_inner_markup(r, self, to_str_null_empty(value)); });
    I.node_getter(&is_shadow_root, "activeElement", [](Realm& r, Node& self) {
        auto document = document_of(r, self);
        auto focused = document ? document->focused_element() : nullptr;
        for (std::shared_ptr<Node> at = focused; at; at = at->parent()) {
            if (at.get() == &self) return wrap(r, focused);
        }
        return null();
    });
}

} // namespace

// ---------------------------------------------------------------------------
// What the rest of the bindings use
// ---------------------------------------------------------------------------

std::shared_ptr<Element> query_first(Realm& realm, Node& root, const std::string& selector) {
    auto list = parse_selector(realm, selector);
    auto found = list->select(root, true);
    return found.empty() ? nullptr : found.front();
}

NodeVector query_all(Realm& realm, Node& root, const std::string& selector) {
    auto list = parse_selector(realm, selector);
    NodeVector out;
    for (auto& element : list->select(root, false)) out.push_back(std::move(element));
    return out;
}

bool matches_selector(Realm& realm, Element& element, const std::string& selector) {
    return parse_selector(realm, selector)->matches(element, &element);
}

void validate_name(Realm& realm, const std::string& name) {
    bool valid = !name.empty() && is_name_start(static_cast<unsigned char>(name[0]));
    for (std::size_t i = 1; valid && i < name.size(); ++i) valid = is_name_char(static_cast<unsigned char>(name[i]));
    if (!valid) throw_dom(realm, "InvalidCharacterError", "'" + name + "' is not a valid name.");
}

void set_attribute_from_script(Realm& realm, Element& element, const std::string& name, const std::string& value) {
    validate_name(realm, name);
    element.set_attribute(attribute_name(element, name), value);
}

std::shared_ptr<Node> parse_fragment(Realm& realm, Element& context, const std::string& markup) {
    auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
    auto document = document_of(realm, context);
    if (document) fragment->set_owner_document(document);
    for (const auto& node : HtmlParser::parse_inner_html(context, markup)) fragment->append_child(node);
    // Elements made from markup become instances of their definitions when
    // the call that parsed the markup returns, in the document or not.
    upgrade_subtree(realm, *fragment);
    return fragment;
}

void set_inner_markup(Realm& realm, Node& node, const std::string& markup) {
    if (auto* shadow = dynamic_cast<ShadowRoot*>(&node)) {
        // The shadow root keeps the style sheets of its markup for layout.
        const NodeVector removed = node.children();
        ChildListBatch batch(realm, node);
        shadow->set_inner_html(markup);
        if (auto document = document_of(realm, node)) {
            for (const auto& child : node.children()) child->set_owner_document(document);
        }
        batch.commit(node.children(), removed, nullptr, nullptr);
        upgrade_subtree(realm, node);
        return;
    }
    if (!node.is_element()) return;
    auto& element = static_cast<Element&>(node);
    Node& target = markup_target(element);
    auto parsed = HtmlParser::parse_inner_html(element, markup);
    if (&target != &element) {
        // A template's contents belong to a document of their own.
        if (auto document = document_of(realm, element)) {
            target.set_owner_document(document->template_document());
        }
    }
    replace_all_children(realm, target, parsed);
    upgrade_subtree(realm, target);
    element.mark_needs_layout();
}

void register_element_interfaces() {
    register_interface({"Element", "Node", setup_element, nullptr, 0, nullptr});
    register_interface({"ShadowRoot", "DocumentFragment", setup_shadow_root, nullptr, 0, nullptr});
    register_interface({"DOMRectReadOnly", nullptr, setup_rect_read_only, construct_rect, 0, nullptr});
    register_interface({"DOMRect", "DOMRectReadOnly", setup_rect, construct_rect, 0, nullptr});
}

} // namespace dom
} // namespace litebird
