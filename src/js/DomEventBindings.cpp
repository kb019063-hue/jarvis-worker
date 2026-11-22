// Events: EventTarget, Event and its subclasses, the dispatch algorithm,
// `on<type>` handlers, AbortController, and what happens to an exception
// nothing caught.

#include "litebird/js/DomBindings.hpp"
#include "litebird/js/EventLoop.hpp"

#include <chrono>

namespace litebird {
namespace dom {

namespace {

// ---------------------------------------------------------------------------
// Per-realm state
// ---------------------------------------------------------------------------

struct EventsPart : RealmPart {
    std::chrono::steady_clock::time_point origin{EventLoop::default_loop().now()};
    /// `window.event` while a listener runs.
    JsValue current_event;
    /// An uncaught exception is being reported; one thrown by the `error`
    /// listeners themselves is not reported again.
    bool reporting{false};
};

// ---------------------------------------------------------------------------
// Event objects
// ---------------------------------------------------------------------------

enum class FieldKind { Bool, Long, Double, String, NullableString, Object, Any, AnyUndefined };

struct FieldDef {
    const char* name;
    FieldKind kind;
    double number_default;
    const char* string_default;
};

struct EventClass {
    const char* name;
    const char* parent;
    std::vector<FieldDef> fields;
};

FieldDef f_bool(const char* name) { return {name, FieldKind::Bool, 0, ""}; }
FieldDef f_long(const char* name, double fallback = 0) { return {name, FieldKind::Long, fallback, ""}; }
FieldDef f_double(const char* name, double fallback = 0) { return {name, FieldKind::Double, fallback, ""}; }
FieldDef f_string(const char* name, const char* fallback = "") { return {name, FieldKind::String, 0, fallback}; }
FieldDef f_nullable_string(const char* name) { return {name, FieldKind::NullableString, 0, ""}; }
FieldDef f_object(const char* name) { return {name, FieldKind::Object, 0, ""}; }
FieldDef f_any(const char* name) { return {name, FieldKind::Any, 0, ""}; }
FieldDef f_any_undefined(const char* name) { return {name, FieldKind::AnyUndefined, 0, ""}; }

const std::vector<EventClass>& event_classes() {
    static const std::vector<EventClass> s_classes = {
        {"UIEvent", "Event", {f_object("view"), f_long("detail")}},
        {"MouseEvent", "UIEvent",
         {f_double("screenX"), f_double("screenY"), f_double("clientX"), f_double("clientY"), f_bool("ctrlKey"),
          f_bool("shiftKey"), f_bool("altKey"), f_bool("metaKey"), f_long("button"), f_long("buttons"),
          f_object("relatedTarget"), f_double("movementX"), f_double("movementY")}},
        {"PointerEvent", "MouseEvent",
         {f_long("pointerId"), f_double("width", 1), f_double("height", 1), f_double("pressure"),
          f_double("tangentialPressure"), f_long("tiltX"), f_long("tiltY"), f_long("twist"),
          f_double("altitudeAngle"), f_double("azimuthAngle"), f_string("pointerType"), f_bool("isPrimary")}},
        {"WheelEvent", "MouseEvent", {f_double("deltaX"), f_double("deltaY"), f_double("deltaZ"), f_long("deltaMode")}},
        {"KeyboardEvent", "UIEvent",
         {f_string("key"), f_string("code"), f_long("location"), f_bool("repeat"), f_bool("isComposing"),
          f_bool("ctrlKey"), f_bool("shiftKey"), f_bool("altKey"), f_bool("metaKey"), f_long("charCode"),
          f_long("keyCode")}},
        {"FocusEvent", "UIEvent", {f_object("relatedTarget")}},
        {"InputEvent", "UIEvent", {f_nullable_string("data"), f_bool("isComposing"), f_string("inputType")}},
        {"CompositionEvent", "UIEvent", {f_string("data")}},
        {"TouchEvent", "UIEvent", {f_bool("ctrlKey"), f_bool("shiftKey"), f_bool("altKey"), f_bool("metaKey")}},
        {"CustomEvent", "Event", {f_any("detail")}},
        {"ErrorEvent", "Event",
         {f_string("message"), f_string("filename"), f_long("lineno"), f_long("colno"), f_any_undefined("error")}},
        {"HashChangeEvent", "Event", {f_string("oldURL"), f_string("newURL")}},
        {"PopStateEvent", "Event", {f_any("state")}},
        {"ProgressEvent", "Event", {f_bool("lengthComputable"), f_double("loaded"), f_double("total")}},
        {"SubmitEvent", "Event", {f_object("submitter")}},
        {"FormDataEvent", "Event", {f_object("formData")}},
        {"PageTransitionEvent", "Event", {f_bool("persisted")}},
        {"StorageEvent", "Event",
         {f_nullable_string("key"), f_nullable_string("oldValue"), f_nullable_string("newValue"), f_string("url"),
          f_object("storageArea")}},
        {"MessageEvent", "Event",
         {f_any("data"), f_string("origin"), f_string("lastEventId"), f_object("source"), f_any("ports")}},
        {"CloseEvent", "Event", {f_bool("wasClean"), f_long("code"), f_string("reason")}},
        {"TransitionEvent", "Event", {f_string("propertyName"), f_double("elapsedTime"), f_string("pseudoElement")}},
        {"AnimationEvent", "Event", {f_string("animationName"), f_double("elapsedTime"), f_string("pseudoElement")}},
        {"PromiseRejectionEvent", "Event", {f_object("promise"), f_any_undefined("reason")}},
        {"BeforeUnloadEvent", "Event", {}},
        {"ToggleEvent", "Event", {f_string("oldState"), f_string("newState")}},
        {"DeviceMotionEvent", "Event", {f_double("interval")}},
        {"DeviceOrientationEvent", "Event", {f_bool("absolute")}},
    };
    return s_classes;
}

const EventClass* find_event_class(const std::string& name) {
    for (const auto& cls : event_classes()) {
        if (name == cls.name) return &cls;
    }
    return nullptr;
}

struct EventState : JsExotic {
    std::string type;
    bool bubbles{false};
    bool cancelable{false};
    bool composed{false};
    bool trusted{false};
    bool initialized{true};
    bool dispatching{false};
    bool stop_propagation{false};
    bool stop_immediate{false};
    bool canceled{false};
    bool in_passive_listener{false};
    int phase{0};
    double time_stamp{0};
    JsValue target;
    JsValue current_target;
    std::vector<JsValue> path;
    /// The attributes the subclasses add, by name.
    std::vector<std::pair<std::string, JsValue>> fields;

