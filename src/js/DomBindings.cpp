// The core of the DOM bindings: realms, interface prototypes, the object of
// a node, DOMException and the conversions everything else uses. See
// DomBindings.hpp for the design.

#include "litebird/js/DomBindings.hpp"
#include "litebird/js/DomLive.hpp"
#include "litebird/dom/ProcessingInstruction.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/xml/XmlDom.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"

#include <cmath>
#include <deque>
#include <mutex>

namespace litebird {
namespace dom {

// ---------------------------------------------------------------------------
// The table of interfaces
// ---------------------------------------------------------------------------

namespace {

struct Registry {
    std::unordered_map<std::string, InterfaceDef> interfaces;
    std::unordered_map<std::string, std::string> aliases;
};

Registry& registry() {
    static Registry s_registry;
    return s_registry;
}

void setup_dom_exception(Iface& I);
JsValue construct_dom_exception(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived);

void ensure_registered() {
    static std::once_flag once;
    std::call_once(once, [] {
        register_interface({"DOMException", nullptr, setup_dom_exception, construct_dom_exception, 0, nullptr});
        register_event_interfaces();
        register_node_interfaces();
        register_collection_interfaces();
        register_element_interfaces();
        register_style_interfaces();
        register_document_interfaces();
        register_traversal_interfaces();
        register_html_interfaces();
        register_form_interfaces();
        register_file_interfaces();
        register_url_interfaces();
        register_observer_interfaces();
        register_window_interfaces();
    });
}

} // namespace

void register_interface(const InterfaceDef& def) {
    registry().interfaces[def.name] = def;
    if (def.alias) registry().aliases[def.alias] = def.name;
}

const InterfaceDef* find_interface(const std::string& name) {
    ensure_registered();
    auto it = registry().interfaces.find(name);
    return it == registry().interfaces.end() ? nullptr : &it->second;
}

const InterfaceDef* find_interface_or_alias(const std::string& name) {
    if (const InterfaceDef* def = find_interface(name)) return def;
    auto it = registry().aliases.find(name);
    return it == registry().aliases.end() ? nullptr : find_interface(it->second);
}

// ---------------------------------------------------------------------------
// Realms
// ---------------------------------------------------------------------------

namespace {

// Realms are never freed. Every function made for a realm points at it, and
// such a function can outlive its interpreter (the heap is shared by the
// process and collected later). A realm that has ended is an empty shell.
struct RealmTable {
    std::mutex mutex;
    std::deque<std::unique_ptr<Realm>> all;
    std::unordered_map<uint64_t, Realm*> live;
    uint64_t next_id{1};
};

RealmTable& realm_table() {
    static RealmTable* s_table = new RealmTable();      // see Heap::default_heap: never destroyed
    return *s_table;
}

thread_local uint64_t t_last_realm_id = 0;
thread_local Realm* t_last_realm = nullptr;

void trace_wrapper(Heap& heap, JsObject& cell);
void release_wrapper(JsObject& cell);
void install_tree_hooks();

} // namespace

Realm& realm_of(JsIntrinsics& intrinsics) {
    if (intrinsics.dom_realm) return *static_cast<Realm*>(intrinsics.dom_realm);
    ensure_registered();
    RealmTable& table = realm_table();
    std::lock_guard<std::mutex> lock(table.mutex);
    auto realm = std::make_unique<Realm>();
    realm->id = table.next_id++;
    realm->intrinsics = &intrinsics;
    Realm* raw = realm.get();
    table.live[raw->id] = raw;
    table.all.push_back(std::move(realm));
    intrinsics.dom_realm = raw;
    if (!JsObject::trace_dom_node) {
        JsObject::trace_dom_node = &trace_wrapper;
        JsObject::release_dom_node = &release_wrapper;
        install_tree_hooks();
    }
    return *raw;
}

Realm& current_realm() {
    return realm_of(JsIntrinsics::get());
}

Realm* find_realm(uint64_t id) {
    if (id == 0) return nullptr;
    if (id == t_last_realm_id && t_last_realm && t_last_realm->alive) return t_last_realm;
    RealmTable& table = realm_table();
    std::lock_guard<std::mutex> lock(table.mutex);
    auto it = table.live.find(id);
    if (it == table.live.end()) return nullptr;
    t_last_realm_id = id;
    t_last_realm = it->second;
    return it->second;
}

Realm* realm_if_any(const Node& node) {
    if (node.script_object()) {
        if (Realm* realm = find_realm(node.script_realm())) return realm;
    }
    if (node.is_document()) return nullptr;
    if (auto owner = node.owner_document()) {
        if (owner->script_object()) return find_realm(owner->script_realm());
    }
    return nullptr;
}

Realm& realm_for_node(const Node& node) {
    if (Realm* realm = realm_if_any(node)) return *realm;
    return current_realm();
}

void shutdown_realm(JsIntrinsics& intrinsics) {
    if (!intrinsics.dom_realm) return;
    Realm* realm = static_cast<Realm*>(intrinsics.dom_realm);
    intrinsics.dom_realm = nullptr;
    {
        RealmTable& table = realm_table();
        std::lock_guard<std::mutex> lock(table.mutex);
        table.live.erase(realm->id);
    }
    if (t_last_realm == realm) {
        t_last_realm = nullptr;
        t_last_realm_id = 0;
    }
    realm->alive = false;
    // Everything the realm held is let go. The objects themselves are freed
    // by the next collection.
    auto parts = std::move(realm->parts);
    realm->parts.clear();
    parts.clear();
    auto protos = std::move(realm->protos);
    realm->protos.clear();
    auto ctors = std::move(realm->ctors);
    realm->ctors.clear();
    realm->window = JsValue();
    realm->document.reset();
    realm->global_scope.reset();
    realm->intrinsics = nullptr;
}

JsObject* Realm::proto(const std::string& name) {
    auto found = protos.find(name);
    if (found != protos.end()) return found->second.object_val;

    const InterfaceDef* def = find_interface(name);
    if (!def) js_throw_type_error("The DOM has no interface called " + name);

    JsObject* parent = nullptr;
    if (def->parent) parent = proto(def->parent);
    else if (name == "DOMException") parent = intrinsics->error_protos[static_cast<int>(JsErrorKind::Error)];
    else parent = intrinsics->object_proto;

    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = parent;
    JsValue proto_value = JsValue::make_object(cell);
    protos[name] = proto_value;

    Realm* realm = this;
    JsValue constructor = js_make_native(def->name, def->length, [realm, def](Args args) -> JsValue {
        if (!realm->alive) return JsValue::make_undefined();
        if (!JsValue::current_is_construct()) {
            js_throw_type_error(std::string("Class constructor ") + def->name + " cannot be invoked without 'new'");
        }
        if (!def->construct) js_throw_type_error("Illegal constructor");
        // Read before anything else is called: the call context belongs to
        // the call in progress.
        if (const JsValue* derived = JsValue::current_derived_this()) {
            const JsValue self = *derived;
            return def->construct(*realm, *def, args, &self);
        }
        return def->construct(*realm, *def, args, nullptr);
    });
    constructor.define_property("prototype", proto_value, 0);
    if (def->parent) {
        // Statics are inherited: `HTMLElement.ELEMENT_NODE`, and the engine
        // looks here for the parent of a class that extends this one.
        constructor.define_property("%super", ctor(def->parent), PropertyMap::Hidden);
    }
    ctors[name] = constructor;

    auto& ctor_slot = cell->properties.entry("constructor");
    ctor_slot.second = constructor;
    ctor_slot.flags = PropertyMap::Hidden;
    auto& tag_slot = cell->properties.entry("@@toStringTag");
    tag_slot.second = JsValue::make_string(def->name);
    tag_slot.flags = PropertyMap::Configurable;

    Iface iface(*this, *def, cell, constructor);
    if (def->setup) def->setup(iface);
    return cell;
}

JsValue Realm::ctor(const std::string& name) {
    proto(name);
    return ctors[name];
}

JsValue Realm::make(const char* name, std::shared_ptr<JsExotic> state, const JsValue* derived) {
    JsObject* prototype = proto(name);
    JsValue object;
    if (derived && derived->type == JsType::Object && derived->object_val) {
        object = *derived;          // a subclass instance: it keeps the subclass's prototype
    } else {
        JsObject* cell = Heap::default_heap().allocate_object();
        cell->prototype = prototype;
        object = JsValue::make_object(cell);
    }
    if (state) object.object_val->exotic = std::move(state);
    return object;
}

// ---------------------------------------------------------------------------
// The object of a node
// ---------------------------------------------------------------------------

namespace {

JsObject* existing_object(const Realm& realm, const Node& node) {
    if (!node.script_object() || node.script_realm() != realm.id) return nullptr;
    return static_cast<JsObject*>(node.script_object());
}

// The node whose object must exist before `node` gets one.
std::shared_ptr<Node> wrapping_parent(const Node& node) {
    if (auto parent = node.parent()) return parent;
    if (node.is_document_fragment()) {
        if (auto* shadow = dynamic_cast<const ShadowRoot*>(&node)) return shadow->host();
    }
    return nullptr;
}

JsObject* create_object(Realm& realm, const std::shared_ptr<Node>& node) {
    JsObject* prototype = nullptr;
    std::shared_ptr<JsExotic> exotic;
    if (node->is_element()) {
        auto& element = static_cast<Element&>(*node);
        if (element.custom_state() == CustomElementState::Custom) {
            prototype = custom_element_prototype(realm, element);
        } else if (element.custom_state() == CustomElementState::Failed && !element.is_foreign() &&
                   CustomElementRegistry::is_valid_custom_element_name(element.tag_name())) {
            // What createElement gives when the constructor threw.
            prototype = realm.proto("HTMLUnknownElement");
        }
        exotic = element_exotic(realm, element);
    } else if (node->is_document()) {
        exotic = document_exotic(realm, static_cast<Document&>(*node));
    }
    if (!prototype) prototype = realm.proto(interface_for(*node));

    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = prototype;
    cell->dom_node = node;
    cell->exotic = std::move(exotic);
    node->set_script_object(cell, realm.id);
    return cell;
}

void mark_object_of(Heap& heap, const Node* node, uint64_t realm) {
    if (node && node->script_object() && node->script_realm() == realm) {
        heap.mark_object(static_cast<JsObject*>(node->script_object()));
    }
}

// What the object of a node keeps alive: the objects of the nodes a script
// can walk to from it. Parent and children are enough to cover a tree,
// because a node that has an object, its parent has one (see DomBindings.hpp).
void trace_wrapper(Heap& heap, JsObject& cell) {
    Node* node = cell.dom_node.get();
    if (!node || node->script_object() != &cell) return;        // the node has moved on to another realm
    const uint64_t realm = node->script_realm();

    if (auto parent = node->parent()) mark_object_of(heap, parent.get(), realm);
    for (const auto& child : node->children()) mark_object_of(heap, child.get(), realm);

    switch (node->type()) {
        case NodeType::Element: {
            auto& element = static_cast<Element&>(*node);
            mark_object_of(heap, element.m_shadow_root.get(), realm);
            mark_object_of(heap, element.m_template_contents.get(), realm);
            break;
        }
        case NodeType::DocumentFragment:
            if (auto* shadow = dynamic_cast<ShadowRoot*>(node)) {
                if (auto host = shadow->host()) mark_object_of(heap, host.get(), realm);
            }
            break;
        case NodeType::Document:
            mark_object_of(heap, static_cast<Document&>(*node).existing_doctype_node(), realm);
            break;
        case NodeType::DocumentType:
            if (auto document = static_cast<DocumentTypeNode&>(*node).document()) {
                mark_object_of(heap, document.get(), realm);
            }
            break;
        default:
            break;
    }
    if (!node->is_document()) {
        if (auto owner = node->owner_document()) mark_object_of(heap, owner.get(), realm);
    }
}

void release_wrapper(JsObject& cell) {
    Node* node = cell.dom_node.get();
    if (node && node->script_object() == &cell) node->set_script_object(nullptr, 0);
}

// A node that has an object was put under a parent that has none.
void hook_wrapper_needs_parent(Node& parent, const Node& child) {
    if (Realm* realm = find_realm(child.script_realm())) wrap(*realm, parent.shared_from_this());
}

void hook_children_changed(Node& parent, const NodeVector& added, const NodeVector& removed,
                           const std::shared_ptr<Node>& previous, const std::shared_ptr<Node>& next) {
    live_note_children_changed(parent, added, removed, previous, next);
    note_children_changed(parent, added, removed, previous, next);
}

void hook_attribute_changed(Element& element, const std::string& name, const std::string* old_value,
                            const std::string* new_value) {
    note_attribute_changed(element, name, old_value, new_value);
}

void hook_character_data_changed(Node& node, const std::string& old_value) {
    note_character_data_changed(node, old_value);
}

void hook_connected(Element& element, bool connected) {
    note_connected(element, connected);
}

bool hook_click(Element& element) {
    Realm* realm = realm_if_any(element);
    if (!realm || !realm->alive) return false;
    JsValue target = wrap(*realm, element.shared_from_this());
    JsValue event = make_event(*realm, "PointerEvent", "click", true, true,
                               {{"view", window_of(*realm)}, {"detail", num(1)}}, true);
    dispatch(*realm, target, event, true);
    return true;
}

bool hook_focus(Element& element, bool focus) {
    Realm* realm = realm_if_any(element);
    if (!realm || !realm->alive) return false;
    if (focus) {
        focus_element(*realm, &element);
    } else {
        auto document = element.owner_document();
        if (document && document->focused_element().get() == &element) focus_element(*realm, nullptr, document);
    }
    return true;
}

// Native code dispatched an event (the shell: a key, a click at a point).
// Script listeners must see it, so the bindings run the dispatch.
bool hook_dispatch(Node& target, const std::shared_ptr<Event>& event, bool& result) {
    Realm* realm = realm_if_any(target);
    if (!realm || !realm->alive) return false;
    const std::string& type = event->type();
    const char* iface = "Event";
    std::vector<EventField> fields;
    if (type == "click" || type == "dblclick" || type == "mousedown" || type == "mouseup" || type == "mousemove" ||
        type == "mouseover" || type == "mouseout" || type == "mouseenter" || type == "mouseleave" || type == "contextmenu") {
        iface = "MouseEvent";
    } else if (type == "keydown" || type == "keyup" || type == "keypress") {
        iface = "KeyboardEvent";
        fields.push_back({"key", str(event->key)});
        fields.push_back({"code", str(event->code)});
    } else if (type == "input" || type == "beforeinput") {
        iface = "InputEvent";
        fields.push_back({"data", event->data.empty() ? null() : str(event->data)});
    } else if (type == "focus" || type == "blur" || type == "focusin" || type == "focusout") {
        iface = "FocusEvent";
    }
    JsValue object = make_event(*realm, iface, type, event->bubbles(), event->cancelable(), std::move(fields));
    result = dispatch(*realm, wrap(*realm, target.shared_from_this()), object, true);
    if (!result) event->prevent_default();
    return true;
}

void install_tree_hooks() {
    DomHooks& hooks = Node::hooks();
    hooks.wrapper_needs_parent = &hook_wrapper_needs_parent;
    hooks.children_changed = &hook_children_changed;
    hooks.attribute_changed = &hook_attribute_changed;
    hooks.character_data_changed = &hook_character_data_changed;
    hooks.connected = &hook_connected;
    hooks.click = &hook_click;
    hooks.focus = &hook_focus;
    hooks.dispatch = &hook_dispatch;
}

} // namespace

JsValue wrap(Realm& realm, const std::shared_ptr<Node>& node) {
    if (!node) return JsValue::make_null();
    if (JsObject* cell = existing_object(realm, *node)) return JsValue::make_object(cell);
    if (!realm.alive) return JsValue::make_null();

    // The ancestors that have no object yet get theirs first, from the top
    // down. A loop, not recursion: a tree can be as deep as memory allows.
    std::vector<std::shared_ptr<Node>> chain;
    chain.push_back(node);
    for (auto up = wrapping_parent(*node); up && !existing_object(realm, *up); up = wrapping_parent(*up)) {
        chain.push_back(up);
    }
    JsObject* cell = nullptr;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) cell = create_object(realm, *it);
    return JsValue::make_object(cell);
}

std::shared_ptr<Element> element_of(const JsValue& value) {
    auto node = node_of(value);
    return node && node->is_element() ? std::static_pointer_cast<Element>(node) : nullptr;
}

void trace_node(Heap& heap, const Node* node) {
    if (node && node->script_object()) heap.mark_object(static_cast<JsObject*>(node->script_object()));
}

std::shared_ptr<Document> document_of(Realm& realm, const Node& node) {
    if (node.is_document()) {
        return std::static_pointer_cast<Document>(const_cast<Node&>(node).shared_from_this());
    }
    if (auto owner = node.owner_document()) return owner;
    return realm.document;
}

// ---------------------------------------------------------------------------
// Which interface a node is an instance of
// ---------------------------------------------------------------------------

const char* html_interface_for_tag(const std::string& tag) {
    static const std::unordered_map<std::string, const char*> s_table = {
        {"a", "HTMLAnchorElement"}, {"area", "HTMLAreaElement"}, {"audio", "HTMLAudioElement"},
        {"base", "HTMLBaseElement"}, {"body", "HTMLBodyElement"}, {"br", "HTMLBRElement"},
        {"button", "HTMLButtonElement"}, {"canvas", "HTMLCanvasElement"}, {"caption", "HTMLTableCaptionElement"},
        {"col", "HTMLTableColElement"}, {"colgroup", "HTMLTableColElement"}, {"data", "HTMLDataElement"},
        {"datalist", "HTMLDataListElement"}, {"del", "HTMLModElement"}, {"ins", "HTMLModElement"},
        {"details", "HTMLDetailsElement"}, {"dialog", "HTMLDialogElement"}, {"dir", "HTMLDirectoryElement"},
        {"div", "HTMLDivElement"}, {"dl", "HTMLDListElement"}, {"embed", "HTMLEmbedElement"},
        {"fieldset", "HTMLFieldSetElement"}, {"font", "HTMLFontElement"}, {"form", "HTMLFormElement"},
        {"frame", "HTMLFrameElement"}, {"frameset", "HTMLFrameSetElement"},
        {"h1", "HTMLHeadingElement"}, {"h2", "HTMLHeadingElement"}, {"h3", "HTMLHeadingElement"},
        {"h4", "HTMLHeadingElement"}, {"h5", "HTMLHeadingElement"}, {"h6", "HTMLHeadingElement"},
        {"head", "HTMLHeadElement"}, {"hr", "HTMLHRElement"}, {"html", "HTMLHtmlElement"},
        {"iframe", "HTMLIFrameElement"}, {"img", "HTMLImageElement"}, {"input", "HTMLInputElement"},
        {"label", "HTMLLabelElement"}, {"legend", "HTMLLegendElement"}, {"li", "HTMLLIElement"},
        {"link", "HTMLLinkElement"}, {"map", "HTMLMapElement"}, {"marquee", "HTMLMarqueeElement"},
        {"menu", "HTMLMenuElement"}, {"meta", "HTMLMetaElement"}, {"meter", "HTMLMeterElement"},
        {"object", "HTMLObjectElement"}, {"ol", "HTMLOListElement"}, {"optgroup", "HTMLOptGroupElement"},
        {"option", "HTMLOptionElement"}, {"output", "HTMLOutputElement"}, {"p", "HTMLParagraphElement"},
        {"param", "HTMLParamElement"}, {"picture", "HTMLPictureElement"}, {"pre", "HTMLPreElement"},
        {"listing", "HTMLPreElement"}, {"xmp", "HTMLPreElement"}, {"progress", "HTMLProgressElement"},
        {"q", "HTMLQuoteElement"}, {"blockquote", "HTMLQuoteElement"}, {"script", "HTMLScriptElement"},
        {"select", "HTMLSelectElement"}, {"slot", "HTMLSlotElement"}, {"source", "HTMLSourceElement"},
        {"span", "HTMLSpanElement"}, {"style", "HTMLStyleElement"}, {"table", "HTMLTableElement"},
        {"tbody", "HTMLTableSectionElement"}, {"thead", "HTMLTableSectionElement"},
        {"tfoot", "HTMLTableSectionElement"}, {"td", "HTMLTableCellElement"}, {"th", "HTMLTableCellElement"},
        {"template", "HTMLTemplateElement"}, {"textarea", "HTMLTextAreaElement"}, {"time", "HTMLTimeElement"},
        {"title", "HTMLTitleElement"}, {"tr", "HTMLTableRowElement"}, {"track", "HTMLTrackElement"},
        {"ul", "HTMLUListElement"}, {"video", "HTMLVideoElement"},
    };
    // Elements that are HTMLElement and nothing more.
    static const std::unordered_map<std::string, bool> s_plain = [] {
        std::unordered_map<std::string, bool> out;
        for (const char* tag : {"abbr", "address", "article", "aside", "b", "bdi", "bdo", "cite", "code", "dd",
                                "dfn", "dt", "em", "figcaption", "figure", "footer", "header", "hgroup", "i",
                                "kbd", "main", "mark", "nav", "noscript", "rp", "rt", "ruby", "s", "samp",
                                "section", "small", "strong", "sub", "summary", "sup", "u", "var", "wbr",
                                "acronym", "basefont", "big", "center", "nobr", "noembed", "noframes",
                                "plaintext", "rb", "rtc", "strike", "tt", "search"}) {
            out[tag] = true;
        }
        return out;
    }();
    auto it = s_table.find(tag);
    if (it != s_table.end()) return it->second;
    if (s_plain.count(tag)) return "HTMLElement";
    if (CustomElementRegistry::is_valid_custom_element_name(tag)) return "HTMLElement";
    return "HTMLUnknownElement";
}

std::string namespace_uri_of(const Element& element) {
    return xml::element_namespace_uri(element);
}

bool in_html_document(const Node& node) {
    if (node.is_document()) return static_cast<const Document&>(node).is_html_document();
    auto owner = node.owner_document();
    return !owner || owner->is_html_document();
}

bool is_html_in_html_document(const Element& element) {
    return !element.is_foreign() && in_html_document(element);
}

void collect_by_qualified_name(Node& root, const std::string& name, NodeVector& out) {
    const bool all = name == "*";
    const bool html_document = in_html_document(root);
    const std::string lower = ascii_lower(name);
    for_each_descendant(root, [&](Node& node) {
        if (!node.is_element()) return true;
        const auto& element = static_cast<Element&>(node);
        if (all) {
            out.push_back(node.shared_from_this());
            return true;
        }
        const std::string& wanted = html_document && !element.is_foreign() ? lower : name;
        const bool matches = element.prefix().empty() ? element.tag_name() == wanted : qualified_name(element) == wanted;
        if (matches) out.push_back(node.shared_from_this());
        return true;
    });
}

std::string qualified_name(const Element& element) {
    if (element.prefix().empty()) return element.tag_name();
    return element.prefix() + ":" + element.tag_name();
}

std::string interface_for(const Node& node) {
    switch (node.type()) {
        case NodeType::Document:
            return static_cast<const Document&>(node).is_xml_document_object() ? "XMLDocument" : "Document";
        case NodeType::Element: {
            const auto& element = static_cast<const Element&>(node);
            switch (element.element_namespace()) {
                case ElementNamespace::HTML:
                    return html_interface_for_tag(element.tag_name());
                case ElementNamespace::SVG:
                    if (element.tag_name() == "svg") return "SVGSVGElement";
                    if (element.tag_name() == "title") return "SVGTitleElement";
                    return "SVGElement";
                case ElementNamespace::MathML:
                case ElementNamespace::Other:
                    return "Element";
            }
            return "Element";
        }
        case NodeType::Text:
            return node.is_cdata_section() ? "CDATASection" : "Text";
        case NodeType::Comment:
            return "Comment";
        case NodeType::DocumentFragment:
            return dynamic_cast<const ShadowRoot*>(&node) ? "ShadowRoot" : "DocumentFragment";
        case NodeType::DocumentType:
            return "DocumentType";
        case NodeType::ProcessingInstruction:
            return "ProcessingInstruction";
    }
    return "Node";
}

// ---------------------------------------------------------------------------
// DOMException
// ---------------------------------------------------------------------------

namespace {

struct LegacyCode {
    const char* name;
    const char* constant;
    int code;
};

const LegacyCode kLegacyCodes[] = {
    {"IndexSizeError", "INDEX_SIZE_ERR", 1},
    {"DOMStringSizeError", "DOMSTRING_SIZE_ERR", 2},
    {"HierarchyRequestError", "HIERARCHY_REQUEST_ERR", 3},
    {"WrongDocumentError", "WRONG_DOCUMENT_ERR", 4},
    {"InvalidCharacterError", "INVALID_CHARACTER_ERR", 5},
    {"NoDataAllowedError", "NO_DATA_ALLOWED_ERR", 6},
    {"NoModificationAllowedError", "NO_MODIFICATION_ALLOWED_ERR", 7},
    {"NotFoundError", "NOT_FOUND_ERR", 8},
    {"NotSupportedError", "NOT_SUPPORTED_ERR", 9},
    {"InUseAttributeError", "INUSE_ATTRIBUTE_ERR", 10},
    {"InvalidStateError", "INVALID_STATE_ERR", 11},
    {"SyntaxError", "SYNTAX_ERR", 12},
    {"InvalidModificationError", "INVALID_MODIFICATION_ERR", 13},
    {"NamespaceError", "NAMESPACE_ERR", 14},
    {"InvalidAccessError", "INVALID_ACCESS_ERR", 15},
    {"ValidationError", "VALIDATION_ERR", 16},
    {"TypeMismatchError", "TYPE_MISMATCH_ERR", 17},
    {"SecurityError", "SECURITY_ERR", 18},
    {"NetworkError", "NETWORK_ERR", 19},
    {"AbortError", "ABORT_ERR", 20},
    {"URLMismatchError", "URL_MISMATCH_ERR", 21},
    {"QuotaExceededError", "QUOTA_EXCEEDED_ERR", 22},
    {"TimeoutError", "TIMEOUT_ERR", 23},
    {"InvalidNodeTypeError", "INVALID_NODE_TYPE_ERR", 24},
    {"DataCloneError", "DATA_CLONE_ERR", 25},
};

int legacy_code(const std::string& name) {
    for (const auto& entry : kLegacyCodes) {
        if (name == entry.name) return entry.code;
    }
    return 0;
}

void fill_dom_exception(JsValue& error, const std::string& name, const std::string& message) {
    error.object_val->klass = ObjectClass::Error;
    error.define_property("name", JsValue::make_string(name), PropertyMap::Hidden);
    error.define_property("message", JsValue::make_string(message), PropertyMap::Hidden);
    error.define_property("code", JsValue::make_number(legacy_code(name)), PropertyMap::Hidden);
    error.define_property("stack", JsValue::make_string(name + ": " + message + "\n    at <anonymous>"),
                          PropertyMap::Hidden);
}

void setup_dom_exception(Iface& I) {
    for (const auto& entry : kLegacyCodes) I.constant(entry.constant, entry.code);
}

JsValue construct_dom_exception(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    JsValue error = realm.make("DOMException", nullptr, derived);
    const std::string message = js_arg(args, 0).is_undefined() ? std::string() : to_str(js_arg(args, 0));
    const std::string name = js_arg(args, 1).is_undefined() ? std::string("Error") : to_str(js_arg(args, 1));
    fill_dom_exception(error, name, message);
    return error;
}

} // namespace

JsValue make_dom_exception(Realm& realm, const std::string& name, const std::string& message) {
    JsValue error = realm.make("DOMException");
    fill_dom_exception(error, name, message);
    return error;
}

void throw_dom(Realm& realm, const char* name, const std::string& message) {
    js_throw(make_dom_exception(realm, name, message));
}

void throw_illegal_invocation(const char* iface) {
    js_throw_type_error(std::string("Illegal invocation: the receiver is not a valid instance of ") + iface);
}

void require_args(Args args, std::size_t count, const char* what) {
    if (args.size() >= count) return;
    js_throw_type_error(std::string("Failed to execute '") + what + "': " + std::to_string(count) +
                        " argument" + (count == 1 ? "" : "s") + " required, but only " +
                        std::to_string(args.size()) + " present.");
}

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

int32_t to_long(const JsValue& value) {
    return js_to_int32(js_to_number(value));
}

uint32_t to_ulong(const JsValue& value) {
    return js_to_uint32(js_to_number(value));
}

double to_double(const JsValue& value) {
    return js_to_number(value);
}

bool to_bool(const JsValue& value) {
    return value.is_truthy();
}

JsValue member(const JsValue& dict, const char* name) {
    if (!dict.is_object_like()) return JsValue::make_undefined();
    return dict.get_property(name);
}

bool parse_index(const std::string& name, std::size_t& out) {
    if (name.empty() || name.size() > 10) return false;
    if (name[0] == '0' && name.size() > 1) return false;
    std::size_t value = 0;
    for (char c : name) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    if (value >= 4294967295u) return false;
    out = value;
    return true;
}

std::string ascii_lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

std::string ascii_upper(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return text;
}

bool is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string strip_ascii_space(const std::string& text) {
    std::size_t start = 0, end = text.size();
    while (start < end && is_ascii_space(text[start])) ++start;
    while (end > start && is_ascii_space(text[end - 1])) --end;
    return text.substr(start, end - start);
}

std::vector<std::string> split_tokens(const std::string& text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && is_ascii_space(text[at])) ++at;
        const std::size_t start = at;
        while (at < text.size() && !is_ascii_space(text[at])) ++at;
        if (at > start) out.push_back(text.substr(start, at - start));
    }
    return out;
}

