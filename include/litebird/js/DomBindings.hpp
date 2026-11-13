#pragma once

// The DOM as a script sees it.
//
// The C++ tree (Node, Element, Document) knows nothing about scripts. This
// layer shows it to them the way the DOM and HTML standards describe:
//
//   - Every node has one script object per realm, for as long as a script can
//     reach it: `document.body === document.body`, and a property a script
//     puts on an element is still there the next time it finds the element.
//   - What a node can do comes from its interface. An interface is a
//     prototype object: `div` is an HTMLDivElement, whose prototype is
//     HTMLElement.prototype, then Element, Node, EventTarget. Members are
//     accessors and methods on those prototypes, so `instanceof`, subclassing,
//     `Object.getOwnPropertyDescriptor(Node.prototype, "textContent")` and
//     patching a prototype all work as they do in a browser.
//   - Misuse throws what the standard says: a TypeError for a wrong receiver
//     or a missing argument, a DOMException with the right name for a request
//     the tree cannot satisfy.
//
// How an object is tied to a node
// -------------------------------
// The object is an ordinary JsObject whose `dom_node` is the node: the object
// owns the node. The node points back with a plain pointer
// (Node::script_object), cleared when the collector frees the object. One rule
// holds throughout: a node that has an object, its parent has one too. An
// object keeps the objects of its parent and children alive (see trace_node in
// DomBindings.cpp), so holding any node of a tree keeps every object of that
// tree, and nothing a script stored on them is lost while it can still get
// there.
//
// Host objects that are not nodes (events, lists, style declarations, the
// window) carry their C++ state as the object's JsExotic. Bindings recognise
// them by the dynamic type of that state, never by a property a script could
// set.
//
// Realms
// ------
// One Realm per interpreter: its interface prototypes, its window, its custom
// element definitions. Prototypes are built the first time they are needed,
// so an interpreter that never touches the DOM pays nothing for it.

#include "litebird/dom/Comment.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace litebird {

class JsEnvironment;

namespace dom {

using Args = const std::vector<JsValue>&;

struct Realm;
class Iface;

// ---------------------------------------------------------------------------
// Interfaces
// ---------------------------------------------------------------------------

/// One interface: its name, what it inherits from, how its prototype is
/// filled in and what `new` does. The definitions are a table that is the same
/// for every realm; each realm builds its own objects from it.
struct InterfaceDef {
    const char* name;
    /// Null for an interface that inherits from Object only.
    const char* parent;
    /// Defines the members on the prototype and the statics on the constructor.
    void (*setup)(Iface&);
    /// `new Name(...)`. `derived` is the object a subclass constructor is
    /// building when this runs as `super(...)`: the result must be that same
    /// object. Null here means the interface cannot be constructed by script.
    JsValue (*construct)(Realm&, const InterfaceDef&, Args, const JsValue* derived);
    /// The constructor's `length`.
    int length;
    /// Another global name for the same constructor (`webkitURL` style
    /// aliases, `HTMLDocument`). Null for none.
    const char* alias;
    /// For families of interfaces that share one setup and one constructor
    /// (the event classes): what tells them apart.
    const void* data{nullptr};
};

void register_interface(const InterfaceDef& def);
const InterfaceDef* find_interface(const std::string& name);
/// The interface name a global identifier stands for, following aliases.
const InterfaceDef* find_interface_or_alias(const std::string& name);

/// State a part of the bindings keeps per realm (the window's history, the
/// custom element definitions, the active mutation observers).
struct RealmPart {
    virtual ~RealmPart() = default;
    /// Reports the values the part holds to the collector. Parts are not
    /// cells, so what they hold is alive anyway; this exists for parts that
    /// hold nodes and must keep those nodes' script objects.
    virtual void trace(Heap&) {}
};

struct Realm {
    uint64_t id{0};
    /// False once the interpreter that owned the realm is gone. Functions
    /// made for the realm still exist then; they do nothing.
    bool alive{true};
    JsIntrinsics* intrinsics{nullptr};

    /// The global object when the realm has a window; undefined otherwise.
    JsValue window;
    /// The document the window shows.
    std::shared_ptr<Document> document;
    std::weak_ptr<JsEnvironment> global_scope;

