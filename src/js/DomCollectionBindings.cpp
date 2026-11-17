// The list-like objects of the DOM: NodeList and HTMLCollection (live or
// fixed), DOMTokenList, DOMStringMap, NamedNodeMap and Attr.
//
// None of them stores what it shows. Each is a view computed from the tree
// when it is asked: `list[3]`, `list.length`, `"x" in dataset`.

#include "litebird/js/DomBindings.hpp"
#include "litebird/xml/XmlDom.hpp"
#include "litebird/js/JsBuiltins.hpp"

#include <algorithm>

namespace litebird {
namespace dom {

namespace {

// ---------------------------------------------------------------------------
// Iteration shared by the lists
// ---------------------------------------------------------------------------

/// Item `index` of the list `self`; false past the end. Throws when `self`
/// is not the right kind of object.
using IndexFn = bool (*)(Realm&, const JsValue& self, std::size_t index, JsValue& out);

void define_list_iteration(Iface& I, IndexFn at, bool with_pairs) {
    if (with_pairs) {
        // Web IDL's indexed-value iterables (NodeList and DOMTokenList) use
        // the Array prototype functions themselves, not lookalike wrappers.
        // Those functions read length and indexed properties on the original
        // receiver, so a DOM list remains live while its iterator advances.
        JsValue array = js_object_value(I.realm.intrinsics->array_proto);
        for (const char* name : {"values", "keys", "entries", "forEach"}) {
            define_method_value(I.proto, name, array.get_property(name));
        }
        define_method_value(I.proto, kIteratorKey, array.get_property(kIteratorKey), PropertyMap::Hidden);
        return;
    }
    auto values = make_function(I.realm, "values", 0, [at](Realm& r, Args) -> JsValue {
        const JsValue self = JsValue::current_this();
        JsValue probe;
        at(r, self, 0, probe);                  // checks the receiver
        auto index = std::make_shared<std::size_t>(0);
        Realm* realm = &r;
        return js_make_iterator([realm, self, index, at](JsValue& value) {
            if (!realm->alive || !at(*realm, self, *index, value)) return false;
            ++*index;
            return true;
        }, {self});
    });
    define_method_value(I.proto, kIteratorKey, values, PropertyMap::Hidden);
    if (!with_pairs) return;
    define_method_value(I.proto, "values", values);
    I.method("keys", 0, [at](Realm& r, Args) -> JsValue {
        const JsValue self = JsValue::current_this();
        JsValue probe;
        at(r, self, 0, probe);
        auto index = std::make_shared<std::size_t>(0);
        Realm* realm = &r;
        return js_make_iterator([realm, self, index, at](JsValue& value) {
            JsValue ignored;
            if (!realm->alive || !at(*realm, self, *index, ignored)) return false;
            value = num(static_cast<double>((*index)++));
            return true;
        }, {self});
    });
    I.method("entries", 0, [at](Realm& r, Args) -> JsValue {
        const JsValue self = JsValue::current_this();
        JsValue probe;
        at(r, self, 0, probe);
        auto index = std::make_shared<std::size_t>(0);
        Realm* realm = &r;
        return js_make_iterator([realm, self, index, at](JsValue& value) {
            JsValue item;
            if (!realm->alive || !at(*realm, self, *index, item)) return false;
            value = JsValue::make_array({num(static_cast<double>(*index)), item});
            ++*index;
            return true;
        }, {self});
    });
    I.method("forEach", 1, [at](Realm& r, Args args) -> JsValue {
        const JsValue self = JsValue::current_this();
        require_args(args, 1, "forEach");
        const JsValue callback = args[0];
        if (!callback.is_function()) js_throw_type_error("Failed to execute 'forEach': parameter 1 is not of type 'Function'.");
        const JsValue this_arg = js_arg(args, 1);
        JsValue item;
        for (std::size_t i = 0; at(r, self, i, item); ++i) {
            js_call(callback, this_arg, {item, num(static_cast<double>(i)), self});
        }
        return undefined();
    });
}

// ---------------------------------------------------------------------------
// NodeList and HTMLCollection
// ---------------------------------------------------------------------------

struct ListState : JsExotic {
    Realm* realm{nullptr};
    /// The node a live list was made for; null for a fixed list.
    std::shared_ptr<Node> root;
    std::function<void(Node&, NodeVector&)> compute;
    /// The contents: fixed, or what `compute` gave at `version`.
    NodeVector nodes;
    uint64_t version{0};
    /// HTMLCollection and its relatives also answer names.
    bool named{false};
    /// Set for collections that answer names their own way.
    NamedGetter named_getter;

    bool lookup_name(const std::string& name, JsValue& out) {
        if (named_getter) return named_getter(items(), name, out);
        auto node = named_item(name);
        if (!node || !realm->alive) return false;
        out = wrap(*realm, node);
        return true;
    }

    const NodeVector& items() {
        if (compute && version != Node::tree_version()) {
            nodes.clear();
            compute(*root, nodes);
            version = Node::tree_version();
        }
        return nodes;
    }