std::size_t utf16_length(const std::string& text) {
    return js_string_length(text);
}

bool parse_html_integer(const std::string& text, long& out) {
    std::size_t at = 0;
    while (at < text.size() && is_ascii_space(text[at])) ++at;
    bool negative = false;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        negative = text[at] == '-';
        ++at;
    }
    if (at >= text.size() || text[at] < '0' || text[at] > '9') return false;
    double value = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        value = value * 10 + (text[at] - '0');
        if (value > 1e15) return false;
        ++at;
    }
    if (negative) value = -value;
    if (value < -2147483648.0 || value > 2147483647.0) return false;
    out = static_cast<long>(value);
    return true;
}

bool parse_html_non_negative_integer(const std::string& text, unsigned long& out) {
    long value = 0;
    if (!parse_html_integer(text, value) || value < 0) return false;
    out = static_cast<unsigned long>(value);
    return true;
}

bool document_base_url(Realm& realm, const Node& node, Url& out) {
    auto document = document_of(realm, node);
    if (!Url::parse(document ? document->url() : std::string("about:blank"), nullptr, out)) return false;
    // A <base href> changes what relative addresses are relative to. One
    // that is not a URL changes nothing.
    if (document) {
        if (auto head = document->head()) {
            for (const auto& child : head->children()) {
                if (!is_html_element(*child, "base")) continue;
                const std::string* href = static_cast<Element&>(*child).attribute("href");
                if (!href) continue;
                Url base;
                if (Url::parse(*href, &out, base)) out = std::move(base);
                break;
            }
        }
    }
    return true;
}

