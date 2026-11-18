// What happens because the tree changed: MutationObserver and custom
// elements. Node.cpp reports every change through DomHooks; this is where
// those reports arrive.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/JsOps.hpp"
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
    bool has_namespace{false};
    std::string attribute_namespace;
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
        const RecordState& record = this_record();
        return record.has_namespace ? str(record.attribute_namespace) : null();
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
    /// The namespace of the attribute, empty when it stands in none.
    const std::string* namespace_uri{nullptr};
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
            if (data.namespace_uri && !data.namespace_uri->empty()) {
                made->has_namespace = true;
                made->attribute_namespace = *data.namespace_uri;
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
    /// The element name its instances have: `name` itself, or the built-in
    /// element it extends (`{extends: 'button'}`).
    std::string local_name;
    JsValue constructor;
    JsValue prototype;
    std::vector<std::string> observed;
    JsValue connected;
    JsValue disconnected;
    JsValue adopted;
    JsValue attribute_changed;

    /// The HTML standard's construction stack, innermost last: the elements an
    /// upgrade is building right now. The interface's constructor takes the
    /// top entry — that is how `super()` finds the element to build on — and
    /// marks it, so a second `super()` in the same constructor is refused
    /// rather than given the element twice. `run_upgrade` owns the push and
    /// the comparison, not the constructor.
    struct Construction {
        std::shared_ptr<Element> element;
        bool taken{false};      // the standard's "already constructed" marker
    };
    std::vector<Construction> construction;
};

/// Something a custom element is to be told, or its upgrade.
struct Reaction {
    std::shared_ptr<Element> element;
    std::shared_ptr<Definition> definition;
    bool upgrade{false};
    bool done{false};
    JsValue callback;
    std::vector<JsValue> args;
};

struct CustomPart : RealmPart {
    std::vector<std::shared_ptr<Definition>> definitions;
    std::unordered_map<std::string, std::shared_ptr<PromiseData>> waiting;
    bool defining{false};
    /// Reactions waiting for the DOM call that caused them to return. Each
    /// call owns the part of the queue from its mark on (see ReactionScope).
    std::vector<Reaction> queue;

    std::shared_ptr<Definition> find(const std::string& name) const {
        for (const auto& definition : definitions) {
            if (definition->name == name) return definition;
        }
        return nullptr;
    }
};

/// The definition `element` is an instance of, or would be upgraded with.
std::shared_ptr<Definition> definition_for(Realm& realm, const Element& element) {
    if (element.is_foreign()) return nullptr;
    CustomPart* part = realm.find_part<CustomPart>();
    if (!part || part->definitions.empty()) return nullptr;
    // Custom elements exist in the window's own document. A document made
    // by a script, and the one that holds the contents of templates, has no
    // window and no definitions: what is in a <template> stays inert.
    if (auto owner = element.owner_document()) {
        if (owner != realm.document) return nullptr;
    }
    const std::string& is = element.is_value();
    if (!is.empty()) {
        auto definition = part->find(is);
        if (definition && definition->local_name == element.tag_name() && definition->name != definition->local_name) {
            return definition;
        }
        return nullptr;
    }
    auto definition = part->find(element.tag_name());
    return definition && definition->local_name == definition->name ? definition : nullptr;
}

void enqueue(Realm& realm, CustomPart& part, Reaction reaction) {
    part.queue.push_back(std::move(reaction));
    realm.reactions_queued = part.queue.size();
    // Nobody to wait for: the tree was changed by the host, not by a script
    // calling in.
    if (realm.api_depth == 0) {
        ++realm.api_depth;
        run_reactions(realm, part.queue.size() - 1);
        --realm.api_depth;
    }
}

void enqueue_callback(Realm& realm, Element& element, const std::shared_ptr<Definition>& definition,
                      const JsValue& callback, std::vector<JsValue> args) {
    if (!callback.is_function()) return;
    Reaction reaction;
    reaction.element = shared(element);
    reaction.definition = definition;
    reaction.callback = callback;
    reaction.args = std::move(args);
    enqueue(realm, realm.part<CustomPart>(), std::move(reaction));
}

bool observes(const Definition& definition, const std::string& name) {
    return definition.attribute_changed.is_function() &&
           std::find(definition.observed.begin(), definition.observed.end(), name) != definition.observed.end();
}

