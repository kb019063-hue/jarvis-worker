#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <iostream>
#include <variant>
#include <tuple>
#include <cstdint>
#include <exception>
#include "litebird/common/StackLimit.hpp"
#include "litebird/js/JsString.hpp"

namespace litebird {

/// Thrown when the engine is about to run out of native stack. Parsing,
/// evaluating and several built-ins recurse once per level of nesting in the
/// script or in its data, so without a limit a deeply nested expression, an
/// ordinary runaway recursion or `String([[[[...]]]])` overflows the stack and
/// the process dies with no error at all. Scripts see it as a RangeError.
class JsStackOverflow : public std::exception {
public:
    const char* what() const noexcept override { return "RangeError: Maximum call stack size exceeded"; }
};

/// For engine code that recurses over something a script controls: call this
/// once per level. It measures the stack that is really left on this thread
/// (see StackLimit.hpp) instead of counting levels.
inline void js_check_stack() {
    if (stack_is_exhausted()) throw JsStackOverflow();
}

class Node;
class Element;
class Document;

enum class JsType {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Object,
    Function,
    NativeFunction,
    DomNode,
    Symbol
};

class JsEnvironment;
class AstNode;
class JsObject;
class PropertyMap;
class Heap;

/// Base of every garbage-collected cell.
class HeapObject {
public:
    bool is_marked{false};
    /// How many handles (see CellRef) point at this cell right now.
    uint32_t handle_count{0};
    /// Collector scratch: how many of those handles it found inside the heap.
    uint32_t gc_seen{0};
    /// The agent (page or worker) that created the cell; see Heap::current_agent.
    uint32_t agent{0};
    HeapObject* next_cell{nullptr};

    virtual ~HeapObject() = default;
    /// Reports every value this cell holds to the collector.
    virtual void trace(Heap& heap) = 0;
    /// Drops everything this cell holds. Called on garbage before it is freed.
    virtual void release_contents() = 0;
};

/// A counted reference to a heap cell.
///
/// The collector cannot look inside C++ locals, std::function captures or the
/// containers native code keeps values in. It does not have to: every JsValue
/// that points at a cell holds one of these, so each cell knows how many
/// references exist in total. During a collection the collector counts the
/// references it can see, the ones stored inside other cells. A cell with more
/// references than that is being held by something outside the heap, and is
/// treated as a root. Native code therefore never registers what it holds; a
/// JsValue that exists keeps its object alive.
///
/// It behaves like a `JsObject*`: it converts to one, compares like one and
/// can be assigned from one.
class CellRef {
public:
    CellRef() = default;
    CellRef(JsObject* cell) : m_cell(header(cell)) { retain(); }
    CellRef(const CellRef& other) : m_cell(other.m_cell) { retain(); }
    CellRef(CellRef&& other) noexcept : m_cell(other.m_cell) { other.m_cell = nullptr; }
    ~CellRef() { release(); }

    CellRef& operator=(const CellRef& other) {
        if (m_cell != other.m_cell) {
            release();
            m_cell = other.m_cell;
            retain();
        }
        return *this;
    }
    CellRef& operator=(CellRef&& other) noexcept {
        if (this != &other) {
            release();
            m_cell = other.m_cell;
            other.m_cell = nullptr;
        }
        return *this;
    }
    CellRef& operator=(JsObject* cell) {
        HeapObject* target = header(cell);
        if (m_cell != target) {
            release();
            m_cell = target;
            retain();
        }
        return *this;
    }

    // JsObject is only declared here. HeapObject is its first and only base,
    // so the two share an address; Heap.cpp checks that this holds.
    operator JsObject*() const { return reinterpret_cast<JsObject*>(m_cell); }
    JsObject* operator->() const { return reinterpret_cast<JsObject*>(m_cell); }
    JsObject* get() const { return reinterpret_cast<JsObject*>(m_cell); }

private:
    static HeapObject* header(JsObject* cell) { return reinterpret_cast<HeapObject*>(cell); }
    void retain() const { if (m_cell) ++m_cell->handle_count; }
    void release() const { if (m_cell) --m_cell->handle_count; }

    HeapObject* m_cell{nullptr};
};
struct BytecodeFunction;
class CanvasRenderingContext2D;
class CanvasGradient;

class JsValue {
public:
    using NativeFn = std::function<JsValue(const std::vector<JsValue>& args)>;

    JsType type{JsType::Undefined};
    double number_val{0.0};
    bool bool_val{false};
    /// The text of a string. For a symbol, the unique property key it stands
    /// for. A shared handle: copying the value does not copy the text.
    JsString string_val;

    // The heap cell this value refers to, if any. A counted handle: see CellRef.
    CellRef object_val;
    std::shared_ptr<PropertyMap> properties;