    JsValue* field(const std::string& name) {
        for (auto& entry : fields) {
            if (entry.first == name) return &entry.second;
        }
        return nullptr;
    }
    void set_field(const std::string& name, JsValue value) {
        if (JsValue* existing = field(name)) *existing = std::move(value);
        else fields.emplace_back(name, std::move(value));
    }
    void trace(Heap& heap) override {
        heap.mark_value(target);
        heap.mark_value(current_target);
        for (const auto& entry : path) heap.mark_value(entry);
        for (const auto& entry : fields) heap.mark_value(entry.second);
    }
};

EventState& this_event() {
    return this_state<EventState>("Event");
}

JsValue default_of(const FieldDef& def) {
    switch (def.kind) {
        case FieldKind::Bool: return boolean(false);
        case FieldKind::Long:
        case FieldKind::Double: return num(def.number_default);
        case FieldKind::String: return str(def.string_default);
        case FieldKind::NullableString:
        case FieldKind::Object:
        case FieldKind::Any: return null();
        case FieldKind::AnyUndefined: return undefined();
    }
    return undefined();
}

JsValue convert_field(const FieldDef& def, const JsValue& value) {
    switch (def.kind) {
        case FieldKind::Bool: return boolean(to_bool(value));
        case FieldKind::Long: return num(to_long(value));
        case FieldKind::Double: {
            const double number = to_double(value);
            if (!std::isfinite(number)) js_throw_type_error(std::string("The provided double value for '") + def.name + "' is non-finite.");
            return num(number);
        }
        case FieldKind::String: return str(to_str(value));
        case FieldKind::NullableString: return value.is_nullish() ? null() : str(to_str(value));
        case FieldKind::Object:
            if (value.is_nullish()) return null();
            if (!value.is_object_like()) js_throw_type_error(std::string("The provided value for '") + def.name + "' is not an object.");
            return value;
        case FieldKind::Any:
        case FieldKind::AnyUndefined: return value;
    }
    return value;
}

// The classes from Event down to `name`, base first.
std::vector<const EventClass*> class_chain(const std::string& name) {
    std::vector<const EventClass*> chain;
    for (const EventClass* cls = find_event_class(name); cls; cls = find_event_class(cls->parent)) chain.push_back(cls);
    return {chain.rbegin(), chain.rend()};
}

/// A `FrozenArray` as WebIDL makes one: an ordinary array that is frozen, so
/// the array a script reads cannot be grown or written and `Object.isFrozen`
/// answers true.
///
/// HTML §9.1 declares `ports` as `FrozenArray<MessagePort>`, and the engine
/// handed scripts a plain mutable array: `event.ports.push(1)` grew it to one
/// element where a browser throws a TypeError. `Object.isFrozen` said `false`.
JsValue frozen_array(const std::vector<JsValue>& elements) {
    JsValue array = JsValue::make_array(elements);
    if (JsObject* cell = array.property_cell(false)) {
        cell->frozen = true;
        cell->sealed = true;
        cell->extensible = false;
    }
    return array;
}

void fill_defaults(EventState& state, const std::string& iface) {
    for (const EventClass* cls : class_chain(iface)) {
        for (const auto& def : cls->fields) state.set_field(def.name, default_of(def));
    }
    // `ports` is stored as the elements it was built from and frozen when it is
    // read, so each read is a fresh array as the IDL requires.
    if (iface == "MessageEvent") state.set_field("ports", frozen_array({}));
}

JsValue new_event(Realm& realm, const std::string& iface, const JsValue* derived) {
    auto state = std::make_shared<EventState>();
    state->time_stamp = time_now(realm);
    fill_defaults(*state, iface);
    return realm.make(iface.c_str(), state, derived);
}

JsValue construct_event(Realm& realm, const InterfaceDef& def, Args args, const JsValue* derived) {
    require_args(args, 1, (std::string(def.name) + " constructor").c_str());
    const std::string type = to_str(args[0]);
    const JsValue& init = js_arg(args, 1);
    if (!init.is_nullish() && !init.is_object_like()) {
        js_throw_type_error(std::string("Failed to construct '") + def.name + "': the init argument is not an object.");
    }
    JsValue event = new_event(realm, def.name, derived);
    EventState& state = *state_of<EventState>(event);
    state.type = type;
    if (init.is_object_like()) {
        state.bubbles = to_bool(init.get_property("bubbles"));
        state.cancelable = to_bool(init.get_property("cancelable"));
        state.composed = to_bool(init.get_property("composed"));
        for (const EventClass* cls : class_chain(def.name)) {
            for (const auto& field : cls->fields) {
                JsValue value = init.get_property(field.name);
                if (!value.is_undefined()) state.set_field(field.name, convert_field(field, value));
            }
        }
    }
    return event;
}

void setup_event(Iface& I) {
    I.constant("NONE", 0);
    I.constant("CAPTURING_PHASE", 1);
    I.constant("AT_TARGET", 2);
    I.constant("BUBBLING_PHASE", 3);
    I.getter("type", [](Realm&) { return str(this_event().type); });
    I.getter("target", [](Realm&) {
        EventState& state = this_event();
        return state.target.is_undefined() ? null() : state.target;
    });
    I.alias("srcElement", "target");
    I.getter("currentTarget", [](Realm&) {
        EventState& state = this_event();
        return state.current_target.is_undefined() ? null() : state.current_target;
    });
    I.getter("eventPhase", [](Realm&) { return num(this_event().phase); });
    I.getter("bubbles", [](Realm&) { return boolean(this_event().bubbles); });
    I.getter("cancelable", [](Realm&) { return boolean(this_event().cancelable); });
    I.getter("composed", [](Realm&) { return boolean(this_event().composed); });
    I.getter("defaultPrevented", [](Realm&) { return boolean(this_event().canceled); });
    I.getter("isTrusted", [](Realm&) { return boolean(this_event().trusted); });
    I.getter("timeStamp", [](Realm&) { return num(this_event().time_stamp); });
    I.accessor("cancelBubble",
        [](Realm&) { return boolean(this_event().stop_propagation); },
        [](Realm&, const JsValue& value) {
            if (to_bool(value)) this_event().stop_propagation = true;
        });
    I.accessor("returnValue",
        [](Realm&) { return boolean(!this_event().canceled); },
        [](Realm&, const JsValue& value) {
            EventState& state = this_event();
            if (!to_bool(value) && state.cancelable && !state.in_passive_listener) state.canceled = true;
        });
    I.method("stopPropagation", 0, [](Realm&, Args) {
        this_event().stop_propagation = true;
        return undefined();
    });
    I.method("stopImmediatePropagation", 0, [](Realm&, Args) {
        EventState& state = this_event();
        state.stop_propagation = true;
        state.stop_immediate = true;
        return undefined();
    });
    I.method("preventDefault", 0, [](Realm&, Args) {
        EventState& state = this_event();
        if (state.cancelable && !state.in_passive_listener) state.canceled = true;
        return undefined();
    });
    I.method("composedPath", 0, [](Realm&, Args) {
        EventState& state = this_event();
        return JsValue::make_array(state.path);
    });
    I.method("initEvent", 1, [](Realm&, Args args) {
        require_args(args, 1, "Event.initEvent");
        EventState& state = this_event();
        if (state.dispatching) return undefined();
        state.type = to_str(args[0]);
        state.bubbles = to_bool(js_arg(args, 1));
        state.cancelable = to_bool(js_arg(args, 2));
        state.initialized = true;
        state.stop_propagation = state.stop_immediate = state.canceled = false;
        state.trusted = false;
        state.target = JsValue();
        return undefined();
    });
}

JsValue field_value(const char* name) {
    EventState& state = this_event();
    JsValue* value = state.field(name);
    return value ? *value : undefined();
}

bool modifier_state(EventState& state, const std::string& key) {
    const char* field = key == "Control" ? "ctrlKey" : key == "Shift" ? "shiftKey" : key == "Alt" ? "altKey"
                      : key == "Meta" ? "metaKey" : nullptr;
    if (!field) return false;
    JsValue* value = state.field(field);
    return value && value->is_truthy();
}

// Shared by every Event subclass: one getter per field of the class.
void setup_event_class(Iface& I) {
    const auto* cls = static_cast<const EventClass*>(I.def.data);
    for (const auto& field : cls->fields) {
        const char* name = field.name;
        I.getter(name, [name](Realm&) { return field_value(name); });
    }
    const std::string name = cls->name;
    if (name == "MessageEvent") {
        // HTML §9.1: `ports` is a `FrozenArray<MessagePort>`. WebIDL says the
        // attribute "must return the value it was initialized to", so it is
        // **one** frozen array, not a fresh one per read: Node answers
        // `e.ports === e.ports` true and `Object.isFrozen(e.ports)` true, and
        // so does this. The generic field getter would hand back the same array
        // too, but an unfrozen one; this replaces it so the array a script gets
        // cannot be grown (`e.ports.push(1)` is a TypeError).
        I.getter("ports", [](Realm&) {
            JsValue* stored = this_event().field("ports");
            return stored ? *stored : frozen_array({});
        });
        // HTML §9.1 `initMessageEvent(type, bubbles, cancelable, data, origin,
        // lastEventId, source, ports)`. It was declared by the standard and
        // absent from the engine: `new MessageEvent("x").initMessageEvent`
        // was `undefined`, so a script that initialized an existing event — the
        // only reason the method exists — hit a TypeError.
        I.method("initMessageEvent", 0, [](Realm&, Args args) {
            EventState& state = this_event();
            // HTML §9.1: "If the dispatch flag is set, or the *initialized
            // flag* is set, then return." A constructor sets initialized, so
            // `new MessageEvent("x").initMessageEvent("y", ...)` does nothing
            // at all — Node answers type "x" and data null, and so must this.
            // The method exists for `document.createEvent("[Message]Event")`,
            // which leaves the flag unset.
            if (state.dispatching || state.initialized) return undefined();
            state.type = to_str(js_arg(args, 0));
            state.bubbles = to_bool(js_arg(args, 1));
            state.cancelable = to_bool(js_arg(args, 2));
            state.set_field("data", js_arg(args, 3));
            state.set_field("origin", str(to_str(js_arg(args, 4))));
            state.set_field("lastEventId", str(to_str(js_arg(args, 5))));
            JsValue source = js_arg(args, 6);
            state.set_field("source", source.is_nullish() ? null() : source);
            state.set_field("ports", frozen_array({}));
            // `initialized` is what `dispatchEvent` refuses an event without;
            // initMessageEvent leaves it set, as initEvent does.
            state.initialized = true;
            return undefined();
        });
    }
    if (name == "UIEvent") {
        I.getter("which", [](Realm&) {
            EventState& state = this_event();
            if (JsValue* key_code = state.field("keyCode")) return *key_code;
            if (JsValue* button = state.field("button")) return num(to_long(*button) + 1);
            return num(0);
        });
        I.method("initUIEvent", 1, [](Realm&, Args args) {
            EventState& state = this_event();
            if (state.dispatching) return undefined();
            state.type = to_str(js_arg(args, 0));
            state.bubbles = to_bool(js_arg(args, 1));
            state.cancelable = to_bool(js_arg(args, 2));
            state.set_field("view", js_arg(args, 3).is_undefined() ? null() : js_arg(args, 3));
            state.set_field("detail", num(to_long(js_arg(args, 4))));
            state.initialized = true;
            return undefined();
        });
    } else if (name == "MouseEvent") {
        I.getter("pageX", [](Realm&) { return field_value("clientX"); });
        I.getter("pageY", [](Realm&) { return field_value("clientY"); });
        I.getter("x", [](Realm&) { return field_value("clientX"); });
        I.getter("y", [](Realm&) { return field_value("clientY"); });
        I.getter("offsetX", [](Realm&) { return field_value("clientX"); });
        I.getter("offsetY", [](Realm&) { return field_value("clientY"); });
        I.method("getModifierState", 1, [](Realm&, Args args) {
            require_args(args, 1, "MouseEvent.getModifierState");
            return boolean(modifier_state(this_event(), to_str(args[0])));
        });
        I.method("initMouseEvent", 1, [](Realm&, Args args) {
            EventState& state = this_event();
            if (state.dispatching) return undefined();
            state.type = to_str(js_arg(args, 0));
            state.bubbles = to_bool(js_arg(args, 1));
            state.cancelable = to_bool(js_arg(args, 2));
            state.set_field("view", js_arg(args, 3).is_undefined() ? null() : js_arg(args, 3));
            state.set_field("detail", num(to_long(js_arg(args, 4))));
            static const char* const kNumbers[] = {"screenX", "screenY", "clientX", "clientY"};
            for (int i = 0; i < 4; ++i) state.set_field(kNumbers[i], num(to_long(js_arg(args, 5 + i))));
            static const char* const kKeys[] = {"ctrlKey", "altKey", "shiftKey", "metaKey"};
            for (int i = 0; i < 4; ++i) state.set_field(kKeys[i], boolean(to_bool(js_arg(args, 9 + i))));
            state.set_field("button", num(to_long(js_arg(args, 13))));
            state.set_field("relatedTarget", js_arg(args, 14).is_undefined() ? null() : js_arg(args, 14));
            state.initialized = true;
            return undefined();
        });
    } else if (name == "KeyboardEvent") {
        I.constant("DOM_KEY_LOCATION_STANDARD", 0);
        I.constant("DOM_KEY_LOCATION_LEFT", 1);
        I.constant("DOM_KEY_LOCATION_RIGHT", 2);
        I.constant("DOM_KEY_LOCATION_NUMPAD", 3);
        I.method("getModifierState", 1, [](Realm&, Args args) {
            require_args(args, 1, "KeyboardEvent.getModifierState");
            return boolean(modifier_state(this_event(), to_str(args[0])));
        });
    } else if (name == "WheelEvent") {
        I.constant("DOM_DELTA_PIXEL", 0);
        I.constant("DOM_DELTA_LINE", 1);
        I.constant("DOM_DELTA_PAGE", 2);
    } else if (name == "CustomEvent") {
        I.method("initCustomEvent", 1, [](Realm&, Args args) {
            require_args(args, 1, "CustomEvent.initCustomEvent");
            EventState& state = this_event();
            if (state.dispatching) return undefined();
            state.type = to_str(args[0]);
            state.bubbles = to_bool(js_arg(args, 1));
            state.cancelable = to_bool(js_arg(args, 2));
            state.set_field("detail", js_arg(args, 3).is_undefined() ? null() : js_arg(args, 3));
            state.initialized = true;
            return undefined();
        });
    }
}

// ---------------------------------------------------------------------------
// Listeners
// ---------------------------------------------------------------------------

struct Listener {
    std::string type;
    /// A function, or an object with a handleEvent method. Unused for the
    /// `on<type>` handler, which is read when it runs.
    JsValue callback;
    bool capture{false};
    bool once{false};
    bool passive{false};
    bool removed{false};
    bool is_handler{false};
};

struct ListenerStore : JsExotic {
    std::vector<std::shared_ptr<Listener>> items;
    /// `on<type>` handlers: a function, or null once a script has cleared it.
    std::unordered_map<std::string, JsValue> handlers;
    /// Handlers compiled from an `on…` attribute, with the source they were
    /// compiled from.
    std::unordered_map<std::string, std::string> compiled_from;

