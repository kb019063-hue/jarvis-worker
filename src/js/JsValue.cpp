#include "litebird/js/JsValue.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/Storage.hpp"
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
    if (extra()) if (auto v = std::get_if<std::shared_ptr<Node>>(&extra()->payload)) return *v;
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
void JsValue::set_dom_node_value(std::shared_ptr<Node> v) { mutable_extra().payload = std::move(v); }
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

long PropertyMap::index_of(const std::string& key) const {
    if (m_entries.size() > kIndexThreshold) {
        auto it = m_index.find(key);
        return it == m_index.end() ? -1 : static_cast<long>(it->second);
    }
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].first == key) return static_cast<long>(i);
    }
    return -1;
}

void PropertyMap::rebuild_index() {
    m_index.clear();
    if (m_entries.size() <= kIndexThreshold) return;
    m_index.reserve(m_entries.size() * 2);
    for (size_t i = 0; i < m_entries.size(); ++i) m_index[m_entries[i].first] = static_cast<uint32_t>(i);
}

PropertyMap::iterator PropertyMap::find(const std::string& key) {
    long i = index_of(key);
    return i < 0 ? m_entries.end() : m_entries.begin() + i;
}

PropertyMap::const_iterator PropertyMap::find(const std::string& key) const {
    long i = index_of(key);
    return i < 0 ? m_entries.end() : m_entries.begin() + i;
}

PropertyMap::Entry& PropertyMap::entry(const std::string& key) {
    long i = index_of(key);
    if (i >= 0) return m_entries[static_cast<size_t>(i)];
    m_entries.emplace_back();
    m_entries.back().first = key;
    if (m_entries.size() == kIndexThreshold + 1) rebuild_index();
    else if (m_entries.size() > kIndexThreshold + 1) m_index[key] = static_cast<uint32_t>(m_entries.size() - 1);
    return m_entries.back();
}

JsValue& PropertyMap::operator[](const std::string& key) {
    return entry(key).second;
}

bool PropertyMap::erase(const std::string& key) {
    long i = index_of(key);
    if (i < 0) return false;
    m_entries.erase(m_entries.begin() + i);
    rebuild_index();     // deletion is rare; keeping insertion order matters more
    return true;
}

void PropertyMap::clear() {
    m_entries.clear();
    m_index.clear();
}

// ---------------------------------------------------------------------------
// Call context: how `this` reaches a native function
// ---------------------------------------------------------------------------

