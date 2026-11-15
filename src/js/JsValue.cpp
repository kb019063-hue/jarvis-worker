#include "litebird/js/JsValue.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Event.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/common/ReleaseQueue.hpp"
#include "litebird/common/StackLimit.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace litebird {

static thread_local JsValue::FunctionCaller s_function_caller = nullptr;

namespace {
/// What a dying function payload hands to the release queue: everything in it
/// that can own another function.
struct ExtraContents {
    std::shared_ptr<PropertyMap> props;
    JsValue::Extra::Payload payload;
};
}

// A closure owns the scope it was created in, the scope owns its bindings, and
// a binding can be another closure. `f = wrap(f)` in a loop therefore builds a
// chain as long as the loop ran, and letting each payload destroy the next in
// place would use native stack in proportion to it.
JsValue::Extra::~Extra() {
    if (payload.index() == 0 && !props) return;     // owns nothing
    using Queue = ReleaseQueue<ExtraContents>;
    if (Queue::busy()) {
        Queue::defer(ExtraContents{std::move(props), std::move(payload)});
        return;
    }
    Queue::Drain drain;
    props.reset();
    payload.emplace<std::monostate>();
}

void JsValue::set_function_caller(FunctionCaller caller) {
    s_function_caller = std::move(caller);
}

JsValue::Extra& JsValue::mutable_extra() {
    if (!m_extra) m_extra = std::make_shared<Extra>();
    else if (!m_extra.unique()) m_extra = std::make_shared<Extra>(*m_extra);
    return *m_extra;
}

JsValue::FunctionData& JsValue::mutable_function_data() {
    auto& payload = mutable_extra().payload;
    if (!std::holds_alternative<FunctionData>(payload)) payload = FunctionData{};
    return std::get<FunctionData>(payload);
}

const std::vector<std::string>& JsValue::function_params() const {
    static const std::vector<std::string> empty;
    if (extra()) if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->params;
    return empty;
}
const std::vector<std::shared_ptr<AstNode>>& JsValue::function_default_values() const {
    static const std::vector<std::shared_ptr<AstNode>> empty;
    if (extra()) if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->default_values;
    return empty;
}
const std::shared_ptr<AstNode>& JsValue::function_body() const {
    static const std::shared_ptr<AstNode> empty;
    if (extra()) if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->body;
    return empty;
}
const std::shared_ptr<JsEnvironment>& JsValue::function_closure() const {
    static const std::shared_ptr<JsEnvironment> empty;
    if (extra()) if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->closure;
    return empty;
}
const std::shared_ptr<BytecodeFunction>& JsValue::function_bytecode() const {
    static const std::shared_ptr<BytecodeFunction> empty;
    if (extra()) if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->bytecode_fn;
    return empty;
}
bool JsValue::function_is_async() const {
    if (extra()) if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->is_async;
    return false;
}
bool JsValue::function_is_generator() const {
    if (!extra()) return false;
    if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->is_generator;
    if (auto g = std::get_if<std::shared_ptr<GeneratorData>>(&extra()->payload)) return static_cast<bool>(*g);
    return false;
}
const JsValue::NativeFn& JsValue::native_function() const {
    static const NativeFn empty;
    if (!extra()) return empty;
    if (auto f = std::get_if<FunctionData>(&extra()->payload)) return f->native_fn;
    if (auto f = std::get_if<NativeFn>(&extra()->payload)) return *f;
    return empty;
}
const std::shared_ptr<Node>& JsValue::dom_node_value() const {
    static const std::shared_ptr<Node> empty;
    // The object of a node is an ordinary object that owns the node.
    if (type == JsType::Object && object_val) return object_val->dom_node;
    return empty;
}
const std::shared_ptr<CanvasRenderingContext2D>& JsValue::canvas_context() const {
    static const std::shared_ptr<CanvasRenderingContext2D> empty;
    if (extra()) if (auto v = std::get_if<std::shared_ptr<CanvasRenderingContext2D>>(&extra()->payload)) return *v;
    return empty;
}
const std::shared_ptr<CanvasGradient>& JsValue::canvas_gradient_value() const {
    static const std::shared_ptr<CanvasGradient> empty;
    if (extra()) if (auto v = std::get_if<std::shared_ptr<CanvasGradient>>(&extra()->payload)) return *v;
    return empty;
}
const std::shared_ptr<PromiseData>& JsValue::promise_value() const {
    static const std::shared_ptr<PromiseData> empty;
    if (extra()) if (auto v = std::get_if<std::shared_ptr<PromiseData>>(&extra()->payload)) return *v;
    return empty;
}
const std::shared_ptr<GeneratorData>& JsValue::generator_value() const {
    static const std::shared_ptr<GeneratorData> empty;
    if (extra()) if (auto v = std::get_if<std::shared_ptr<GeneratorData>>(&extra()->payload)) return *v;
    return empty;
}

void JsValue::set_function_params(std::vector<std::string> v) { mutable_function_data().params = std::move(v); }
void JsValue::add_function_default_value(std::shared_ptr<AstNode> v) { mutable_function_data().default_values.push_back(std::move(v)); }
void JsValue::set_function_default_values(std::vector<std::shared_ptr<AstNode>> v) { mutable_function_data().default_values = std::move(v); }
void JsValue::set_function_body(std::shared_ptr<AstNode> v) { mutable_function_data().body = std::move(v); }
void JsValue::set_function_closure(std::shared_ptr<JsEnvironment> v) { mutable_function_data().closure = std::move(v); }
void JsValue::set_function_bytecode(std::shared_ptr<BytecodeFunction> v) { mutable_function_data().bytecode_fn = std::move(v); }
void JsValue::set_function_async(bool v) { mutable_function_data().is_async = v; }
void JsValue::set_function_generator(bool v) { mutable_function_data().is_generator = v; }
void JsValue::set_native_function(NativeFn v) {
    if (type == JsType::Function) mutable_function_data().native_fn = std::move(v);
    else mutable_extra().payload = std::move(v);
}
void JsValue::set_canvas_context(std::shared_ptr<CanvasRenderingContext2D> v) { mutable_extra().payload = std::move(v); }
void JsValue::set_canvas_gradient_value(std::shared_ptr<CanvasGradient> v) { mutable_extra().payload = std::move(v); }
void JsValue::set_promise_value(std::shared_ptr<PromiseData> v) { mutable_extra().payload = std::move(v); }
void JsValue::set_generator_value(std::shared_ptr<GeneratorData> v) { mutable_extra().payload = std::move(v); }