    // Cold, type-specific payload. Primitive values and ordinary objects don't
    // carry function/DOM/canvas/promise/generator members inline.
    struct FunctionData {
        std::vector<std::string> params;
        std::vector<std::shared_ptr<AstNode>> default_values;
        std::shared_ptr<AstNode> body;
        std::shared_ptr<JsEnvironment> closure;
        std::shared_ptr<BytecodeFunction> bytecode_fn;
        NativeFn native_fn;
        bool is_async{false};
        bool is_generator{false};

        // Functions created by the tree-walking interpreter. `node` is the
        // FunctionNode; class constructors also keep their ClassNode alive
        // because instance field initialisers live there.
        std::shared_ptr<const AstNode> node;
        std::shared_ptr<const AstNode> class_node;
        std::string name;
        int length{0};
        bool is_arrow{false};
        bool is_class_constructor{false};
        bool is_constructor{true};     // false for arrows, methods and async/generator functions

        /// Heap cell holding the function's own properties (`prototype`,
        /// statics, expandos). Created on first need: most closures never have
        /// a property set on them, and a cell is only reclaimed by the collector.
        mutable CellRef cell;
    };
    struct Extra {
        Extra() = default;
        Extra(const Extra&) = default;
        Extra& operator=(const Extra&) = default;
        /// Releases the payload without one destructor running inside the
        /// next along a chain of closures; see common/ReleaseQueue.hpp.
        ~Extra();

        /// Own properties of a function value that has no heap cell (native
        /// functions). Lives here, not in the value, so every copy of the
        /// function sees a property set through any other copy.
        std::shared_ptr<PropertyMap> props;
        using Payload = std::variant<std::monostate, FunctionData, NativeFn,
                                     std::shared_ptr<Node>,
                                     std::shared_ptr<CanvasRenderingContext2D>,
                                     std::shared_ptr<CanvasGradient>,
                                     std::shared_ptr<struct PromiseData>,
                                     std::shared_ptr<struct GeneratorData>>;
        Payload payload;
    };

    JsValue() : type(JsType::Undefined) {}

    /// The function payload, or null when this value is not a script function.
    const FunctionData* function_data() const;
    /// Creates the payload if needed. Call only while building a function value,
    /// before it is copied: the payload is copy-on-write.
    FunctionData& ensure_function_data() { return mutable_function_data(); }

    const std::vector<std::string>& function_params() const;
    const std::vector<std::shared_ptr<AstNode>>& function_default_values() const;
    const std::shared_ptr<AstNode>& function_body() const;
    const std::shared_ptr<JsEnvironment>& function_closure() const;
    const std::shared_ptr<BytecodeFunction>& function_bytecode() const;
    bool function_is_async() const;
    bool function_is_generator() const;
    const NativeFn& native_function() const;
    const std::shared_ptr<Node>& dom_node_value() const;
    const std::shared_ptr<CanvasRenderingContext2D>& canvas_context() const;
    const std::shared_ptr<CanvasGradient>& canvas_gradient_value() const;
    const std::shared_ptr<struct PromiseData>& promise_value() const;
    const std::shared_ptr<struct GeneratorData>& generator_value() const;

    void set_function_params(std::vector<std::string> value);
    void add_function_default_value(std::shared_ptr<AstNode> value);
    void set_function_default_values(std::vector<std::shared_ptr<AstNode>> value);
    void set_function_body(std::shared_ptr<AstNode> value);
    void set_function_closure(std::shared_ptr<JsEnvironment> value);
    void set_function_bytecode(std::shared_ptr<BytecodeFunction> value);
    void set_function_async(bool value);
    void set_function_generator(bool value);
    void set_native_function(NativeFn value);
    void set_dom_node_value(std::shared_ptr<Node> value);
    void set_canvas_context(std::shared_ptr<CanvasRenderingContext2D> value);
    void set_canvas_gradient_value(std::shared_ptr<CanvasGradient> value);
    void set_promise_value(std::shared_ptr<struct PromiseData> value);
    void set_generator_value(std::shared_ptr<struct GeneratorData> value);