namespace {

struct CallContext {
    JsValue this_val;
    const JsValue* callee{nullptr};   // the function being called; valid for the call's duration
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

/// Looks `name` up on `start` and then along its prototype chain.
bool lookup_chain(JsObject* start, const std::string& name, const JsValue& receiver, JsValue& out) {
    int guard = 0;
    for (JsObject* obj = start; obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
        auto it = obj->properties.find(name);
        if (it != obj->properties.end()) {
            out = read_entry(*it, receiver);
            return true;
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

bool JsValue::take_call_context(JsValue& this_out, bool& is_construct_out, JsValue* callee_out) {
    if (!t_call_context.fresh) {
        this_out = JsValue::make_undefined();
        is_construct_out = false;
        if (callee_out) *callee_out = JsValue::make_undefined();
        return false;
    }
    t_call_context.fresh = false;
    this_out = t_call_context.this_val;
    is_construct_out = t_call_context.is_construct;
    if (callee_out) *callee_out = t_call_context.callee ? *t_call_context.callee : JsValue::make_undefined();
    return true;
}

JsObject* JsValue::property_cell(bool create) const {
    if (object_val) return object_val;
    if (type != JsType::Function) return nullptr;
    const FunctionData* fd = function_data();
    if (!fd || !fd->node) return nullptr;
    if (!fd->cell && create) {
        fd->cell = Heap::default_heap().allocate_object();
        fd->cell->prototype = JsIntrinsics::get().function_proto;
        fd->cell->klass = ObjectClass::Function;
    }
    return fd->cell;
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

JsValue JsValue::construct(const std::vector<JsValue>& args, const JsValue* this_val) const {
    static const JsValue undefined_this;
    if (stack_is_exhausted()) throw JsStackOverflow();
    const NativeFn& fn = native_function();
    if (fn) {
        CallContextScope scope(this_val ? *this_val : undefined_this, true, this);
        return fn(args);
    }
    if (type == JsType::Function && s_function_caller) {
        return s_function_caller(*this, args, this_val);
    }
    return JsValue::make_undefined();
}

JsValue JsValue::make_object(JsObject* obj) {
    JsValue v;
    v.type = JsType::Object;
    if (obj) {
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
    if (type == JsType::DomNode) return dom_node_value().get();
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
        case JsType::DomNode:
            // A DOM wrapper is minted fresh on every property access, so its
            // JsObject address is NOT a stable identity. The node behind it is.
            // Comparing nodes is what makes `document.body === document.body`
            // and `el.shadowRoot === sr` behave the way script expects.
            return a.dom_node_value() == b.dom_node_value();
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
        case JsType::DomNode:
        case JsType::Symbol:
            return true;
    }
    return false;
}

bool JsValue::is_array() const {
    return type == JsType::Object && object_val && object_val->is_array;
}

std::vector<JsValue>& JsValue::array_elements() {
    if (!object_val) {
        object_val = Heap::default_heap().allocate_object();
        object_val->is_array = true;
        object_val->klass = ObjectClass::Array;
        object_val->prototype = JsIntrinsics::get().array_proto;
    }
    return object_val->array_elements;
}

const std::vector<JsValue>& JsValue::array_elements() const {
    static const std::vector<JsValue> s_empty;
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
            else if (const PropertyMap* map = loose_properties()) {
                auto it = map->find("name");
                if (it != map->end()) name = it->second.string_val.str();
            }
            // Bound functions print without a name, as V8 prints them.
            if (name.compare(0, 6, "bound ") == 0) name.clear();
            return "function " + name + "() { [native code] }";
        }
        case JsType::DomNode: {
            const auto& dom_node = dom_node_value();
            if (dom_node && dom_node->is_element()) {
                auto el = std::static_pointer_cast<Element>(dom_node);
                return "[object HTML" + el->tag_name() + "Element]";
            }
            return "[object Node]";
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

bool js_set_prototype(JsObject* cell, JsObject* proto) {
    if (!cell) return true;
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
    if (is_function() && (name == "name" || name == "length")) return function_data() != nullptr;
    return false;
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
    }
    if (object_val && object_val->lookup_fallback) {
        JsValue ignored;
        if ((*object_val->lookup_fallback)(name, ignored)) return true;
    }
    return false;
}

bool JsValue::delete_property(const std::string& name) {
    if (JsObject* cell = property_cell()) {
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
    if (type == JsType::DomNode) return keys;

    JsObject* cell = property_cell();
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
JsValue js_event_target_method(const std::shared_ptr<Node>& target, const std::string& name) {
    if (!target) return JsValue::make_undefined();
    if (name == "addEventListener") {
        auto node = target;
        return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
            if (args.size() < 2 || !args[1].is_function()) return JsValue::make_undefined();
            std::string ev_type = args[0].to_string();
            JsValue cb = args[1];
            // The third argument is `useCapture` or an options object.
            bool use_capture = false;
            bool once = false;
            if (args.size() > 2) {
                if (args[2].is_object()) {
                    use_capture = args[2].get_property("capture").is_truthy();
                    once = args[2].get_property("once").is_truthy();
                } else {
                    use_capture = args[2].is_truthy();
                }
            }
            const void* key = cb.identity();
            std::weak_ptr<Node> weak_node = node;
            node->add_event_listener(ev_type, [cb, weak_node, ev_type, key, use_capture, once](const std::shared_ptr<Event>& ev) mutable {
                if (once) {
                    if (auto target = weak_node.lock()) target->remove_event_listener(ev_type, key, use_capture);
                }
                JsValue ev_obj = JsValue::make_object();
                ev_obj.set_property("type", JsValue::make_string(ev->type()));
                ev_obj.set_property("eventPhase", JsValue::make_number(static_cast<int>(ev->event_phase())));
                ev_obj.set_property("bubbles", JsValue::make_boolean(ev->bubbles()));
                ev_obj.set_property("cancelable", JsValue::make_boolean(ev->cancelable()));
                ev_obj.set_property("defaultPrevented", JsValue::make_boolean(ev->default_prevented()));
                ev_obj.set_property("data", JsValue::make_string(ev->data));
                ev_obj.set_property("key", JsValue::make_string(ev->key));

                if (ev->target()) ev_obj.set_property("target", JsValue::make_dom_node(ev->target()));
                if (ev->current_target()) ev_obj.set_property("currentTarget", JsValue::make_dom_node(ev->current_target()));

                auto ev_ptr = ev;
                ev_obj.set_property("stopPropagation", JsValue::make_native_function([ev_ptr](const std::vector<JsValue>&) {
                    ev_ptr->stop_propagation();
                    return JsValue::make_undefined();
                }));
                ev_obj.set_property("preventDefault", JsValue::make_native_function([ev_ptr](const std::vector<JsValue>&) {
                    ev_ptr->prevent_default();
                    return JsValue::make_undefined();
                }));

                // A listener runs with `this` set to the node it was added to.
                JsValue receiver;
                if (auto current = ev->current_target()) {
                    auto element = std::dynamic_pointer_cast<Element>(current);
                    receiver = (element && !element->custom_element_instance().is_undefined())
                                   ? element->custom_element_instance() : JsValue::make_dom_node(current);
                }
                cb.call_with_this(receiver, {ev_obj});
            }, use_capture, key);
            return JsValue::make_undefined();
        });
    }
    if (name == "removeEventListener") {
        auto node = target;
        return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
            if (args.size() < 2 || !args[1].is_function()) return JsValue::make_undefined();
            bool use_capture = false;
            if (args.size() > 2) {
                use_capture = args[2].is_object() ? args[2].get_property("capture").is_truthy() : args[2].is_truthy();
            }
            node->remove_event_listener(args[0].to_string(), args[1].identity(), use_capture);
            return JsValue::make_undefined();
        });
    }
    if (name == "dispatchEvent") {
        auto node = target;
        return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
            // Takes an event object ({type, bubbles, cancelable}) or, as a
            // shorthand, just the type.
            std::string ev_type = "custom";
            bool bubbles = true;
            bool cancelable = true;
            if (!args.empty() && args[0].is_object()) {
                ev_type = args[0].get_property("type").to_string();
                JsValue b = args[0].get_property("bubbles");
                JsValue c = args[0].get_property("cancelable");
                if (!b.is_undefined()) bubbles = b.is_truthy();
                if (!c.is_undefined()) cancelable = c.is_truthy();
            } else if (!args.empty()) {
                ev_type = args[0].to_string();
            }
            auto ev = std::make_shared<Event>(ev_type, bubbles, cancelable);
            bool ok = node->dispatch_event(ev);
            return JsValue::make_boolean(ok);
        });
    }
    return JsValue::make_undefined();
}

// The DOM's pre-insertion check: a node may not be inserted into itself or
// into anything it contains (its descendants, and what is in its shadow tree).
// Throws what a browser throws; Node::append_child refuses the same cases on
// its own, so this only decides what the script is told.
static void require_insertable(const std::shared_ptr<Node>& parent, const std::shared_ptr<Node>& child,
                               const char* method) {
    for (std::shared_ptr<Node> at = parent; at;) {
        if (at == child) {
            JsValue error = js_make_error(JsErrorKind::Error,
                std::string("Failed to execute '") + method +
                "' on 'Node': The new child element contains the parent.");
            error.set_property("name", JsValue::make_string("HierarchyRequestError"));
            error.set_property("code", JsValue::make_number(3));
            js_throw(std::move(error));
        }
        std::shared_ptr<Node> up = at->parent();
        if (!up) {
            if (auto shadow = std::dynamic_pointer_cast<ShadowRoot>(at)) up = shadow->host();
        }
        at = std::move(up);
    }
}

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