const JsValue::FunctionData* JsValue::function_data() const {
    if (!extra()) return nullptr;
    return std::get_if<FunctionData>(&extra()->payload);
}

// ---------------------------------------------------------------------------
// PropertyMap
// ---------------------------------------------------------------------------

void PropertyMap::build_index() {
    m_index.clear();
    m_index.reserve(m_entries.size() * 2 + 1);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) m_index.emplace(it->first, it);
    m_index_built = true;
}

PropertyMap::iterator PropertyMap::find(const std::string& key) {
    if (m_index_built) {
        auto it = m_index.find(key);
        return it == m_index.end() ? m_entries.end() : it->second;
    }
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->first == key) return it;
    }
    return m_entries.end();
}

PropertyMap::const_iterator PropertyMap::find(const std::string& key) const {
    if (m_index_built) {
        auto it = m_index.find(key);
        return it == m_index.end() ? m_entries.end() : it->second;
    }
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->first == key) return it;
    }
    return m_entries.end();
}

PropertyMap::Entry& PropertyMap::entry(const std::string& key) {
    if (auto it = m_index_built ? m_index.find(key) : m_index.end(); it != m_index.end()) return *it->second;
    if (!m_index_built) {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->first == key) return *it;
        }
    }
    m_entries.emplace_back();
    m_entries.back().first = key;
    auto back = std::prev(m_entries.end());
    if (m_entries.size() > kIndexThreshold && !m_index_built) build_index();
    else if (m_index_built) m_index.emplace(back->first, back);
    return *back;
}

JsValue& PropertyMap::operator[](const std::string& key) {
    return entry(key).second;
}

bool PropertyMap::erase(const std::string& key) {
    auto node = find(key);
    if (node == m_entries.end()) return false;
    m_entries.erase(node);         // O(1), every other iterator and index entry stays valid
    if (m_index_built) m_index.erase(key);
    if (m_entries.size() <= kIndexThreshold / 2) {
        // Drop the bucket array once the map is small again; rebuilding it is
        // bounded by the threshold and avoids keeping a large table alive.
        m_index.clear();
        m_index_built = false;
    }
    return true;
}

void PropertyMap::clear() {
    m_entries.clear();
    m_index.clear();
    m_index_built = false;
}

// ---------------------------------------------------------------------------
// Call context: how `this` reaches a native function
// ---------------------------------------------------------------------------

namespace {

struct CallContext {
    JsValue this_val;
    const JsValue* callee{nullptr};   // the function being called; valid for the call's duration
    const JsValue* derived_this{nullptr};   // see JsValue::construct_as_super
    const JsValue* new_target{nullptr};     // see JsValue::construct; null when it is the callee
    JsValue* derived_slot{nullptr};         // see JsValue::construct_for: where derived_this is made on demand
    bool is_construct{false};
    bool fresh{false};      // set by the caller, consumed by a script function on entry
};

thread_local CallContext t_call_context;

struct CallContextScope {
    CallContext saved;
    CallContextScope(const JsValue& this_val, bool is_construct, const JsValue* callee)
        : saved(std::move(t_call_context)) {
        t_call_context.this_val = this_val;
        t_call_context.callee = callee;
        t_call_context.derived_this = nullptr;
        t_call_context.new_target = nullptr;
        t_call_context.derived_slot = nullptr;
        t_call_context.is_construct = is_construct;
        t_call_context.fresh = true;
    }
    ~CallContextScope() { t_call_context = std::move(saved); }
};

/// Canonical array index: "0", "17", but not "01", "-1" or "1.5".
bool parse_array_index(const std::string& name, size_t& out) {
    if (name.empty() || name.size() > 10) return false;
    if (name[0] == '0' && name.size() > 1) return false;
    size_t value = 0;
    for (char c : name) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<size_t>(c - '0');
    }
    if (value >= 4294967295u) return false;
    out = value;
    return true;
}

/// Keys that are engine bookkeeping rather than script-visible properties:
/// symbols ("@@..."), internal slots ("%...") and the legacy "__name__" markers.
bool is_hidden_key(const std::string& key) {
    if (key.size() >= 2 && key[0] == '@' && key[1] == '@') return true;
    if (!key.empty() && (key[0] == '%' || key[0] == '#')) return true;   // internal slots, #private names
    return key.size() >= 5 && key[0] == '_' && key[1] == '_' &&
           key[key.size() - 1] == '_' && key[key.size() - 2] == '_';
}

bool is_legacy_getter_key(const std::string& key) {
    return key.size() > 8 && key.compare(0, 6, "__get_") == 0;
}

JsValue read_entry(const PropertyMap::Entry& entry, const JsValue& receiver) {
    if (entry.flags & PropertyMap::Accessor) {
        if (entry.second.is_function()) return entry.second.call_with_this(receiver, {});
        return JsValue::make_undefined();
    }
    return entry.second;
}

/// What a script function has without storing it: `name`, `length`, and for a
/// constructor the `prototype` object, which is made when it is first asked
/// for and kept in `cell` (the function's own cell; null when it has none and
/// only `name` and `length` can be answered).
bool function_implicit_property(const JsValue& fn, JsObject* cell, const std::string& name, JsValue& out) {
    if (fn.type != JsType::Function) return false;
    const JsValue::FunctionData* fd = fn.function_data();
    if (!fd) return false;
    if (name == "name") { out = JsValue::make_string(fd->name); return true; }
    if (name == "length") { out = JsValue::make_number(fd->length); return true; }
    if (cell && name == "prototype" && fd->node && fd->is_constructor) {
        // Created on first use: most functions are never constructors.
        JsValue proto = JsValue::make_object();
        proto.define_property("constructor", fn, PropertyMap::Hidden);
        auto& slot = cell->properties.entry(name);
        slot.second = proto;
        slot.flags = PropertyMap::Writable;
        out = proto;
        return true;
    }
    return false;
}

