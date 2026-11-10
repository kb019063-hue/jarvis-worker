#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsParser.hpp"
#include "litebird/js/BytecodeCompiler.hpp"
#include "litebird/js/VM.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/common/StackLimit.hpp"
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

/// Thrown when a script recurses deeper than the engine can represent on the
/// native stack. Every JS call consumes C++ frames in the AST interpreter
/// (`eval_expr` -> call -> `execute` -> `exec_stmt`), so without a limit an
/// ordinary recursive function overflows the stack and the process dies with no
/// error at all. Reported to script as a caught-able RangeError.
class JsStackOverflow : public std::exception {
public:
    const char* what() const noexcept override { return "RangeError: Maximum call stack size exceeded"; }
};

class JsInterpreter {
public:
    explicit JsInterpreter(std::shared_ptr<Document> doc = nullptr);
    ~JsInterpreter();

    JsInterpreter(const JsInterpreter&) = delete;
    JsInterpreter& operator=(const JsInterpreter&) = delete;

    /// JS call-depth ceiling, used only where native stack bounds cannot be
    /// queried (see StackLimit.hpp). A JS call costs several native frames in
    /// the AST interpreter and the cost per frame swings ~12x between an -O2
    /// and an AddressSanitizer build, so depth is bounded by the remaining
    /// stack and this value is just a backstop.
    static constexpr int kMaxCallDepth = 20000;

    JsValue eval(std::string_view source);
    JsValue eval_ast(std::string_view source);
    std::shared_ptr<BytecodeFunction> compile_to_bytecode(std::string_view source);

    JsValue execute(const AstNode* node, std::shared_ptr<JsEnvironment> env);

    std::shared_ptr<JsEnvironment> global_env() const { return m_global_env; }
    VM& vm() { return m_vm; }
    void set_document(std::shared_ptr<Document> doc);

    std::shared_ptr<class History> history() const { return m_history; }

private:
    void init_builtins();

    /// RAII call-depth guard. Throws JsStackOverflow once the native stack is
    /// within kStackReserveBytes of its budget, and always restores the counter,
    /// including on the unwinding path.
    class CallDepthGuard {
    public:
        explicit CallDepthGuard(JsInterpreter& interp) : m_interp(interp) {
            if (++m_interp.m_call_depth > kMaxCallDepth || stack_is_exhausted()) {
                --m_interp.m_call_depth;
                throw JsStackOverflow();
            }
        }
        ~CallDepthGuard() { --m_interp.m_call_depth; }
        CallDepthGuard(const CallDepthGuard&) = delete;
        CallDepthGuard& operator=(const CallDepthGuard&) = delete;
    private:
        JsInterpreter& m_interp;
    };

    int m_call_depth{0};

    JsValue eval_expr(const Expr* expr, std::shared_ptr<JsEnvironment> env);

    // Shared lvalue machinery for plain assignment, compound assignment and
    // ++/--. Keeping one implementation means `obj.x += 1`, `obj.x++` and
    // `obj.x = obj.x + 1` cannot drift apart.
    JsValue read_lvalue(const Expr* target, const std::shared_ptr<JsEnvironment>& env);
    void write_lvalue(const Expr* target, const std::shared_ptr<JsEnvironment>& env,
                      const JsValue& value);
    JsValue apply_compound(JsTokenType op, const JsValue& left, const JsValue& right);
    JsValue exec_stmt(const Stmt* stmt, std::shared_ptr<JsEnvironment> env);

    std::shared_ptr<JsEnvironment> m_global_env;
    std::shared_ptr<Document> m_document;
    std::shared_ptr<class History> m_history;
    std::shared_ptr<JsEnvironment> m_current_env;
    VM m_vm;
};

} // namespace litebird