    /// The prototype of `name`, built on first use together with those it
    /// inherits from. Throws when there is no such interface.
    JsObject* proto(const std::string& name);
    /// Its constructor.
    JsValue ctor(const std::string& name);
    bool has_proto(const std::string& name) const { return protos.count(name) != 0; }

    /// A new object of interface `name` carrying `state`. With `derived`
    /// (see InterfaceDef::construct) that object is used instead of a new one.
    JsValue make(const char* name, std::shared_ptr<JsExotic> state = nullptr, const JsValue* derived = nullptr);

    template <typename T> T& part() {
        auto& slot = parts[std::type_index(typeid(T))];
        if (!slot) slot = std::make_unique<T>();
        return static_cast<T&>(*slot);
    }
    template <typename T> T* find_part() {
        auto it = parts.find(std::type_index(typeid(T)));
        return it == parts.end() ? nullptr : static_cast<T*>(it->second.get());
    }

    std::unordered_map<std::string, JsValue> protos;
    std::unordered_map<std::string, JsValue> ctors;
    std::unordered_map<std::type_index, std::unique_ptr<RealmPart>> parts;
};

/// The realm of `intrinsics`, made on first use.
Realm& realm_of(JsIntrinsics& intrinsics);
/// The realm of the interpreter running on this thread.
Realm& current_realm();
/// The live realm with this id, or null.
Realm* find_realm(uint64_t id);
/// The realm whose objects `node` and its tree belong to: the one that made
/// its script object, else its document's, else the current one.
Realm& realm_for_node(const Node& node);
/// Ends a realm: called when its interpreter is destroyed.
void shutdown_realm(JsIntrinsics& intrinsics);

/// Gives a realm its window: `global` becomes a Window showing `document`.
/// `scope` is the global scope, whose variables are the window's properties.
void attach_window(Realm& realm, const JsValue& global, const std::shared_ptr<Document>& document,
                   const std::shared_ptr<JsEnvironment>& scope);

// ---------------------------------------------------------------------------
// Nodes and their objects
// ---------------------------------------------------------------------------

/// The script object of `node` in `realm`; JavaScript null for no node.
JsValue wrap(Realm& realm, const std::shared_ptr<Node>& node);
inline JsValue wrap(Realm& realm, Node& node) { return wrap(realm, node.shared_from_this()); }
/// The node `value` is the object of, or null.
inline Node* node_ptr(const JsValue& value) {
    if (value.type != JsType::Object || !value.object_val) return nullptr;
    return value.object_val->dom_node.get();
}
inline std::shared_ptr<Node> node_of(const JsValue& value) {
    if (value.type != JsType::Object || !value.object_val) return nullptr;
    return value.object_val->dom_node;
}
std::shared_ptr<Element> element_of(const JsValue& value);
/// For a state object's trace(): keeps the script object of a node it holds.
void trace_node(Heap& heap, const Node* node);
/// The name of the interface an element, text node, ... is an instance of.
std::string interface_for(const Node& node);
/// The interface of an HTML element by its tag ("HTMLDivElement").
const char* html_interface_for_tag(const std::string& tag);

inline bool is_html_element(const Node& node, const char* tag) {
    return node.is_element() && !static_cast<const Element&>(node).is_foreign() &&
           static_cast<const Element&>(node).tag_name() == tag;
}
/// Declares `bool name(const Node&)`: true for the HTML element `tag`.
#define LITEBIRD_DOM_TAG(name, tag) \
    inline bool name(const Node& node) { return ::litebird::dom::is_html_element(node, tag); }

inline std::shared_ptr<Element> shared(Element& element) {
    return std::static_pointer_cast<Element>(element.shared_from_this());
}
inline std::shared_ptr<Node> shared(Node& node) { return node.shared_from_this(); }

/// The document `node` belongs to; the realm's own when the node was built
/// by hand and never told.
std::shared_ptr<Document> document_of(Realm& realm, const Node& node);

// ---------------------------------------------------------------------------
// Errors and conversions
// ---------------------------------------------------------------------------

JsValue make_dom_exception(Realm& realm, const std::string& name, const std::string& message);
[[noreturn]] void throw_dom(Realm& realm, const char* name, const std::string& message);
[[noreturn]] void throw_illegal_invocation(const char* iface);
/// TypeError unless at least `count` arguments were passed.
void require_args(Args args, std::size_t count, const char* what);

inline JsValue str(std::string_view text) { return JsValue::make_string(text); }
inline JsValue num(double value) { return JsValue::make_number(value); }
inline JsValue boolean(bool value) { return JsValue::make_boolean(value); }
inline JsValue null() { return JsValue::make_null(); }
inline JsValue undefined() { return JsValue::make_undefined(); }
inline JsValue nullable_str(const std::string* text) { return text ? JsValue::make_string(*text) : JsValue::make_null(); }

/// DOMString: ToString.
inline std::string to_str(const JsValue& value) { return js_to_string(value); }
/// [LegacyNullToEmptyString] DOMString.
inline std::string to_str_null_empty(const JsValue& value) {
    return value.type == JsType::Null ? std::string() : js_to_string(value);
}
/// `long` and `unsigned long` as WebIDL converts them.
int32_t to_long(const JsValue& value);
uint32_t to_ulong(const JsValue& value);
double to_double(const JsValue& value);
bool to_bool(const JsValue& value);
/// A dictionary member: undefined when `dict` is not an object or lacks it.
JsValue member(const JsValue& dict, const char* name);
/// Canonical array index ("0", "17"; not "01", "-1").
bool parse_index(const std::string& name, std::size_t& out);

std::string ascii_lower(std::string text);
std::string ascii_upper(std::string text);
bool is_ascii_space(char c);
std::string strip_ascii_space(const std::string& text);
/// Splits on ASCII whitespace.
std::vector<std::string> split_tokens(const std::string& text);
/// The length of UTF-8 `text` in UTF-16 code units, which is how the DOM
/// counts characters.
std::size_t utf16_length(const std::string& text);

// ---------------------------------------------------------------------------
// Hidden slots and cached objects
// ---------------------------------------------------------------------------

/// A value kept on `owner` where scripts cannot see it. Null when absent.
const JsValue* slot(const JsValue& owner, const std::string& name);
void set_slot(const JsValue& owner, const std::string& name, JsValue value);
void clear_slot(const JsValue& owner, const std::string& name);
/// The object in `owner`'s slot `name`, made by `make` the first time: how
/// `element.classList === element.classList` holds.
JsValue cached(const JsValue& owner, const std::string& name, const std::function<JsValue()>& make);

template <typename T> T* state_of(const JsValue& value) {
    if (value.type != JsType::Object || !value.object_val || !value.object_val->exotic) return nullptr;
    return dynamic_cast<T*>(value.object_val->exotic.get());
}
/// The state of `this`, or a TypeError when `this` is not such an object.
template <typename T> T& this_state(const char* iface) {
    T* state = state_of<T>(JsValue::current_this());
    if (!state) throw_illegal_invocation(iface);
    return *state;
}

// ---------------------------------------------------------------------------
// Building a prototype
// ---------------------------------------------------------------------------

/// Flags of interface members: enumerable and configurable, as WebIDL has it.
inline constexpr uint8_t kMemberFlags = PropertyMap::Enumerable | PropertyMap::Writable | PropertyMap::Configurable;

/// A native function for `realm`. It does nothing once the realm has ended.
template <typename Fn> JsValue make_function(Realm& realm, const char* name, int length, Fn fn) {
    Realm* r = &realm;
    return js_make_native(name, length, [r, fn](Args args) -> JsValue {
        if (!r->alive) return JsValue::make_undefined();
        return fn(*r, args);
    });
}

void define_method_value(JsObject* object, const char* name, JsValue function, uint8_t flags = kMemberFlags);
void define_accessor_value(JsObject* object, const char* name, const JsValue& getter, const JsValue& setter);

/// Fills in one interface's prototype. `Fn` signatures:
///   method:   JsValue(Realm&, Args)
///   getter:   JsValue(Realm&)
///   setter:   void(Realm&, const JsValue&)
/// The receiver is JsValue::current_this(); the `node_`/`el_` forms check it
/// first and hand it over.
class Iface {
public:
    Iface(Realm& realm, const InterfaceDef& def, JsObject* proto, JsValue ctor)
        : realm(realm), def(def), proto(proto), ctor(std::move(ctor)) {}