    void trace(Heap& heap) override {
        for (const auto& listener : items) heap.mark_value(listener->callback);
        for (const auto& entry : handlers) heap.mark_value(entry.second);
    }
};

ListenerStore* listeners_of(Realm& realm, const JsValue& target, bool create) {
    if (const JsValue* existing = slot(target, "listeners")) return state_of<ListenerStore>(*existing);
    if (!create) return nullptr;
    auto store = std::make_shared<ListenerStore>();
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = realm.intrinsics->object_proto;
    cell->exotic = store;
    set_slot(target, "listeners", JsValue::make_object(cell));
    return store.get();
}

JsValue event_target_receiver(Realm& realm) {
    const JsValue& self = JsValue::current_this();
    if (self.is_nullish() && realm.window.is_object()) return realm.window;
    if (!is_event_target(realm, self)) throw_illegal_invocation("EventTarget");
    return self;
}

struct ListenerOptions {
    bool capture{false};
    bool once{false};
    bool passive{false};
    JsValue signal;
};

ListenerOptions read_options(const JsValue& value, bool for_removal) {
    ListenerOptions options;
    if (value.is_object_like()) {
        options.capture = to_bool(value.get_property("capture"));
        if (!for_removal) {
            options.once = to_bool(value.get_property("once"));
            options.passive = to_bool(value.get_property("passive"));
            options.signal = value.get_property("signal");
        }
    } else {
        options.capture = to_bool(value);
    }
    return options;
}

void remove_listener(ListenerStore& store, const std::shared_ptr<Listener>& listener) {
    listener->removed = true;
    for (auto it = store.items.begin(); it != store.items.end(); ++it) {
        if (*it == listener) {
            store.items.erase(it);
            return;
        }
    }
}

JsValue construct_event_target(Realm& realm, const InterfaceDef&, Args, const JsValue* derived) {
    return realm.make("EventTarget", nullptr, derived);
}

void setup_event_target(Iface& I) {
    I.method("addEventListener", 2, [](Realm& r, Args args) {
        JsValue target = event_target_receiver(r);
        require_args(args, 2, "EventTarget.addEventListener");
        const std::string type = to_str(args[0]);
        const JsValue& callback = args[1];
        if (callback.is_nullish()) return undefined();
        if (!callback.is_object_like()) {
            js_throw_type_error("Failed to execute 'addEventListener' on 'EventTarget': parameter 2 is not of type 'Object'.");
        }
        ListenerOptions options = read_options(js_arg(args, 2), false);
        if (!options.signal.is_undefined()) {
            bool aborted = false;
            if (!on_abort(options.signal, nullptr, aborted)) {
                js_throw_type_error("Failed to execute 'addEventListener' on 'EventTarget': member signal is not of type 'AbortSignal'.");
            }
            if (aborted) return undefined();
        }
        ListenerStore& store = *listeners_of(r, target, true);
        for (const auto& existing : store.items) {
            if (!existing->is_handler && existing->type == type && existing->capture == options.capture &&
                strict_equals(existing->callback, callback)) {
                return undefined();
            }
        }
        auto listener = std::make_shared<Listener>();
        listener->type = type;
        listener->callback = callback;
        listener->capture = options.capture;
        listener->once = options.once;
        listener->passive = options.passive;
        store.items.push_back(listener);
        if (!options.signal.is_undefined()) {
            // The store lives as long as the target's object; the listener
            // is found again through it, so nothing here outlives either.
            std::weak_ptr<Listener> weak = listener;
            JsValue holder = target;
            Realm* realm = &r;
            bool aborted = false;
            on_abort(options.signal, [weak, holder, realm]() {
                auto held = weak.lock();
                if (!held || !realm->alive) return;
                if (ListenerStore* current = listeners_of(*realm, holder, false)) remove_listener(*current, held);
            }, aborted);
        }
        return undefined();
    });
    I.method("removeEventListener", 2, [](Realm& r, Args args) {
        JsValue target = event_target_receiver(r);
        require_args(args, 2, "EventTarget.removeEventListener");
        const std::string type = to_str(args[0]);
        const JsValue& callback = args[1];
        if (callback.is_nullish()) return undefined();
        const ListenerOptions options = read_options(js_arg(args, 2), true);
        ListenerStore* store = listeners_of(r, target, false);
        if (!store) return undefined();
        for (const auto& existing : store->items) {
            if (!existing->is_handler && existing->type == type && existing->capture == options.capture &&
                strict_equals(existing->callback, callback)) {
                remove_listener(*store, existing);
                break;
            }
        }
        return undefined();
    });
    I.method("dispatchEvent", 1, [](Realm& r, Args args) {
        JsValue target = event_target_receiver(r);
        require_args(args, 1, "EventTarget.dispatchEvent");
        EventState* state = state_of<EventState>(args[0]);
        if (!state) {
            js_throw_type_error("Failed to execute 'dispatchEvent' on 'EventTarget': parameter 1 is not of type 'Event'.");
        }
        if (state->dispatching || !state->initialized) {
            throw_dom(r, "InvalidStateError", "The event is already being dispatched or has not been initialized.");
        }
        return boolean(dispatch(r, target, args[0], false));
    });
}

// ---------------------------------------------------------------------------
// Event handlers: on<type>
// ---------------------------------------------------------------------------

const char* const kGlobalHandlers[] = {
    "abort", "auxclick", "beforeinput", "beforematch", "beforetoggle", "blur", "cancel", "canplay",
    "canplaythrough", "change", "click", "close", "contextlost", "contextmenu", "contextrestored", "copy",
    "cuechange", "cut", "dblclick", "drag", "dragend", "dragenter", "dragleave", "dragover", "dragstart", "drop",
    "durationchange", "emptied", "ended", "error", "focus", "formdata", "input", "invalid", "keydown", "keypress",
    "keyup", "load", "loadeddata", "loadedmetadata", "loadstart", "mousedown", "mouseenter", "mouseleave",
    "mousemove", "mouseout", "mouseover", "mouseup", "paste", "pause", "play", "playing", "progress", "ratechange",
    "reset", "resize", "scroll", "scrollend", "securitypolicyviolation", "seeked", "seeking", "select",
    "slotchange", "stalled", "submit", "suspend", "timeupdate", "toggle", "volumechange", "waiting", "wheel",
    "touchstart", "touchend", "touchmove", "touchcancel", "animationstart", "animationiteration", "animationend",
    "animationcancel", "transitionrun", "transitionstart", "transitionend", "transitioncancel", "pointerdown",
    "pointerup", "pointermove", "pointerover", "pointerout", "pointerenter", "pointerleave", "pointercancel",
    "gotpointercapture", "lostpointercapture", "selectstart", "selectionchange", "readystatechange",
    "visibilitychange", "fullscreenchange", "fullscreenerror",
};

const char* const kWindowHandlers[] = {
    "afterprint", "beforeprint", "beforeunload", "hashchange", "languagechange", "message", "messageerror",
    "offline", "online", "pagehide", "pageshow", "popstate", "rejectionhandled", "storage", "unhandledrejection",
    "unload",
};

// Handlers that, written on <body>, belong to the window.
bool is_window_reflecting(const std::string& type) {
    static const char* const kReflecting[] = {"blur", "error", "focus", "load", "resize", "scroll"};
    for (const char* name : kReflecting) {
        if (type == name) return true;
    }
    for (const char* name : kWindowHandlers) {
        if (type == name) return true;
    }
    return false;
}

bool is_body_like(const Node* node) {
    return node && (is_html_element(*node, "body") || is_html_element(*node, "frameset"));
}

// The element whose `on<type>` attribute supplies the handler of `target`.
Element* handler_attribute_source(Realm& realm, const JsValue& target, const std::string& type) {
    if (Node* node = node_ptr(target)) {
        if (!node->is_element()) return nullptr;
        if (is_body_like(node) && is_window_reflecting(type)) return nullptr;       // that one is the window's
        return static_cast<Element*>(node);
    }
    if (realm.window.is_object() && target.object_val == realm.window.object_val && realm.document &&
        is_window_reflecting(type)) {
        auto body = realm.document->body();
        return body ? body.get() : nullptr;
    }
    return nullptr;
}

void ensure_handler_listener(ListenerStore& store, const std::string& type, bool at_front) {
    for (const auto& existing : store.items) {
        if (existing->is_handler && existing->type == type) return;
    }
    auto listener = std::make_shared<Listener>();
    listener->type = type;
    listener->is_handler = true;
    if (at_front) store.items.insert(store.items.begin(), listener);
    else store.items.push_back(listener);
}

// Compiles the source of an `on…` attribute. The global Function constructor
// does the work, so the handler is an ordinary function of one parameter.
JsValue compile_handler(Realm& realm, const std::string& source) {
    if (!realm.window.is_object()) return null();
    JsValue function_ctor = realm.window.get_property("Function");
    if (!function_ctor.is_function()) return null();
    JsValue compiled = null();
    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        std::vector<JsValue> args;
        args.push_back(str("event"));
        args.push_back(str(source));
        compiled = js_construct(function_ctor, args);
    }, thrown);
    if (!ok) {
        js_report_uncaught(thrown);
        return null();
    }
    return compiled;
}

} // namespace