/// True when function_implicit_property would answer `name` for `fn`.
bool function_has_implicit_property(const JsValue& fn, const std::string& name) {
    if (fn.type != JsType::Function) return false;
    const JsValue::FunctionData* fd = fn.function_data();
    if (!fd) return false;
    return name == "name" || name == "length" || (name == "prototype" && fd->node && fd->is_constructor);
}

/// Looks `name` up on `start` and then along its prototype chain. A chain
/// can run through a function (`class B extends A` puts A's cell behind B's),
/// and there the properties a function does not store are asked for too.
bool lookup_chain(JsObject* start, const std::string& name, const JsValue& receiver, JsValue& out) {
    int guard = 0;
    for (JsObject* obj = start; obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
        auto it = obj->properties.find(name);
        if (it != obj->properties.end()) {
            out = read_entry(*it, receiver);
            return true;
        }
        if (const JsValue* function = obj->function_value()) {
            if (function_implicit_property(*function, obj, name, out)) return true;
        }
    }
    return false;
}

thread_local int t_to_string_depth = 0;

} // namespace

const JsValue& JsValue::current_this() {
    return t_call_context.this_val;
}

bool JsValue::current_is_construct() {
    return t_call_context.is_construct;
}

// ---------------------------------------------------------------------------
// Constructions in place
// ---------------------------------------------------------------------------
//
// construct(args, this) with a `this` that is a platform object already asks
// the constructor to make that very object what it constructs: this is how an
// element that exists is upgraded to a custom element. A class gets there by
// itself, because `super()` hands `this` up and the interface's constructor
// finds it in current_derived_this(). A constructor compiled for older
// browsers does not pass `this` on. It calls
// `Reflect.construct(HTMLElement, [], new.target)` and uses what comes back,
// and the one thing that ties that call to the construction it is part of is
// new.target. So a construction in place is noted here for as long as it
// runs, under the function `new` is being applied to, and construct_for()
// offers its object to the native that asks, once. This is the HTML
// standard's "construction stack": there each definition has one, and a
// definition is found by its constructor, which is new.target.

namespace {

struct InPlaceConstruction {
    const void* new_target;     // identity of the function being constructed
    JsValue object;             // what it is constructed on
    bool handed_out{false};     // the standard's "already constructed" marker
};

thread_local std::vector<InPlaceConstruction> t_in_place;

struct InPlaceScope {
    bool noted{false};
    InPlaceScope(const JsValue& callee, const JsValue* this_val, const JsValue* new_target) {
        if (!this_val || this_val->type != JsType::Object || !this_val->object_val ||
            !this_val->object_val->dom_node) {
            return;
        }
        // `super(...)` constructs the parent on the same object with the same
        // new.target: it is one construction, noted once.
        if (!t_in_place.empty() && t_in_place.back().object.object_val.get() == this_val->object_val.get()) return;
        t_in_place.push_back({(new_target ? *new_target : callee).identity(), *this_val});
        noted = true;
    }
    ~InPlaceScope() {
        if (noted) t_in_place.pop_back();
    }
    InPlaceScope(const InPlaceScope&) = delete;
    InPlaceScope& operator=(const InPlaceScope&) = delete;
};

}  // namespace

const JsValue* JsValue::current_derived_this() {
    if (t_call_context.derived_this || !t_call_context.derived_slot) return t_call_context.derived_this;
    // construct_for(): the native asks, so the object is made now, with the
    // prototype new.target has at this moment. Reading that can run a getter,
    // which is a call with a context of its own; this one is back when it
    // returns.
    JsValue* slot = t_call_context.derived_slot;
    const JsValue* new_target = t_call_context.new_target;
    t_call_context.derived_slot = nullptr;          // asked once
    if (!new_target) return nullptr;
    // Is this function being constructed in place right now? Then the object
    // it runs on is the one, whatever prototype it has, and only once: a
    // constructor that asks a second time, or makes another instance of its
    // own class while it runs, is refused, as the standard has it.
    for (auto entry = t_in_place.rbegin(); entry != t_in_place.rend(); ++entry) {
        if (entry->new_target != new_target->identity()) continue;
        if (entry->handed_out) js_throw_type_error("This instance is already constructed");
        entry->handed_out = true;
        *slot = entry->object;
        t_call_context.derived_this = slot;
        return slot;
    }
    const JsValue proto = new_target->get_property("prototype");
    // Nothing usable there: the native's own prototype is the right one, and
    // it gets that by building its object as if none had been offered. Null
    // in the slot says that the property was read and what was found.
    JsObject* target = proto.is_object_like() ? proto.as_prototype() : nullptr;
    if (!target) {
        *slot = JsValue::make_null();
        return nullptr;
    }
    *slot = JsValue::make_object();
    slot->object_val->prototype = target;
    t_call_context.derived_this = slot;
    return slot;
}

const JsValue* JsValue::current_new_target() {
    return t_call_context.is_construct ? t_call_context.new_target : nullptr;
}

bool JsValue::take_call_context(JsValue& this_out, bool& is_construct_out, JsValue* callee_out,
                                JsValue* new_target_out) {
    if (!t_call_context.fresh) {
        this_out = JsValue::make_undefined();
        is_construct_out = false;
        if (callee_out) *callee_out = JsValue::make_undefined();
        if (new_target_out) *new_target_out = JsValue::make_undefined();
        return false;
    }
    t_call_context.fresh = false;
    this_out = t_call_context.this_val;
    is_construct_out = t_call_context.is_construct;
    if (callee_out) *callee_out = t_call_context.callee ? *t_call_context.callee : JsValue::make_undefined();
    if (new_target_out) {
        if (!t_call_context.is_construct) *new_target_out = JsValue::make_undefined();
        else if (t_call_context.new_target) *new_target_out = *t_call_context.new_target;
        else *new_target_out = t_call_context.callee ? *t_call_context.callee : JsValue::make_undefined();
    }
    return true;
}