/// Makes an existing element an instance of the definition's class, the way
/// the standard does it: the constructor runs the ordinary way — on an object
/// of its own, with the class's prototype — and reaches the element through
/// `super()`, which takes it from the definition's construction stack. The
/// upgrade counts only if what the constructor returns *is* the element; one
/// that throws, or that returns something else, leaves the element not
/// `:defined` and with no reactions — the prototype the interface gave it when
/// it handed it out stays on it, as it does in a browser.
void run_upgrade(Realm& realm, Element& element, const std::shared_ptr<Definition>& definition) {
    if (element.custom_state() != CustomElementState::Uncustomized) return;
    auto keep = shared(element);
    CustomPart& part = realm.part<CustomPart>();
    // "Failed" until the constructor has returned, and for good if it does not
    // come back with the element: what the constructor does to its own object
    // is not reported back to it.
    element.set_custom_state(CustomElementState::FailedUpgrade);

    // What it will be told once it is constructed: its attributes as they
    // are now, and that it is in the document. These run right after the
    // constructor, before any other element's reactions.
    const std::size_t first = part.queue.size();
    {
        // Not through enqueue(): nothing here may run before the constructor.
        auto add = [&](const JsValue& callback, std::vector<JsValue> args) {
            Reaction reaction;
            reaction.element = keep;
            reaction.definition = definition;
            reaction.callback = callback;
            reaction.args = std::move(args);
            part.queue.push_back(std::move(reaction));
        };
        if (definition->attribute_changed.is_function()) {
            for (const auto& attribute : element.attributes()) {
                if (observes(*definition, attribute.name)) {
                    add(definition->attribute_changed, {str(attribute.name), null(), str(attribute.value), null()});
                }
            }
        }
        if (element.is_connected() && definition->connected.is_function()) add(definition->connected, {});
        realm.reactions_queued = part.queue.size();
    }

    // Push the element where `super()` will find it, then construct the
    // definition's constructor as `new` would: `this` is an object of its own,
    // with the constructor's prototype at this moment, and the constructor is
    // handed nothing else.
    definition->construction.push_back({keep, false});
    JsValue instance = JsValue::make_object();
    {
        const JsValue prototype = definition->constructor.get_property("prototype");
        if (prototype.is_object_like()) {
            if (JsObject* target = prototype.as_prototype()) instance.object_val->prototype = target;
        }
    }
    JsValue returned;
    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        JsScriptScope scope;
        returned = definition->constructor.construct({}, &instance);
    }, thrown);
    // Whatever happened, the entry this upgrade pushed is gone. One the
    // constructor never took is still on the top and is dropped here; one it
    // took is what it built on, and it is the feature that says whether the
    // element was reached at all.
    bool reached_the_element = false;
    if (!definition->construction.empty() && definition->construction.back().element.get() == &element) {
        reached_the_element = definition->construction.back().taken;
        definition->construction.pop_back();
    }

    // What the construction made: what the constructor returned when that is an
    // object, and `this` otherwise — which is the element when `super()` was
    // reached, since `super()` hands back the element and binds `this` to it.
    const bool is_the_element = ok &&
                                ((returned.object_val &&
                                  returned.object_val->dom_node.get() == static_cast<Node*>(&element)) ||
                                 (!returned.is_object_like() && reached_the_element));
    if (!is_the_element) {
        // It stays a plain element, and is told nothing.
        for (std::size_t i = first; i < part.queue.size(); ++i) {
            if (part.queue[i].element == keep) part.queue[i].done = true;
        }
        if (ok) {
            js_capture_throw([] {
                js_throw_type_error("The custom element constructor returned a different object.");
            }, thrown);
        }
        js_report_uncaught(thrown);
        return;
    }
    element.set_custom_state(CustomElementState::Custom);
    Node::bump_tree_version();          // `:defined` matches it now
}