    static JsValue make_undefined() { return JsValue(); }
    static JsValue make_null() {
        JsValue v;
        v.type = JsType::Null;
        return v;
    }
    static JsValue make_boolean(bool b) {
        JsValue v;
        v.type = JsType::Boolean;
        v.bool_val = b;
        return v;
    }
    static JsValue make_number(double n) {
        JsValue v;
        v.type = JsType::Number;
        v.number_val = n;
        return v;
    }
    /// A string holding a copy of `s` (UTF-8; anything malformed is repaired).
    static JsValue make_string(std::string_view s) {
        JsValue v;
        v.type = JsType::String;
        v.string_val = JsString(s);
        return v;
    }
    /// A string that shares `s`: nothing is copied.
    static JsValue make_string(JsString s) {
        JsValue v;
        v.type = JsType::String;
        v.string_val = std::move(s);
        return v;
    }
    static JsValue make_object(JsObject* obj = nullptr);
    static JsValue make_array(const std::vector<JsValue>& elements = {});
    static JsValue make_native_function(NativeFn fn) {
        JsValue v;
        v.type = JsType::NativeFunction;
        v.set_native_function(std::move(fn));
        return v;
    }
    static JsValue make_dom_node(std::shared_ptr<Node> node);
    /// A symbol. `key` is the unique property key it stands for; `description`
    /// is what `Symbol(...)` was called with.
    static JsValue make_symbol(std::string key, std::string description);

    bool is_truthy() const;
    bool is_array() const;
    bool is_object() const { return type == JsType::Object; }
    bool is_undefined() const { return type == JsType::Undefined; }
    bool is_function() const { return type == JsType::Function || type == JsType::NativeFunction; }
    bool is_callable() const { return is_function(); }
    bool is_symbol() const { return type == JsType::Symbol; }
    bool is_nullish() const { return type == JsType::Undefined || type == JsType::Null; }
    /// Objects, arrays, functions and DOM wrappers: anything with identity.
    bool is_object_like() const {
        return type == JsType::Object || type == JsType::Function ||
               type == JsType::NativeFunction || type == JsType::DomNode;
    }
    /// Address that is equal for two values exactly when they are the same
    /// object or function. Null for primitives.
    const void* identity() const;

    /// `name in value`: own property or anywhere on the prototype chain.
    bool has_property(const std::string& name) const;
    bool has_own_property(const std::string& name) const;
    /// Returns false when the property exists and cannot be deleted.
    bool delete_property(const std::string& name);
    /// Own property keys in ECMAScript order: integer keys ascending, then the
    /// rest in insertion order. Symbol keys are never included.
    std::vector<std::string> own_keys(bool include_non_enumerable = false) const;
    /// Defines a data property with explicit PropertyMap flags.
    void define_property(const std::string& name, JsValue value, uint8_t flags);
    /// Defines a getter/setter pair. Either may be undefined.
    void define_accessor(const std::string& name, const JsValue& getter, const JsValue& setter, uint8_t flags);
    /// The [[Prototype]] of an object-like value, or null.
    JsObject* prototype_object() const;
    /// Human-oriented rendering used by console.log.
    std::string inspect() const;
    std::vector<JsValue>& array_elements();
    const std::vector<JsValue>& array_elements() const;
    std::string to_string() const;
    double to_number() const;

    using FunctionCaller = std::function<JsValue(const JsValue& fn, const std::vector<JsValue>& args, const JsValue* this_val)>;
    static void set_function_caller(FunctionCaller caller);

    JsValue call(const std::vector<JsValue>& args = {}) const;
    JsValue call_with_this(const JsValue& this_val, const std::vector<JsValue>& args = {}) const;
    /// Invokes the function as a constructor (`new f(...)`). `this_val` is the
    /// freshly created instance for script functions; natives build their own.
    JsValue construct(const std::vector<JsValue>& args, const JsValue* this_val = nullptr) const;
    /// `super(...args)` in a class whose parent is a native constructor.
    /// `derived_this` is the object the `new` expression created, with the
    /// subclass's prototype. A native that can turn an existing object into
    /// what it constructs (see current_derived_this) does so, and then the
    /// subclass instance and the native object are one; the others build
    /// their own and the caller takes over its state.
    JsValue construct_as_super(const std::vector<JsValue>& args, const JsValue& derived_this) const;

    // --- Call context ---------------------------------------------------------
    //
    // Native functions have the signature `JsValue(const std::vector<JsValue>&)`,
    // which has no room for `this`. The receiver travels in a thread-local slot
    // that call_with_this()/construct() set for the duration of the call.
    /// `this` for the native function currently executing.
    static const JsValue& current_this();
    /// True when the native function currently executing was invoked with `new`.
    static bool current_is_construct();
    /// For a native constructor reached through `super()`: the subclass's
    /// object, which should become the thing constructed. Null otherwise.
    static const JsValue* current_derived_this();
    /// For script functions: consumes the pending call context. Returns false
    /// (and leaves `this_out` undefined) when the function was entered without
    /// going through call_with_this()/construct().
    static bool take_call_context(JsValue& this_out, bool& is_construct_out, JsValue* callee_out = nullptr);

    JsValue get_property(const std::string& name) const;
    void set_property(const std::string& name, JsValue val);