JsObject* JsValue::property_cell(bool create) const {
    if (object_val) return object_val;
    if (!is_function() || !m_extra) return nullptr;
    if (m_extra->cell) return m_extra->cell;
    if (!create) return nullptr;
    // Only a script function gets a cell for the asking. A native function
    // keeps its properties in a table until function_cell() is called on it.
    const FunctionData* fd = function_data();
    if (type != JsType::Function || !fd || !fd->node) return nullptr;
    m_extra->cell = Heap::default_heap().allocate_object();
    m_extra->cell->prototype = JsIntrinsics::get().function_proto;
    m_extra->cell->klass = ObjectClass::Function;
    return m_extra->cell;
}

JsObject* JsValue::function_cell(JsObject* adopt) const {
    if (!is_function()) return nullptr;
    if (JsObject* cell = property_cell(true)) return cell;
    if (!m_extra) return nullptr;

    // A native function. What it had in its table is copied entry by entry:
    // the table can be shared with another payload (a function value that was
    // changed while it was being built), and a PropertyMap's index points
    // into its own list.
    //
    // An adopted cell keeps the prototype it has. Nothing is asked of the
    // realm for it: the realm is what is being built when this happens.
    JsObject* cell = adopt;
    if (!cell) {
        cell = Heap::default_heap().allocate_object();
        cell->prototype = JsIntrinsics::get().function_proto;
    }
    cell->klass = ObjectClass::Function;
    if (m_extra->props) {
        for (const auto& entry : *m_extra->props) {
            auto& slot = cell->properties.entry(entry.first);
            slot.second = entry.second;
            slot.setter = entry.setter;
            slot.flags = entry.flags;
            if (entry.flags & PropertyMap::Accessor) cell->has_accessors = true;
            if (is_legacy_getter_key(entry.first)) cell->has_legacy_getters = true;
        }
        m_extra->props.reset();
    }
    m_extra->cell = cell;
    return cell;
}

JsObject* JsValue::as_prototype() const {
    if (object_val) return object_val;
    if (!is_function()) return nullptr;
    JsObject* cell = function_cell();
    if (cell && !cell->function_value()) cell->set_function_value(*this);
    return cell;
}

PropertyMap* JsValue::loose_properties(bool create) const {
    if (properties) return properties.get();
    if (is_function() && m_extra) {
        if (!m_extra->props && create) m_extra->props = std::make_shared<PropertyMap>();
        return m_extra->props.get();
    }
    return nullptr;
}

JsValue JsValue::call(const std::vector<JsValue>& args) const {
    static const JsValue undefined_this;
    return call_with_this(undefined_this, args);
}

JsValue JsValue::call_with_this(const JsValue& this_val, const std::vector<JsValue>& args) const {
    // Every call goes through here or construct(), including the ones built-in
    // functions make to each other: [[[...]]].toString() is join calling
    // toString calling join, with no script function in between for the
    // evaluator's own guard to see.
    if (stack_is_exhausted()) throw JsStackOverflow();
    const NativeFn& fn = native_function();
    if (fn) {
        CallContextScope scope(this_val, false, this);
        return fn(args);
    }
    // Functions compiled by the bytecode compiler carry no native entry point.
    if (type == JsType::Function && s_function_caller) {
        return s_function_caller(*this, args, &this_val);
    }
    return JsValue::make_undefined();
}

JsValue JsValue::construct(const std::vector<JsValue>& args, const JsValue* this_val,
                           const JsValue* new_target) const {
    static const JsValue undefined_this;
    if (stack_is_exhausted()) throw JsStackOverflow();
    const NativeFn& fn = native_function();
    InPlaceScope in_place(*this, this_val, new_target);
    if (fn) {
        CallContextScope scope(this_val ? *this_val : undefined_this, true, this);
        t_call_context.new_target = new_target;
        return fn(args);
    }
    if (type == JsType::Function && s_function_caller) {
        return s_function_caller(*this, args, this_val);
    }
    return JsValue::make_undefined();
}

JsValue JsValue::construct_as_super(const std::vector<JsValue>& args, const JsValue& derived_this,
                                    const JsValue* new_target) const {
    static const JsValue undefined_this;
    if (stack_is_exhausted()) throw JsStackOverflow();
    const NativeFn& fn = native_function();
    if (fn) {
        CallContextScope scope(undefined_this, true, this);
        t_call_context.derived_this = &derived_this;
        t_call_context.new_target = new_target;
        return fn(args);
    }
    return construct(args, &derived_this, new_target);
}

JsValue JsValue::construct_for(const std::vector<JsValue>& args, const JsValue& new_target, JsValue& made) const {
    static const JsValue undefined_this;
    if (stack_is_exhausted()) throw JsStackOverflow();
    const NativeFn& fn = native_function();
    if (!fn) return construct(args, nullptr, &new_target);
    CallContextScope scope(undefined_this, true, this);
    t_call_context.new_target = &new_target;
    t_call_context.derived_slot = &made;
    return fn(args);
}

JsValue JsValue::make_object(JsObject* obj) {
    JsValue v;
    v.type = JsType::Object;
    if (obj) {
        // A function's cell, reached through a prototype link: the function.
        if (const JsValue* function = obj->function_value()) return *function;
        v.object_val = obj;
    } else {
        v.object_val = Heap::default_heap().allocate_object();
        v.object_val->prototype = JsIntrinsics::get().object_proto;
    }
    return v;
}

JsValue JsValue::make_symbol(std::string key, std::string description) {
    JsValue v;
    v.type = JsType::Symbol;
    v.string_val = JsString(key);
    // The description rides in the property table so the value stays small.
    v.properties = std::make_shared<PropertyMap>();
    (*v.properties)["description"] = JsValue::make_string(std::move(description));
    return v;
}

const void* JsValue::identity() const {
    if (type == JsType::Function || type == JsType::NativeFunction) {
        // The payload is shared by every copy of a function value and is never
        // replaced after creation, so its address identifies the function.
        if (m_extra) return m_extra.get();
        return object_val;
    }
    return object_val;
}

