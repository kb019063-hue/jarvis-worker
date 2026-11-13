// What happens because the tree changed: MutationObserver and custom
// elements. Node.cpp reports every change through DomHooks; this is where
// those reports arrive.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Promise.hpp"

#include <algorithm>

namespace litebird {
namespace dom {

namespace {

// ---------------------------------------------------------------------------
// MutationObserver
// ---------------------------------------------------------------------------

struct ObserveOptions {
    bool child_list{false};
    bool attributes{false};
    bool character_data{false};
    bool subtree{false};
    bool attribute_old_value{false};
    bool character_data_old_value{false};
    bool has_filter{false};
    std::vector<std::string> attribute_filter;
};

struct Observation {
    std::weak_ptr<Node> target;
    ObserveOptions options;
};

struct ObserverState : JsExotic {
    JsValue callback;
    std::vector<JsValue> records;
    std::vector<Observation> observations;
    void trace(Heap& heap) override {
        heap.mark_value(callback);
        for (const auto& record : records) heap.mark_value(record);
        for (const auto& observation : observations) {
            if (auto target = observation.target.lock()) trace_node(heap, target.get());
        }
    }
};

struct RecordState : JsExotic {
    std::string type;
    JsValue target;
    JsValue added;
    JsValue removed;
    JsValue previous;
    JsValue next;
    bool has_name{false};
    std::string name;
    bool has_old_value{false};
    std::string old_value;
    void trace(Heap& heap) override {
        heap.mark_value(target);
        heap.mark_value(added);
        heap.mark_value(removed);
        heap.mark_value(previous);
        heap.mark_value(next);
    }
};

struct ObserverPart : RealmPart {
    /// Observers that are observing something. They stay alive for as long
    /// as they do, whether or not a script still holds them.
    std::vector<JsValue> active;
    /// Observers with records to deliver.
    std::vector<JsValue> pending;
    bool microtask_queued{false};
    /// See ChildListBatch.
    Node* suppressed{nullptr};
};

ObserverState& this_observer() {
    return this_state<ObserverState>("MutationObserver");
}

RecordState& this_record() {
    return this_state<RecordState>("MutationRecord");
}

void setup_mutation_record(Iface& I) {
    I.getter("type", [](Realm&) { return str(this_record().type); });
    I.getter("target", [](Realm&) { return this_record().target; });
    I.getter("addedNodes", [](Realm&) { return this_record().added; });
    I.getter("removedNodes", [](Realm&) { return this_record().removed; });
    I.getter("previousSibling", [](Realm&) { return this_record().previous; });
    I.getter("nextSibling", [](Realm&) { return this_record().next; });
    I.getter("attributeName", [](Realm&) {
        const RecordState& record = this_record();
        return record.has_name ? str(record.name) : null();
    });
    I.getter("attributeNamespace", [](Realm&) {
        this_record();
        return null();
    });
    I.getter("oldValue", [](Realm&) {
        const RecordState& record = this_record();
        return record.has_old_value ? str(record.old_value) : null();
    });
}

void deliver_records(Realm& realm) {
    ObserverPart& part = realm.part<ObserverPart>();
    part.microtask_queued = false;
    const std::vector<JsValue> observers = std::move(part.pending);
    part.pending.clear();
    for (const JsValue& observer : observers) {
        ObserverState* state = state_of<ObserverState>(observer);
        if (!state || state->records.empty()) continue;
        std::vector<JsValue> records = std::move(state->records);
        state->records.clear();
        call_reporting(realm, state->callback, observer, {JsValue::make_array(records), observer});
    }
}

void queue_delivery(Realm& realm, const JsValue& observer) {
    ObserverPart& part = realm.part<ObserverPart>();
    for (const JsValue& queued : part.pending) {
        if (queued.object_val == observer.object_val) return;
    }
    part.pending.push_back(observer);
    if (part.microtask_queued) return;
    part.microtask_queued = true;
    Realm* r = &realm;
    EventLoop::default_loop().post_microtask([r]() {
        if (r->alive) deliver_records(*r);
    });
}

// True when `ancestor` is `node` or above it.
bool is_inclusive_ancestor(const Node* ancestor, const Node& node) {
    if (ancestor == &node) return true;
    for (auto at = script_parent(node); at; at = script_parent(*at)) {
        if (at.get() == ancestor) return true;
    }
    return false;
}

struct RecordData {
    explicit RecordData(const char* kind) : type(kind) {}
    const char* type;
    const std::string* name{nullptr};
    const std::string* old_value{nullptr};
    const NodeVector* added{nullptr};
    const NodeVector* removed{nullptr};
    std::shared_ptr<Node> previous;
    std::shared_ptr<Node> next;
};

void queue_record(Realm& realm, Node& target, const RecordData& data) {
    ObserverPart& part = realm.part<ObserverPart>();
    if (part.active.empty()) return;
    const std::string type = data.type;
    JsValue with_old, without_old;      // the record, made once per variant and shared
    // An observer may be listening on several ancestors; it gets one record.
    const std::vector<JsValue> observers = part.active;
    for (const JsValue& observer : observers) {
        ObserverState* state = state_of<ObserverState>(observer);
        if (!state) continue;
        bool interested = false;
        bool wants_old = false;
        for (const auto& observation : state->observations) {
            auto observed = observation.target.lock();
            if (!observed) continue;
            const ObserveOptions& options = observation.options;
            if (observed.get() != &target && !(options.subtree && is_inclusive_ancestor(observed.get(), target))) continue;
            if (type == "attributes") {
                if (!options.attributes) continue;
                if (options.has_filter && std::find(options.attribute_filter.begin(), options.attribute_filter.end(),
                                                    *data.name) == options.attribute_filter.end()) {
                    continue;
                }
                wants_old = wants_old || options.attribute_old_value;
            } else if (type == "characterData") {
                if (!options.character_data) continue;
                wants_old = wants_old || options.character_data_old_value;
            } else if (!options.child_list) {
                continue;
            }
            interested = true;
        }
        if (!interested) continue;
        JsValue& record = wants_old ? with_old : without_old;
        if (record.is_undefined()) {
            auto made = std::make_shared<RecordState>();
            made->type = type;
            made->target = wrap(realm, target);
            made->added = make_static_list(realm, data.added ? *data.added : NodeVector());
            made->removed = make_static_list(realm, data.removed ? *data.removed : NodeVector());
            made->previous = wrap(realm, data.previous);
            made->next = wrap(realm, data.next);
            if (data.name) {
                made->has_name = true;
                made->name = *data.name;
            }
            if (wants_old && data.old_value) {
                made->has_old_value = true;
                made->old_value = *data.old_value;
            }
            record = realm.make("MutationRecord", made);
        }
        state->records.push_back(record);
        queue_delivery(realm, observer);
    }
}

JsValue construct_observer(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 1, "MutationObserver constructor");
    if (!args[0].is_function()) {
        js_throw_type_error("Failed to construct 'MutationObserver': parameter 1 is not of type 'Function'.");
    }
    auto state = std::make_shared<ObserverState>();
    state->callback = args[0];
    return realm.make("MutationObserver", state, derived);
}

void setup_mutation_observer(Iface& I) {
    I.method("observe", 1, [](Realm& r, Args args) {
        const JsValue self = JsValue::current_this();
        ObserverState& state = this_observer();
        require_args(args, 1, "MutationObserver.observe");
        Node& target = node_argument(args, 0, "MutationObserver.observe");
        const JsValue& init = js_arg(args, 1);
        if (!init.is_nullish() && !init.is_object_like()) {
            js_throw_type_error("Failed to execute 'observe' on 'MutationObserver': parameter 2 is not an object.");
        }
        ObserveOptions options;
        const JsValue attributes = member(init, "attributes");
        const JsValue character_data = member(init, "characterData");
        const JsValue old_attribute = member(init, "attributeOldValue");
        const JsValue old_data = member(init, "characterDataOldValue");
        const JsValue filter = member(init, "attributeFilter");
        options.child_list = to_bool(member(init, "childList"));
        options.subtree = to_bool(member(init, "subtree"));
        options.attribute_old_value = to_bool(old_attribute);
        options.character_data_old_value = to_bool(old_data);
        if (!filter.is_undefined()) {
            options.has_filter = true;
            for (const JsValue& name : js_iterable_to_vector(filter)) options.attribute_filter.push_back(to_str(name));
        }
        // Asking for old values, or for certain attributes, asks for the kind.
        options.attributes = attributes.is_undefined() ? (!old_attribute.is_undefined() || options.has_filter)
                                                        : to_bool(attributes);
        options.character_data = character_data.is_undefined() ? !old_data.is_undefined() : to_bool(character_data);
        if (!options.child_list && !options.attributes && !options.character_data) {
            js_throw_type_error("Failed to execute 'observe' on 'MutationObserver': The options object must set at least one of 'attributes', 'characterData', or 'childList' to true.");
        }
        if (options.attribute_old_value && !options.attributes) {
            js_throw_type_error("Failed to execute 'observe' on 'MutationObserver': The options object may only set 'attributeOldValue' to true when 'attributes' is true or not present.");
        }
        if (options.has_filter && !options.attributes) {
            js_throw_type_error("Failed to execute 'observe' on 'MutationObserver': The options object may only set 'attributeFilter' when 'attributes' is true or not present.");
        }
        if (options.character_data_old_value && !options.character_data) {
            js_throw_type_error("Failed to execute 'observe' on 'MutationObserver': The options object may only set 'characterDataOldValue' to true when 'characterData' is true or not present.");
        }
        bool replaced = false;
        for (auto& observation : state.observations) {
            if (observation.target.lock().get() == &target) {
                observation.options = options;
                replaced = true;
            }
        }
        if (!replaced) state.observations.push_back({target.shared_from_this(), options});
        ObserverPart& part = r.part<ObserverPart>();
        bool listed = false;
        for (const JsValue& active : part.active) listed = listed || active.object_val == self.object_val;
        if (!listed) part.active.push_back(self);
        return undefined();
    });
    I.method("disconnect", 0, [](Realm& r, Args) {
        const JsValue self = JsValue::current_this();
        ObserverState& state = this_observer();
        state.observations.clear();
        state.records.clear();
        ObserverPart& part = r.part<ObserverPart>();
        part.active.erase(std::remove_if(part.active.begin(), part.active.end(),
                                         [&](const JsValue& active) { return active.object_val == self.object_val; }),
                          part.active.end());
        return undefined();
    });
    I.method("takeRecords", 0, [](Realm&, Args) {
        ObserverState& state = this_observer();
        std::vector<JsValue> records = std::move(state.records);
        state.records.clear();
        return JsValue::make_array(records);
    });
}

// ---------------------------------------------------------------------------
// Custom elements
// ---------------------------------------------------------------------------

struct Definition {
    std::string name;
    JsValue constructor;
    JsValue prototype;
    std::vector<std::string> observed;
    JsValue connected;
    JsValue disconnected;
    JsValue adopted;
    JsValue attribute_changed;
};

struct CustomPart : RealmPart {
    std::vector<std::shared_ptr<Definition>> definitions;
    std::unordered_map<std::string, std::shared_ptr<PromiseData>> waiting;
    bool defining{false};

