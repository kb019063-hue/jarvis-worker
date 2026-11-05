#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/js/JsParser.hpp"
#include "litebird/js/BytecodeCompiler.hpp"
#include "litebird/js/VM.hpp"
#include "litebird/dom/Document.hpp"
#include <unordered_map>
#include <memory>
#include <string>

namespace litebird {

class JsEnvironment : public std::enable_shared_from_this<JsEnvironment> {
public:
    explicit JsEnvironment(std::shared_ptr<JsEnvironment> parent = nullptr)
        : m_parent(std::move(parent)) {}

    void define(const std::string& name, JsValue value) {
        m_values[name] = std::move(value);
    }

    bool assign(const std::string& name, JsValue value) {
        auto it = m_values.find(name);
        if (it != m_values.end()) {
            it->second = std::move(value);
            return true;
        }
        if (m_parent) {
            return m_parent->assign(name, std::move(value));
        }
        // Implicit global definition if not found
        m_values[name] = std::move(value);
        return true;
    }

    JsValue get(const std::string& name) const {
        auto it = m_values.find(name);
        if (it != m_values.end()) {
            return it->second;
        }
        if (m_parent) {
            return m_parent->get(name);
        }
        return JsValue::make_undefined();
    }

    std::shared_ptr<JsEnvironment> parent() const { return m_parent; }
    const std::unordered_map<std::string, JsValue>& values() const { return m_values; }

    void collect_roots(std::vector<JsValue*>& roots) {
        for (auto& [k, v] : m_values) {
            roots.push_back(&v);
        }
        if (m_parent) {
            m_parent->collect_roots(roots);
        }
    }

private:
    std::shared_ptr<JsEnvironment> m_parent;
    std::unordered_map<std::string, JsValue> m_values;
};

class ReturnException {
public:
    JsValue value;
    explicit ReturnException(JsValue v) : value(std::move(v)) {}
};

class JsInterpreter {
public:
    explicit JsInterpreter(std::shared_ptr<Document> doc = nullptr);

    JsValue eval(std::string_view source);
    JsValue eval_ast(std::string_view source);
    std::shared_ptr<BytecodeFunction> compile_to_bytecode(std::string_view source);

    JsValue execute(const AstNode* node, std::shared_ptr<JsEnvironment> env);

    std::shared_ptr<JsEnvironment> global_env() const { return m_global_env; }
    VM& vm() { return m_vm; }
    void set_document(std::shared_ptr<Document> doc);

private:
    void init_builtins();

    JsValue eval_expr(const Expr* expr, std::shared_ptr<JsEnvironment> env);
    JsValue exec_stmt(const Stmt* stmt, std::shared_ptr<JsEnvironment> env);

    std::shared_ptr<JsEnvironment> m_global_env;
    std::shared_ptr<Document> m_document;
    std::shared_ptr<JsEnvironment> m_current_env;
    VM m_vm;
};

} // namespace litebird