    Realm& realm;
    const InterfaceDef& def;
    JsObject* proto;
    JsValue ctor;

    template <typename Fn> void method(const char* name, int length, Fn fn) {
        define_method_value(proto, name, make_function(realm, name, length, fn));
    }
    template <typename Fn> void static_method(const char* name, int length, Fn fn) {
        ctor.define_property(name, make_function(realm, name, length, fn), kMemberFlags);
    }
    template <typename Get> void getter(const char* name, Get get) {
        define_accessor_value(proto, name,
            make_function(realm, name, 0, [get](Realm& r, Args) -> JsValue { return get(r); }), JsValue());
    }
    template <typename Get, typename Set> void accessor(const char* name, Get get, Set set) {
        define_accessor_value(proto, name,
            make_function(realm, name, 0, [get](Realm& r, Args) -> JsValue { return get(r); }),
            make_function(realm, name, 1, [set](Realm& r, Args args) -> JsValue {
                set(r, js_arg(args, 0));
                return JsValue::make_undefined();
            }));
    }
    /// A numeric constant, on the prototype and on the constructor.
    void constant(const char* name, double value);
    /// A second name for a member already defined.
    void alias(const char* name, const char* existing);
    /// A hidden data property of the prototype.
    void hidden(const std::string& name, JsValue value);