    std::shared_ptr<Definition> find(const std::string& name) const {
        for (const auto& definition : definitions) {
            if (definition->name == name) return definition;
        }
        return nullptr;
    }
};

std::shared_ptr<Definition> definition_for(Realm& realm, const Element& element) {
    if (element.is_foreign()) return nullptr;
    CustomPart* part = realm.find_part<CustomPart>();
    return part ? part->find(element.tag_name()) : nullptr;
}

void run_upgrade(Realm& realm, Element& element, const std::shared_ptr<Definition>& definition) {
    auto keep = shared(element);
    JsValue object = wrap(realm, element);
    // The element's own object becomes an instance of the class: it gets the
    // class's prototype, and then the constructor runs on it.
    if (definition->prototype.is_object()) object.object_val->prototype = definition->prototype.object_val;
    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        JsScriptScope scope;
        definition->constructor.construct({}, &object);
    }, thrown);
    if (!ok) {
        element.set_custom_state(CustomElementState::Failed);
        js_report_uncaught(thrown);
        return;
    }
    element.set_custom_state(CustomElementState::Custom);
    Node::bump_tree_version();
    if (definition->attribute_changed.is_function()) {
        const AttributeList attributes = element.attributes();
        for (const auto& [name, value] : attributes) {
            if (std::find(definition->observed.begin(), definition->observed.end(), name) == definition->observed.end()) continue;
            call_reporting(realm, definition->attribute_changed, object, {str(name), null(), str(value), null()});
        }
    }
    if (element.is_connected() && definition->connected.is_function()) {
        call_reporting(realm, definition->connected, object, {});
    }
}

