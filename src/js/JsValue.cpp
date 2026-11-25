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
#include <cassert>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace litebird {

/// How far past the elements an index may be before extending the element
/// vector would be paying for holes it does not need. A place just past the
/// end extends the vector (that is what makes `a[a.length] = x` cheap and
/// dense); a place far past it only moves the length, and the places skipped
/// stay the holes they are. 2^16 places is 5.7 MiB of `JsValue` — the point
/// where "extend it anyway" stops being obviously right.
static constexpr size_t kMaxDenseGap = 1u << 16;

static thread_local JsValue::FunctionCaller s_function_caller = nullptr;

namespace {
/// What a dying function payload hands to the release queue: everything in it
/// that can own another function.
struct ExtraContents {
    std::shared_ptr<PropertyMap> props;
    JsValue::Extra::Payload payload;
    std::shared_ptr<std::vector<JsValue>> captured;
};
}

// A closure owns the scope it was created in, the scope owns its bindings, and
// a binding can be another closure. `f = wrap(f)` in a loop therefore builds a
// chain as long as the loop ran, and letting each payload destroy the next in
// place would use native stack in proportion to it.
JsValue::Extra::~Extra() {
    if (payload.index() == 0 && !props && !captured) return;     // owns nothing
    using Queue = ReleaseQueue<ExtraContents>;
    if (Queue::busy()) {
        Queue::defer(ExtraContents{std::move(props), std::move(payload), std::move(captured)});
        return;
    }
    Queue::Drain drain;
    props.reset();
    captured.reset();
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
const std::shared_ptr<CanvasPattern>& JsValue::canvas_pattern_value() const {
    static const std::shared_ptr<CanvasPattern> empty;
    if (extra()) if (auto v = std::get_if<std::shared_ptr<CanvasPattern>>(&extra()->payload)) return *v;
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

const JsValue& JsValue::bound_target() const {
    static const JsValue kEmpty;
    if (!m_extra || !m_extra->bound_target) return kEmpty;
    return *m_extra->bound_target;
}

void JsValue::set_bound_target(const JsValue& target) { mutable_extra().bound_target = std::make_shared<JsValue>(target); }

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
void JsValue::set_canvas_pattern_value(std::shared_ptr<CanvasPattern> v) { mutable_extra().payload = std::move(v); }
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

/// How many times a property whose name begins with a digit has entered a
/// property map, any map. This is the one place a key enters a map, so an
/// object comes to have an index in its map only when this goes up, and what
/// js_array_holes_read_undefined worked out about a prototype chain at one
/// count holds for that chain while the count stands. Atomic because a worker
/// runs on a thread of its own; the answers are remembered per thread.
static std::atomic<uint64_t> g_index_named_keys{0};

PropertyMap::Entry& PropertyMap::entry(const std::string& key) {
    if (auto it = m_index_built ? m_index.find(key) : m_index.end(); it != m_index.end()) return *it->second;
    if (!m_index_built) {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->first == key) return *it;
        }
    }
    if (!key.empty() && key[0] >= '0' && key[0] <= '9') g_index_named_keys.fetch_add(1, std::memory_order_relaxed);
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
    JsValue* derived_slot{nullptr};         // see JsValue::current_derived_this: where an object it chooses is kept
    bool derived_asked{false};              // current_derived_this() has answered for this call
    bool is_construct{false};
    bool fresh{false};      // set by the caller, consumed by a script function on entry
    // What the callee captures (JsValue::Extra::captured). A native entry point
    // reads its arguments out of here, because a capture of its own would be
    // invisible to the collector; see Extra::captured.
    const std::vector<JsValue>* captured{nullptr};
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
        t_call_context.derived_asked = false;
        t_call_context.is_construct = is_construct;
        t_call_context.fresh = true;
        t_call_context.captured = callee ? callee->payload_captured() : nullptr;
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
///
/// A `#` prefix used to be one of these, because a private field was stored as
/// a property called `#p` and hiding it from enumeration was all that could be
/// done for it. Private members have their own table since Phase 88, so a
/// property a script itself names `#p` is an ordinary property again:
/// `Object.keys`, `getOwnPropertyNames` and `JSON.stringify` all see it, as
/// they do in every other engine.
bool is_hidden_key(const std::string& key) {
    if (key.size() >= 2 && key[0] == '@' && key[1] == '@') return true;
    if (!key.empty() && key[0] == '%') return true;      // internal slots
    // `__name__` is the engine's own bookkeeping -- `__get_x__`, `__set_x__`
    // and the rest -- and is not a property a script made. `__proto__` is the
    // exception: it is Annex B.2.2.1's real accessor on `Object.prototype`, so
    // hiding it was why `Object.getOwnPropertyNames(Object.prototype)` left it
    // out after it had been installed, while
    // `Object.getOwnPropertyDescriptor` answered for it.
    if (key == "__proto__") return false;
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
    // `name` and `length` used to be answered here, out of the payload. They
    // are own properties now, installed by `make_function` with the attributes
    // §10.2.9/§10.2.5 give them (Phase 146): answering them from here as well
    // made `has_own_property` say no, `Object.getOwnPropertyNames` omit them,
    // a write to a non-writable property go through, and a delete remove
    // nothing. What is still implicit is the one thing that must be — a
    // constructor's `prototype`, made on first read because most functions are
    // never constructors.
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
    return name == "prototype" && fd->node && fd->is_constructor;
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
        // An array prototype can have an index of its own, and it keeps those
        // in `array_elements` rather than in the property map: without this,
        // `Array.prototype[4] = 5` would not reach `[1, 2, 3][4]`. A hole is
        // not one of them, so the walk goes on past it.
        if (obj->is_array) {
            // `length` is the other thing an array keeps in `array_elements`
            // rather than in the property map, so a read that falls through
            // to an array prototype did not find it: `Object.create([7, 8, 9])
            // .length` was `undefined` where the language has `3` (finding
            // 138). It is not the object's own property and has no descriptor
            // to describe (`Object.getOwnPropertyDescriptor(o, 'length')` is
            // `undefined`), but `HasProperty(o, 'length')` is true through the
            // chain, so a read has to reach it. The walk stops here whichever
            // object asked, because `length` on the array is where the
            // standard's lookup ends.
            if (name == "length") {
                // The count, not the storage: a length that names places
                // nothing was written to holds no elements at all (see the
                // grow branch in `set_property`), so the entry is the length
                // and `array_elements` is only where the places below it are.
                const auto entry = obj->properties.find("length");
                if (entry != obj->properties.end()) {
                    out = JsValue::make_number(js_to_number(entry->second));
                    return true;
                }
                out = JsValue::make_number(static_cast<double>(obj->array_elements.size()));
                return true;
            }
            size_t index = 0;
            if (parse_array_index(name, index) && index < obj->array_elements.size()
                && !(obj->array_holes && obj->array_holes->empty_slot(index))) {
                out = obj->array_elements[index];
                return true;
            }
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

bool JsValue::current_requires_new() {
    return !t_call_context.is_construct;
}

namespace {
// An empty capture list, so a native entry point written to take one always
// has something to index.
const std::vector<JsValue>& no_captures() {
    static const std::vector<JsValue> empty;
    return empty;
}
}

const std::vector<JsValue>& JsValue::current_captured() {
    return t_call_context.captured ? *t_call_context.captured : no_captures();
}

// ---------------------------------------------------------------------------
// The object a constructor builds on
// ---------------------------------------------------------------------------
//
// A native constructor reached from a class does not always get the object the
// class's `this` is: `super(...)` hands on the object `new` made, and
// `Reflect.construct(HTMLElement, [], new.target)` hands on nothing at all,
// and neither says by itself whether the caller has an object already. So the
// interface asks for it (current_derived_this), and the answer is made here
// from the call context, once per call: the object the class's constructor was
// given, or — when there is none to give, as under construct_for() — a new one
// with the prototype of new.target.
//
// The element an upgrade builds is not that object: the definition owns it, in
// its construction stack (DomObserverBindings.cpp), and the interface hands it
// back with hand_back_constructed_object() so that `super(...)` makes it
// `this`.

const JsValue* JsValue::current_derived_this() {
    CallContext& context = t_call_context;
    if (context.derived_asked) return context.derived_this;
    context.derived_asked = true;
    // super(...): the object the subclass's constructor was given.
    if (context.derived_this) return context.derived_this;
    const JsValue* new_target = context.new_target;
    JsValue* slot = context.derived_slot;
    if (!slot || !new_target) return nullptr;
    // construct_for(), or an interface that may not have the object
    // super(...) came with: the object is made now, with the
    // prototype new.target has at this moment. Reading that can run a getter,
    // which is a call with a context of its own; this one is back when it
    // returns.
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
    context.derived_this = slot;
    return slot;
}

void JsValue::hand_back_constructed_object(const JsValue& object) {
    CallContext& context = t_call_context;
    if (context.derived_slot) *context.derived_slot = object;
}

const JsValue* JsValue::current_new_target() {
    return t_call_context.is_construct ? t_call_context.new_target : nullptr;
}

bool JsValue::take_call_context(JsValue& this_out, bool& is_construct_out, JsValue* callee_out,
                                const JsValue** new_target_out) {
    if (!t_call_context.fresh) {
        this_out = JsValue::make_undefined();
        is_construct_out = false;
        if (callee_out) *callee_out = JsValue::make_undefined();
        if (new_target_out) *new_target_out = nullptr;
        return false;
    }
    t_call_context.fresh = false;
    this_out = t_call_context.this_val;
    is_construct_out = t_call_context.is_construct;
    if (callee_out) *callee_out = t_call_context.callee ? *t_call_context.callee : JsValue::make_undefined();
    if (new_target_out) {
        const JsValue* named = t_call_context.is_construct ? t_call_context.new_target : nullptr;
        *new_target_out = named && named->is_function() ? named : nullptr;
    }
    return true;
}

namespace {

/// The %Function.prototype% a function of this shape hangs under: its kind's
/// own prototype for an async or a generator function, the ordinary one for
/// everything else.
JsObject* function_kind_prototype(const JsValue::FunctionData* fd) {
    const JsIntrinsics& realm = JsIntrinsics::get();
    if (fd == nullptr) return realm.function_proto;
    if (fd->is_generator) return realm.generator_function_proto;
    if (fd->is_async) return realm.async_function_proto;
    return realm.function_proto;
}

}   // namespace

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
    // Which %Function.prototype% this one hangs under (§20.2.3.2,
    // §27.3.3.2): an async function and a generator function each have one of
    // their own, because `typeof` says "function" for all three and only the
    // chain can tell them apart.
    m_extra->cell->prototype = function_kind_prototype(fd);
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
                                    const JsValue* new_target, JsValue* in_place) const {
    static const JsValue undefined_this;
    if (stack_is_exhausted()) throw JsStackOverflow();
    const NativeFn& fn = native_function();
    if (fn) {
        JsValue taken;      // what the native built on, when that was not derived_this
        CallContextScope scope(undefined_this, true, this);
        t_call_context.derived_this = &derived_this;
        t_call_context.new_target = new_target;
        t_call_context.derived_slot = &taken;
        JsValue result = fn(args);
        if (in_place && taken.is_object()) *in_place = taken;
        return result;
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
    // In the extra block, not the property table: a description stored as a
    // property would be an own property of the symbol, and the standard's is
    // not — `hasOwnProperty(Symbol('x'), 'description')` is false in a
    // browser. Nothing in the table is invisible to `ownKeys`. The empty
    // string is a real description (`Symbol('')`), so it is stored like any
    // other; `mark_symbol_without_description` is what says "there is none".
    v.set_symbol_description(std::move(description));
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

// --- Array holes -----------------------------------------------------------
//
// The bits are kept to the right of the elements: `Holes::shift` moves them
// with the elements when an insert or an erase moves those.

void JsObject::Holes::shift(size_t from, std::ptrdiff_t delta) {
    if (!any() || delta == 0) return;
    // Rebuilt from the bits that are set: an insert or an erase of an array
    // with a hole is rare, and this is linear in the words that exist.
    std::vector<uint64_t> moved;
    const size_t total = bits.size() * 64;
    auto bit = [&](size_t i) { return i < total && ((bits[i >> 6] >> (i & 63)) & 1u); };
    auto put = [&](size_t i) {
        if (i >= moved.size() * 64) moved.resize((i >> 6) + 1, 0);
        moved[i >> 6] |= uint64_t(1) << (i & 63);
    };
    for (size_t i = 0; i < from; ++i) {
        if (bit(i)) put(i);
    }
    for (size_t i = from; i < total; ++i) {
        if (!bit(i)) continue;
        // Nothing at or after `from` can move below zero: an erase only takes
        // away places that were there.
        const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(i) + delta;
        if (target >= 0) put(static_cast<size_t>(target));
    }
    bits = std::move(moved);
}

bool JsValue::is_array_hole(size_t index) const {
    if (!object_val || !object_val->is_array || !object_val->array_holes) return false;
    return object_val->array_holes->empty_slot(index);
}

/// The elements of `cell` moved by `delta` places from `from` onwards (`delta`
/// negative for an erase): the hole bits move with them, and the places that
/// were inserted are elements, not holes.
static void move_holes(JsObject* cell, size_t from, std::ptrdiff_t delta) {
    if (!cell || !cell->array_holes || delta == 0) return;
    cell->array_holes->shift(from, delta);
}

void JsValue::set_array_hole(size_t index) {
    JsObject* cell = object_val.get();
    if (!cell || !cell->is_array) return;
    if (!cell->array_holes) cell->array_holes = std::make_unique<JsObject::Holes>();
    if (index >= cell->array_elements.size()) cell->array_elements.resize(index + 1, JsValue::make_undefined());
    cell->array_elements[index] = JsValue::make_undefined();
    cell->array_holes->set(index);
}

void JsValue::clear_array_hole(size_t index) {
    if (!object_val || !object_val->array_holes) return;
    object_val->array_holes->clear(index);
}

JsValue JsValue::array_element(size_t index) const {
    if (!object_val || !object_val->is_array) return JsValue::make_undefined();
    if (index >= object_val->array_elements.size()) return JsValue::make_undefined();
    if (is_array_hole(index)) return JsValue::make_undefined();
    return object_val->array_elements[index];
}

JsValue js_array_get(const JsValue& array, size_t index) {
    const JsObject* cell = array.object_val.get();
    if (!cell || !cell->is_array) return JsValue::make_undefined();
    if (index < cell->array_elements.size() && !(cell->array_holes && cell->array_holes->empty_slot(index))) {
        return cell->array_elements[index];
    }
    // A hole, or a place past the end. Nearly always nothing answers for it,
    // and that is known without looking the index up by name; otherwise the
    // long way, which finds what `Object.defineProperty` made of the index
    // and what the prototype has.
    if (js_array_holes_read_undefined(array)) return JsValue::make_undefined();
    return array.get_property(std::to_string(index));
}

bool js_array_holes_read_undefined(const JsValue& array) {
    const JsObject* cell = array.object_val.get();
    if (!cell || !cell->is_array) return true;
    // Its own: an index is in the property map only after define_accessor.
    if (cell->has_accessors || cell->exotic || cell->has_legacy_getters || cell->lookup_fallback) return false;
    // The chain: an array with elements, or a property whose name is a
    // number. What an object is and whether an array has elements is looked
    // at every time, and costs nothing. The names are the long part
    // (Array.prototype and Object.prototype have some sixty between them, and
    // none begins with a digit), so the chain whose names were last found
    // clean is remembered with the count of g_index_named_keys it was found
    // at: the same objects at the same count have the same answer. An object
    // that takes the address of one that is gone starts with no names, and
    // gets every one of them through PropertyMap::entry, which counts.
    struct Clean {
        uint64_t count = 0;
        int length = -1;
        const JsObject* chain[4] = {nullptr, nullptr, nullptr, nullptr};
    };
    static thread_local Clean clean;
    const uint64_t count = g_index_named_keys.load(std::memory_order_relaxed);
    const int remembered = clean.count == count ? clean.length : -1;
    bool same = remembered >= 0;
    const JsObject* chain[4] = {nullptr, nullptr, nullptr, nullptr};
    int length = 0;
    for (const JsObject* proto = cell->prototype; proto; proto = proto->prototype, ++length) {
        if (length >= kMaxPrototypeChain) return false;
        if (proto->exotic || proto->has_legacy_getters || proto->lookup_fallback) return false;
        if (proto->is_array && !proto->array_elements.empty()) return false;
        if (length < 4) {
            chain[length] = proto;
            if (same && (length >= remembered || clean.chain[length] != proto)) same = false;
        } else {
            same = false;
        }
    }
    if (same && length == remembered) return true;
    for (const JsObject* proto = cell->prototype; proto; proto = proto->prototype) {
        for (const auto& entry : proto->properties) {
            if (!entry.first.empty() && entry.first[0] >= '0' && entry.first[0] <= '9') return false;
        }
    }
    if (length <= 4) {
        clean.count = count;
        clean.length = length;
        for (int i = 0; i < 4; ++i) clean.chain[i] = chain[i];
    }
    return true;
}

std::vector<JsValue> JsValue::array_snapshot() const {
    std::vector<JsValue> out;
    if (!object_val || !object_val->is_array) return out;
    out.reserve(object_val->array_elements.size());
    // The iterator reads each place as `a[i]` reads it. When no hole can
    // read as anything, nothing runs between the first place and the last
    // and the storage is the answer; otherwise each place is read in turn,
    // and the length is looked at again each time, because a getter can
    // change it.
    // The length, not the storage: a length that names places nothing was
    // written to holds no elements (Phase 288), and those places are still
    // places the iterator produces -- `[...a]` on five holes is five
    // `undefined`, not an empty array.
    const size_t length = static_cast<size_t>(js_to_number(get_property("length")));
    out.reserve(length);
    if (js_array_holes_read_undefined(*this)) {
        for (size_t i = 0; i < length; ++i) out.push_back(array_element(i));
    } else {
        for (size_t i = 0; i < length; ++i) out.push_back(js_array_get(*this, i));
    }
    return out;
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
            return "Symbol(" + symbol_description_text() + ")";
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
                    // A hole joins as the empty string, like undefined and null.
                    if (is_array_hole(i)) continue;
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
            if (parse_array_index(name, index)) {
                // An element is an own property, and a place past the end is
                // none. A hole is not one either, which is the whole of
                // `1 in a`, unless `Object.defineProperty` has made
                // something of the index: an accessor, or a data property
                // written over one. Those are kept in the property map, with
                // the place left a hole (define_accessor), so a hole asks the
                // map. Only an array that has had an accessor defined on it
                // can have one, and no other pays for the lookup.
                if (index >= cell->array_elements.size()) return false;
                if (!is_array_hole(index)) return true;
                return cell->has_accessors && cell->properties.find(name) != cell->properties.end();
            }
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
    // `caller` and `arguments` answer `true` for every function that does not
    // store them itself because %Function.prototype% carries the pair as real
    // accessors (Phase 269), not because of anything here.
    for (JsObject* obj = proto; obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
        if (obj->properties.find(name) != obj->properties.end()) return true;
        if (obj->is_array) {
            // `length` and the indices are both kept in `array_elements` and
            // not in the property map, so `HasProperty` has to answer for
            // both — the same two answers `lookup_chain` gives a read
            // (finding 138).
            if (name == "length") return true;
            size_t index = 0;
            if (parse_array_index(name, index) && index < obj->array_elements.size()
                && !(obj->array_holes && obj->array_holes->empty_slot(index))) return true;
        }
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
        // `length` of an array is not an element and not in the property map:
        // it is computed, and it is not configurable, so [[Delete]] answers
        // false and a strict-mode delete is a TypeError.
        if (cell->is_array && name == "length") return false;
        size_t index = 0;
        if (cell->is_array && parse_array_index(name, index)) {
            // An index can also be in the property map: `Object.defineProperty`
            // puts an accessor there, and a data property defined over one
            // stays there too. Both are the index's own property, so both have
            // to go; and erasing one that was never an element must not mark a
            // hole, or `0 in [, ]` stays true after `delete Array.prototype[0]`.
            auto mapped = cell->properties.find(name);
            if (mapped != cell->properties.end()) {
                if (!(mapped->flags & PropertyMap::Configurable)) return false;
                cell->properties.erase(name);
            }
            // A delete of an element leaves a hole: the index stops being an
            // own property, and stays one of the array's places (its `length`
            // does not move).
            auto& elements = cell->array_elements;
            if (index < elements.size()) set_array_hole(index);
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
    if (cell && cell->exotic) {
        if (include_non_enumerable) cell->exotic->own_keys(cell, keys);
        else cell->exotic->keys(cell, keys);
    }
    // The keys of the property map: the ones that are an index in ascending
    // order, then the rest in the order they were made.
    const PropertyMap* map = cell ? &cell->properties : loose_properties();
    std::vector<std::pair<size_t, const std::string*>> index_keys;
    std::vector<const std::string*> named_keys;
    if (map) {
        for (const auto& entry : *map) {
            if (is_hidden_key(entry.first)) continue;
            if (!include_non_enumerable && !(entry.flags & PropertyMap::Enumerable)) continue;
            size_t index = 0;
            if (parse_array_index(entry.first, index)) index_keys.emplace_back(index, &entry.first);
            else named_keys.push_back(&entry.first);
        }
        std::sort(index_keys.begin(), index_keys.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    if (cell && cell->is_array) {
        // A hole is not a key, unless the map has the index (has_own_property
        // says how it comes to): then it is a key where its number puts it,
        // among the elements, and is not listed a second time below. An
        // index the map has where an element is is not listed at all: the
        // element is the property.
        const size_t length = cell->array_elements.size();
        size_t mapped = 0;
        for (size_t i = 0; i < length; ++i) {
            if (!is_array_hole(i)) {
                keys.push_back(std::to_string(i));
                continue;
            }
            while (mapped < index_keys.size() && index_keys[mapped].first < i) ++mapped;
            if (mapped < index_keys.size() && index_keys[mapped].first == i) keys.push_back(*index_keys[mapped].second);
        }
        index_keys.erase(std::remove_if(index_keys.begin(), index_keys.end(),
                                        [&](const auto& key) { return key.first < length; }),
                         index_keys.end());
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

    for (const auto& [index, key] : index_keys) { (void)index; keys.push_back(*key); }
    for (const auto* key : named_keys) {
        // An array's `length` is listed in the array's own order above, and
        // an entry for it is only where the length names places nothing was
        // written to: listing it twice is what test262's
        // `order-after-define-property` catches.
        if (cell && cell->is_array && include_non_enumerable && *key == "length") continue;
        keys.push_back(*key);
    }
    return keys;
}

void JsValue::define_property(const std::string& name, JsValue value, uint8_t flags) {
    PropertyMap* map = nullptr;
    if (JsObject* cell = property_cell(true)) {
        map = &cell->properties;
        if (is_legacy_getter_key(name)) cell->has_legacy_getters = true;
        if (cell->exotic) {
            JsExotic::PropertyDefinition definition;
            definition.value = &value;
            definition.unwritable = !(flags & PropertyMap::Writable);
            definition.enumerable = (flags & PropertyMap::Enumerable) != 0;
            definition.configurable = (flags & PropertyMap::Configurable) != 0;
            // The host keeps the name: the value and the attributes stay where
            // the host holds them (an arguments place is its parameter's
            // binding, and the flags the host narrowed are its own), so
            // nothing is written into the dense elements or beside them.
            const JsExotic::Definition outcome = cell->exotic->defining(cell, name, definition);
            if (outcome == JsExotic::Definition::Given) {
                // It has given the name up and left the value it held in the
                // table for it, over a hole: the table is what answers now, so
                // it must be asked before the elements (JsValue::get_property,
                // has_own_property) and the descriptor must come from it.
                cell->has_accessors = true;
                uint8_t kept_flags = static_cast<uint8_t>(flags & ~PropertyMap::Accessor);
                // `entry` answers a mutable reference to the entry for the
                // name — null-iterator aside, the map is never indexed by a
                // temporary, so this is the entry the table now holds.
                PropertyMap::Entry& fresh = map->entry(name);
                if (definition.value != nullptr) fresh.second = value;
                fresh.setter.reset();
                fresh.flags = kept_flags;
                return;
            }
            if (outcome == JsExotic::Definition::Kept) {
                // The host object keeps the name and has applied the whole
                // descriptor itself — the value into its place and the
                // attributes into a table entry of its own making — so nothing
                // here is written for it.
                return;
            }
        }
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
        // A host object is asked first: it may answer for the place, and for an
        // arguments place an accessor descriptor unmaps it (§10.6
        // [[DefineOwnProperty]] step 5.a.i) and leaves the accessor to be
        // stored below like any other. Asking after storing it would let the
        // mapping's own bookkeeping write over the accessor.
        if (cell->exotic) {
            JsExotic::PropertyDefinition definition;
            definition.accessor = true;
            definition.unwritable = !(flags & PropertyMap::Writable);
            definition.enumerable = (flags & PropertyMap::Enumerable) != 0;
            definition.configurable = (flags & PropertyMap::Configurable) != 0;
            if (cell->exotic->defining(cell, name, definition) == JsExotic::Definition::Kept) return;
        }
        cell->has_accessors = true;
        size_t index = 0;
        if (cell->is_array && parse_array_index(name, index)) {
            // An accessor on an index of an array. The elements hold values
            // and nothing else, so the accessor goes into the property map
            // like any other, and the place is made a hole: every read of an
            // index answers with the element when there is one and goes on
            // to the map when there is a hole (has_own_property,
            // get_property, set_property), so the hole is what makes the
            // accessor the index's property. Before this the element stayed
            // and was the answer, and the accessor was never called.
            //
            // An index at or past the end makes the array that long, as
            // defining any index does; the places in between are holes.
            auto& elements = cell->array_elements;
            if (index > elements.size() + (1u << 24)) {
                js_throw_range_error("Array index too far past the end of the array");
            }
            if (!cell->array_holes) cell->array_holes = std::make_unique<JsObject::Holes>();
            const size_t previous = elements.size();
            if (index >= previous) elements.resize(index + 1, JsValue::make_undefined());
            else elements[index] = JsValue::make_undefined();
            for (size_t i = std::min(previous, index); i <= index; ++i) cell->array_holes->set(i);
        }
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

// ---------------------------------------------------------------------------
// Private fields, methods and accessors
// ---------------------------------------------------------------------------
//
// The brand check is the whole difference between these and get_property: a
// private slot belongs to the object, not to a chain, and an object that the
// declaring class did not make carries nothing under the name. Reading such a
// name is an error rather than `undefined`, which is what makes a private field
// private: outside code cannot even tell whether the object has one.

namespace {
/// The own private slot for `key`, or null: the brand check, and the only way
/// into the private table.
PropertyMap::Entry* private_entry_of(const JsValue& value, const std::string& key) {
    JsObject* cell = value.property_cell();
    PropertyMap* map = cell ? cell->private_map() : nullptr;
    if (!map) return nullptr;
    auto found = map->find(key);
    return found == map->end() ? nullptr : &*found;
}
}   // namespace

bool JsValue::has_private(const std::string& key) const {
    return private_entry_of(*this, key) != nullptr;
}

void JsValue::private_accessor(const std::string& key, JsValue& getter, JsValue& setter) const {
    const PropertyMap::Entry* entry = private_entry_of(*this, key);
    if (!entry || !(entry->flags & PropertyMap::Accessor)) return;
    getter = entry->second;
    if (entry->setter) setter = *entry->setter;
}

JsValue JsValue::get_private(const std::string& key, const std::string& display) const {
    const PropertyMap::Entry* entry = private_entry_of(*this, key);
    if (!entry) {
        js_throw_type_error("Cannot read private member " + display +
                            " from an object whose class did not declare it");
    }
    if (entry->flags & PropertyMap::Accessor) {
        if (!entry->second.is_function()) {
            js_throw_type_error("'" + display + "' was defined without a getter");
        }
        return entry->second.call_with_this(*this, {});
    }
    return entry->second;
}

void JsValue::set_private(const std::string& key, const JsValue& value, const std::string& display) {
    PropertyMap::Entry* entry = private_entry_of(*this, key);
    if (!entry) {
        js_throw_type_error("Cannot write private member " + display +
                            " to an object whose class did not declare it");
    }
    if (entry->flags & PropertyMap::Accessor) {
        // An accessor pair with no setter refuses the write, as it does in
        // strict code, which every class body is.
        if (!entry->setter) {
            js_throw_type_error("Cannot set private accessor " + display + " which has only a getter");
        }
        entry->setter->call_with_this(*this, {value});
        return;
    }
    if (!(entry->flags & PropertyMap::Writable)) {
        js_throw_type_error("Cannot assign to read only private member " + display);
    }
    entry->second = value;
}

void JsValue::define_private(const std::string& key, JsValue value, uint8_t flags) {
    JsObject* cell = property_cell(true);
    PropertyMap* map = cell ? cell->private_map(true) : nullptr;
    if (!map) js_throw_type_error("Cannot define a private member on this value");
    auto& entry = map->entry(key);
    entry.second = std::move(value);
    entry.setter.reset();
    entry.flags = static_cast<uint8_t>(flags & ~PropertyMap::Accessor);
}

void JsValue::define_private_accessor(const std::string& key, const JsValue& getter, const JsValue& setter,
                                      uint8_t flags) {
    JsObject* cell = property_cell(true);
    PropertyMap* map = cell ? cell->private_map(true) : nullptr;
    if (!map) js_throw_type_error("Cannot define a private member on this value");
    auto& entry = map->entry(key);
    entry.second = getter;
    entry.setter = setter.is_function() ? std::make_shared<JsValue>(setter) : nullptr;
    entry.flags = static_cast<uint8_t>((flags | PropertyMap::Accessor) & ~PropertyMap::Writable);
}

// The EventTarget methods, for any node: elements, text nodes and the document.
namespace {

/// True when `Object.prototype`'s own `__proto__` entry is no longer the
/// accessor `install_object_builtins` put there -- a script replaced it, and
/// the replacement is what a read must call.
bool object_proto_has_replaced_accessor() {
    const JsObject* op = JsIntrinsics::get().object_proto;
    if (!op) return false;
    const auto it = op->properties.find("__proto__");
    if (it == op->properties.end()) return true;      // removed: nothing to stand in for
    if (!(it->flags & PropertyMap::Accessor)) return true;
    const JsValue& getter = it->second;
    // The installed getter is a native whose name is "get __proto__".
    if (!getter.is_function() || !getter.function_data()) return true;
    return getter.function_data()->name != "get __proto__";
}

/// True when `start` reaches `Object.prototype`, which is where the `__proto__`
/// accessor lives. An object that replaced that prototype with `null` (or with
/// anything else not ending there) has no `__proto__` to answer with.
bool prototype_chain_has_object_proto(JsObject* start) {
    JsObject* proto = start;
    for (int depth = 0; proto && depth < 1000; ++depth) {
        if (proto == JsIntrinsics::get().object_proto) return true;
        proto = proto->prototype;
    }
    return false;
}

} // namespace

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
        // `__proto__` is an accessor on `Object.prototype` (Annex B.2.2.1,
        // installed by `install_object_builtins`). This synthesis is the answer
        // for a receiver that has no such own accessor -- which is every object
        // whose prototype is not `Object.prototype`, since the accessor is
        // inherited through the chain and found there before this runs.
        //
        // It stays because it is what makes `obj.__proto__` work for a receiver
        // built before that accessor existed, and because it is faster than a
        // chain walk; it must not run when the object has its own `__proto__`
        // entry, or `Object.defineProperty(o, "__proto__", ...)` would be
        // invisible to reads.
        // The answer for `__proto__` here is the receiver's [[Prototype]] -- but
        // only when the receiver would have reached the accessor on
        // `Object.prototype`. A receiver whose chain does not include it has no
        // `__proto__` at all: after `o.__proto__ = null`, Node answers
        // `undefined`, not `null`, because the accessor went with the prototype.
        // The synthesis answers only while the accessor it stands in for is
        // still there. `Object.defineProperty(Object.prototype, "__proto__",
        // {...})` replaces it, and from then on the replacement must be what
        // reads: Node answers the script's getter, and a synthesis that ran
        // first would answer the internal slot and hide it.
        if (name == "__proto__" && obj->properties.find("__proto__") == obj->properties.end() &&
            !object_proto_has_replaced_accessor() &&
            prototype_chain_has_object_proto(obj->prototype)) {
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
                // `length` is the element count until a descriptor says
                // otherwise: `Object.defineProperty(a, "length",
                // { writable: false })` keeps the count and its attributes in
                // the property map, with the value the count still is
                // (finding 253).
                auto length_entry = obj->properties.find("length");
                if (length_entry != obj->properties.end()) return read_entry(*length_entry, *this);
                return JsValue::make_number(static_cast<double>(obj->array_elements.size()));
            }
            size_t index = 0;
            if (parse_array_index(name, index)) {
                // An index that is an element is the answer. A hole is not an
                // own property at all (`1 in a` is false), so the read goes on
                // and may find one on the prototype, as `HasProperty` does;
                // and when nothing does, it is `undefined`.
                if (index < obj->array_elements.size() && !is_array_hole(index)) {
                    return obj->array_elements[index];
                }
                // What `Object.defineProperty` made of a hole (an accessor,
                // see define_accessor) is in the property map, and is read
                // below with the other own properties.
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
            // The object that was set, not a description of it: §4.12.5.1's
            // getter answers what the setter took, and `ctx.fillStyle === g`
            // has to hold for a gradient and a pattern as it does for a colour.
            if (canvas_context()->fill_paint.type != JsType::Undefined) return canvas_context()->fill_paint;
            return JsValue::make_string(canvas_context()->fill_style.to_hex());
        }
        if (name == "strokeStyle") {
            if (canvas_context()->stroke_paint.type != JsType::Undefined) return canvas_context()->stroke_paint;
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

namespace {

/// What an array element write has to ask the prototype chain, and the one
/// question the element path used to skip (ECMAScript §10.1.9.2,
/// OrdinarySetWithOwnDescriptor).
///
/// A key the array does not have of its own — a hole, or a place past the end
/// — is a key the chain may have something to say about, and an index is a
/// key. Three answers are possible, and each is one line of the standard:
///
/// * an accessor with a setter: the setter runs with the array as `this` and
///   the place stays a hole (the element is *not* written);
/// * an accessor with no setter, or a **non-writable** data property: the
///   write is refused, which is `false` here and a TypeError in strict code;
/// * a **writable** data property, or nothing at all: the ordinary element
///   write happens and shadows the prototype, which is `false` returned with
///   `handled` still false.
///
/// The bug this replaces set `handled` as soon as a map had the key, so a
/// *data* property on the prototype refused a write that should have shadowed
/// it — `Object.defineProperty(Array.prototype, '0', { value: 'p', writable:
/// true })` then `[][0] = 5` left the array empty where a browser fills it.
/// A data property is therefore never `handled` here: the element write below
/// already shadows it, and only an accessor can redirect the write to code.
///
/// Worth a walk only when some accessor whose name is an index exists
/// anywhere, so an ordinary program pays one relaxed load per element write.
bool set_through_prototype(const JsValue& receiver, const std::string& name, const JsValue& val,
                           bool& handled) {
    handled = false;
    // Any property whose name is an index, anywhere: an accessor *or* a data
    // property that was given attributes. Both can answer a write, and the
    // count of them is the same one the hole-read question keeps (above), so
    // an ordinary program — whose maps hold no index at all — pays one
    // relaxed load per element write and no walk.
    if (g_index_named_keys.load(std::memory_order_relaxed) == 0) return false;
    const JsObject* cell = receiver.object_val.get();
    if (cell == nullptr || cell->prototype == nullptr) return false;
    int guard = 0;
    for (JsObject* proto = cell->prototype; proto && guard < kMaxPrototypeChain;
         proto = proto->prototype, ++guard) {
        const auto found = proto->properties.find(name);
        if (found == proto->properties.end()) continue;
        if (found->flags & PropertyMap::Accessor) {
            handled = true;
            if (found->setter && found->setter->is_function()) {
                found->setter->call_with_this(receiver, {val});
                return true;
            }
            return false;      // an accessor with no setter: refused
        }
        // A data property the prototype keeps in its map — an index it was
        // given attributes for, since an ordinary element has none (Phase
        // 249). Non-writable refuses the write; writable is left to the
        // element write, which shadows it.
        if (!(found->flags & PropertyMap::Writable)) {
            handled = true;
            return false;
        }
        return false;
    }
    return false;
}

}   // namespace

// Returns false when the write was refused — nothing was stored, and a
// strict-mode assignment must throw a TypeError, as [[Set]]'s false requires.
bool JsValue::set_property(const std::string& name, JsValue val) {
    if (canvas_context()) {
        if (name == "fillStyle") {
            // A style is exactly one of the three: a colour, a gradient or a
            // pattern. Setting any one clears the other two, so a `fillStyle`
            // left over from the previous drawing cannot paint under this one.
            if (val.canvas_gradient_value()) {
                canvas_context()->fill_gradient = val.canvas_gradient_value();
                canvas_context()->fill_pattern = nullptr;
                canvas_context()->fill_paint = val;
            } else if (val.canvas_pattern_value()) {
                canvas_context()->fill_pattern = val.canvas_pattern_value();
                canvas_context()->fill_gradient = nullptr;
                canvas_context()->fill_paint = val;
            } else {
                canvas_context()->fill_gradient = nullptr;
                canvas_context()->fill_pattern = nullptr;
                canvas_context()->fill_paint = JsValue();
                canvas_context()->fill_style = Color::parse(val.to_string());
            }
        } else if (name == "strokeStyle") {
            if (val.canvas_gradient_value()) {
                canvas_context()->stroke_gradient = val.canvas_gradient_value();
                canvas_context()->stroke_pattern = nullptr;
                canvas_context()->stroke_paint = val;
            } else if (val.canvas_pattern_value()) {
                canvas_context()->stroke_pattern = val.canvas_pattern_value();
                canvas_context()->stroke_gradient = nullptr;
                canvas_context()->stroke_paint = val;
            } else {
                canvas_context()->stroke_pattern = nullptr;
                canvas_context()->stroke_gradient = nullptr;
                canvas_context()->stroke_paint = JsValue();
                canvas_context()->stroke_style = Color::parse(val.to_string());
            }
        } else if (name == "lineWidth") {
            canvas_context()->line_width = static_cast<float>(val.to_number());
        } else if (name == "globalCompositeOperation") {
            canvas_context()->global_composite_operation = val.to_string();
        }
    }

    if (type == JsType::String || type == JsType::Number || type == JsType::Boolean || type == JsType::Symbol) {
        return false;     // assigning to a primitive's property is refused
    }

    // `fn.__proto__ = x` needs the function to have a cell, whatever kind of
    // function it is: a native one would otherwise store a property by that name.
    const bool sets_prototype = name == "__proto__";
    if (JsObject* obj = (sets_prototype && is_function()) ? function_cell() : property_cell(true)) {
        if (obj->exotic) {
            switch (obj->exotic->set(obj, name, val)) {
                case JsExotic::SetResult::Handled: return true;
                case JsExotic::SetResult::Refused: return false;
                case JsExotic::SetResult::NotHandled: break;
            }
        }
        if (obj->assign_hook && (*obj->assign_hook)(name, val)) return true;
        // The same rule as the read side: this stands in for the accessor on
        // `Object.prototype` and must not run when it has been replaced or when
        // the receiver has an entry of its own -- otherwise a script's setter
        // would be silently skipped, and `o.__proto__ = v` would move the
        // prototype instead of calling it.
        const bool own_proto_entry = sets_prototype &&
            obj->properties.find("__proto__") != obj->properties.end();
        const bool proto_still_ours = sets_prototype &&
            !own_proto_entry && !object_proto_has_replaced_accessor();
        if (proto_still_ours) {
            // Anything but an object or null is ignored, as the setter on
            // Object.prototype does. A change that is refused throws.
            if (!val.is_object_like() && val.type != JsType::Null) return false;
            JsObject* target = val.type == JsType::Null ? nullptr : val.as_prototype();
            if (!js_set_prototype(obj, target)) {
                js_throw_error(JsErrorKind::TypeError,
                               obj->extensible ? "Cyclic __proto__ value" : "Object is not extensible");
            }
            return true;
        }
        if (is_wrapped_string_index(obj, name)) return false;      // the characters are read-only

        if (obj->is_array) {
            if (name == "length") {
                if (obj->frozen) return false;
                double requested = js_to_number(val);
                if (!(requested >= 0) || requested != std::floor(requested) || requested > 4294967295.0) {
                    js_throw_range_error("Invalid array length");
                }
                // The length this array has now, which is the count in the
                // entry when a grow stopped materialising places: the elements
                // can be fewer than the length, never more.
                size_t previous = obj->array_elements.size();
                if (const auto entry = obj->properties.find("length"); entry != obj->properties.end()) {
                    previous = std::max(previous, static_cast<size_t>(js_to_number(entry->second)));
                }
                size_t next = static_cast<size_t>(requested);
                // What `Object.defineProperty` made of an index is in the
                // property map (has_own_property). A shorter array has lost
                // the indices past its new end, those included; and one of
                // them that is not configurable cannot be lost, so the array
                // ends just after the last such one and the write is refused,
                // as the standard's ArraySetLength has it.
                bool refused = false;
                if (obj->has_accessors && next < previous) {
                    std::vector<std::string> lost;
                    for (const auto& entry : obj->properties) {
                        size_t at = 0;
                        if (!parse_array_index(entry.first, at) || at < next || at >= previous || !is_array_hole(at)) continue;
                        if (entry.flags & PropertyMap::Configurable) continue;
                        next = at + 1;
                        refused = true;
                    }
                    for (const auto& entry : obj->properties) {
                        size_t at = 0;
                        if (parse_array_index(entry.first, at) && at >= next && at < previous) lost.push_back(entry.first);
                    }
                    for (const auto& key : lost) obj->properties.erase(key);
                }
                // Growing an array adds places that are not properties: holes.
                // A hole is not storage — it is the *absence* of it — and the
                // places a growing length names are all holes, so materialising
                // one `JsValue` each is the engine paying 88 bytes for "there
                // is nothing here". At length 2^32-1 that was 377 GiB: the
                // allocation either threw `std::bad_alloc` (an exception no
                // script can catch) or the process was killed, where Node
                // answers the same assignment instantly because a hole costs
                // it nothing. So a grow past the places that exist only
                // remembers the length: the entry written above is what
                // `length` reads back, and every read, `in`, `Object.keys`,
                // and index write already treats a place past the end of
                // `array_elements` as the hole it is.
                const size_t from_elements = obj->array_elements.size();
                if (next <= from_elements) {
                    // Shrinking does lose elements — the places past the new
                    // length are gone, and their hole bits with them.
                    obj->array_elements.resize(next, JsValue::make_undefined());
                    if (obj->array_holes) {
                        for (size_t i = next; i < from_elements; ++i) obj->array_holes->clear(i);
                    }
                } else if (obj->array_holes) {
                    for (size_t i = next; i < previous; ++i) obj->array_holes->clear(i);
                }
                // The entry is the length now, whichever way it moved. Its
                // attributes are an array's `length`: writable, not
                // enumerable, not configurable — so a later length write comes
                // back through this branch, and `Object.getOwnPropertyDescriptor`
                // answers what it answered when the element count was the length.
                // And when the length agrees with the elements again, the
                // entry goes: the element count *is* the length then, and a
                // stale entry beside it is a second source of truth. That is
                // not hypothetical — `log.length = 0; log.push('a')` left the
                // entry at 0 while the elements had one place, and `push`
                // answered 0 with the element at index 0.
                auto existing = obj->properties.find("length");
                if (next == obj->array_elements.size()) {
                    if (existing != obj->properties.end()) obj->properties.erase("length");
                    return !refused;
                }
                if (existing != obj->properties.end()) {
                    existing->second = JsValue::make_number(static_cast<double>(next));
                } else {
                    PropertyMap::Entry& entry = obj->properties.entry("length");
                    entry.second = JsValue::make_number(static_cast<double>(next));
                    entry.flags = PropertyMap::Writable;
                }
                return !refused;
            }
            size_t index = 0;
            if (parse_array_index(name, index)) {
                if (obj->frozen) return false;
                // A hole that `Object.defineProperty` has made something of
                // (has_own_property): the write goes to that, a setter or a
                // data property with attributes of its own, and the place
                // stays a hole. Writing the element here would hide it.
                if (obj->has_accessors && index < obj->array_elements.size() && is_array_hole(index)) {
                    auto mapped = obj->properties.find(name);
                    if (mapped != obj->properties.end()) {
                        if (mapped->flags & PropertyMap::Accessor) {
                            // Held here: the setter may delete the property it is.
                            const std::shared_ptr<JsValue> setter = mapped->setter;
                            if (!setter || !setter->is_function()) return false;
                            setter->call_with_this(*this, {val});
                            return true;
                        }
                        if (!(mapped->flags & PropertyMap::Writable)) return false;
                        mapped->second = std::move(val);
                        return true;
                    }
                }
                // A place the array does not own — a hole, or past the end —
                // is a key the prototype chain may answer for, and an index is
                // a key (§10.1.9.2). A setter there runs instead of the write,
                // a non-writable data property or a getter with no setter
                // refuses it, and a writable data property is shadowed by the
                // element write below, as the standard has it. An element the
                // array already has is its own property and the chain has no
                // say, so this is asked only for a place it does not own.
                if (index >= obj->array_elements.size() ||
                    (obj->array_holes && obj->array_holes->empty_slot(index))) {
                    bool handled = false;
                    const bool answer = set_through_prototype(*this, name, val, handled);
                    if (handled) return answer;
                }
                if (index >= obj->array_elements.size()) {
                    if (!obj->extensible) return false;
                    // The places it skips over are holes, and a hole is not
                    // storage (Phase 288). Writing just past the elements
                    // extends the vector by a little; writing far past them —
                    // `a[99999999] = 1` — grows the length and stores nothing,
                    // because Node answers that with a length of 100000000 and
                    // no allocation. This used to throw a RangeError invented
                    // here at 2^24, and before that it allocated everything in
                    // between.
                    const size_t previous = obj->array_elements.size();
                    if (index > previous + kMaxDenseGap) {
                        if (!obj->array_holes) obj->array_holes = std::make_unique<JsObject::Holes>();
                        for (size_t i = previous; i < previous + kMaxDenseGap && i < index; ++i) {
                            obj->array_holes->set(i);
                        }
                    } else {
                        obj->array_elements.resize(index + 1, JsValue::make_undefined());
                        if (!obj->array_holes) obj->array_holes = std::make_unique<JsObject::Holes>();
                        for (size_t i = previous; i < index; ++i) obj->array_holes->set(i);
                    }
                }
                if (index < obj->array_elements.size()) {
                    obj->array_elements[index] = std::move(val);
                    clear_array_hole(index);   // writing a hole makes it an element again
                } else {
                    // Past the elements: a place of its own, and the length
                    // follows it.
                    obj->properties.entry(name).second = val;
                }
                // The length has to reach past the place just written. When an
                // entry holds it (any array whose length named places nothing
                // was written to has one, Phase 288) that entry is raised;
                // without one the element count *is* the length, and extending
                // the vector already raised it. An entry that agrees with the
                // count goes, so there is only ever one answer.
                {
                    const double wanted = static_cast<double>(index + 1);
                    auto entry = obj->properties.find("length");
                    if (entry != obj->properties.end()) {
                        if (js_to_number(entry->second) < wanted) {
                            entry->second = JsValue::make_number(wanted);
                        }
                    } else if (wanted > static_cast<double>(obj->array_elements.size())) {
                        // A place past the elements with no entry: the count
                        // would be the length, but the count does not include
                        // a place that went to the map, so the length is
                        // written down.
                        PropertyMap::Entry& made = obj->properties.entry("length");
                        made.second = JsValue::make_number(wanted);
                        made.flags = PropertyMap::Writable;
                    }
                    // Not erased here even when it agrees with the count: the
                    // place just written may live in the map rather than the
                    // elements, and then the count is not the length at all.
                    // The length assignment is where the entry goes, and it
                    // knows both numbers.
                }
                return true;
            }
        }

        auto it = obj->properties.find(name);
        if (it != obj->properties.end()) {
            if (it->flags & PropertyMap::Accessor) {
                if (it->setter && it->setter->is_function()) { it->setter->call_with_this(*this, {val}); return true; }
                return false;
            }
            if (obj->frozen || !(it->flags & PropertyMap::Writable)) return false;
            it->second = std::move(val);
            return true;
        }

        // `F.prototype = {...}` before anything has read F.prototype. The
        // property is there already as far as a script can tell (see
        // function_implicit_property), writable and nothing else, so the
        // assignment must not make an enumerable, deletable one: a loop that
        // copies the static members of F would copy `prototype` with them.
        if (name == "prototype" && function_has_implicit_property(*this, name)) {
            if (obj->frozen) return false;
            auto& slot = obj->properties.entry(name);
            slot.second = std::move(val);
            slot.flags = PropertyMap::Writable;
            return true;
        }

        // A setter inherited from a prototype (a class `set x(v)`) runs instead
        // of creating an own property.
        int guard = 0;
        for (JsObject* proto = obj->prototype; proto && guard < kMaxPrototypeChain; proto = proto->prototype, ++guard) {
            if (!proto->has_accessors) continue;
            auto pit = proto->properties.find(name);
            if (pit == proto->properties.end()) continue;
            if (pit->flags & PropertyMap::Accessor) {
                if (pit->setter && pit->setter->is_function()) { pit->setter->call_with_this(*this, {val}); return true; }
                return false;
            }
            break;
        }

        if (!obj->extensible || obj->frozen) return false;
        if (is_legacy_getter_key(name)) obj->has_legacy_getters = true;
        obj->properties[name] = std::move(val);
        return true;
    }

    PropertyMap* map = loose_properties(true);
    if (!map) {
        properties = std::make_shared<PropertyMap>();
        map = properties.get();
    }
    auto it = map->find(name);
    if (it != map->end()) {
        if (it->flags & PropertyMap::Accessor) {
            if (it->setter && it->setter->is_function()) { it->setter->call_with_this(*this, {val}); return true; }
            return false;
        }
        if (!(it->flags & PropertyMap::Writable)) return false;
        it->second = std::move(val);
        return true;
    }
    (*map)[name] = std::move(val);
    return true;
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