void enqueue_upgrade(Realm& realm, Element& element, const std::shared_ptr<Definition>& definition) {
    Reaction reaction;
    reaction.element = shared(element);
    reaction.definition = definition;
    reaction.upgrade = true;
    enqueue(realm, realm.part<CustomPart>(), std::move(reaction));
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
        // {extends: 'button'}: a customized built-in element.
        std::string local_name = name;
        const JsValue extends = member(js_arg(args, 2), "extends");
        if (!extends.is_undefined()) {
            local_name = to_str(extends);
            if (CustomElementRegistry::is_valid_custom_element_name(local_name)) {
                throw_dom(r, "NotSupportedError", "\"" + local_name + "\" is a custom element name and cannot be extended.");
            }
            if (std::string(html_interface_for_tag(local_name)) == "HTMLUnknownElement") {
                throw_dom(r, "NotSupportedError", "\"" + local_name + "\" is not an element that can be extended.");
            }
        }
        if (part.defining) throw_dom(r, "NotSupportedError", "A definition is already being made.");
        part.defining = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{part.defining};

        auto definition = std::make_shared<Definition>();
        definition->name = name;
        definition->local_name = local_name;
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

JsObject* custom_element_prototype(Realm& realm, const Element& element) {
    auto definition = definition_for(realm, element);
    return definition && definition->prototype.is_object() ? definition->prototype.object_val.get() : nullptr;
}

void run_reactions(Realm& realm, std::size_t mark) {
    CustomPart* part = realm.find_part<CustomPart>();
    if (!part) {
        realm.reactions_queued = 0;
        return;
    }
    // Element by element, in the order the elements first came up: all that
    // one element is to be told, then the next. By index throughout: a
    // callback is script, and what it does can add to the queue.
    for (std::size_t i = mark; i < part->queue.size(); ++i) {
        if (part->queue[i].done) continue;
        const std::shared_ptr<Element> element = part->queue[i].element;
        for (std::size_t j = i; j < part->queue.size(); ++j) {
            if (part->queue[j].done || part->queue[j].element != element) continue;
            part->queue[j].done = true;
            const Reaction reaction = part->queue[j];
            if (!realm.alive) continue;
            if (reaction.upgrade) {
                run_upgrade(realm, *reaction.element, reaction.definition);
            } else {
                call_reporting(realm, reaction.callback, wrap(realm, reaction.element), reaction.args);
            }
        }
    }
    if (part->queue.size() > mark) part->queue.erase(part->queue.begin() + static_cast<std::ptrdiff_t>(mark), part->queue.end());
    realm.reactions_queued = part->queue.size();
}

void try_upgrade(Realm& realm, Element& element) {
    if (element.custom_state() != CustomElementState::Uncustomized) return;
    if (auto definition = definition_for(realm, element)) enqueue_upgrade(realm, element, definition);
}

void upgrade_subtree(Realm& realm, Node& root) {
    CustomPart* part = realm.find_part<CustomPart>();
    if (!part || part->definitions.empty()) return;
    // Listed first: a constructor is script and may change the tree.
    std::vector<std::shared_ptr<Element>> candidates;
    auto consider = [&](Node& node) {
        if (!node.is_element()) return;
        auto& element = static_cast<Element&>(node);
        if (element.custom_state() != CustomElementState::Uncustomized || element.is_foreign()) return;
        if (element.tag_name().find('-') == std::string::npos && element.is_value().empty()) return;
        if (definition_for(realm, element)) candidates.push_back(shared(element));
    };
    consider(root);
    for_each_descendant(root, [&](Node& node) {
        consider(node);
        return true;
    });
    for (const auto& element : candidates) try_upgrade(realm, *element);
}

std::shared_ptr<Element> create_custom_element(Realm& realm, Document& document, const std::string& local_name,
                                               const std::string& is, bool& failed) {
    failed = false;
    CustomPart* part = realm.find_part<CustomPart>();
    if (!part || part->definitions.empty() || realm.document.get() != &document) return nullptr;
    std::shared_ptr<Definition> definition;
    if (!is.empty()) {
        definition = part->find(is);
        if (definition && (definition->local_name != local_name || definition->name == definition->local_name)) definition = nullptr;
    } else {
        definition = part->find(local_name);
        if (definition && definition->local_name != definition->name) definition = nullptr;
    }
    if (!definition) return nullptr;

    std::shared_ptr<Element> made;
    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        JsScriptScope scope;
        JsValue result = js_construct(definition->constructor, {});
        Node* node = node_ptr(result);
        if (!node || !node->is_element() || static_cast<Element*>(node)->is_foreign()) {
            js_throw_type_error("The custom element constructor did not produce an HTML element.");
        }
        auto& element = static_cast<Element&>(*node);
        // createElement returns a new, empty element; a constructor that
        // gives its element attributes or children is at fault.
        const char* problem = !element.attributes().empty() ? "must not have attributes"
                            : !element.children().empty() ? "must not have children"
                            : element.parent() ? "must not have a parent"
                            : (element.owner_document() && element.owner_document().get() != &document) ? "must be in the same document"
                            : element.tag_name() != definition->local_name ? "must have the defined name"
                            : nullptr;
        if (problem) throw_dom(realm, "NotSupportedError", std::string("The result of the custom element constructor ") + problem + ".");
        made = shared(element);
    }, thrown);
    if (!ok) {
        js_report_uncaught(thrown);
        failed = true;
        return nullptr;
    }
    return made;
}