// ECMAScript Strict Equality Comparison (7.2.16).
// No type coercion happens here, which is the entire point of `===`.
bool strict_equals(const JsValue& a, const JsValue& b) {
    // Different types are never strictly equal, with no coercion.
    if (a.type != b.type) {
        // A function is a function whether it is native or script.
        return a.is_function() && b.is_function() && a.identity() == b.identity();
    }

    switch (a.type) {
        case JsType::Undefined:
        case JsType::Null:
            return true;              // undefined === undefined, null === null
        case JsType::Boolean:
            return a.bool_val == b.bool_val;
        case JsType::Number:
            return a.number_val == b.number_val;   // NaN !== NaN, as required
        case JsType::String:
            return a.string_val == b.string_val;
        case JsType::Symbol:
            return a.string_val == b.string_val;   // the unique key is the identity
        case JsType::Object:
            // Reference identity, never structural equality.
            return a.object_val == b.object_val;
        case JsType::Function:
        case JsType::NativeFunction:
            return a.identity() == b.identity();
        default:
            return false;
    }
}

bool JsValue::is_truthy() const {
    switch (type) {
        case JsType::Undefined:
        case JsType::Null:
            return false;
        case JsType::Boolean:
            return bool_val;
        case JsType::Number:
            return number_val != 0.0 && !std::isnan(number_val);
        case JsType::String:
            return !string_val.empty();
        case JsType::Object:
        case JsType::Function:
        case JsType::NativeFunction:
        case JsType::Symbol:
            return true;
    }
    return false;
}

bool JsValue::is_array() const {
    return type == JsType::Object && object_val && object_val->is_array;
}

std::deque<JsValue>& JsValue::array_elements() {
    if (!object_val) {
        object_val = Heap::default_heap().allocate_object();
        object_val->is_array = true;
        object_val->klass = ObjectClass::Array;
        object_val->prototype = JsIntrinsics::get().array_proto;
    }
    return object_val->array_elements;
}

const std::deque<JsValue>& JsValue::array_elements() const {
    static const std::deque<JsValue> s_empty;
    return (object_val && object_val->is_array) ? object_val->array_elements : s_empty;
}

// ECMAScript ToString, except that it never throws for a missing toString():
// host code calls this on arbitrary values and expects a string back.
std::string JsValue::to_string() const {
    switch (type) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "null";
        case JsType::Boolean: return bool_val ? "true" : "false";
        case JsType::Number: return js_number_to_string(number_val);
        case JsType::String: return string_val.str();
        case JsType::Symbol: {
            std::string desc = properties ? (*properties)["description"].string_val.str() : std::string();
            return "Symbol(" + desc + ")";
        }
        case JsType::Object: {
            if (object_val && object_val->is_array) {
                // Array.prototype.toString is join(","); null and undefined are empty.
                if (t_to_string_depth > 64) return "";
                struct Depth { Depth() { ++t_to_string_depth; } ~Depth() { --t_to_string_depth; } } depth;
                std::string res;
                const auto& elements = object_val->array_elements;
                for (size_t i = 0; i < elements.size(); ++i) {
                    if (i) res += ',';
                    if (!elements[i].is_nullish()) res += elements[i].to_string();
                }
                return res;
            }
            if (t_to_string_depth > 64) return "[object Object]";
            struct Depth { Depth() { ++t_to_string_depth; } ~Depth() { --t_to_string_depth; } } depth;
            for (const char* method : {"toString", "valueOf"}) {
                JsValue fn = get_property(method);
                if (!fn.is_function()) continue;
                JsValue result = fn.call_with_this(*this, {});
                if (!result.is_object_like()) return result.to_string();
            }
            return "[object Object]";
        }
        case JsType::Function:
        case JsType::NativeFunction: {
            std::string name;
            if (const FunctionData* fd = function_data()) {
                // A script function prints as its own source text. Code that
                // tests for built-ins with /native code/ depends on that.
                std::string text = js_function_source_text(fd->class_node ? fd->class_node.get() : fd->node.get());
                if (!text.empty()) return text;
                name = fd->name;
            }
            else {
                // In the function's cell once it has one, in its table before.
                const JsObject* cell = property_cell();
                const PropertyMap* map = cell ? &cell->properties : loose_properties();
                if (map) {
                    auto it = map->find("name");
                    if (it != map->end()) name = it->second.string_val.str();
                }
            }
            // Bound functions print without a name, as V8 prints them.
            if (name.compare(0, 6, "bound ") == 0) name.clear();
            return "function " + name + "() { [native code] }";
        }
    }
    return "undefined";
}

std::string JsValue::inspect() const {
    if (type == JsType::Number && number_val == 0.0 && std::signbit(number_val)) return "-0";
    if (type == JsType::Object && object_val && object_val->is_array) {
        if (t_to_string_depth > 64) return "[...]";
        struct Depth { Depth() { ++t_to_string_depth; } ~Depth() { --t_to_string_depth; } } depth;
        std::string res = "[";
        const auto& elements = object_val->array_elements;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (i) res += ", ";
            res += elements[i].inspect();
        }
        return res + "]";
    }
    return to_string();
}

// Lenient host conversion: never NaN. Bindings use this for coordinates and
// sizes, where a missing argument should behave like 0. Script arithmetic uses
// js_to_number(), which follows ECMAScript.
double JsValue::to_number() const {
    switch (type) {
        case JsType::Undefined: return 0.0;
        case JsType::Null: return 0.0;
        case JsType::Boolean: return bool_val ? 1.0 : 0.0;
        case JsType::Number: return number_val;
        case JsType::String: {
            try { return std::stod(string_val.str()); } catch (...) { return 0.0; }
        }
        default: return 0.0;
    }
}

JsObject* JsValue::prototype_object() const {
    if (JsObject* cell = property_cell()) return cell->prototype;
    if (is_function()) return JsIntrinsics::get().function_proto;
    return nullptr;
}

JsValue js_get_from_chain(JsObject* start, const std::string& name, const JsValue& receiver) {
    JsValue found;
    if (lookup_chain(start, name, receiver, found)) return found;
    return JsValue::make_undefined();
}