    std::shared_ptr<Node> named_item(const std::string& name) {
        if (name.empty()) return nullptr;
        for (const auto& node : items()) {
            if (!node->is_element()) continue;
            const auto& element = static_cast<const Element&>(*node);
            if (const std::string* id = element.attribute("id")) {
                if (*id == name) return node;
            }
            if (!element.is_foreign()) {
                if (const std::string* other = element.attribute("name")) {
                    if (*other == name) return node;
                }
            }
        }
        return nullptr;
    }

    bool get(JsObject*, const std::string& name, JsValue& out) override {
        std::size_t index = 0;
        if (!parse_index(name, index)) return false;
        const NodeVector& list = items();
        if (index >= list.size() || !realm->alive) return false;
        out = wrap(*realm, list[index]);
        return true;
    }
    bool get_last(JsObject*, const std::string& name, JsValue& out) override {
        if (!named || !realm->alive || name.empty() || name[0] == '%' || name[0] == '@') return false;
        return lookup_name(name, out);
    }
    bool has(JsObject*, const std::string& name) override {
        std::size_t index = 0;
        if (parse_index(name, index)) return index < items().size();
        JsValue ignored;
        return named && realm->alive && !name.empty() && name[0] != '%' && name[0] != '@' && lookup_name(name, ignored);
    }
    bool set(JsObject*, const std::string& name, const JsValue&) override {
        std::size_t index = 0;
        return parse_index(name, index);        // the items cannot be assigned
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        const std::size_t count = items().size();
        for (std::size_t i = 0; i < count; ++i) out.push_back(std::to_string(i));
    }
    /// The indices, then the names: an HTMLCollection's named properties do
    /// not enumerate, but they are own properties, and
    /// `Object.getOwnPropertyNames` lists them (`[LegacyUnenumerableNamedProperties]`).
    void own_keys(JsObject*, std::vector<std::string>& out) override {
        keys(nullptr, out);
        if (!named) return;
        std::vector<std::string> names;
        for (const auto& node : items()) {
            if (!node->is_element()) continue;
            const auto& element = static_cast<const Element&>(*node);
            if (const std::string* id = element.attribute("id")) {
                if (!id->empty() && std::find(names.begin(), names.end(), *id) == names.end()) names.push_back(*id);
            }
            // `name` counts on an element of the HTML namespace only, and in
            // an HTML document it counts in lower case.
            if (!element.is_foreign()) {
                if (const std::string* name = element.attribute("name")) {
                    std::string value = *name;
                    if (is_html_in_html_document(element)) value = ascii_lower(value);
                    if (!value.empty() && std::find(names.begin(), names.end(), value) == names.end()) {
                        names.push_back(value);
                    }
                }
            }
        }
        for (auto& name : names) out.push_back(std::move(name));
    }
    bool computed_property(JsObject*, const std::string& name, bool& enumerable, bool& writable) override {
        std::size_t index = 0;
        if (parse_index(name, index)) {
            if (index >= items().size()) return false;
            enumerable = true;
            writable = false;       // a written index is refused, not stored
            return true;
        }
        if (!named) return false;
        JsValue ignored;
        if (!lookup_name(name, ignored)) return false;
        enumerable = false;         // [LegacyUnenumerableNamedProperties]
        writable = false;
        return true;
    }
    void trace(Heap& heap) override {
        trace_node(heap, root.get());
        for (const auto& node : nodes) trace_node(heap, node.get());
    }
};

ListState& this_list() {
    return this_state<ListState>("NodeList");
}

bool list_item_at(Realm& r, const JsValue& self, std::size_t index, JsValue& out) {
    ListState* state = state_of<ListState>(self);
    if (!state) throw_illegal_invocation("NodeList");
    const NodeVector& list = state->items();
    if (index >= list.size()) return false;
    out = wrap(r, list[index]);
    return true;
}

void setup_node_list(Iface& I) {
    I.getter("length", [](Realm&) { return num(static_cast<double>(this_list().items().size())); });
    I.method("item", 1, [](Realm& r, Args args) {
        require_args(args, 1, "NodeList.item");
        const NodeVector& list = this_list().items();
        const std::size_t index = to_ulong(args[0]);
        return index < list.size() ? wrap(r, list[index]) : null();
    });
    define_list_iteration(I, &list_item_at, true);
}

void setup_html_collection(Iface& I) {
    I.getter("length", [](Realm&) { return num(static_cast<double>(this_list().items().size())); });
    I.method("item", 1, [](Realm& r, Args args) {
        require_args(args, 1, "HTMLCollection.item");
        const NodeVector& list = this_list().items();
        const std::size_t index = to_ulong(args[0]);
        return index < list.size() ? wrap(r, list[index]) : null();
    });
    I.method("namedItem", 1, [](Realm&, Args args) {
        require_args(args, 1, "HTMLCollection.namedItem");
        JsValue out;
        return this_list().lookup_name(to_str(args[0]), out) ? out : null();
    });
    define_list_iteration(I, &list_item_at, false);
}

bool is_collection_interface(Realm& realm, const char* iface) {
    // Anything that inherits from HTMLCollection answers names.
    for (const InterfaceDef* def = find_interface(iface); def; def = def->parent ? find_interface(def->parent) : nullptr) {
        if (std::string(def->name) == "HTMLCollection") return true;
    }
    (void)realm;
    return false;
}

} // namespace

JsValue make_live_list(Realm& realm, const char* iface, const std::shared_ptr<Node>& root,
                       std::function<void(Node& root, NodeVector& out)> compute) {
    auto state = std::make_shared<ListState>();
    state->realm = &realm;
    state->root = root;
    state->compute = std::move(compute);
    state->named = is_collection_interface(realm, iface);
    return realm.make(iface, state);
}

JsValue make_static_list(Realm& realm, NodeVector nodes, const char* iface) {
    auto state = std::make_shared<ListState>();
    state->realm = &realm;
    state->nodes = std::move(nodes);
    state->named = is_collection_interface(realm, iface);
    return realm.make(iface, state);
}

void set_list_named_getter(const JsValue& list, NamedGetter getter) {
    if (ListState* state = state_of<ListState>(list)) state->named_getter = std::move(getter);
}

bool list_nodes(const JsValue& list, NodeVector& out) {
    ListState* state = state_of<ListState>(list);
    if (!state) return false;
    out = state->items();
    return true;
}

// ---------------------------------------------------------------------------
// DOMTokenList
// ---------------------------------------------------------------------------

namespace {

struct TokenListState : JsExotic {
    Realm* realm{nullptr};
    std::shared_ptr<Element> element;
    std::string attribute;
    std::vector<std::string> supported;