    const auto dom_node = dom_node_value();
    if (type == JsType::DomNode && dom_node) {
        if (name == "addEventListener" || name == "removeEventListener" || name == "dispatchEvent") {
            return js_event_target_method(dom_node, name);
        }
        if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
            if (name == "tagName") return JsValue::make_string(el->tag_name());
            if (name == "id") return JsValue::make_string(el->id());
            if (name == "textContent" || name == "innerText") return JsValue::make_string(el->text_content());
            if (name == "innerHTML") return JsValue::make_string(el->inner_html());
            if (name == "outerHTML") return JsValue::make_string(el->outer_html());
            if (name == "style") {
                JsValue style_obj = JsValue::make_object();
                style_obj.set_property("color", JsValue::make_string(el->get_attribute("style")));
                return style_obj;
            }
            if (name == "value") return JsValue::make_string(el->value());
            if (name == "disabled") return JsValue::make_boolean(el->disabled());
            if (name == "checked") return JsValue::make_boolean(el->checked());
            if (name == "type") return JsValue::make_string(el->input_type());
            if (name == "shadowRoot") {
                auto sr = el->shadow_root();
                if (sr && sr->mode() == ShadowRootMode::Open) {
                    return JsValue::make_dom_node(sr);
                }
                return JsValue::make_null();
            }
            if (name == "attachShadow") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    std::string mode = "open";
                    if (!args.empty() && args[0].type == JsType::Object) {
                        mode = args[0].get_property("mode").to_string();
                    }
                    auto sr = elem->attach_shadow(mode);
                    if (!sr) return JsValue::make_null();
                    return JsValue::make_dom_node(sr);
                });
            }
            if (name == "getBoundingClientRect") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    Rect r = elem->bounding_box();
                    if (r.width <= 0.0f && r.height <= 0.0f) {
                        r.width = 100.0f;
                        r.height = 30.0f;
                    }
                    JsValue ro = JsValue::make_object();
                    ro.set_property("x", JsValue::make_number(r.x));
                    ro.set_property("y", JsValue::make_number(r.y));
                    ro.set_property("width", JsValue::make_number(r.width));
                    ro.set_property("height", JsValue::make_number(r.height));
                    ro.set_property("top", JsValue::make_number(r.top()));
                    ro.set_property("bottom", JsValue::make_number(r.bottom()));
                    ro.set_property("left", JsValue::make_number(r.left()));
                    ro.set_property("right", JsValue::make_number(r.right()));
                    return ro;
                });
            }
            if (name == "setAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.size() >= 2) {
                        elem->set_attribute(args[0].to_string(), args[1].to_string());
                    }
                    return JsValue::make_undefined();
                });
            }
            if (name == "getAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    std::string attr = args[0].to_string();
                    if (elem->has_attribute(attr)) {
                        return JsValue::make_string(elem->get_attribute(attr));
                    }
                    return JsValue::make_null();
                });
            }
            if (name == "hasAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_boolean(false);
                    return JsValue::make_boolean(elem->has_attribute(args[0].to_string()));
                });
            }
            if (name == "removeAttribute") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (!args.empty()) {
                        elem->remove_attribute(args[0].to_string());
                    }
                    return JsValue::make_undefined();
                });
            }
            if (name == "assignedNodes") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    auto nodes = ShadowRoot::assigned_nodes(elem);
                    JsValue arr = JsValue::make_array();
                    for (const auto& n : nodes) {
                        arr.array_elements().push_back(JsValue::make_dom_node(n));
                    }
                    return arr;
                });
            }
            if (name == "assignedElements") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    auto elems = ShadowRoot::assigned_elements(elem);
                    JsValue arr = JsValue::make_array();
                    for (const auto& e : elems) {
                        arr.array_elements().push_back(JsValue::make_dom_node(e));
                    }
                    return arr;
                });
            }
            if (name == "querySelector") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    auto found = elem->query_selector(args[0].to_string());
                    return found ? JsValue::make_dom_node(found) : JsValue::make_null();
                });
            }
            if (name == "querySelectorAll") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>& args) {
                    JsValue arr = JsValue::make_array();
                    if (!args.empty()) {
                        auto list = elem->query_selector_all(args[0].to_string());
                        for (const auto& item : list) {
                            arr.array_elements().push_back(JsValue::make_dom_node(item));
                        }
                    }
                    return arr;
                });
            }
            if (name == "focus") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    elem->focus();
                    return JsValue::make_undefined();
                });
            }
            if (name == "blur") {
                auto elem = el;
                return JsValue::make_native_function([elem](const std::vector<JsValue>&) {
                    elem->blur();
                    return JsValue::make_undefined();
                });
            }
            if (name == "click") {
                auto node = dom_node;
                return JsValue::make_native_function([node](const std::vector<JsValue>&) {
                    if (auto elem = std::dynamic_pointer_cast<Element>(node)) {
                        elem->click();
                    }
                    return JsValue::make_undefined();
                });
            }
            if (name == "getContext") {
                auto node = dom_node;
                return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    std::string ctx_type = args[0].to_string();
                    if (ctx_type == "2d" || ctx_type == "2D") {
                        if (auto el = std::dynamic_pointer_cast<Element>(node)) {
                            auto ctx = el->get_or_create_canvas_context_2d();
                            if (ctx) {
                                return ctx->to_js_value(ctx);
                            }
                        }
                    }
                    return JsValue::make_null();
                });
            }
            if (name == "width") {
                if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
                    if (el->tag_name() == "canvas") {
                        auto ctx = el->get_or_create_canvas_context_2d();
                        return JsValue::make_number(ctx->width());
                    }
                }
            }
            if (name == "height") {
                if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
                    if (el->tag_name() == "canvas") {
                        auto ctx = el->get_or_create_canvas_context_2d();
                        return JsValue::make_number(ctx->height());
                    }
                }
            }
        }

        if (auto sr = std::dynamic_pointer_cast<ShadowRoot>(dom_node)) {
            if (name == "host") {
                auto h = sr->host();
                return h ? JsValue::make_dom_node(h) : JsValue::make_null();
            }
            if (name == "mode") {
                return JsValue::make_string(sr->mode_str());
            }
            if (name == "innerHTML") {
                return JsValue::make_string(sr->inner_html());
            }
            if (name == "getElementById") {
                auto sroot = sr;
                return JsValue::make_native_function([sroot](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    auto found = sroot->get_element_by_id(args[0].to_string());
                    return found ? JsValue::make_dom_node(found) : JsValue::make_null();
                });
            }
            if (name == "querySelector") {
                auto sroot = sr;
                return JsValue::make_native_function([sroot](const std::vector<JsValue>& args) {
                    if (args.empty()) return JsValue::make_null();
                    auto found = sroot->query_selector(args[0].to_string());
                    return found ? JsValue::make_dom_node(found) : JsValue::make_null();
                });
            }
            if (name == "querySelectorAll") {
                auto sroot = sr;
                return JsValue::make_native_function([sroot](const std::vector<JsValue>& args) {
                    JsValue arr = JsValue::make_array();
                    if (!args.empty()) {
                        auto list = sroot->query_selector_all(args[0].to_string());
                        for (const auto& item : list) {
                            arr.array_elements().push_back(JsValue::make_dom_node(item));
                        }
                    }
                    return arr;
                });
            }
        }

        // Generic Node properties & methods
        if (name == "appendChild") {
            auto node = dom_node;
            return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                if (args.empty() || !args[0].dom_node_value()) return JsValue::make_null();
                require_insertable(node, args[0].dom_node_value(), "appendChild");
                node->append_child(args[0].dom_node_value());
                return args[0];
            });
        }
        if (name == "removeChild") {
            auto node = dom_node;
            return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                if (args.empty() || !args[0].dom_node_value()) return JsValue::make_null();
                node->remove_child(args[0].dom_node_value());
                return args[0];
            });
        }
        if (name == "insertBefore") {
            auto node = dom_node;
            return JsValue::make_native_function([node](const std::vector<JsValue>& args) {
                if (args.empty() || !args[0].dom_node_value()) return JsValue::make_null();
                auto new_ch = args[0].dom_node_value();
                auto ref_ch = (args.size() > 1) ? args[1].dom_node_value() : nullptr;
                require_insertable(node, new_ch, "insertBefore");
                node->insert_before(new_ch, ref_ch);
                return args[0];
            });
        }
        if (name == "parentNode" || name == "parentElement") {
            auto p = dom_node->parent();
            return p ? JsValue::make_dom_node(p) : JsValue::make_null();
        }
        if (name == "firstChild") {
            auto fc = dom_node->first_child();
            return fc ? JsValue::make_dom_node(fc) : JsValue::make_null();
        }
        if (name == "lastChild") {
            auto lc = dom_node->last_child();
            return lc ? JsValue::make_dom_node(lc) : JsValue::make_null();
        }
        if (name == "previousSibling") {
            auto ps = dom_node->previous_sibling();
            return ps ? JsValue::make_dom_node(ps) : JsValue::make_null();
        }
        if (name == "nextSibling") {
            auto ns = dom_node->next_sibling();
            return ns ? JsValue::make_dom_node(ns) : JsValue::make_null();
        }
        if (name == "childNodes") {
            JsValue arr = JsValue::make_array();
            for (const auto& ch : dom_node->children()) {
                arr.array_elements().push_back(JsValue::make_dom_node(ch));
            }
            return arr;
        }
        if (name == "children") {
            JsValue arr = JsValue::make_array();
            for (const auto& ch : dom_node->children()) {
                if (ch->is_element()) {
                    arr.array_elements().push_back(JsValue::make_dom_node(ch));
                }
            }
            return arr;
        }
        if (name == "isConnected") {
            return JsValue::make_boolean(dom_node->is_connected());
        }
    }


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

        if (type == JsType::Function) {
            if (const FunctionData* fd = function_data()) {
                if (name == "name") return JsValue::make_string(fd->name);
                if (name == "length") return JsValue::make_number(fd->length);
                if (name == "prototype" && fd->node && fd->is_constructor) {
                    // Created on first use: most functions are never constructors.
                    JsValue proto = JsValue::make_object();
                    proto.define_property("constructor", *this, PropertyMap::Hidden);
                    auto& slot = obj->properties.entry(name);
                    slot.second = proto;
                    slot.flags = PropertyMap::Writable;
                    return proto;
                }
            }
            // Static members are inherited from the parent class.
            auto parent = obj->properties.find("%super");
            if (parent != obj->properties.end() && parent->second.is_function()) {
                if (parent->second.has_property(name)) return parent->second.get_property(name);
            }
        }

        // Prototype chain delegation
        JsValue found;
        if (lookup_chain(obj->prototype, name, *this, found)) return found;

        if (obj->lookup_fallback && (*obj->lookup_fallback)(name, found)) return found;
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

    if (name == "cookie" && type != JsType::Function && type != JsType::NativeFunction) {
        return JsValue::make_string(CookieJar::default_jar().get_document_cookie());
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
        if (const FunctionData* fd = function_data()) {
            if (name == "name") return JsValue::make_string(fd->name);
            if (name == "length") return JsValue::make_number(fd->length);
        }
        JsValue found;
        if (lookup_chain(JsIntrinsics::get().function_proto, name, *this, found)) return found;
    }
    return JsValue::make_undefined();
}