JsValue get_event_handler(Realm& realm, const JsValue& target, const std::string& type) {
    ListenerStore* store = listeners_of(realm, target, false);
    Element* source = handler_attribute_source(realm, target, type);
    const std::string* text = source ? source->attribute("on" + type) : nullptr;
    if (store) {
        auto found = store->handlers.find(type);
        if (found != store->handlers.end()) {
            auto compiled = store->compiled_from.find(type);
            // A handler a script assigned, or one compiled from the text the
            // attribute still has.
            if (compiled == store->compiled_from.end() || (text && *text == compiled->second)) return found->second;
        }
    }
    if (!text) return null();
    if (!store) store = listeners_of(realm, target, true);
    JsValue compiled = compile_handler(realm, *text);
    store->handlers[type] = compiled;
    store->compiled_from[type] = *text;
    ensure_handler_listener(*store, type, true);
    return compiled;
}

void set_event_handler(Realm& realm, const JsValue& target, const std::string& type, const JsValue& handler) {
    ListenerStore& store = *listeners_of(realm, target, true);
    store.compiled_from.erase(type);
    store.handlers[type] = handler.is_object_like() ? handler : null();
    if (handler.is_object_like()) ensure_handler_listener(store, type, false);
}

void event_handler_attribute_changed(Realm& realm, Element& element, const std::string& name) {
    if (name.size() < 3 || name.compare(0, 2, "on") != 0) return;
    const std::string type = name.substr(2);
    JsValue target;
    if (is_body_like(&element) && is_window_reflecting(type)) target = realm.window;
    else if (element.script_object()) target = wrap(realm, element);
    if (!target.is_object()) return;
    if (ListenerStore* store = listeners_of(realm, target, false)) {
        // The attribute is the handler from now on, whatever a script had set.
        store->handlers.erase(type);
        store->compiled_from.erase(type);
    }
}