bool parse_url(Realm& realm, const Node& node, std::string_view reference, Url& out) {
    Url base;
    if (!document_base_url(realm, node, base)) return Url::parse(reference, nullptr, out);
    return Url::parse(reference, &base, out);
}

std::string resolve_url(Realm& realm, const Node& node, const std::string& reference) {
    Url url;
    return parse_url(realm, node, reference, url) ? url.href() : reference;
}

// ---------------------------------------------------------------------------
// Hidden slots
// ---------------------------------------------------------------------------

const JsValue* slot(const JsValue& owner, const std::string& name) {
    JsObject* cell = owner.property_cell();
    if (!cell) return nullptr;
    auto it = cell->properties.find("%" + name);
    return it == cell->properties.end() ? nullptr : &it->second;
}

void set_slot(const JsValue& owner, const std::string& name, JsValue value) {
    JsObject* cell = owner.property_cell(true);
    if (!cell) return;
    auto& entry = cell->properties.entry("%" + name);
    entry.second = std::move(value);
    entry.flags = PropertyMap::Hidden;
}

void clear_slot(const JsValue& owner, const std::string& name) {
    if (JsObject* cell = owner.property_cell()) cell->properties.erase("%" + name);
}

JsValue cached(const JsValue& owner, const std::string& name, const std::function<JsValue()>& make) {
    if (const JsValue* existing = slot(owner, name)) return *existing;
    JsValue made = make();
    set_slot(owner, name, made);
    return made;
}