JsValue callback_of(const JsValue& prototype, const char* name) {
    JsValue callback = prototype.get_property(name);
    if (callback.is_undefined()) return callback;
    if (!callback.is_function()) {
        js_throw_type_error(std::string("Failed to execute 'define' on 'CustomElementRegistry': ") + name + " is not a function.");
    }
    return callback;
}

void require_registry() {
    const JsValue* brand = slot(JsValue::current_this(), "brand");
    if (!brand || brand->to_string() != "CustomElementRegistry") throw_illegal_invocation("CustomElementRegistry");
}

void setup_registry(Iface& I) {
    I.method("define", 2, [](Realm& r, Args args) {
        require_registry();
        require_args(args, 2, "CustomElementRegistry.define");
        const std::string name = to_str(args[0]);
        const JsValue constructor = args[1];
        if (!constructor.is_function()) {
            js_throw_type_error("Failed to execute 'define' on 'CustomElementRegistry': parameter 2 is not of type 'Function'.");
        }
        if (!CustomElementRegistry::is_valid_custom_element_name(name)) {
            throw_dom(r, "SyntaxError", "\"" + name + "\" is not a valid custom element name.");
        }
        CustomPart& part = r.part<CustomPart>();
        if (part.find(name)) {
            throw_dom(r, "NotSupportedError", "the name \"" + name + "\" has already been used with this registry.");
        }
        for (const auto& existing : part.definitions) {
            if (strict_equals(existing->constructor, constructor)) {
                throw_dom(r, "NotSupportedError", "this constructor has already been used with this registry.");
            }
        }
        if (!member(js_arg(args, 2), "extends").is_undefined()) {
            throw_dom(r, "NotSupportedError", "Customized built-in elements are not supported.");
        }
        if (part.defining) throw_dom(r, "NotSupportedError", "A definition is already being made.");
        part.defining = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{part.defining};

        auto definition = std::make_shared<Definition>();
        definition->name = name;
        definition->constructor = constructor;
        definition->prototype = constructor.get_property("prototype");
        if (!definition->prototype.is_object()) {
            js_throw_type_error("Failed to execute 'define' on 'CustomElementRegistry': the constructor's prototype is not an object.");
        }
        definition->connected = callback_of(definition->prototype, "connectedCallback");
        definition->disconnected = callback_of(definition->prototype, "disconnectedCallback");
        definition->adopted = callback_of(definition->prototype, "adoptedCallback");
        definition->attribute_changed = callback_of(definition->prototype, "attributeChangedCallback");
        if (definition->attribute_changed.is_function()) {
            const JsValue observed = constructor.get_property("observedAttributes");
            if (!observed.is_undefined()) {
                for (const JsValue& attribute : js_iterable_to_vector(observed)) {
                    definition->observed.push_back(to_str(attribute));
                }
            }
        }
        part.definitions.push_back(definition);
        part.defining = false;

        // Elements of that name already in the document become instances now.
        if (r.document) upgrade_subtree(r, *r.document);
        auto waiting = part.waiting.find(name);
        if (waiting != part.waiting.end()) {
            auto promise = waiting->second;
            part.waiting.erase(waiting);
            Promise::resolve(promise, constructor);
        }
        return undefined();
    });
    I.method("get", 1, [](Realm& r, Args args) {
        require_registry();
        require_args(args, 1, "CustomElementRegistry.get");
        auto definition = r.part<CustomPart>().find(to_str(args[0]));
        return definition ? definition->constructor : undefined();
    });
    I.method("getName", 1, [](Realm& r, Args args) {
        require_registry();
        require_args(args, 1, "CustomElementRegistry.getName");
        for (const auto& definition : r.part<CustomPart>().definitions) {
            if (strict_equals(definition->constructor, args[0])) return str(definition->name);
        }
        return null();
    });
    I.method("whenDefined", 1, [](Realm& r, Args args) {
        require_registry();
        require_args(args, 1, "CustomElementRegistry.whenDefined");
        const std::string name = to_str(args[0]);
        if (!CustomElementRegistry::is_valid_custom_element_name(name)) {
            return Promise::make_rejected(make_dom_exception(r, "SyntaxError", "\"" + name + "\" is not a valid custom element name."));
        }
        CustomPart& part = r.part<CustomPart>();
        if (auto definition = part.find(name)) return Promise::make_resolved(definition->constructor);
        auto& promise = part.waiting[name];
        if (!promise) promise = std::make_shared<PromiseData>();
        return Promise::create_promise_instance(promise);
    });
    I.method("upgrade", 1, [](Realm& r, Args args) {
        require_registry();
        require_args(args, 1, "CustomElementRegistry.upgrade");
        upgrade_subtree(r, node_argument(args, 0, "CustomElementRegistry.upgrade"));
        return undefined();
    });
}

} // namespace