namespace {

JsValue handler_target(Realm& realm, const std::string& type) {
    const JsValue& self = JsValue::current_this();
    if (self.is_nullish() && realm.window.is_object()) return realm.window;
    if (is_body_like(node_ptr(self)) && is_window_reflecting(type) && realm.window.is_object() &&
        document_of(realm, *node_ptr(self)) == realm.document) {
        return realm.window;
    }
    if (!is_event_target(realm, self)) throw_illegal_invocation("EventTarget");
    return self;
}

void define_handler(Iface& I, const char* type) {
    const std::string name = std::string("on") + type;
    const std::string event_type = type;
    Realm& realm = I.realm;
    define_accessor_value(I.proto, name.c_str(),
        make_function(realm, name.c_str(), 0, [event_type](Realm& r, Args) -> JsValue {
            return get_event_handler(r, handler_target(r, event_type), event_type);
        }),
        make_function(realm, name.c_str(), 1, [event_type](Realm& r, Args args) -> JsValue {
            set_event_handler(r, handler_target(r, event_type), event_type, js_arg(args, 0));
            return JsValue::make_undefined();
        }));
}

} // namespace

void define_event_handlers(Iface& I, bool window_reflecting) {
    for (const char* type : kGlobalHandlers) define_handler(I, type);
    if (window_reflecting) {
        for (const char* type : kWindowHandlers) define_handler(I, type);
    }
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

namespace {

struct PathEntry {
    JsValue object;
    /// What `event.target` is for listeners here: the target itself, or the
    /// host of the shadow tree it is in.
    JsValue target;
    bool at_target{false};
    Node* node{nullptr};
};

void call_listener(Realm& realm, EventState& state, const JsValue& event, const JsValue& current,
                   const std::shared_ptr<Listener>& listener) {
    EventsPart& part = realm.part<EventsPart>();
    JsValue callback = listener->callback;
    if (listener->is_handler) {
        callback = get_event_handler(realm, current, state.type);
        if (!callback.is_object_like()) return;
    }
    const JsValue previous_event = part.current_event;
    part.current_event = event;
    state.in_passive_listener = listener->passive;

    JsValue result;
    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        JsScriptScope scope;
        if (listener->is_handler) {
            const bool is_window = realm.window.is_object() && current.object_val == realm.window.object_val;
            if (state.type == "error" && is_window && state.field("error")) {
                // window.onerror(message, source, line, column, error)
                result = js_call(callback, current, {*state.field("message"), *state.field("filename"),
                                                     *state.field("lineno"), *state.field("colno"),
                                                     *state.field("error")});
                if (result.type == JsType::Boolean && result.bool_val) state.canceled = true;
            } else {
                if (!callback.is_function()) return;
                result = js_call(callback, current, {event});
                if (result.type == JsType::Boolean && !result.bool_val && state.cancelable) state.canceled = true;
            }
        } else if (callback.is_function()) {
            js_call(callback, current, {event});
        } else {
            JsValue handle = callback.get_property("handleEvent");
            if (!handle.is_function()) js_throw_type_error("The listener's handleEvent is not a function");
            js_call(handle, callback, {event});
        }
    }, thrown);
    state.in_passive_listener = false;
    part.current_event = previous_event;
    if (!ok) js_report_uncaught(thrown);
}