    // --- Members of node interfaces --------------------------------------------
    //
    // `accepts` says which nodes are instances of the interface; anything
    // else as the receiver is a TypeError.
    template <typename Fn> void node_method(bool (*accepts)(const Node&), const char* name, int length, Fn fn) {
        const char* iface = def.name;
        method(name, length, [accepts, iface, fn](Realm& r, Args args) -> JsValue {
            return fn(r, receiver(accepts, iface), args);
        });
    }
    template <typename Get> void node_getter(bool (*accepts)(const Node&), const char* name, Get get) {
        const char* iface = def.name;
        getter(name, [accepts, iface, get](Realm& r) -> JsValue { return get(r, receiver(accepts, iface)); });
    }
    template <typename Get, typename Set>
    void node_accessor(bool (*accepts)(const Node&), const char* name, Get get, Set set) {
        const char* iface = def.name;
        accessor(name,
            [accepts, iface, get](Realm& r) -> JsValue { return get(r, receiver(accepts, iface)); },
            [accepts, iface, set](Realm& r, const JsValue& value) { set(r, receiver(accepts, iface), value); });
    }

    /// The node `this` is the object of, checked with `accepts`.
    static Node& receiver(bool (*accepts)(const Node&), const char* iface) {
        Node* node = node_ptr(JsValue::current_this());
        if (!node || !accepts(*node)) throw_illegal_invocation(iface);
        return *node;
    }
};

/// Members of element interfaces: the receiver is an Element accepted by
/// `Accepts::test` (see the helpers in DomElementBindings.cpp).
template <bool (*Accepts)(const Node&)>
struct ElementMembers {
    Iface& I;
    explicit ElementMembers(Iface& iface) : I(iface) {}

    template <typename Fn> void method(const char* name, int length, Fn fn) {
        I.node_method(Accepts, name, length, [fn](Realm& r, Node& node, Args args) -> JsValue {
            return fn(r, static_cast<Element&>(node), args);
        });
    }
    template <typename Get> void getter(const char* name, Get get) {
        I.node_getter(Accepts, name, [get](Realm& r, Node& node) -> JsValue {
            return get(r, static_cast<Element&>(node));
        });
    }
    template <typename Get, typename Set> void accessor(const char* name, Get get, Set set) {
        I.node_accessor(Accepts, name,
            [get](Realm& r, Node& node) -> JsValue { return get(r, static_cast<Element&>(node)); },
            [set](Realm& r, Node& node, const JsValue& value) { set(r, static_cast<Element&>(node), value); });
    }