void JsValue::set_property(const std::string& name, JsValue val) {
    const auto dom_node = dom_node_value();
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

    if (name == "cookie") {
        CookieJar::default_jar().set_document_cookie(val.to_string());
        if (object_val) object_val->properties[name] = JsValue::make_string(CookieJar::default_jar().get_document_cookie());
        else if (properties) (*properties)[name] = JsValue::make_string(CookieJar::default_jar().get_document_cookie());
        return;
    }

    if (type == JsType::DomNode && dom_node) {
        if (name == "innerHTML") {
            if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
                el->set_inner_html(val.to_string());
                return;
            }
            if (auto sr = std::dynamic_pointer_cast<ShadowRoot>(dom_node)) {
                sr->set_inner_html(val.to_string());
                return;
            }
        }
        if (auto el = std::dynamic_pointer_cast<Element>(dom_node)) {
            if (name == "textContent" || name == "innerText") {
                el->children().clear();
                el->append_child(std::make_shared<Text>(val.to_string()));
                return;
            }
            if (name == "value") {
                el->set_value(val.to_string());
                return;
            }
            if (name == "disabled") {
                el->set_disabled(val.is_truthy());
                return;
            }
            if (name == "checked") {
                el->set_checked(val.is_truthy());
                return;
            }
            if (name == "width" && el->tag_name() == "canvas") {
                int w = std::max(1, static_cast<int>(val.to_number()));
                el->set_attribute("width", std::to_string(w));
                auto ctx = el->get_or_create_canvas_context_2d();
                ctx->resize(w, ctx->height());
                return;
            }
            if (name == "height" && el->tag_name() == "canvas") {
                int h = std::max(1, static_cast<int>(val.to_number()));
                el->set_attribute("height", std::to_string(h));
                auto ctx = el->get_or_create_canvas_context_2d();
                ctx->resize(ctx->width(), h);
                return;
            }
        }
    }

    if (type == JsType::String || type == JsType::Number || type == JsType::Boolean || type == JsType::Symbol) {
        return;     // assigning to a primitive's property has no effect
    }

    if (JsObject* obj = property_cell(true)) {
        if (obj->assign_hook && (*obj->assign_hook)(name, val)) return;
        if (name == "__proto__") {
            if (obj->frozen) return;
            if (val.object_val && val.is_object_like()) {
                if (!js_set_prototype(obj, val.object_val)) js_throw_error(JsErrorKind::TypeError, "Cyclic __proto__ value");
            } else if (val.type == JsType::Null) {
                obj->prototype = nullptr;
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
    obj->array_elements = elements;
    // Methods live on Array.prototype. They used to be six closures installed
    // on every single array, which also made them show up as own properties.
    obj->prototype = JsIntrinsics::get().array_proto;
    return JsValue::make_object(obj);
}

JsValue JsValue::make_dom_node(std::shared_ptr<Node> node) {
    JsValue v;
    v.type = JsType::DomNode;
    v.set_dom_node_value(std::move(node));
    v.object_val = Heap::default_heap().allocate_object();
    v.object_val->prototype = JsIntrinsics::get().object_proto;
    return v;
}

} // namespace litebird
