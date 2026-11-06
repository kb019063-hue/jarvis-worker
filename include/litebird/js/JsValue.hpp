#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <iostream>

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

    // For Functions
    std::vector<std::string> params;
    std::vector<std::shared_ptr<AstNode>> default_values;
    std::shared_ptr<AstNode> body;
    std::shared_ptr<JsEnvironment> closure;
    std::shared_ptr<BytecodeFunction> bytecode_fn;

    // For Native Functions
    NativeFn native_fn;

    // For DOM Nodes
    std::shared_ptr<Node> dom_node;

    // For HTML5 Canvas 2D Context
    std::shared_ptr<CanvasRenderingContext2D> canvas_ctx;

    JsValue() : type(JsType::Undefined) {}

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
        v.native_fn = std::move(fn);
        return v;
    }
    static JsValue make_dom_node(std::shared_ptr<Node> node);

    bool is_truthy() const;
    bool is_array() const;
    std::string to_string() const;
    double to_number() const;

    using FunctionCaller = std::function<JsValue(const JsValue& fn, const std::vector<JsValue>& args)>;
    static void set_function_caller(FunctionCaller caller);

    JsValue call(const std::vector<JsValue>& args = {}) const;

    JsValue get_property(const std::string& name) const;
    void set_property(const std::string& name, JsValue val);
};

} // namespace litebird