// ---------------------------------------------------------------------------
// Prototype building
// ---------------------------------------------------------------------------

void define_method_value(JsObject* object, const char* name, JsValue function, uint8_t flags) {
    auto& entry = object->properties.entry(name);
    entry.second = std::move(function);
    entry.setter.reset();
    entry.flags = flags;
}

void define_accessor_value(JsObject* object, const char* name, const JsValue& getter, const JsValue& setter) {
    auto& entry = object->properties.entry(name);
    entry.second = getter;
    entry.setter = setter.is_function() ? std::make_shared<JsValue>(setter) : nullptr;
    entry.flags = PropertyMap::Enumerable | PropertyMap::Configurable | PropertyMap::Accessor;
    object->has_accessors = true;
}

void Iface::constant(const char* name, double value) {
    auto& entry = proto->properties.entry(name);
    entry.second = JsValue::make_number(value);
    entry.flags = PropertyMap::Enumerable;
    ctor.define_property(name, JsValue::make_number(value), PropertyMap::Enumerable);
}

void Iface::alias(const char* name, const char* existing) {
    auto it = proto->properties.find(existing);
    if (it == proto->properties.end()) return;
    const PropertyMap::Entry source = *it;
    auto& entry = proto->properties.entry(name);
    entry.second = source.second;
    entry.setter = source.setter;
    entry.flags = source.flags;
}

void Iface::hidden(const std::string& name, JsValue value) {
    auto& entry = proto->properties.entry(name);
    entry.second = std::move(value);
    entry.flags = PropertyMap::Hidden;
}

} // namespace dom
} // namespace litebird