// Runs the listeners of one object on the path. `capture_pass` says which of
// the two walks this is; an object the event is targeted at hears both kinds
// of listener, each in its walk.
void invoke_listeners(Realm& realm, EventState& state, const JsValue& event, const PathEntry& entry,
                      bool capture_pass, std::shared_ptr<Event>& native) {
    if (state.stop_propagation) return;
    state.current_target = entry.object;
    state.target = entry.target;
    state.phase = entry.at_target ? 2 : (capture_pass ? 1 : 3);

    // Listeners native code put on the node.
    if (entry.node && entry.node->has_listeners(state.type)) {
        if (!native) {
            native = std::make_shared<Event>(state.type, state.bubbles, state.cancelable);
            if (Node* target_node = node_ptr(state.target)) native->set_target(target_node->shared_from_this());
            if (JsValue* key = state.field("key")) native->key = key->to_string();
            if (JsValue* code = state.field("code")) native->code = code->to_string();
        }
        native->set_current_target(entry.node->shared_from_this());
        native->set_event_phase(static_cast<EventPhase>(state.phase));
        // At the target the native listeners of both kinds run once.
        if (!entry.at_target || capture_pass) entry.node->run_native_listeners(native, capture_pass, entry.at_target);
        if (native->default_prevented() && state.cancelable) state.canceled = true;
        if (native->propagation_stopped()) state.stop_propagation = true;
    }

    // A handler written as an attribute is a listener from the start.
    if (Element* source = handler_attribute_source(realm, entry.object, state.type)) {
        if (source->has_attribute("on" + state.type)) {
            ensure_handler_listener(*listeners_of(realm, entry.object, true), state.type, true);
        }
    }
    ListenerStore* store = listeners_of(realm, entry.object, false);
    if (!store || store->items.empty()) return;
    const std::vector<std::shared_ptr<Listener>> snapshot = store->items;
    for (const auto& listener : snapshot) {
        if (listener->removed || listener->type != state.type) continue;
        if (listener->capture != capture_pass) continue;
        if (listener->once) {
            // The store may have been replaced by nothing, never by another.
            if (ListenerStore* current = listeners_of(realm, entry.object, false)) remove_listener(*current, listener);
        }
        call_listener(realm, state, event, entry.object, listener);
        if (state.stop_immediate) break;
    }
}

} // namespace