bool js_set_prototype(JsObject* cell, JsObject* proto) {
    if (!cell) return true;
    if (cell->prototype == proto) return true;       // nothing changes, so nothing can refuse it
    if (!cell->extensible) return false;             // Object.preventExtensions, seal, freeze
    for (JsObject* p = proto; p; p = p->prototype) {
        if (p == cell) return false;
    }
    cell->prototype = proto;
    return true;
}

// True when `name` is the index of a character of the string that a String
// object wraps. Those properties exist without being stored, and are read-only.
static bool is_wrapped_string_index(const JsObject* cell, const std::string& name) {
    if (cell->klass != ObjectClass::String || name.empty() || name[0] < '0' || name[0] > '9') return false;
    const auto* wrapped = static_cast<const JsValue*>(cell->internal.get());
    size_t index = 0;
    return wrapped && wrapped->type == JsType::String && parse_array_index(name, index) &&
           index < wrapped->string_val.length();
}

bool JsValue::has_own_property(const std::string& name) const {
    size_t index = 0;
    if (type == JsType::String) {
        if (name == "length") return true;
        return parse_array_index(name, index) && index < string_val.length();
    }
    if (JsObject* cell = property_cell()) {
        if (cell->exotic && cell->exotic->has(cell, name)) return true;
        if (cell->is_array) {
            if (name == "length") return true;
            if (parse_array_index(name, index)) return index < cell->array_elements.size();
        }
        if (is_wrapped_string_index(cell, name)) return true;
        if (cell->properties.find(name) != cell->properties.end()) return true;
    }
    if (const PropertyMap* map = loose_properties()) {
        if (map->find(name) != map->end()) return true;
    }
    // What a script function has without storing it, the `prototype` of a
    // constructor included: it is there before it is first read.
    return function_has_implicit_property(*this, name);
}

bool JsValue::has_property(const std::string& name) const {
    if (has_own_property(name)) return true;
    if (object_val && object_val->is_array && (name == "@@iterator" || name == "Symbol.iterator")) return true;
    JsObject* proto = nullptr;
    const auto& intrinsics = JsIntrinsics::get();
    switch (type) {
        case JsType::String: proto = intrinsics.string_proto; break;
        case JsType::Number: proto = intrinsics.number_proto; break;
        case JsType::Boolean: proto = intrinsics.boolean_proto; break;
        case JsType::Symbol: proto = intrinsics.symbol_proto; break;
        default: proto = prototype_object(); break;
    }
    int guard = 0;
    for (JsObject* obj = proto; obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
        if (obj->properties.find(name) != obj->properties.end()) return true;
        if (const JsValue* function = obj->function_value()) {
            if (function_has_implicit_property(*function, name)) return true;
        }
    }
    if (object_val && object_val->lookup_fallback) {
        JsValue ignored;
        if ((*object_val->lookup_fallback)(name, ignored)) return true;
    }
    return false;
}

bool JsValue::delete_property(const std::string& name) {
    // A constructor's `prototype` cannot be deleted, whether it has been made yet or not.
    if (name == "prototype" && function_has_implicit_property(*this, name)) return false;
    if (JsObject* cell = property_cell()) {
        if (cell->exotic) {
            bool result = true;
            if (cell->exotic->remove(cell, name, result)) return result;
        }
        if (cell->frozen || cell->sealed) {
            return !has_own_property(name);
        }
        if (is_wrapped_string_index(cell, name)) return false;
        size_t index = 0;
        if (cell->is_array && parse_array_index(name, index)) {
            // Arrays are dense here: deleting an element leaves undefined behind.
            auto& elements = cell->array_elements;
            if (index < elements.size()) elements[index] = JsValue::make_undefined();
            return true;
        }
        auto it = cell->properties.find(name);
        if (it == cell->properties.end()) return true;
        if (!(it->flags & PropertyMap::Configurable)) return false;
        cell->properties.erase(name);
        return true;
    }
    if (PropertyMap* map = loose_properties()) {
        auto it = map->find(name);
        if (it == map->end()) return true;
        if (!(it->flags & PropertyMap::Configurable)) return false;
        map->erase(name);
    }
    return true;
}

std::vector<std::string> JsValue::own_keys(bool include_non_enumerable) const {
    std::vector<std::string> keys;
    if (type == JsType::String) {
        size_t length = string_val.length();
        for (size_t i = 0; i < length; ++i) keys.push_back(std::to_string(i));
        if (include_non_enumerable) keys.emplace_back("length");
        return keys;
    }

    JsObject* cell = property_cell();
    if (cell && cell->exotic) cell->exotic->keys(cell, keys);
    if (cell && cell->is_array) {
        for (size_t i = 0; i < cell->array_elements.size(); ++i) keys.push_back(std::to_string(i));
        if (include_non_enumerable) keys.emplace_back("length");
    }
    if (cell && cell->klass == ObjectClass::String) {
        // The characters come first; `length` is in the property map already.
        const auto* wrapped = static_cast<const JsValue*>(cell->internal.get());
        if (wrapped && wrapped->type == JsType::String) {
            size_t length = wrapped->string_val.length();
            for (size_t i = 0; i < length; ++i) keys.push_back(std::to_string(i));
        }
    }

    const PropertyMap* map = cell ? &cell->properties : loose_properties();
    if (!map) return keys;

    std::vector<std::pair<size_t, const std::string*>> index_keys;
    std::vector<const std::string*> named_keys;
    for (const auto& entry : *map) {
        if (is_hidden_key(entry.first)) continue;
        if (!include_non_enumerable && !(entry.flags & PropertyMap::Enumerable)) continue;
        size_t index = 0;
        if (parse_array_index(entry.first, index)) index_keys.emplace_back(index, &entry.first);
        else named_keys.push_back(&entry.first);
    }
    std::sort(index_keys.begin(), index_keys.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, key] : index_keys) { (void)index; keys.push_back(*key); }
    for (const auto* key : named_keys) keys.push_back(*key);
    return keys;
}