    // --- IDL attributes that reflect a content attribute -------------------------
    /// DOMString: the attribute's value, "" when absent.
    void string(const char* name, const char* attribute = nullptr) {
        const std::string attr = attribute ? attribute : ascii_lower(name);
        accessor(name,
            [attr](Realm&, Element& el) { return str(el.get_attribute(attr)); },
            [attr](Realm&, Element& el, const JsValue& value) { el.set_attribute(attr, to_str(value)); });
    }
    /// boolean: whether the attribute is present.
    void flag(const char* name, const char* attribute = nullptr) {
        const std::string attr = attribute ? attribute : ascii_lower(name);
        accessor(name,
            [attr](Realm&, Element& el) { return boolean(el.has_attribute(attr)); },
            [attr](Realm&, Element& el, const JsValue& value) {
                if (to_bool(value)) {
                    if (!el.has_attribute(attr)) el.set_attribute(attr, "");
                } else {
                    el.remove_attribute(attr);
                }
            });
    }
    /// long: the attribute parsed as an integer, `fallback` when it is not one.
    void integer(const char* name, long fallback, const char* attribute = nullptr);
    /// unsigned long. With `positive`, zero is not a value and setting it throws.
    void unsigned_integer(const char* name, unsigned long fallback, bool positive = false, const char* attribute = nullptr);
    /// A URL: the attribute resolved against the document's address.
    void url(const char* name, const char* attribute = nullptr);
    /// One of `keywords` (lower case); `missing` when absent, `invalid` when
    /// the value is none of them.
    void keyword(const char* name, std::vector<std::string> keywords, std::string missing, std::string invalid,
                 const char* attribute = nullptr);
};

/// "  -12abc" -> -12, per HTML's rules for parsing integers. False when the
/// text does not start with one.
bool parse_html_integer(const std::string& text, long& out);
bool parse_html_non_negative_integer(const std::string& text, unsigned long& out);
/// Resolves `reference` against the address of `node`'s document. Returns
/// the reference unchanged when it cannot be resolved.
std::string resolve_url(Realm& realm, const Node& node, const std::string& reference);

template <bool (*Accepts)(const Node&)>
void ElementMembers<Accepts>::integer(const char* name, long fallback, const char* attribute) {
    const std::string attr = attribute ? attribute : ascii_lower(name);
    accessor(name,
        [attr, fallback](Realm&, Element& el) {
            long value = 0;
            const std::string* text = el.attribute(attr);
            return num(text && parse_html_integer(*text, value) ? static_cast<double>(value) : static_cast<double>(fallback));
        },
        [attr](Realm&, Element& el, const JsValue& value) { el.set_attribute(attr, std::to_string(to_long(value))); });
}

template <bool (*Accepts)(const Node&)>
void ElementMembers<Accepts>::unsigned_integer(const char* name, unsigned long fallback, bool positive, const char* attribute) {
    const std::string attr = attribute ? attribute : ascii_lower(name);
    accessor(name,
        [attr, fallback, positive](Realm&, Element& el) {
            unsigned long value = 0;
            const std::string* text = el.attribute(attr);
            if (text && parse_html_non_negative_integer(*text, value) && value <= 2147483647ul && !(positive && value == 0)) {
                return num(static_cast<double>(value));
            }
            return num(static_cast<double>(fallback));
        },
        [attr, fallback, positive](Realm& r, Element& el, const JsValue& value) {
            unsigned long number = to_ulong(value);
            if (positive && number == 0) throw_dom(r, "IndexSizeError", "The value provided (0) is not positive.");
            if (number > 2147483647ul) number = fallback;
            el.set_attribute(attr, std::to_string(number));
        });
}

template <bool (*Accepts)(const Node&)>
void ElementMembers<Accepts>::url(const char* name, const char* attribute) {
    const std::string attr = attribute ? attribute : ascii_lower(name);
    accessor(name,
        [attr](Realm& r, Element& el) {
            const std::string* text = el.attribute(attr);
            return str(text ? resolve_url(r, el, *text) : std::string());
        },
        [attr](Realm&, Element& el, const JsValue& value) { el.set_attribute(attr, to_str(value)); });
}

template <bool (*Accepts)(const Node&)>
void ElementMembers<Accepts>::keyword(const char* name, std::vector<std::string> keywords, std::string missing,
                                      std::string invalid, const char* attribute) {
    const std::string attr = attribute ? attribute : ascii_lower(name);
    accessor(name,
        [attr, keywords, missing, invalid](Realm&, Element& el) {
            const std::string* text = el.attribute(attr);
            if (!text) return str(missing);
            const std::string wanted = ascii_lower(*text);
            for (const auto& known : keywords) {
                if (known == wanted) return str(known);
            }
            return str(invalid);
        },
        [attr](Realm&, Element& el, const JsValue& value) { el.set_attribute(attr, to_str(value)); });
}

// ---------------------------------------------------------------------------
// What the parts of the bindings offer each other
// ---------------------------------------------------------------------------

using NodeVector = std::vector<std::shared_ptr<Node>>;

// --- DomNodeBindings.cpp ------------------------------------------------------
void register_node_interfaces();
bool is_any_node(const Node& node);
bool is_element_node(const Node& node);
/// Throws the DOMException the standard's "ensure pre-insertion validity"
/// asks for when `node` may not become a child of `parent` before `child`.
void ensure_pre_insert_validity(Realm& realm, Node& parent, Node& node, Node* child);
/// insertBefore as the standard has it: validity, then the insertion.
void pre_insert(Realm& realm, Node& parent, const std::shared_ptr<Node>& node, Node* child);
/// Converts the arguments of append(), before(), replaceWith() and their
/// relatives (nodes and strings) into one node: the single node, or a
/// fragment holding them all.
std::shared_ptr<Node> nodes_from_arguments(Realm& realm, Node& context, Args args);
/// The child nodes of `node` as scripts count them: a document's doctype is
/// one of them.
std::vector<std::shared_ptr<Node>> script_children(const Node& node);
std::shared_ptr<Node> script_parent(const Node& node);
/// cloneNode().
std::shared_ptr<Node> clone_node(Realm& realm, const Node& node, bool deep, const std::shared_ptr<Document>& into);
/// Replaces everything inside `node` by one text node, or by nothing.
void set_text_content(Node& node, const std::string& text);
/// Changes the data of a Text or Comment node, telling observers.
void set_character_data(Node& node, const std::string& data);
const std::string& character_data(const Node& node);
bool is_character_data(const Node& node);
/// The mixins several interfaces share: ChildNode (before, after,
/// replaceWith, remove), ParentNode (children, append, querySelector, ...)
/// and the element-sibling getters.
void define_child_node(Iface& I, bool (*accepts)(const Node&));
void define_parent_node(Iface& I, bool (*accepts)(const Node&));
void define_element_siblings(Iface& I, bool (*accepts)(const Node&));
/// Node.compareDocumentPosition: the DOCUMENT_POSITION bits of `other`
/// as seen from `self`.
int compare_document_position(const Node& self, const Node& other);
/// Takes `node` out of its parent, if it has one.
void remove_node(Realm& realm, Node& node);
/// Replaces every child of `parent` by `nodes`, as one change: observers get
/// one record.
void replace_all_children(Realm& realm, Node& parent, const NodeVector& nodes);
/// The node argument `index` of a call, or a TypeError.
Node& node_argument(Args args, std::size_t index, const char* what);

// --- DomCollectionBindings.cpp ------------------------------------------------
void register_collection_interfaces();
/// A list that is computed again whenever a tree has changed. `compute`
/// receives the root the list was made for.
JsValue make_live_list(Realm& realm, const char* iface, const std::shared_ptr<Node>& root,
                       std::function<void(Node& root, NodeVector& out)> compute);
/// A list fixed at the time it was made (querySelectorAll).
JsValue make_static_list(Realm& realm, NodeVector nodes, const char* iface = "NodeList");
/// The nodes of a NodeList or HTMLCollection object; false for anything else.
bool list_nodes(const JsValue& list, NodeVector& out);
/// `element.classList` and friends: a DOMTokenList over `attribute`.
JsValue make_token_list(Realm& realm, Element& element, const std::string& attribute,
                        std::vector<std::string> supported = {});
JsValue make_dataset(Realm& realm, Element& element);
JsValue make_attribute_map(Realm& realm, Element& element);
/// The Attr object for an attribute of `element`, the same one every time
/// while the attribute exists.
JsValue attr_object(Realm& realm, Element& element, const std::string& name);
/// Called when an attribute has been removed: its Attr object, if a script
/// holds one, keeps the last value and no longer belongs to the element.
void detach_attr_object(Realm& realm, Element& element, const std::string& name, const std::string& last_value);
/// document.createAttribute(name): an Attr that belongs to no element yet.
JsValue create_attribute(Realm& realm, const std::string& name);
/// element.setAttributeNode(attr): returns the Attr it replaced, or null.
JsValue set_attribute_node(Realm& realm, Element& element, const JsValue& attr);
JsValue remove_attribute_node(Realm& realm, Element& element, const JsValue& attr);

// --- DomEventBindings.cpp -------------------------------------------------------
void register_event_interfaces();
/// One member of an event's init dictionary.
struct EventField {
    const char* name;
    JsValue value;
};
/// A new event object of interface `iface`.
JsValue make_event(Realm& realm, const char* iface, const std::string& type, bool bubbles, bool cancelable,
                   std::vector<EventField> fields = {}, bool composed = false);
/// document.createEvent(name): an event that still has to be initialised.
/// Undefined when `name` is not one of the names createEvent knows.
JsValue create_legacy_event(Realm& realm, const std::string& name);
/// True when `value` is an object EventTarget's methods accept as `this`.
bool is_event_target(Realm& realm, const JsValue& value);
/// Runs `algorithm` when `signal` (an AbortSignal) aborts. False when
/// `signal` is not one; `aborted` says whether it has aborted already.
bool on_abort(const JsValue& signal, std::function<void()> algorithm, bool& aborted);
/// Milliseconds since the realm's time origin.
double time_now(Realm& realm);
/// Dispatches `event` at `target` (a node object, the window or any other
/// EventTarget). Returns false when the event was cancelled.
bool dispatch(Realm& realm, const JsValue& target, const JsValue& event, bool trusted = false);
/// Makes a plain Event and dispatches it.
bool fire(Realm& realm, const JsValue& target, const std::string& type, bool bubbles = false, bool cancelable = false);
bool fire(Realm& realm, Node& target, const std::string& type, bool bubbles = false, bool cancelable = false);
bool event_default_prevented(const JsValue& event);
/// Reports an exception nothing caught: an ErrorEvent at the window. True
/// when a handler dealt with it, so that it is not printed.
bool report_exception(Realm& realm, const JsValue& thrown);
/// Calls `fn` with `this_value`; an exception is reported, not thrown.
JsValue call_reporting(Realm& realm, const JsValue& fn, const JsValue& this_value, const std::vector<JsValue>& args);
/// Defines the `on<type>` accessors of GlobalEventHandlers on a prototype.
void define_event_handlers(Iface& I, bool window_reflecting);
/// The handler in `target`'s `on<type>` slot; null when there is none.
JsValue get_event_handler(Realm& realm, const JsValue& target, const std::string& type);
void set_event_handler(Realm& realm, const JsValue& target, const std::string& type, const JsValue& handler);
/// Called when an `on…` content attribute changed.
void event_handler_attribute_changed(Realm& realm, Element& element, const std::string& name);

// --- DomElementBindings.cpp ------------------------------------------------------
void register_element_interfaces();
/// Parses a selector list for script: a SyntaxError DOMException when it
/// does not parse.
struct ParsedSelector;
std::shared_ptr<Element> query_first(Realm& realm, Node& root, const std::string& selector);
NodeVector query_all(Realm& realm, Node& root, const std::string& selector);
bool matches_selector(Realm& realm, Element& element, const std::string& selector);
/// What `innerHTML = markup` does for an element, a fragment or a shadow root.
void set_inner_markup(Realm& realm, Node& node, const std::string& markup);
/// Parses `markup` as the content of `context` into a fragment.
std::shared_ptr<Node> parse_fragment(Realm& realm, Element& context, const std::string& markup);
/// Throws InvalidCharacterError unless `name` can be an element or attribute name.
void validate_name(Realm& realm, const std::string& name);
/// Sets an attribute the way setAttribute does (name lowercased on HTML
/// elements) and returns nothing.
void set_attribute_from_script(Realm& realm, Element& element, const std::string& name, const std::string& value);

// --- DomStyleBindings.cpp ---------------------------------------------------------
void register_style_interfaces();
JsValue inline_style(Realm& realm, Element& element);
JsValue computed_style(Realm& realm, Element& element);
/// `element.style = text`: the text becomes the style attribute's list.
void set_inline_css_text(Realm& realm, Element& element, const std::string& text);

// --- DomDocumentBindings.cpp ------------------------------------------------------
void register_document_interfaces();
/// A new document that has no window: createHTMLDocument, DOMParser.
std::shared_ptr<Document> make_document(Realm& realm, bool with_skeleton, const std::string* title);
std::shared_ptr<Element> create_element(Realm& realm, Document& document, const std::string& local_name,
                                        ElementNamespace ns = ElementNamespace::HTML, bool synchronous_custom = true);

// --- DomTraversalBindings.cpp -----------------------------------------------------
void register_traversal_interfaces();
/// The window's one Selection object.
JsValue selection_object(Realm& realm);
/// document.createRange(): a range collapsed at the start of `document`.
JsValue create_range(Realm& realm, const std::shared_ptr<Node>& document);
/// document.createTreeWalker() and createNodeIterator().
JsValue create_traversal(Realm& realm, bool iterator, const std::shared_ptr<Node>& root, uint32_t what_to_show,
                         const JsValue& filter);

// --- DomHtmlBindings.cpp ----------------------------------------------------------
void register_html_interfaces();

// --- DomFormBindings.cpp ----------------------------------------------------------
void register_form_interfaces();
/// What a click does besides its listeners: the standard's activation
/// behaviour. `before` runs ahead of the dispatch (a checkbox changes first)
/// and returns what `after` needs to finish or undo it.
struct Activation {
    std::shared_ptr<Element> target;
    bool checkbox_was_checked{false};
    bool was_indeterminate{false};
    std::shared_ptr<Element> radio_previous;
    bool legacy_pre{false};
};
Activation before_click(Realm& realm, const NodeVector& path);
void after_click(Realm& realm, Activation& activation, const JsValue& event, bool cancelled);
/// What an element's object computes besides its interface members
/// (`form[0]`, `select[2]`); null for the elements that compute nothing.
std::shared_ptr<JsExotic> element_exotic(Realm& realm, Element& element);
/// Focus, as `element.focus()` and `blur()` do it.
void focus_element(Realm& realm, Element* element);
bool is_focusable(const Element& element);
/// form.reset() and form.requestSubmit(submitter).
void reset_form(Realm& realm, Element& form);
void submit_form(Realm& realm, Element& form, Element* submitter, bool validate);

// --- DomWindowBindings.cpp --------------------------------------------------------
void register_window_interfaces();
/// The document's address, and the same with another fragment.
std::string document_url(Realm& realm);
/// Follows a link or `location.href = ...`: only a change of fragment on
/// the same page does anything.
void navigate(Realm& realm, const std::string& url);
/// DOMContentLoaded, readystatechange and load, in that order.
void fire_load_events(Realm& realm);
JsValue window_of(Realm& realm);
/// Runs a <script> element a script has just inserted.
void run_inserted_script(Realm& realm, Element& script);
/// Set by the interpreter: evaluates source text in the global scope.
using ScriptRunner = std::function<void(const std::string& source)>;
void set_script_runner(Realm& realm, ScriptRunner runner);

// --- DomObserverBindings.cpp ------------------------------------------------------
void register_observer_interfaces();
/// What Node.cpp tells the bindings (see DomHooks).
void note_children_changed(Node& parent, const NodeVector& added, const NodeVector& removed,
                           const std::shared_ptr<Node>& previous, const std::shared_ptr<Node>& next);
void note_attribute_changed(Element& element, const std::string& name, const std::string* old_value,
                            const std::string* new_value);
void note_character_data_changed(Node& node, const std::string& old_value);
void note_connected(Element& element, bool connected);
/// While one of these exists, changes to the children of `parent` queue no
/// mutation record of their own; commit() queues one for the whole
/// operation (replaceChild, innerHTML, textContent).
class ChildListBatch {
public:
    ChildListBatch(Realm& realm, Node& parent);
    ~ChildListBatch();
    ChildListBatch(const ChildListBatch&) = delete;
    ChildListBatch& operator=(const ChildListBatch&) = delete;
    void commit(const NodeVector& added, const NodeVector& removed, const std::shared_ptr<Node>& previous,
                const std::shared_ptr<Node>& next);
private:
    Realm& m_realm;
    Node* m_previous;
};
/// Upgrades `element` if a definition for its name exists.
void try_upgrade(Realm& realm, Element& element);
void upgrade_subtree(Realm& realm, Node& root);
/// True when `name` has been given to customElements.define in `realm`.
bool custom_element_defined(Realm& realm, const std::string& name);
/// The prototype an element of this name gets when it is created, when it
/// is a defined custom element. Null otherwise.
JsObject* custom_element_prototype(Realm& realm, const std::string& name);

} // namespace dom
} // namespace litebird
