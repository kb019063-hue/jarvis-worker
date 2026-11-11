#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <iostream>
#include <variant>
#include <tuple>
#include <cstdint>

namespace litebird {

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
struct BytecodeFunction;
class CanvasRenderingContext2D;
class CanvasGradient;

class JsValue {
public:
    using NativeFn = std::function<JsValue(const std::vector<JsValue>& args)>;

    JsType type{JsType::Undefined};
    double number_val{0.0};
    bool bool_val{false};
    std::string string_val;

    // For Garbage-Collected Objects
    JsObject* object_val{nullptr};
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
        mutable JsObject* cell{nullptr};
    };
    struct Extra {
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
    static JsValue make_string(std::string s) {
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

    // --- Call context ---------------------------------------------------------
    //
    // Native functions have the signature `JsValue(const std::vector<JsValue>&)`,
    // which has no room for `this`. The receiver travels in a thread-local slot
    // that call_with_this()/construct() set for the duration of the call.
    /// `this` for the native function currently executing.
    static const JsValue& current_this();
    /// True when the native function currently executing was invoked with `new`.
    static bool current_is_construct();
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