void JsValue::define_property(const std::string& name, JsValue value, uint8_t flags) {
    PropertyMap* map = nullptr;
    if (JsObject* cell = property_cell(true)) {
        map = &cell->properties;
        if (is_legacy_getter_key(name)) cell->has_legacy_getters = true;
    } else {
        map = loose_properties(true);
        if (!map) {
            properties = std::make_shared<PropertyMap>();
            map = properties.get();
        }
    }
    auto& entry = map->entry(name);
    entry.second = std::move(value);
    entry.setter.reset();
    entry.flags = static_cast<uint8_t>(flags & ~PropertyMap::Accessor);
}

void JsValue::define_accessor(const std::string& name, const JsValue& getter, const JsValue& setter, uint8_t flags) {
    PropertyMap* map = nullptr;
    if (JsObject* cell = property_cell(true)) {
        map = &cell->properties;
        cell->has_accessors = true;
    } else {
        map = loose_properties(true);
        if (!map) {
            properties = std::make_shared<PropertyMap>();
            map = properties.get();
        }
    }
    auto& entry = map->entry(name);
    entry.second = getter;
    entry.setter = setter.is_function() ? std::make_shared<JsValue>(setter) : nullptr;
    entry.flags = static_cast<uint8_t>((flags | PropertyMap::Accessor) & ~PropertyMap::Writable);
}

// The EventTarget methods, for any node: elements, text nodes and the document.
JsValue JsValue::get_property(const std::string& name) const {
    // Primitives have no properties of their own; they borrow the methods of
    // their wrapper prototype (String.prototype and friends).
    if (type == JsType::String) {
        // `s.length` and `s[i]` run once per character in any loop over a
        // string, so neither may read the text: the length is a field of the
        // handle and a code unit is one array read. See JsString.
        if (name == "length") return JsValue::make_number(static_cast<double>(string_val.length()));
        size_t index = 0;
        if (parse_array_index(name, index)) {
            if (index < string_val.length()) return JsValue::make_string(JsString::from_unit(string_val.unit_at(index)));
            return JsValue::make_undefined();
        }
        JsValue found;
        if (lookup_chain(JsIntrinsics::get().string_proto, name, *this, found)) return found;
        return JsValue::make_undefined();
    }
    if (type == JsType::Number || type == JsType::Boolean || type == JsType::Symbol) {
        const auto& intrinsics = JsIntrinsics::get();
        JsObject* proto = type == JsType::Number ? intrinsics.number_proto
                        : type == JsType::Boolean ? intrinsics.boolean_proto : intrinsics.symbol_proto;
        JsValue found;
        if (lookup_chain(proto, name, *this, found)) return found;
        return JsValue::make_undefined();
    }
    if (type == JsType::Undefined || type == JsType::Null) return JsValue::make_undefined();

    // Script functions keep their properties in a cell created on demand; the
    // `prototype` of a constructor is what usually forces it into existence.
    JsObject* obj = property_cell();
    if (!obj && type == JsType::Function && name == "prototype") {
        const FunctionData* fd = function_data();
        if (fd && fd->node && fd->is_constructor) obj = property_cell(true);
    }
    if (obj) {
        if (name == "__proto__") {
            if (obj->prototype) return JsValue::make_object(obj->prototype);
            return JsValue::make_null();
        }

        // Host objects first: what they compute comes before what is stored.
        if (obj->exotic) {
            JsValue computed;
            if (obj->exotic->get(obj, name, computed)) return computed;
        }

        if (obj->is_array) {
            if (name == "length") {
                return JsValue::make_number(static_cast<double>(obj->array_elements.size()));
            }
            size_t index = 0;
            if (parse_array_index(name, index)) {
                if (index < obj->array_elements.size()) return obj->array_elements[index];
                return JsValue::make_undefined();
            }
        }

        // A String object exposes the characters of the string it wraps.
        if (is_wrapped_string_index(obj, name)) {
            return static_cast<const JsValue*>(obj->internal.get())->get_property(name);
        }

        // Legacy host getters: a native stored under "__get_<name>__" is run on
        // every read of <name>. Only objects that have one pay for the lookup.
        if (obj->has_legacy_getters) {
            auto it_getter = obj->properties.find("__get_" + name + "__");
            if (it_getter != obj->properties.end() && it_getter->second.native_function()) {
                return it_getter->second.native_function()({});
            }
        }

        // Own properties
        auto it = obj->properties.find(name);
        if (it != obj->properties.end()) return read_entry(*it, *this);

        // Prototype chain delegation. For a class with a parent the chain
        // starts at the parent's cell, which is how static members are
        // inherited, with this function as the receiver of a static getter.
        JsValue found;
        if (type == JsType::Function && function_implicit_property(*this, obj, name, found)) return found;
        if (lookup_chain(obj->prototype, name, *this, found)) return found;

        if (obj->lookup_fallback && (*obj->lookup_fallback)(name, found)) return found;
        if (obj->exotic && obj->exotic->get_last(obj, name, found)) return found;
    }

    if (canvas_context()) {
        if (name == "fillStyle") {
            if (canvas_context()->fill_gradient) return JsValue::make_string("[CanvasGradient]");
            return JsValue::make_string(canvas_context()->fill_style.to_hex());
        }
        if (name == "strokeStyle") {
            if (canvas_context()->stroke_gradient) return JsValue::make_string("[CanvasGradient]");
            return JsValue::make_string(canvas_context()->stroke_style.to_hex());
        }
        if (name == "lineWidth") return JsValue::make_number(canvas_context()->line_width);
        if (name == "globalCompositeOperation") return JsValue::make_string(canvas_context()->global_composite_operation);
    }

    if (const PropertyMap* map = loose_properties()) {
        auto it_prop_getter = map->find("__get_" + name + "__");
        if (it_prop_getter != map->end() && it_prop_getter->second.native_function()) {
            return it_prop_getter->second.native_function()({});
        }
        auto it = map->find(name);
        if (it != map->end()) return read_entry(*it, *this);
    }

    if (is_function() && !obj) {
        // No cell: nothing has set this function's prototype, so it is
        // Function.prototype.
        JsValue found;
        if (name == "__proto__") return JsValue::make_object(JsIntrinsics::get().function_proto);
        if (function_implicit_property(*this, nullptr, name, found)) return found;
        if (lookup_chain(JsIntrinsics::get().function_proto, name, *this, found)) return found;
    }
    return JsValue::make_undefined();
}