    /// The heap cell that stores this value's own properties: `object_val` for
    /// objects, the lazily created cell for script functions. Null when there
    /// is none and `create` is false.
    JsObject* property_cell(bool create = false) const;
    /// The property table of a value that has no cell: native functions and
    /// the few legacy values that carry `properties` directly.
    PropertyMap* loose_properties(bool create = false) const;

    // --- For the collector ------------------------------------------------------
    //
    // Copies of a function, promise or DOM value share one payload. The
    // collector compares how many copies exist with how many it can find.
    const void* payload_identity() const { return m_extra.get(); }
    long payload_owners() const { return m_extra.use_count(); }
    /// The payload's own property table when this payload is its only owner.
    const PropertyMap* payload_properties() const {
        return (m_extra && m_extra->props && m_extra->props.use_count() == 1) ? m_extra->props.get() : nullptr;
    }

private:
    std::shared_ptr<Extra> m_extra;
    Extra& mutable_extra();
    FunctionData& mutable_function_data();
    const Extra* extra() const { return m_extra.get(); }
};

/// An insertion-ordered property table.
///
/// ECMAScript requires property enumeration to follow insertion order, which a
/// hash map cannot provide; `Object.keys`, `for...in` and `JSON.stringify` all
/// depend on it. Small tables are scanned linearly; larger ones keep an index.
class PropertyMap {
public:
    enum Flag : uint8_t {
        Enumerable = 1,
        Writable = 2,
        Configurable = 4,
        Accessor = 8,       // `second` is the getter, `setter` the setter
        Default = Enumerable | Writable | Configurable,
        Hidden = Writable | Configurable
    };

    struct Entry {
        std::string first;                  // key
        JsValue second;                     // value, or the getter for accessors
        std::shared_ptr<JsValue> setter;    // accessors only
        uint8_t flags{Default};

        // Lets `for (const auto& [key, value] : map)` keep working: an entry
        // destructures as its key and value.
        template <std::size_t I> auto& get() & {
            if constexpr (I == 0) return first; else return second;
        }
        template <std::size_t I> const auto& get() const& {
            if constexpr (I == 0) return first; else return second;
        }
    };

    using iterator = std::vector<Entry>::iterator;
    using const_iterator = std::vector<Entry>::const_iterator;

    iterator begin() { return m_entries.begin(); }
    iterator end() { return m_entries.end(); }
    const_iterator begin() const { return m_entries.begin(); }
    const_iterator end() const { return m_entries.end(); }

    iterator find(const std::string& key);
    const_iterator find(const std::string& key) const;
    /// Returns the value slot for `key`, inserting a default data property.
    JsValue& operator[](const std::string& key);
    /// Returns the entry for `key`, inserting a default data property.
    Entry& entry(const std::string& key);
    bool erase(const std::string& key);
    size_t count(const std::string& key) const { return find(key) == end() ? 0 : 1; }
    size_t size() const { return m_entries.size(); }
    bool empty() const { return m_entries.empty(); }
    void clear();

private:
    static constexpr size_t kIndexThreshold = 12;
    long index_of(const std::string& key) const;
    void rebuild_index();

    std::vector<Entry> m_entries;
    std::unordered_map<std::string, uint32_t> m_index;   // populated above kIndexThreshold
};

/// Upper bound on a walk up a prototype chain. Chains cannot be circular (see
/// js_set_prototype), so this is never reached by a correct engine; it only
/// keeps a bug from becoming an endless loop. It is far beyond what fits in
/// memory, so it does not cut real chains short the way the old limit of
/// 10,000 did (lookups past it silently answered "not found").
inline constexpr int kMaxPrototypeChain = 1 << 30;

/// Sets the prototype of `cell`. Returns false, changing nothing, when that
/// would make the prototype chain circular. Every walk up a chain relies on
/// this: chains are finite because nothing else lets script close a loop.
bool js_set_prototype(JsObject* cell, JsObject* proto);

/// The source text of a script function or class (a FunctionNode or ClassNode),
/// or an empty string when the node has none. Defined in JsParser.cpp.
std::string js_function_source_text(const AstNode* node);

/// ECMAScript `===` (Strict Equality Comparison, 7.2.16).
///
/// Unlike `==`, this never coerces: values of different types are simply not
/// equal, and objects compare by identity. The old engine had no strict
/// comparison at all -- the lexer folded `===` into `==`, so `1 === "1"`
/// evaluated true and bugs like it were invisible.
bool strict_equals(const JsValue& a, const JsValue& b);

} // namespace litebird

namespace std {
template <> struct tuple_size<litebird::PropertyMap::Entry> : integral_constant<size_t, 2> {};
template <> struct tuple_element<0, litebird::PropertyMap::Entry> { using type = std::string; };
template <> struct tuple_element<1, litebird::PropertyMap::Entry> { using type = litebird::JsValue; };
} // namespace std