bool dispatch(Realm& realm, const JsValue& target, const JsValue& event, bool trusted, const JsValue* shown_target) {
    EventState* found = state_of<EventState>(event);
    if (!found || !target.is_object()) return true;
    // The state is shared with the object; a listener cannot make it go away
    // while `event` is held here.
    EventState& state = *found;
    state.trusted = trusted;
    state.dispatching = true;
    // Not cleared here: an event whose propagation was stopped before it was
    // dispatched (`e.cancelBubble = true`) reaches no listener. The flags are
    // cleared when the dispatch is over.

    // The path: the target, its ancestors, and the window after the document.
    std::vector<PathEntry> path;
    NodeVector node_path;
    if (Node* start = node_ptr(target)) {
        std::shared_ptr<Node> at = start->shared_from_this();
        JsValue seen_target = target;
        bool at_target = true;
        std::shared_ptr<Node> root;
        while (at) {
            PathEntry entry;
            entry.object = wrap(realm, at);
            entry.target = seen_target;
            entry.at_target = at_target;
            entry.node = at.get();
            path.push_back(entry);
            node_path.push_back(at);
            at_target = false;
            root = at;
            auto parent = at->parent();
            if (!parent && at->is_document_fragment()) {
                if (auto* shadow = dynamic_cast<ShadowRoot*>(at.get())) {
                    if (!state.composed) break;
                    // Outside the shadow tree the event appears to come from the host.
                    parent = shadow->host();
                    if (parent) {
                        seen_target = wrap(realm, parent);
                        at_target = true;
                    }
                }
            }
            at = parent;
        }
        if (root && root->is_document() && root == realm.document && realm.window.is_object() && state.type != "load") {
            PathEntry entry;
            entry.object = realm.window;
            entry.target = seen_target;
            path.push_back(entry);
        }
    } else {
        PathEntry entry;
        entry.object = target;
        entry.target = shown_target ? *shown_target : target;
        entry.at_target = true;
        path.push_back(entry);
    }
    state.path.clear();
    for (const auto& entry : path) state.path.push_back(entry.object);
    state.target = shown_target ? *shown_target : target;

    // What a click does: a checkbox changes before its listeners run.
    Activation activation;
    const bool is_click = state.type == "click" && state.field("button") != nullptr;
    if (is_click && !node_path.empty()) {
        NodeVector candidates;
        candidates.push_back(node_path.front());
        if (state.bubbles) candidates.insert(candidates.end(), node_path.begin() + 1, node_path.end());
        activation = before_click(realm, candidates);
    }

    std::shared_ptr<Event> native;
    for (std::size_t i = path.size(); i-- > 0;) {
        invoke_listeners(realm, state, event, path[i], true, native);
    }
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!path[i].at_target && !state.bubbles) continue;
        invoke_listeners(realm, state, event, path[i], false, native);
    }

    state.phase = 0;
    state.current_target = JsValue();
    state.dispatching = false;
    state.stop_propagation = state.stop_immediate = false;
    state.path.clear();
    // After a dispatch `target` stays what it was for listeners outside any
    // shadow tree.
    state.target = path.empty() ? target : path.back().target;

    const bool cancelled = state.canceled;
    if (activation.target) after_click(realm, activation, event, cancelled);
    return !cancelled;
}

JsValue make_event(Realm& realm, const char* iface, const std::string& type, bool bubbles, bool cancelable,
                   std::vector<EventField> fields, bool composed) {
    JsValue event = new_event(realm, iface, nullptr);
    EventState& state = *state_of<EventState>(event);
    state.type = type;
    state.bubbles = bubbles;
    state.cancelable = cancelable;
    state.composed = composed;
    for (auto& field : fields) state.set_field(field.name, std::move(field.value));
    return event;
}

JsValue create_legacy_event(Realm& realm, const std::string& name) {
    static const std::pair<const char*, const char*> kLegacy[] = {
        {"beforeunloadevent", "BeforeUnloadEvent"}, {"compositionevent", "CompositionEvent"},
        {"customevent", "CustomEvent"}, {"devicemotionevent", "DeviceMotionEvent"},
        {"deviceorientationevent", "DeviceOrientationEvent"}, {"event", "Event"}, {"events", "Event"},
        {"focusevent", "FocusEvent"}, {"hashchangeevent", "HashChangeEvent"}, {"htmlevents", "Event"},
        {"keyboardevent", "KeyboardEvent"}, {"messageevent", "MessageEvent"}, {"mouseevent", "MouseEvent"},
        {"mouseevents", "MouseEvent"}, {"storageevent", "StorageEvent"}, {"svgevents", "Event"},
        {"touchevent", "TouchEvent"}, {"uievent", "UIEvent"}, {"uievents", "UIEvent"},
    };
    const std::string wanted = ascii_lower(name);
    for (const auto& entry : kLegacy) {
        if (wanted == entry.first) {
            JsValue event = new_event(realm, entry.second, nullptr);
            state_of<EventState>(event)->initialized = false;
            return event;
        }
    }
    return undefined();
}

bool fire(Realm& realm, const JsValue& target, const std::string& type, bool bubbles, bool cancelable) {
    return dispatch(realm, target, make_event(realm, "Event", type, bubbles, cancelable), true);
}

bool fire(Realm& realm, Node& target, const std::string& type, bool bubbles, bool cancelable) {
    return fire(realm, wrap(realm, target), type, bubbles, cancelable);
}

bool event_default_prevented(const JsValue& event) {
    EventState* state = state_of<EventState>(event);
    return state && state->canceled;
}

bool is_event_target(Realm& realm, const JsValue& value) {
    if (value.type != JsType::Object || !value.object_val) return false;
    JsObject* wanted = realm.proto("EventTarget");
    int guard = 0;
    for (JsObject* at = value.object_val->prototype; at && guard < kMaxPrototypeChain; at = at->prototype, ++guard) {
        if (at == wanted) return true;
    }
    return false;
}

JsValue current_event(Realm& realm) {
    return realm.part<EventsPart>().current_event;
}