JsValue construct_custom_element(Realm& realm, const InterfaceDef& def, Args, const JsValue* derived) {
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
    // The class must extend the interface of the element it defines:
    // HTMLElement for a name of its own, the built-in's interface otherwise.
    const std::string expected = definition->local_name == definition->name
        ? std::string("HTMLElement") : html_interface_for_tag(definition->local_name);
    if (expected != def.name) js_throw_type_error("Illegal constructor");

    if (!definition->construction.empty()) {
        // An upgrade is building this element: take it from the definition's
        // construction stack and mark the entry. What comes back is the
        // element's own object — the one a script that reached the element
        // before the definition existed already holds — and it is given the
        // class's prototype now, which is what "the element gets the class's
        // prototype at that moment" amounts to. The object `new` made is left
        // as it was: it is not the element, and `this` becomes the element when
        // `super()` returns it. The state stays "failed" until the upgrade
        // finishes.
        Definition::Construction& entry = definition->construction.back();
        if (entry.taken) {
            js_throw_type_error("This instance is already constructed");
        }
        entry.taken = true;
        JsValue built = wrap(realm, entry.element);
        if (built.object_val) {
            JsObject* prototype = derived->object_val->prototype;
            if (!prototype && definition->prototype.object_val) prototype = definition->prototype.object_val.get();
            if (prototype) built.object_val->prototype = prototype;
        }
        // `super(...)` reports it as the object the interface built on, so the
        // constructor's `this` is the element from its return on.
        JsValue::hand_back_constructed_object(built);
        return built;
    }

    auto element = std::make_shared<Element>(definition->local_name);
    if (realm.document) element->set_owner_document(realm.document);
    if (definition->local_name != definition->name) element->set_is_value(definition->name);
    element->set_custom_state(CustomElementState::Custom);
    JsObject* cell = derived->object_val;
    cell->dom_node = element;
    cell->exotic = element_exotic(realm, *element);
    element->set_script_object(cell, realm.id);
    return *derived;
}

void note_adopted(Realm& realm, Element& element, const std::shared_ptr<Document>& from,
                  const std::shared_ptr<Document>& to) {
    if (element.custom_state() != CustomElementState::Custom || from == to) return;
    // The definition is the one it was made with; it no longer answers to
    // its new document, so it is found by its class.
    CustomPart* part = realm.find_part<CustomPart>();
    if (!part) return;
    JsObject* cell = static_cast<JsObject*>(element.script_object());
    if (!cell) return;
    int guard = 0;
    for (JsObject* proto = cell->prototype; proto && guard < kMaxPrototypeChain; proto = proto->prototype, ++guard) {
        for (const auto& candidate : part->definitions) {
            if (candidate->prototype.object_val.get() != proto) continue;
            enqueue_callback(realm, element, candidate, candidate->adopted,
                             {from ? wrap(realm, from) : null(), to ? wrap(realm, to) : null()});
            return;
        }
    }
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

void note_attribute_changed(Element& element, const AttributeChange& change) {
    const std::string& name = *change.name;
    const std::string* old_value = change.old_value;
    const std::string* new_value = change.new_value;
    Realm* realm = realm_if_any(element);
    if (!realm || !realm->alive) return;
    if (ObserverPart* part = realm->find_part<ObserverPart>()) {
        if (!part->active.empty()) {
            RecordData data("attributes");
            data.name = &name;
            data.namespace_uri = change.namespace_uri;
            data.old_value = old_value;
            queue_record(*realm, element, data);
        }
    }
    if (name.size() > 2 && name[0] == 'o' && name[1] == 'n') event_handler_attribute_changed(*realm, element, name);
    if (!new_value && old_value) {
        // The namespace is null when the attribute stands in none; the object
        // it was kept as is found by the empty name then.
        detach_attr_object(*realm, element, change.namespace_uri ? *change.namespace_uri : std::string(),
                           *change.local_name, *old_value);
    }
    if (is_html_element(element, "iframe") && (name == "src" || name == "srcdoc" || name == "name")) {
        note_iframe_attribute_changed(*realm, element, name);
    }
    if (element.custom_state() == CustomElementState::Custom) {
        auto definition = definition_for(*realm, element);
        if (definition && observes(*definition, name)) {
            enqueue_callback(*realm, element, definition, definition->attribute_changed,
                             {str(name), nullable_str(old_value), nullable_str(new_value),
                              nullable_str(change.namespace_uri)});
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
    if (is_html_element(element, "iframe")) {
        if (Realm* realm = realm_if_any(element); realm && realm->alive) {
            note_iframe_connected(*realm, element, connected);
        }
    }
    // Nearly every element is neither a custom element nor about to be one.
    if (element.custom_state() != CustomElementState::Custom &&
        (element.is_foreign() || (element.tag_name().find('-') == std::string::npos && element.is_value().empty()))) {
        return;
    }
    Realm* realm = realm_if_any(element);
    if (!realm || !realm->alive) return;
    auto definition = definition_for(*realm, element);
    if (!definition) return;
    if (element.custom_state() == CustomElementState::Uncustomized) {
        // Upgrading it also tells it that it is connected.
        if (connected) enqueue_upgrade(*realm, element, definition);
        return;
    }
    if (element.custom_state() != CustomElementState::Custom) return;
    enqueue_callback(*realm, element, definition, connected ? definition->connected : definition->disconnected, {});
}

void register_observer_interfaces() {
    register_interface({"MutationObserver", nullptr, setup_mutation_observer, construct_observer, 1, nullptr});
    register_interface({"MutationRecord", nullptr, setup_mutation_record, nullptr, 0, nullptr});
    register_interface({"CustomElementRegistry", nullptr, setup_registry, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