/// `window.customElements`.
JsValue custom_elements_object(Realm& realm) {
    if (!realm.window.is_object()) return null();
    return cached(realm.window, "customElements", [&]() {
        JsValue registry = realm.make("CustomElementRegistry");
        set_slot(registry, "brand", str("CustomElementRegistry"));
        return registry;
    });
}

bool custom_element_defined(Realm& realm, const std::string& name) {
    CustomPart* part = realm.find_part<CustomPart>();
    return part && part->find(name) != nullptr;
}

JsObject* custom_element_prototype(Realm& realm, const std::string& name) {
    CustomPart* part = realm.find_part<CustomPart>();
    auto definition = part ? part->find(name) : nullptr;
    return definition && definition->prototype.is_object() ? definition->prototype.object_val.get() : nullptr;
}

void try_upgrade(Realm& realm, Element& element) {
    if (element.custom_state() != CustomElementState::Uncustomized) return;
    auto definition = definition_for(realm, element);
    if (definition) run_upgrade(realm, element, definition);
}

void upgrade_subtree(Realm& realm, Node& root) {
    CustomPart* part = realm.find_part<CustomPart>();
    if (!part || part->definitions.empty()) return;
    // Listed first: a constructor is script and may change the tree.
    std::vector<std::shared_ptr<Element>> candidates;
    auto consider = [&](Node& node) {
        if (!node.is_element()) return;
        auto& element = static_cast<Element&>(node);
        if (element.custom_state() == CustomElementState::Uncustomized && !element.is_foreign() &&
            element.tag_name().find('-') != std::string::npos && part->find(element.tag_name())) {
            candidates.push_back(shared(element));
        }
    };
    consider(root);
    for_each_descendant(root, [&](Node& node) {
        consider(node);
        return true;
    });
    for (const auto& element : candidates) try_upgrade(realm, *element);
}