void JsValue::set_property(const std::string& name, JsValue val) {
    if (canvas_context()) {
        if (name == "fillStyle") {
            if (val.canvas_gradient_value()) {
                canvas_context()->fill_gradient = val.canvas_gradient_value();
            } else {
                canvas_context()->fill_gradient = nullptr;
                canvas_context()->fill_style = Color::parse(val.to_string());
            }
        } else if (name == "strokeStyle") {
            if (val.canvas_gradient_value()) {
                canvas_context()->stroke_gradient = val.canvas_gradient_value();
            } else {
                canvas_context()->stroke_gradient = nullptr;
                canvas_context()->stroke_style = Color::parse(val.to_string());
            }
        } else if (name == "lineWidth") {
            canvas_context()->line_width = static_cast<float>(val.to_number());
        } else if (name == "globalCompositeOperation") {
            canvas_context()->global_composite_operation = val.to_string();
        }
    }

    if (type == JsType::String || type == JsType::Number || type == JsType::Boolean || type == JsType::Symbol) {
        return;     // assigning to a primitive's property has no effect
    }

    // `fn.__proto__ = x` needs the function to have a cell, whatever kind of
    // function it is: a native one would otherwise store a property by that name.
    const bool sets_prototype = name == "__proto__";
    if (JsObject* obj = (sets_prototype && is_function()) ? function_cell() : property_cell(true)) {
        if (obj->exotic && obj->exotic->set(obj, name, val)) return;
        if (obj->assign_hook && (*obj->assign_hook)(name, val)) return;
        if (sets_prototype) {
            // Anything but an object or null is ignored, as the setter on
            // Object.prototype does. A change that is refused throws.
            if (!val.is_object_like() && val.type != JsType::Null) return;
            JsObject* target = val.type == JsType::Null ? nullptr : val.as_prototype();
            if (!js_set_prototype(obj, target)) {
                js_throw_error(JsErrorKind::TypeError,
                               obj->extensible ? "Cyclic __proto__ value" : "Object is not extensible");
            }
            return;
        }
        if (is_wrapped_string_index(obj, name)) return;      // the characters are read-only

        if (obj->is_array) {
            if (name == "length") {
                if (obj->frozen) return;
                double requested = js_to_number(val);
                if (!(requested >= 0) || requested != std::floor(requested) || requested > 4294967295.0) {
                    js_throw_range_error("Invalid array length");
                }
                obj->array_elements.resize(static_cast<size_t>(requested), JsValue::make_undefined());
                return;
            }
            size_t index = 0;
            if (parse_array_index(name, index)) {
                if (obj->frozen) return;
                if (index >= obj->array_elements.size()) {
                    if (!obj->extensible) return;
                    // A stray huge index would otherwise allocate gigabytes.
                    if (index > obj->array_elements.size() + (1u << 24)) {
                        js_throw_range_error("Array index too far past the end of the array");
                    }
                    obj->array_elements.resize(index + 1, JsValue::make_undefined());
                }
                obj->array_elements[index] = std::move(val);
                return;
            }
        }

        auto it = obj->properties.find(name);
        if (it != obj->properties.end()) {
            if (it->flags & PropertyMap::Accessor) {
                if (it->setter && it->setter->is_function()) it->setter->call_with_this(*this, {val});
                return;
            }
            if (obj->frozen || !(it->flags & PropertyMap::Writable)) return;
            it->second = std::move(val);
            return;
        }

        // `F.prototype = {...}` before anything has read F.prototype. The
        // property is there already as far as a script can tell (see
        // function_implicit_property), writable and nothing else, so the
        // assignment must not make an enumerable, deletable one: a loop that
        // copies the static members of F would copy `prototype` with them.
        if (name == "prototype" && function_has_implicit_property(*this, name)) {
            if (obj->frozen) return;
            auto& slot = obj->properties.entry(name);
            slot.second = std::move(val);
            slot.flags = PropertyMap::Writable;
            return;
        }

        // A setter inherited from a prototype (a class `set x(v)`) runs instead
        // of creating an own property.
        int guard = 0;
        for (JsObject* proto = obj->prototype; proto && guard < kMaxPrototypeChain; proto = proto->prototype, ++guard) {
            if (!proto->has_accessors) continue;
            auto pit = proto->properties.find(name);
            if (pit == proto->properties.end()) continue;
            if (pit->flags & PropertyMap::Accessor) {
                if (pit->setter && pit->setter->is_function()) pit->setter->call_with_this(*this, {val});
                return;
            }
            break;
        }

        if (!obj->extensible || obj->frozen) return;
        if (is_legacy_getter_key(name)) obj->has_legacy_getters = true;
        obj->properties[name] = std::move(val);
        return;
    }

    PropertyMap* map = loose_properties(true);
    if (!map) {
        properties = std::make_shared<PropertyMap>();
        map = properties.get();
    }
    auto it = map->find(name);
    if (it != map->end()) {
        if (it->flags & PropertyMap::Accessor) {
            if (it->setter && it->setter->is_function()) it->setter->call_with_this(*this, {val});
            return;
        }
        if (!(it->flags & PropertyMap::Writable)) return;
        it->second = std::move(val);
        return;
    }
    (*map)[name] = std::move(val);
}

JsValue JsValue::make_array(const std::vector<JsValue>& elements) {
    JsObject* obj = Heap::default_heap().allocate_object();
    obj->is_array = true;
    obj->klass = ObjectClass::Array;
    obj->array_elements.assign(elements.begin(), elements.end());
    // Methods live on Array.prototype. They used to be six closures installed
    // on every single array, which also made them show up as own properties.
    obj->prototype = JsIntrinsics::get().array_proto;
    return JsValue::make_object(obj);
}

// The script object of a node: the same one every time, for as long as a
// script can reach the node. See DomBindings.hpp.
JsValue JsValue::make_dom_node(std::shared_ptr<Node> node) {
    return dom::wrap(dom::current_realm(), node);
}

} // namespace litebird