double time_now(Realm& realm) {
    const auto elapsed = EventLoop::default_loop().now() - realm.part<EventsPart>().origin;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

JsValue call_reporting(Realm&, const JsValue& fn, const JsValue& this_value, const std::vector<JsValue>& args) {
    JsValue result;
    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        JsScriptScope scope;
        result = js_call(fn, this_value, args);
    }, thrown);
    if (!ok) js_report_uncaught(thrown);
    return result;
}

bool report_exception(Realm& realm, const JsValue& thrown) {
    if (!realm.alive || !realm.window.is_object()) return false;
    EventsPart& part = realm.part<EventsPart>();
    if (part.reporting) return false;
    part.reporting = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{part.reporting};

    std::string message;
    if (thrown.is_object_like()) {
        JsValue text = thrown.get_property("message");
        if (!text.is_undefined() && !text.is_object_like()) message = text.to_string();
    } else {
        message = js_describe_thrown(thrown);
    }
    const std::string source = realm.document ? realm.document->url() : std::string();
    JsValue event = make_event(realm, "ErrorEvent", "error", false, true,
                               {{"message", str(message)}, {"filename", str(source)}, {"lineno", num(0)},
                                {"colno", num(0)}, {"error", thrown}});
    return !dispatch(realm, realm.window, event, true);
}

// ---------------------------------------------------------------------------
// AbortController and AbortSignal
// ---------------------------------------------------------------------------

namespace {

struct AbortSignalState : JsExotic {
    bool aborted{false};
    JsValue reason;
    std::vector<std::function<void()>> algorithms;
    void trace(Heap& heap) override { heap.mark_value(reason); }
};

JsValue new_signal(Realm& realm) {
    return realm.make("AbortSignal", std::make_shared<AbortSignalState>());
}

void signal_abort(Realm& realm, const JsValue& signal, const JsValue& reason) {
    AbortSignalState* state = state_of<AbortSignalState>(signal);
    if (!state || state->aborted) return;
    state->aborted = true;
    state->reason = reason.is_undefined()
        ? make_dom_exception(realm, "AbortError", "signal is aborted without reason") : reason;
    auto algorithms = std::move(state->algorithms);
    state->algorithms.clear();
    for (auto& algorithm : algorithms) algorithm();
    fire(realm, signal, "abort");
}

void setup_abort_signal(Iface& I) {
    I.getter("aborted", [](Realm&) { return boolean(this_state<AbortSignalState>("AbortSignal").aborted); });
    I.getter("reason", [](Realm&) { return this_state<AbortSignalState>("AbortSignal").reason; });
    I.method("throwIfAborted", 0, [](Realm&, Args) {
        AbortSignalState& state = this_state<AbortSignalState>("AbortSignal");
        if (state.aborted) js_throw(state.reason);
        return undefined();
    });
    I.accessor("onabort",
        [](Realm& r) { return get_event_handler(r, JsValue::current_this(), "abort"); },
        [](Realm& r, const JsValue& value) { set_event_handler(r, JsValue::current_this(), "abort", value); });
    I.static_method("abort", 0, [](Realm& r, Args args) {
        JsValue signal = new_signal(r);
        AbortSignalState& state = *state_of<AbortSignalState>(signal);
        state.aborted = true;
        state.reason = js_arg(args, 0).is_undefined()
            ? make_dom_exception(r, "AbortError", "signal is aborted without reason") : args[0];
        return signal;
    });
    I.static_method("timeout", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AbortSignal.timeout");
        JsValue signal = new_signal(r);
        const double delay = to_double(args[0]);
        Realm* realm = &r;
        EventLoop::default_loop().set_timeout([realm, signal]() {
            if (!realm->alive) return;
            signal_abort(*realm, signal, make_dom_exception(*realm, "TimeoutError", "signal timed out"));
        }, delay > 0 ? static_cast<int>(std::min(delay, 2147483647.0)) : 0);
        return signal;
    });
    I.static_method("any", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AbortSignal.any");
        JsValue result = new_signal(r);
        Realm* realm = &r;
        for (const JsValue& source : js_iterable_to_vector(args[0])) {
            AbortSignalState* state = state_of<AbortSignalState>(source);
            if (!state) js_throw_type_error("AbortSignal.any: a member of the list is not an AbortSignal");
            if (state->aborted) {
                AbortSignalState& made = *state_of<AbortSignalState>(result);
                made.aborted = true;
                made.reason = state->reason;
                return result;
            }
        }
        for (const JsValue& source : js_iterable_to_vector(args[0])) {
            JsValue from = source;
            state_of<AbortSignalState>(source)->algorithms.push_back([realm, result, from]() {
                if (!realm->alive) return;
                signal_abort(*realm, result, state_of<AbortSignalState>(from)->reason);
            });
        }
        return result;
    });
}

JsValue construct_abort_controller(Realm& realm, const InterfaceDef&, Args, const JsValue* derived) {
    JsValue controller = realm.make("AbortController", nullptr, derived);
    set_slot(controller, "signal", new_signal(realm));
    return controller;
}

void setup_abort_controller(Iface& I) {
    I.getter("signal", [](Realm&) {
        const JsValue* signal = slot(JsValue::current_this(), "signal");
        if (!signal) throw_illegal_invocation("AbortController");
        return *signal;
    });
    I.method("abort", 0, [](Realm& r, Args args) {
        const JsValue* signal = slot(JsValue::current_this(), "signal");
        if (!signal) throw_illegal_invocation("AbortController");
        signal_abort(r, *signal, js_arg(args, 0));
        return undefined();
    });
}

} // namespace

bool on_abort(const JsValue& signal, std::function<void()> algorithm, bool& aborted) {
    AbortSignalState* state = state_of<AbortSignalState>(signal);
    if (!state) return false;
    aborted = state->aborted;
    if (!aborted && algorithm) state->algorithms.push_back(std::move(algorithm));
    return true;
}

void register_event_interfaces() {
    register_interface({"EventTarget", nullptr, setup_event_target, construct_event_target, 0, nullptr});
    register_interface({"Event", nullptr, setup_event, construct_event, 1, nullptr});
    for (const auto& cls : event_classes()) {
        register_interface({cls.name, cls.parent, setup_event_class, construct_event, 1, nullptr, &cls});
    }
    register_interface({"AbortSignal", "EventTarget", setup_abort_signal, nullptr, 0, nullptr});
    register_interface({"AbortController", nullptr, setup_abort_controller, construct_abort_controller, 0, nullptr});
}

} // namespace dom
} // namespace litebird