JsValue construct_custom_element(Realm& realm, const InterfaceDef&, Args, const JsValue* derived) {
    if (!derived || derived->type != JsType::Object || !derived->object_val) js_throw_type_error("Illegal constructor");
    // Reached through super() during an upgrade: `this` is the element's object already.
    if (node_ptr(*derived)) return *derived;

    // `new MyElement()`: which definition is this class?
    CustomPart* part = realm.find_part<CustomPart>();
    std::shared_ptr<Definition> definition;
    if (part) {
        int guard = 0;
        for (JsObject* proto = derived->object_val->prototype; proto && !definition && guard < kMaxPrototypeChain;
             proto = proto->prototype, ++guard) {
            for (const auto& candidate : part->definitions) {
                if (candidate->prototype.object_val.get() == proto) {
                    definition = candidate;
                    break;
                }
            }
        }
    }
    if (!definition) js_throw_type_error("Illegal constructor");

    auto element = std::make_shared<Element>(definition->name);
    if (realm.document) element->set_owner_document(realm.document);
    element->set_custom_state(CustomElementState::Custom);
    JsObject* cell = derived->object_val;
    cell->dom_node = element;
    element->set_script_object(cell, realm.id);
    return *derived;
}

// ---------------------------------------------------------------------------
// The reports from the tree
// ---------------------------------------------------------------------------