    std::vector<std::string> tokens() const {
        std::vector<std::string> out;
        const std::string* text = element->attribute(attribute);
        if (!text) return out;
        for (auto& token : split_tokens(*text)) {
            if (std::find(out.begin(), out.end(), token) == out.end()) out.push_back(std::move(token));
        }
        return out;
    }
    void update(const std::vector<std::string>& list) {
        if (list.empty() && !element->has_attribute(attribute)) return;
        std::string text;
        for (const auto& token : list) {
            if (!text.empty()) text += ' ';
            text += token;
        }
        element->set_attribute(attribute, text);
    }
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        std::size_t index = 0;
        if (!parse_index(name, index)) return false;
        const auto list = tokens();
        if (index >= list.size()) return false;
        out = str(list[index]);
        return true;
    }
    bool has(JsObject*, const std::string& name) override {
        std::size_t index = 0;
        return parse_index(name, index) && index < tokens().size();
    }
    bool set(JsObject*, const std::string& name, const JsValue&) override {
        std::size_t index = 0;
        return parse_index(name, index);
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        const std::size_t count = tokens().size();
        for (std::size_t i = 0; i < count; ++i) out.push_back(std::to_string(i));
    }
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

TokenListState& this_tokens() {
    return this_state<TokenListState>("DOMTokenList");
}

std::string checked_token(Realm& realm, const JsValue& value) {
    std::string token = to_str(value);
    if (token.empty()) throw_dom(realm, "SyntaxError", "The token provided must not be empty.");
    for (char c : token) {
        if (is_ascii_space(c)) {
            throw_dom(realm, "InvalidCharacterError", "The token provided ('" + token + "') contains HTML space characters, which are not valid in tokens.");
        }
    }
    return token;
}

bool token_at(Realm&, const JsValue& self, std::size_t index, JsValue& out) {
    TokenListState* state = state_of<TokenListState>(self);
    if (!state) throw_illegal_invocation("DOMTokenList");
    const auto list = state->tokens();
    if (index >= list.size()) return false;
    out = str(list[index]);
    return true;
}

void setup_token_list(Iface& I) {
    I.getter("length", [](Realm&) { return num(static_cast<double>(this_tokens().tokens().size())); });
    I.accessor("value",
        [](Realm&) {
            TokenListState& state = this_tokens();
            return str(state.element->get_attribute(state.attribute));
        },
        [](Realm&, const JsValue& value) {
            TokenListState& state = this_tokens();
            state.element->set_attribute(state.attribute, to_str(value));
        });
    I.method("toString", 0, [](Realm&, Args) {
        TokenListState& state = this_tokens();
        return str(state.element->get_attribute(state.attribute));
    });
    I.method("item", 1, [](Realm&, Args args) {
        require_args(args, 1, "DOMTokenList.item");
        const auto list = this_tokens().tokens();
        const std::size_t index = to_ulong(args[0]);
        return index < list.size() ? str(list[index]) : null();
    });
    I.method("contains", 1, [](Realm&, Args args) {
        require_args(args, 1, "DOMTokenList.contains");
        const auto list = this_tokens().tokens();
        return boolean(std::find(list.begin(), list.end(), to_str(args[0])) != list.end());
    });
    I.method("add", 0, [](Realm& r, Args args) {
        TokenListState& state = this_tokens();
        std::vector<std::string> wanted;
        for (const JsValue& arg : args) wanted.push_back(checked_token(r, arg));
        auto list = state.tokens();
        for (auto& token : wanted) {
            if (std::find(list.begin(), list.end(), token) == list.end()) list.push_back(std::move(token));
        }
        state.update(list);
        return undefined();
    });
    I.method("remove", 0, [](Realm& r, Args args) {
        TokenListState& state = this_tokens();
        std::vector<std::string> unwanted;
        for (const JsValue& arg : args) unwanted.push_back(checked_token(r, arg));
        auto list = state.tokens();
        for (const auto& token : unwanted) list.erase(std::remove(list.begin(), list.end(), token), list.end());
        state.update(list);
        return undefined();
    });
    I.method("toggle", 1, [](Realm& r, Args args) {
        require_args(args, 1, "DOMTokenList.toggle");
        TokenListState& state = this_tokens();
        const std::string token = checked_token(r, args[0]);
        const bool has_force = args.size() > 1 && !args[1].is_undefined();
        const bool force = has_force && to_bool(args[1]);
        auto list = state.tokens();
        auto found = std::find(list.begin(), list.end(), token);
        if (found != list.end()) {
            if (has_force && force) return boolean(true);
            list.erase(found);
            state.update(list);
            return boolean(false);
        }
        if (has_force && !force) return boolean(false);
        list.push_back(token);
        state.update(list);
        return boolean(true);
    });
    I.method("replace", 2, [](Realm& r, Args args) {
        require_args(args, 2, "DOMTokenList.replace");
        TokenListState& state = this_tokens();
        const std::string old_token = checked_token(r, args[0]);
        const std::string new_token = checked_token(r, args[1]);
        auto list = state.tokens();
        auto found = std::find(list.begin(), list.end(), old_token);
        if (found == list.end()) return boolean(false);
        auto existing = std::find(list.begin(), list.end(), new_token);
        if (existing != list.end() && existing != found) {
            // The new token is there already: the old one just goes, and the
            // first of the two places is the one that is kept.
            if (existing < found) {
                list.erase(found);
            } else {
                *found = new_token;
                list.erase(existing);
            }
        } else {
            *found = new_token;
        }
        state.update(list);
        return boolean(true);
    });
    I.method("supports", 1, [](Realm&, Args args) {
        require_args(args, 1, "DOMTokenList.supports");
        TokenListState& state = this_tokens();
        if (state.supported.empty()) js_throw_type_error("DOMTokenList has no supported tokens.");
        const std::string token = ascii_lower(to_str(args[0]));
        return boolean(std::find(state.supported.begin(), state.supported.end(), token) != state.supported.end());
    });
    define_list_iteration(I, &token_at, true);
}

} // namespace

JsValue make_token_list(Realm& realm, Element& element, const std::string& attribute, std::vector<std::string> supported) {
    auto state = std::make_shared<TokenListState>();
    state->realm = &realm;
    state->element = shared(element);
    state->attribute = attribute;
    state->supported = std::move(supported);
    return realm.make("DOMTokenList", state);
}

// ---------------------------------------------------------------------------
// DOMStringMap: element.dataset
// ---------------------------------------------------------------------------

namespace {

// "fooBar" -> "data-foo-bar". False for a name that cannot be written.
bool dataset_attribute_name(const std::string& property, std::string& out, bool& invalid) {
    invalid = false;
    out = "data-";
    for (std::size_t i = 0; i < property.size(); ++i) {
        const char c = property[i];
        if (c == '-' && i + 1 < property.size() && property[i + 1] >= 'a' && property[i + 1] <= 'z') {
            invalid = true;
            return false;
        }
        if (c >= 'A' && c <= 'Z') {
            out += '-';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return true;
}

// "data-foo-bar" -> "fooBar". False for an attribute that is not a data attribute.
bool dataset_property_name(const std::string& attribute, std::string& out) {
    if (attribute.size() < 5 || attribute.compare(0, 5, "data-") != 0) return false;
    for (char c : attribute) {
        if (c >= 'A' && c <= 'Z') return false;
    }
    out.clear();
    for (std::size_t i = 5; i < attribute.size(); ++i) {
        const char c = attribute[i];
        if (c == '-' && i + 1 < attribute.size() && attribute[i + 1] >= 'a' && attribute[i + 1] <= 'z') {
            out += static_cast<char>(attribute[i + 1] - 'a' + 'A');
            ++i;
        } else {
            out += c;
        }
    }
    return true;
}

// Names the engine uses for its own bookkeeping. The empty name is not one:
// `data-=""` is the property `dataset['']`.
bool is_internal_name(const std::string& name) {
    return !name.empty() && (name[0] == '%' || (name.size() > 1 && name[0] == '@' && name[1] == '@'));
}

struct DatasetState : JsExotic {
    Realm* realm{nullptr};
    std::shared_ptr<Element> element;

    bool get(JsObject*, const std::string& name, JsValue& out) override {
        if (is_internal_name(name)) return false;
        std::string attribute;
        bool invalid = false;
        if (!dataset_attribute_name(name, attribute, invalid)) return false;
        const std::string* value = element->attribute(attribute);
        if (!value) return false;
        out = str(*value);
        return true;
    }
    bool has(JsObject* self, const std::string& name) override {
        JsValue ignored;
        return get(self, name, ignored);
    }
    bool set(JsObject*, const std::string& name, const JsValue& value) override {
        if (is_internal_name(name)) return false;
        std::string attribute;
        bool invalid = false;
        if (!dataset_attribute_name(name, attribute, invalid)) {
            if (invalid && realm->alive) throw_dom(*realm, "SyntaxError", "'" + name + "' is not a valid property name.");
            return true;
        }
        const std::string text = to_str(value);
        if (realm->alive) validate_attribute_name(*realm, attribute);
        element->set_attribute(attribute, text);
        return true;
    }
    bool remove(JsObject*, const std::string& name, bool& result) override {
        if (is_internal_name(name)) return false;
        std::string attribute;
        bool invalid = false;
        result = true;
        if (dataset_attribute_name(name, attribute, invalid)) element->remove_attribute(attribute);
        return true;
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        for (const auto& attribute : element->attributes()) {
            std::string property;
            if (dataset_property_name(attribute.name, property)) out.push_back(property);
        }
    }
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

void setup_dataset(Iface&) {}

} // namespace

JsValue make_dataset(Realm& realm, Element& element) {
    auto state = std::make_shared<DatasetState>();
    state->realm = &realm;
    state->element = shared(element);
    return realm.make("DOMStringMap", state);
}

// ---------------------------------------------------------------------------
// Attr and NamedNodeMap
// ---------------------------------------------------------------------------

namespace {

struct AttrState : JsExotic {
    Realm* realm{nullptr};
    /// The element the attribute belongs to; null for one that was removed
    /// or has not been set on an element yet.
    std::shared_ptr<Element> owner;
    /// The qualified name, the namespace it stands in (null for none) and
    /// where the local name starts. Those three are the attribute's identity:
    /// an element may have two attributes with one qualified name when their
    /// namespaces differ, and each has its own object.
    std::string name;
    const std::string* ns{nullptr};
    std::uint32_t local_at{0};
    /// The value while the attribute belongs to no element.
    std::string detached_value;
    std::shared_ptr<Document> document;

    std::string local_name() const { return name.substr(local_at); }
    std::string namespace_uri() const { return ns ? *ns : std::string(); }
    /// The attribute on the element, while it is on one.
    const Attribute* attached() const {
        if (!owner) return nullptr;
        return owner->find_attribute_ns(namespace_uri(), local_name());
    }
    std::string value() const {
        if (const Attribute* attribute = attached()) return attribute->value;
        return detached_value;
    }
    void set_value(const std::string& text) {
        if (const Attribute* attribute = attached()) owner->set_attribute_value(*attribute, text);
        else detached_value = text;
    }
    /// The document it belongs to: its element's, which is what adoption
    /// changes, or the one it was created in while it is on none.
    std::shared_ptr<Document> owner_document() const {
        if (owner) return owner->owner_document();
        return document;
    }
    void trace(Heap& heap) override {
        trace_node(heap, owner.get());
        trace_node(heap, document.get());
    }
};

AttrState& this_attr() {
    return this_state<AttrState>("Attr");
}

JsValue new_attr(Realm& realm, const std::shared_ptr<Element>& owner, const std::string& name,
                 const std::string* ns, std::uint32_t local_at) {
    auto state = std::make_shared<AttrState>();
    state->realm = &realm;
    state->owner = owner;
    state->name = name;
    state->ns = ns;
    state->local_at = local_at;
    if (owner) state->document = owner->owner_document();
    else state->document = realm.document;
    return realm.make("Attr", state);
}

// The prefix an attribute's qualified name starts with: the part before the
// colon, and only when it has a namespace to put it in.
std::string attr_prefix(const AttrState& state) {
    if (!state.ns || state.local_at == 0) return std::string();
    return state.name.substr(0, state.local_at - 1);
}

void setup_attr(Iface& I) {
    I.getter("name", [](Realm&) { return str(this_attr().name); });
    I.getter("localName", [](Realm&) { return str(this_attr().local_name()); });
    I.getter("nodeName", [](Realm&) { return str(this_attr().name); });
    I.getter("nodeType", [](Realm&) {
        this_attr();
        return num(2);
    });
    I.getter("namespaceURI", [](Realm&) {
        AttrState& state = this_attr();
        return state.ns ? str(*state.ns) : null();
    });
    I.getter("prefix", [](Realm&) {
        const std::string prefix = attr_prefix(this_attr());
        return prefix.empty() ? null() : str(prefix);
    });
    I.getter("specified", [](Realm&) {
        this_attr();
        return boolean(true);
    });
    I.getter("ownerElement", [](Realm& r) { return wrap(r, this_attr().owner); });
    I.getter("ownerDocument", [](Realm& r) { return wrap(r, this_attr().owner_document()); });
    I.getter("parentNode", [](Realm&) {
        this_attr();
        return null();
    });
    I.getter("parentElement", [](Realm&) {
        this_attr();
        return null();
    });
    I.getter("isConnected", [](Realm&) {
        AttrState& state = this_attr();
        return boolean(state.owner && state.owner->is_connected());
    });
    auto get_value = [](Realm&) { return str(this_attr().value()); };
    auto set_value = [](Realm&, const JsValue& value) {
        AttrState& state = this_attr();
        state.set_value(to_str_null_empty(value));
    };
    I.accessor("value", get_value, set_value);
    I.accessor("nodeValue", get_value, set_value);
    I.accessor("textContent", get_value, set_value);

    // The Node members an attribute answers on its own: the standard's
    // "cloning steps", identity, and the namespace lookup (an attribute has
    // no children, so a prefix is found through the element it belongs to).
    I.method("cloneNode", 0, [](Realm& r, Args) -> JsValue {
        // "Cloning steps" for an Attr: a new attribute with the same
        // namespace, prefix, local name and value, owned by no element.
        AttrState& state = this_attr();
        JsValue copy = new_attr(r, nullptr, state.name, state.ns, state.local_at);
        if (AttrState* made = state_of<AttrState>(copy)) {
            made->detached_value = state.value();
            made->document = r.document;
        }
        return copy;
    });
    I.method("isSameNode", 1, [](Realm&, Args args) -> JsValue {
        this_attr();
        return boolean(strict_equals(JsValue::current_this(), args[0]));
    });
    I.method("lookupNamespaceURI", 1, [](Realm&, Args args) -> JsValue {
        AttrState& state = this_attr();
        // An attribute on no element locates nothing; one on an element asks
        // the element, which is where its declarations are.
        if (!state.owner) return null();
        const std::string prefix = args[0].is_nullish() ? std::string() : to_str(args[0]);
        const std::string uri = xml::locate_namespace(*state.owner, prefix);
        return uri.empty() ? null() : str(uri);
    });
}

struct AttributeMapState : JsExotic {
    Realm* realm{nullptr};
    std::shared_ptr<Element> element;

    bool get(JsObject*, const std::string& name, JsValue& out) override {
        std::size_t index = 0;
        if (!parse_index(name, index) || !realm->alive) return false;
        const auto& attributes = element->attributes();
        if (index >= attributes.size()) return false;
        out = attr_object(*realm, *element, attributes[index]);
        return true;
    }
    bool get_last(JsObject*, const std::string& name, JsValue& out) override {
        if (is_internal_name(name) || !realm->alive) return false;
        const Attribute* attribute = element->find_attribute(name);
        if (!attribute) return false;
        out = attr_object(*realm, *element, *attribute);
        return true;
    }
    bool has(JsObject*, const std::string& name) override {
        std::size_t index = 0;
        if (parse_index(name, index)) return index < element->attributes().size();
        return !is_internal_name(name) && element->has_attribute(name);
    }
    bool set(JsObject*, const std::string& name, const JsValue&) override {
        std::size_t index = 0;
        return parse_index(name, index);
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        const std::size_t count = element->attributes().size();
        for (std::size_t i = 0; i < count; ++i) out.push_back(std::to_string(i));
    }
    /// The indices, then the qualified names: one entry per name, in the
    /// order the attributes were added, lower-cased on an HTML element of an
    /// HTML document. Two attributes with one qualified name in different
    /// namespaces share the name, which is one own property.
    void own_keys(JsObject*, std::vector<std::string>& out) override {
        keys(nullptr, out);
        for (const auto& attribute : element->attributes()) {
            const std::string name = supported_name(attribute);
            if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
        }
    }

    /// The qualified name an attribute is reachable by, or empty when it is
    /// not one of the names this map answers to: on an HTML element of an
    /// HTML document that is the name as it is stored, which is lower case.
    std::string supported_name(const Attribute& attribute) const {
        if (is_html_in_html_document(*element) && ascii_lower(attribute.name) != attribute.name) return std::string();
        return attribute.name;
    }
    bool computed_property(JsObject*, const std::string& name, bool& enumerable, bool& writable) override {
        std::size_t index = 0;
        if (parse_index(name, index)) {
            if (index >= element->attributes().size()) return false;
            enumerable = true;
            writable = false;
            return true;
        }
        if (is_internal_name(name) || !element->has_attribute(name)) return false;
        enumerable = false;         // [LegacyUnenumerableNamedProperties]
        writable = false;
        return true;
    }
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

AttributeMapState& this_map() {
    return this_state<AttributeMapState>("NamedNodeMap");
}

bool attribute_at(Realm& r, const JsValue& self, std::size_t index, JsValue& out) {
    AttributeMapState* state = state_of<AttributeMapState>(self);
    if (!state) throw_illegal_invocation("NamedNodeMap");
    const auto& attributes = state->element->attributes();
    if (index >= attributes.size()) return false;
    out = attr_object(r, *state->element, attributes[index]);
    return true;
}

// The name an attribute is stored under: lower case on an HTML element of
// an HTML document.
std::string stored_name(const Element& element, const std::string& name) {
    return is_html_in_html_document(element) ? ascii_lower(name) : name;
}

// The attribute with this namespace and local name, lower-casing the local
// name on an HTML element of an HTML document.
const Attribute* find_named_item_ns(Realm& realm, const Element& element, const JsValue& ns,
                                    const std::string& local) {
    const std::string uri = ns.is_nullish() ? std::string() : to_str(ns);
    const Attribute* attribute = element.find_attribute_ns(uri, stored_name(element, local));
    if (!attribute && is_html_in_html_document(element) && uri.empty()) {
        attribute = element.find_attribute_ns(uri, local);
    }
    (void)realm;
    return attribute;
}

void setup_attribute_map(Iface& I) {
    I.getter("length", [](Realm&) { return num(static_cast<double>(this_map().element->attributes().size())); });
    I.method("item", 1, [](Realm& r, Args args) {
        require_args(args, 1, "NamedNodeMap.item");
        JsValue out;
        return attribute_at(r, JsValue::current_this(), to_ulong(args[0]), out) ? out : null();
    });
    I.method("getNamedItem", 1, [](Realm& r, Args args) {
        require_args(args, 1, "NamedNodeMap.getNamedItem");
        AttributeMapState& state = this_map();
        const Attribute* attribute = state.element->find_attribute(stored_name(*state.element, to_str(args[0])));
        return attribute ? attr_object(r, *state.element, *attribute) : null();
    });
    I.method("getNamedItemNS", 2, [](Realm& r, Args args) {
        require_args(args, 2, "NamedNodeMap.getNamedItemNS");
        AttributeMapState& state = this_map();
        const Attribute* attribute = find_named_item_ns(r, *state.element, args[0], to_str(args[1]));
        return attribute ? attr_object(r, *state.element, *attribute) : null();
    });
    I.method("setNamedItem", 1, [](Realm& r, Args args) {
        require_args(args, 1, "NamedNodeMap.setNamedItem");
        AttributeMapState& state = this_map();
        JsValue method = wrap(r, state.element).get_property("setAttributeNode");
        return js_call(method, wrap(r, state.element), {args[0]});
    });
    I.alias("setNamedItemNS", "setNamedItem");
    I.method("removeNamedItem", 1, [](Realm& r, Args args) {
        require_args(args, 1, "NamedNodeMap.removeNamedItem");
        AttributeMapState& state = this_map();
        const std::string name = stored_name(*state.element, to_str(args[0]));
        const Attribute* attribute = state.element->find_attribute(name);
        if (!attribute) throw_dom(r, "NotFoundError", "No item with name '" + name + "' was found.");
        JsValue attr = attr_object(r, *state.element, *attribute);
        state.element->remove_attribute(name);
        return attr;
    });
    I.method("removeNamedItemNS", 2, [](Realm& r, Args args) {
        require_args(args, 2, "NamedNodeMap.removeNamedItemNS");
        AttributeMapState& state = this_map();
        const Attribute* attribute = find_named_item_ns(r, *state.element, args[0], to_str(args[1]));
        if (!attribute) {
            throw_dom(r, "NotFoundError", "No item with name '" + to_str(args[1]) + "' was found.");
        }
        JsValue attr = attr_object(r, *state.element, *attribute);
        state.element->remove_attribute_ns(attribute->namespace_uri(), std::string(attribute->local_name()));
        return attr;
    });
    define_list_iteration(I, &attribute_at, false);
}

} // namespace

JsValue make_attribute_map(Realm& realm, Element& element) {
    auto state = std::make_shared<AttributeMapState>();
    state->realm = &realm;
    state->element = shared(element);
    return realm.make("NamedNodeMap", state);
}


namespace {

/// The slot an attribute's object is kept in on its element's object: the
/// expanded name, which is what makes two attributes with one qualified name
/// two objects.
std::string attr_slot_key(const std::string* ns, std::string_view local) {
    std::string key = "attr:";
    if (ns) key += *ns;
    key += '\x01';
    key += local;
    return key;
}

/// Where the local name starts in a qualified name that has a namespace:
/// after the colon. In no namespace the whole name is the local name.
std::uint32_t local_name_at(const std::string* ns, const std::string& name) {
    if (!ns) return 0;
    const auto colon = name.find(':');
    return colon == std::string::npos ? 0 : static_cast<std::uint32_t>(colon + 1);
}

} // namespace

JsValue attr_object(Realm& realm, Element& element, const Attribute& attribute) {
    JsValue owner = wrap(realm, element);
    auto keep = shared(element);
    const std::string* ns = attribute.ns;
    const std::string name = attribute.name;
    const std::uint32_t local_at = attribute.local_at;
    return cached(owner, attr_slot_key(ns, attribute.local_name()), [&]() {
        return new_attr(realm, keep, name, ns, local_at);
    });
}

void detach_attr_object(Realm& realm, Element& element, const std::string& namespace_uri,
                        const std::string& local_name, const std::string& last_value) {
    if (!element.script_object() || element.script_realm() != realm.id) return;
    const std::string* ns = namespace_uri.empty() ? nullptr : intern_namespace_name(namespace_uri);
    const std::string key = attr_slot_key(ns, local_name);
    JsValue owner = wrap(realm, element);
    const JsValue* existing = slot(owner, key);
    if (!existing) return;
    if (AttrState* state = state_of<AttrState>(*existing)) {
        state->owner.reset();
        state->document = element.owner_document();
        state->detached_value = last_value;
    }
    clear_slot(owner, key);
}

/// document.createAttribute(name), and createAttributeNS with a namespace.
bool is_attr_object(const JsValue& value) {
    return state_of<AttrState>(value) != nullptr;
}

JsValue create_attribute(Realm& realm, const std::shared_ptr<Document>& document, const std::string& name,
                         const std::string& namespace_uri) {
    const std::string* ns = namespace_uri.empty() ? nullptr : intern_namespace_name(namespace_uri);
    JsValue attr = new_attr(realm, nullptr, name, ns, local_name_at(ns, name));
    if (AttrState* state = state_of<AttrState>(attr)) state->document = document;
    return attr;
}

JsValue clone_attr_into(Realm& realm, const JsValue& attr, const std::shared_ptr<Document>& document) {
    AttrState* state = state_of<AttrState>(attr);
    if (!state) js_throw_type_error("Failed to execute 'importNode': parameter 1 is not of type 'Node'.");
    const std::string name = state->name;
    const std::string value = state->value();
    JsValue copy = new_attr(realm, nullptr, name, state->ns, state->local_at);
    if (AttrState* made = state_of<AttrState>(copy)) {
        made->detached_value = value;
        made->document = document;
    }
    return copy;
}

JsValue adopt_attr_into(const JsValue& attr, const std::shared_ptr<Document>& document) {
    AttrState* state = state_of<AttrState>(attr);
    if (!state) js_throw_type_error("Failed to execute 'adoptNode': parameter 1 is not of type 'Node'.");
    // An attribute on an element belongs to the element's document, so only a
    // detached one is adopted here, and it is the same object afterwards.
    if (!state->owner) state->document = document;
    return attr;
}

/// element.setAttributeNode(attr): returns the Attr it replaced, or null.
JsValue set_attribute_node(Realm& realm, Element& element, const JsValue& attr) {
    AttrState* state = state_of<AttrState>(attr);
    if (!state) js_throw_type_error("Failed to execute 'setAttributeNode' on 'Element': parameter 1 is not of type 'Attr'.");
    if (state->owner && state->owner.get() != &element) {
        throw_dom(realm, "InUseAttributeError", "The attribute is in use by another element.");
    }
    if (state->owner.get() == &element) return attr;
    JsValue owner = wrap(realm, element);
    JsValue previous = null();
    const std::string value = state->detached_value;
    const std::string uri = state->namespace_uri();
    const std::string local = state->local_name();
    if (const Attribute* existing = element.find_attribute_ns(uri, local)) {
        previous = attr_object(realm, element, *existing);
        detach_attr_object(realm, element, existing->namespace_uri(), std::string(existing->local_name()),
                           existing->value);
        // The attribute keeps its place in the list and its namespace, and
        // takes this one's qualified name: what `getAttribute` finds by.
        element.replace_attribute(*existing, state->name, value);
    } else {
        element.set_attribute_ns(uri, state->name, value);
    }
    state->owner = shared(element);
    state->document = element.owner_document();
    set_slot(owner, attr_slot_key(state->ns, local), attr);
    return previous;
}

/// element.removeAttributeNode(attr).
JsValue remove_attribute_node(Realm& realm, Element& element, const JsValue& attr) {
    AttrState* state = state_of<AttrState>(attr);
    if (!state) js_throw_type_error("Failed to execute 'removeAttributeNode' on 'Element': parameter 1 is not of type 'Attr'.");
    const std::string uri = state->namespace_uri();
    const std::string local = state->local_name();
    if (state->owner.get() != &element || !element.find_attribute_ns(uri, local)) {
        throw_dom(realm, "NotFoundError", "The node provided is owned by another element.");
    }
    element.remove_attribute_ns(uri, local);
    return attr;
}

void register_collection_interfaces() {
    register_interface({"NodeList", nullptr, setup_node_list, nullptr, 0, nullptr});
    register_interface({"HTMLCollection", nullptr, setup_html_collection, nullptr, 0, nullptr});
    register_interface({"DOMTokenList", nullptr, setup_token_list, nullptr, 0, nullptr});
    register_interface({"DOMStringMap", nullptr, setup_dataset, nullptr, 0, nullptr});
    register_interface({"NamedNodeMap", nullptr, setup_attribute_map, nullptr, 0, nullptr});
    register_interface({"Attr", "Node", setup_attr, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
