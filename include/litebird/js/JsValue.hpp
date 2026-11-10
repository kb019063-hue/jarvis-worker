#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <iostream>
#include <variant>

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
    DomNode
};

class JsEnvironment;
class AstNode;
class JsObject;
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
    std::shared_ptr<std::unordered_map<std::string, JsValue>> properties;

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
    };
    struct Extra {
        using Payload = std::variant<std::monostate, FunctionData, NativeFn,
                                     std::shared_ptr<Node>,
                                     std::shared_ptr<CanvasRenderingContext2D>,
                                     std::shared_ptr<CanvasGradient>,
                                     std::shared_ptr<struct PromiseData>,
                                     std::shared_ptr<struct GeneratorData>>;
        Payload payload;
    };

    JsValue() : type(JsType::Undefined) {}

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

    bool is_truthy() const;
    bool is_array() const;
    bool is_object() const { return type == JsType::Object; }
    bool is_undefined() const { return type == JsType::Undefined; }
    bool is_function() const { return type == JsType::Function || type == JsType::NativeFunction; }
    bool is_callable() const { return is_function(); }
    bool has_property(const std::string& name) const;
    std::vector<JsValue>& array_elements();
    const std::vector<JsValue>& array_elements() const;
    std::string to_string() const;
    double to_number() const;

    using FunctionCaller = std::function<JsValue(const JsValue& fn, const std::vector<JsValue>& args, const JsValue* this_val)>;
    static void set_function_caller(FunctionCaller caller);

    JsValue call(const std::vector<JsValue>& args = {}) const;
    JsValue call_with_this(const JsValue& this_val, const std::vector<JsValue>& args = {}) const;

    JsValue get_property(const std::string& name) const;
    void set_property(const std::string& name, JsValue val);

private:
    std::shared_ptr<Extra> m_extra;
    Extra& mutable_extra();
    FunctionData& mutable_function_data();
    const Extra* extra() const { return m_extra.get(); }
};

/// ECMAScript `===` (Strict Equality Comparison, 7.2.16).
///
/// Unlike `==`, this never coerces: values of different types are simply not
/// equal, and objects compare by identity. The old engine had no strict
/// comparison at all -- the lexer folded `===` into `==`, so `1 === "1"`
/// evaluated true and bugs like it were invisible.
bool strict_equals(const JsValue& a, const JsValue& b);

} // namespace litebird