ChildListBatch::ChildListBatch(Realm& realm, Node& parent) : m_realm(realm), m_previous(nullptr) {
    ObserverPart& part = realm.part<ObserverPart>();
    m_previous = part.suppressed;
    part.suppressed = &parent;
}

ChildListBatch::~ChildListBatch() {
    if (m_realm.alive) m_realm.part<ObserverPart>().suppressed = m_previous;
}

void ChildListBatch::commit(const NodeVector& added, const NodeVector& removed, const std::shared_ptr<Node>& previous,
                            const std::shared_ptr<Node>& next) {
    if (!m_realm.alive) return;
    ObserverPart& part = m_realm.part<ObserverPart>();
    if (!part.suppressed || (added.empty() && removed.empty())) return;
    RecordData data("childList");
    data.added = &added;
    data.removed = &removed;
    data.previous = previous;
    data.next = next;
    queue_record(m_realm, *part.suppressed, data);
}

void note_children_changed(Node& parent, const NodeVector& added, const NodeVector& removed,
                           const std::shared_ptr<Node>& previous, const std::shared_ptr<Node>& next) {
    Realm* realm = realm_if_any(parent);
    if (!realm || !realm->alive) return;
    ObserverPart* part = realm->find_part<ObserverPart>();
    if (!part || part->active.empty() || part->suppressed == &parent) return;
    RecordData data("childList");
    data.added = &added;
    data.removed = &removed;
    data.previous = previous;
    data.next = next;
    queue_record(*realm, parent, data);
}

void note_attribute_changed(Element& element, const std::string& name, const std::string* old_value,
                            const std::string* new_value) {
    Realm* realm = realm_if_any(element);
    if (!realm || !realm->alive) return;
    if (ObserverPart* part = realm->find_part<ObserverPart>()) {
        if (!part->active.empty()) {
            RecordData data("attributes");
            data.name = &name;
            data.old_value = old_value;
            queue_record(*realm, element, data);
        }
    }
    if (name.size() > 2 && name[0] == 'o' && name[1] == 'n') event_handler_attribute_changed(*realm, element, name);
    if (!new_value && old_value) detach_attr_object(*realm, element, name, *old_value);
    if (element.custom_state() == CustomElementState::Custom) {
        auto definition = definition_for(*realm, element);
        if (definition && definition->attribute_changed.is_function() &&
            std::find(definition->observed.begin(), definition->observed.end(), name) != definition->observed.end()) {
            call_reporting(*realm, definition->attribute_changed, wrap(*realm, element),
                           {str(name), nullable_str(old_value), nullable_str(new_value), null()});
        }
    }
}

void note_character_data_changed(Node& node, const std::string& old_value) {
    Realm* realm = realm_if_any(node);
    if (!realm || !realm->alive) return;
    ObserverPart* part = realm->find_part<ObserverPart>();
    if (!part || part->active.empty()) return;
    RecordData data("characterData");
    data.old_value = &old_value;
    queue_record(*realm, node, data);
}

void note_connected(Element& element, bool connected) {
    // Nearly every element is neither a custom element nor about to be one.
    if (element.custom_state() != CustomElementState::Custom &&
        (element.is_foreign() || element.tag_name().find('-') == std::string::npos)) {
        return;
    }
    Realm* realm = realm_if_any(element);
    if (!realm || !realm->alive) return;
    auto definition = definition_for(*realm, element);
    if (!definition) return;
    if (element.custom_state() == CustomElementState::Uncustomized) {
        if (connected) run_upgrade(*realm, element, definition);       // which also tells it that it is connected
        return;
    }
    if (element.custom_state() != CustomElementState::Custom) return;
    const JsValue& callback = connected ? definition->connected : definition->disconnected;
    if (callback.is_function()) call_reporting(*realm, callback, wrap(*realm, element), {});
}

void register_observer_interfaces() {
    register_interface({"MutationObserver", nullptr, setup_mutation_observer, construct_observer, 1, nullptr});
    register_interface({"MutationRecord", nullptr, setup_mutation_record, nullptr, 0, nullptr});
    register_interface({"CustomElementRegistry", nullptr, setup_registry, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
