#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsParser.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/BytecodeCompiler.hpp"
#include "litebird/js/VM.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/common/StackLimit.hpp"
#include <atomic>
#include <unordered_map>
#include <memory>
#include <string>

namespace litebird {

/// One lexical scope: a function activation, a block, or the global scope.
///
/// Bindings live in a small vector because most scopes hold a handful of
/// names; the global scope, which holds a hundred or more, gets an index.
class JsEnvironment : public std::enable_shared_from_this<JsEnvironment> {
public:
    struct Binding {
        std::string name;
        JsValue value;
        bool is_const{false};
        /// A non-writable property of the global object (`undefined`, `NaN`,
        /// `Infinity`): assigning to it does nothing in sloppy code and throws
        /// in strict code, and it cannot be redeclared.
        bool is_readonly{false};
    };

    enum class AssignResult { Ok, NotFound, Const, ReadOnly };

    /// True once any scope other than a global one has declared a binding
    /// named `undefined`. Until then every use of that name means the global,
    /// whose value cannot change, so the evaluator skips the lookup.
    static bool undefined_shadowed() { return s_undefined_shadowed.load(std::memory_order_relaxed); }

    explicit JsEnvironment(std::shared_ptr<JsEnvironment> parent = nullptr)
        : m_parent(std::move(parent)), m_is_var_scope(!m_parent), m_agent(Heap::current_agent()) {
        register_scope();
    }
    ~JsEnvironment() {
        unregister_scope();
        // A scope owns its parent. Let go of a run of parents that only this
        // chain keeps alive in a loop, not by one destructor calling the next.
        std::shared_ptr<JsEnvironment> parent = std::move(m_parent);
        while (parent && parent.use_count() == 1) {
            std::shared_ptr<JsEnvironment> next = std::move(parent->m_parent);
            parent = std::move(next);       // the old parent dies here, already detached
        }
    }
    JsEnvironment(const JsEnvironment&) = delete;
    JsEnvironment& operator=(const JsEnvironment&) = delete;

    /// Every scope of `agent` that exists right now, each kept alive by the
    /// returned pointer. The collector starts from this list: a scope can be
    /// garbage (a closure and its scope referring to each other) without any
    /// object pointing at it.
    static void snapshot(uint32_t agent, std::vector<std::shared_ptr<void>>& out) {
        RegistryLock lock;
        out.reserve(out.size() + s_registry_size);
        for (JsEnvironment* scope = s_registry_head; scope; scope = scope->m_registry_next) {
            if (scope->m_agent != agent) continue;
            // A scope whose last owner has just let go is skipped.
            if (auto owner = scope->weak_from_this().lock()) out.push_back(std::move(owner));
        }
    }

    /// Drops every binding. Used by the collector on scopes nothing can reach.
    void clear_bindings() {
        std::vector<Binding> dropped = std::move(m_bindings);
        m_bindings.clear();
        m_index.clear();
    }

    /// Declares `name` in this scope, replacing an existing declaration.
    void define(const std::string& name, JsValue value) {
        Binding* existing = find_own(name);
        if (existing) {
            if (existing->is_readonly) return;
            existing->value = std::move(value);
            existing->is_const = false;
            return;
        }
        add(name, std::move(value), false);
    }

    void define_const(const std::string& name, JsValue value) {
        Binding* existing = find_own(name);
        if (existing) {
            if (existing->is_readonly) return;
            existing->value = std::move(value);
            existing->is_const = true;
            return;
        }
        add(name, std::move(value), true);
    }

    /// Declares a non-writable global such as `undefined`.
    void define_readonly(const std::string& name, JsValue value) {
        define(name, std::move(value));
        if (Binding* b = find_own(name)) b->is_readonly = true;
    }

    Binding* find_own(const std::string& name) {
        if (m_bindings.size() > kIndexThreshold) {
            auto it = m_index.find(name);
            return it == m_index.end() ? nullptr : &m_bindings[it->second];
        }
        for (auto& b : m_bindings) {
            if (b.name == name) return &b;
        }
        return nullptr;
    }

    bool has_own(const std::string& name) const {
        return const_cast<JsEnvironment*>(this)->find_own(name) != nullptr;
    }

    /// Finds `name` in this scope or an enclosing one.
    Binding* lookup(const std::string& name) {
        for (JsEnvironment* env = this; env; env = env->m_parent.get()) {
            if (Binding* b = env->find_own(name)) return b;
        }
        return nullptr;
    }

    /// Assigns to an existing binding without ever creating one.
    AssignResult try_assign(const std::string& name, const JsValue& value) {
        Binding* b = lookup(name);
        if (!b) return AssignResult::NotFound;
        if (b->is_const) return AssignResult::Const;
        if (b->is_readonly) return AssignResult::ReadOnly;
        b->value = value;
        return AssignResult::Ok;
    }

    void mark_var_scope() { m_is_var_scope = true; }
    std::shared_ptr<JsEnvironment> var_scope() {
        auto scope = shared_from_this();
        while (scope->m_parent && !scope->m_is_var_scope) scope = scope->m_parent;
        return scope;
    }
    void hoist_var(const std::string& name) {
        auto scope = var_scope();
        if (!scope->has_own(name)) scope->define(name, JsValue::make_undefined());
    }

    /// Legacy assignment: creates the binding in the outermost scope when the
    /// name is not declared anywhere (sloppy-mode implicit global).
    bool assign(const std::string& name, JsValue value) {
        if (Binding* b = lookup(name)) {
            if (!b->is_readonly) b->value = std::move(value);
            return true;
        }
        JsEnvironment* root = this;
        while (root->m_parent) root = root->m_parent.get();
        root->define(name, std::move(value));
        return true;
    }

    /// Legacy read: undefined when the name is not declared.
    JsValue get(const std::string& name) const {
        if (const Binding* b = const_cast<JsEnvironment*>(this)->lookup(name)) return b->value;
        return JsValue::make_undefined();
    }

    std::shared_ptr<JsEnvironment> parent() const { return m_parent; }
    JsEnvironment* parent_raw() const { return m_parent.get(); }
    std::vector<Binding>& bindings() { return m_bindings; }
    const std::vector<Binding>& bindings() const { return m_bindings; }

    void collect_roots(std::vector<JsValue*>& roots) {
        for (auto& b : m_bindings) {
            roots.push_back(&b.value);
        }
        if (m_parent) {
            m_parent->collect_roots(roots);
        }
    }

    /// The collection in which this scope was last traced; stops the collector
    /// from walking a scope shared by many closures more than once.
    uint64_t gc_epoch{0};

    /// Whether code running directly in this scope is strict: 1 strict,
    /// 0 sloppy, -1 inherit from the enclosing scope. Set on function, class
    /// and program scopes.
    int8_t strict_mode{-1};

    bool is_strict() const {
        for (const JsEnvironment* env = this; env; env = env->m_parent.get()) {
            if (env->strict_mode >= 0) return env->strict_mode == 1;
        }
        return false;
    }

private:
    static constexpr size_t kIndexThreshold = 16;

    void add(const std::string& name, JsValue value, bool is_const) {
        if (m_parent && name.size() == 9 && name == "undefined") {
            s_undefined_shadowed.store(true, std::memory_order_relaxed);
        }
        m_bindings.push_back(Binding{name, std::move(value), is_const, false});
        if (m_bindings.size() == kIndexThreshold + 1) {
            for (size_t i = 0; i < m_bindings.size(); ++i) m_index[m_bindings[i].name] = static_cast<uint32_t>(i);
        } else if (m_bindings.size() > kIndexThreshold + 1) {
            m_index[name] = static_cast<uint32_t>(m_bindings.size() - 1);
        }
    }

    // All live scopes, as an intrusive list. Scopes are created and destroyed
    // on every call, so the lock is a spin lock: it is held for a few
    // instructions and only ever contended by a worker thread.
    struct RegistryLock {
        RegistryLock() { while (s_registry_lock.test_and_set(std::memory_order_acquire)) {} }
        ~RegistryLock() { s_registry_lock.clear(std::memory_order_release); }
    };
    void register_scope() {
        RegistryLock lock;
        m_registry_next = s_registry_head;
        if (s_registry_head) s_registry_head->m_registry_prev = this;
        s_registry_head = this;
        ++s_registry_size;
    }
    void unregister_scope() {
        RegistryLock lock;
        if (m_registry_prev) m_registry_prev->m_registry_next = m_registry_next;
        else s_registry_head = m_registry_next;
        if (m_registry_next) m_registry_next->m_registry_prev = m_registry_prev;
        --s_registry_size;
    }
    static inline std::atomic_flag s_registry_lock = ATOMIC_FLAG_INIT;
    static inline JsEnvironment* s_registry_head = nullptr;
    static inline size_t s_registry_size = 0;
    static inline std::atomic<bool> s_undefined_shadowed{false};

    std::shared_ptr<JsEnvironment> m_parent;
    bool m_is_var_scope{false};
    uint32_t m_agent{0};
    JsEnvironment* m_registry_prev{nullptr};
    JsEnvironment* m_registry_next{nullptr};
    std::vector<Binding> m_bindings;
    std::unordered_map<std::string, uint32_t> m_index;
};

/// Kept for source compatibility. The interpreter no longer throws this:
/// `return` is a completion value, not a C++ exception.
class ReturnException {
public:
    JsValue value;
    explicit ReturnException(JsValue v) : value(std::move(v)) {}
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

    /// Runs a script. A syntax error or an uncaught exception is reported on
    /// stderr as "Uncaught ..." and the result is undefined; see
    /// last_eval_failed(). The script never runs twice and never runs
    /// partially past a syntax error.
    JsValue eval(std::string_view source);
    /// Same as eval(). Kept because callers and tests use both names.
    JsValue eval_ast(std::string_view source);
    /// Experimental: compiles to bytecode and runs it on the VM. Throws if the
    /// compiler does not support a construct. Not used by eval().
    JsValue eval_bytecode(std::string_view source);
    std::shared_ptr<BytecodeFunction> compile_to_bytecode(std::string_view source);

    /// Runs a script and lets exceptions propagate to the caller as JsThrow.
    JsValue run(std::string_view source);

    JsValue execute(const AstNode* node, std::shared_ptr<JsEnvironment> env);

    std::shared_ptr<JsEnvironment> global_env() const { return m_global_env; }
    const JsValue& global_object() const { return m_global_object; }
    VM& vm() { return m_vm; }
    void set_document(std::shared_ptr<Document> doc);

    std::shared_ptr<class History> history() const { return m_history; }

    /// True when the most recent eval()/eval_ast() ended with a syntax error or
    /// an uncaught exception.
    bool last_eval_failed() const { return m_last_eval_failed; }

    /// Tells the page it has finished loading. The host calls this once, after
    /// the document's scripts have run: document.readyState becomes
    /// "interactive" and DOMContentLoaded fires (at the document, then the
    /// window), then readyState becomes "complete" and `load` fires at the
    /// window. Later calls do nothing.
    void notify_document_loaded();

    /// The page's markup has all been parsed and its scripts have run:
    /// `document.readyState` becomes "interactive". What comes next is
    /// notify_document_loaded(). Deferred scripts run between the two.
    void notify_document_interactive();

private:
    using EnvPtr = std::shared_ptr<JsEnvironment>;

    /// How a statement finished.
    enum class Flow : uint8_t { Normal, Break, Continue, Return };

    /// Per-activation state for statements: one per function call or script.
    struct Frame {
        JsValue return_value;
        const std::string* label{nullptr};   // target of a labelled break/continue in flight
        JsValue completion;                  // value of the last expression statement
        bool track_completion{false};
    };

    /// An evaluated assignment target: a variable or `base[key]`.
    struct Reference {
        bool is_property{false};
        std::string name;
        JsValue base;
    };

    enum class BindMode { Var, Let, Const, Assign };

    void init_builtins();
    void init_language_builtins();

    /// RAII call-depth guard. Throws JsStackOverflow once the native stack is
    /// within kStackReserveBytes of its budget, and always restores the counter,
    /// including on the unwinding path.
    ///
    /// The depth is counted per thread, because that is what a native stack is:
    /// a generator or async function parked on its own thread must not use up
    /// the budget of the thread that keeps running.
    class CallDepthGuard {
    public:
        explicit CallDepthGuard(JsInterpreter&) {
            if (++depth() > kMaxCallDepth || stack_is_exhausted()) {
                --depth();
                throw JsStackOverflow();
            }
        }
        ~CallDepthGuard() { --depth(); }
        CallDepthGuard(const CallDepthGuard&) = delete;
        CallDepthGuard& operator=(const CallDepthGuard&) = delete;
    private:
        static int& depth() {
            static thread_local int t_depth = 0;
            return t_depth;
        }
    };

    // --- Programs and functions -------------------------------------------
    JsValue run_program(const Program& program);
    JsValue make_function(const std::shared_ptr<FunctionNode>& node, const EnvPtr& env,
                          const std::string& name_hint = std::string());
    JsValue invoke_function(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& closure,
                            const JsValue& callee, const JsValue& this_val, bool is_construct,
                            const std::vector<JsValue>& args);
    JsValue run_function_body(const FunctionNode& node, const EnvPtr& env);
    JsValue run_async_body(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& env);
    JsValue make_generator(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& env);
    std::shared_ptr<struct GeneratorData> spawn_frame(const std::shared_ptr<const FunctionNode>& node,
                                                      const EnvPtr& env, bool is_coroutine);
    void bind_parameters(const FunctionNode& node, const EnvPtr& env, const std::vector<JsValue>& args);
    void hoist_functions(const std::vector<const FunctionDeclStmt*>& functions, const EnvPtr& env);
    JsValue define_class(const std::shared_ptr<ClassNode>& cls, const EnvPtr& env);
    void initialize_fields(const JsValue& ctor, const JsValue& instance);
    JsValue call_legacy_function(const JsValue& fn, const std::vector<JsValue>& args, const JsValue* this_val);

    // --- Statements ---------------------------------------------------------
    Flow exec(const Stmt* stmt, const EnvPtr& env, Frame& frame);
    Flow exec_list(const std::vector<std::unique_ptr<Stmt>>& list, const EnvPtr& env, Frame& frame);
    Flow exec_block(const BlockStmt* block, const EnvPtr& env, Frame& frame);
    Flow exec_labeled(const Stmt* stmt, const EnvPtr& env, Frame& frame,
                      std::vector<const std::string*>& labels);
    Flow exec_for(const ForStmt* stmt, const EnvPtr& env, Frame& frame,
                  const std::vector<const std::string*>& labels);
    Flow exec_for_each(const ForEachStmt* stmt, const EnvPtr& env, Frame& frame,
                       const std::vector<const std::string*>& labels);
    Flow exec_while(const WhileStmt* stmt, const EnvPtr& env, Frame& frame,
                    const std::vector<const std::string*>& labels);
    Flow exec_do_while(const DoWhileStmt* stmt, const EnvPtr& env, Frame& frame,
                       const std::vector<const std::string*>& labels);
    Flow exec_try(const TryStmt* stmt, const EnvPtr& env, Frame& frame);
    Flow exec_switch(const SwitchStmt* stmt, const EnvPtr& env, Frame& frame);
    void exec_var_decl(const VarDeclStmt* stmt, const EnvPtr& env);

    // --- Expressions --------------------------------------------------------
    JsValue evaluate(const Expr* expr, const EnvPtr& env);
    JsValue evaluate_named(const Expr* expr, const EnvPtr& env, const std::string& name_hint);
    JsValue eval_binary(const BinaryExpr* expr, const EnvPtr& env);
    JsValue eval_binary_chain(const BinaryExpr* expr, const EnvPtr& env);
    JsValue combine_binary(const BinaryExpr* expr, JsValue left, const EnvPtr& env);
    JsValue apply_binary(JsTokenType op, const JsValue& left, const JsValue& right);
    JsValue eval_unary(const UnaryExpr* expr, const EnvPtr& env);
    JsValue eval_assign(const AssignExpr* expr, const EnvPtr& env);
    JsValue eval_call(const CallExpr* expr, const EnvPtr& env);
    JsValue eval_new(const NewExpr* expr, const EnvPtr& env);
    JsValue eval_object_literal(const ObjectLiteralExpr* expr, const EnvPtr& env);
    JsValue eval_super_call(const SuperCallExpr* expr, const EnvPtr& env);
    JsValue eval_yield(const YieldExpr* expr, const EnvPtr& env);
    JsValue eval_await(const JsValue& value);
    JsValue eval_tagged_template(const TaggedTemplateExpr* expr, const EnvPtr& env);
    std::vector<JsValue> eval_arguments(const std::vector<std::unique_ptr<Expr>>& args, const EnvPtr& env);

    // --- Variables and properties -------------------------------------------
    JsValue lookup_identifier(const std::string& name, const EnvPtr& env);
    void assign_identifier(const std::string& name, const JsValue& value, const EnvPtr& env);
    JsValue get_member(const JsValue& base, const std::string& key);
    void set_member(const JsValue& base, const std::string& key, const JsValue& value);
    Reference resolve_reference(const Expr* target, const EnvPtr& env);
    JsValue read_reference(const Reference& ref, const EnvPtr& env);
    void write_reference(const Reference& ref, const JsValue& value, const EnvPtr& env);
    void bind_pattern(const Pattern& pattern, const JsValue& value, const EnvPtr& env, BindMode mode);
    JsValue super_base(const EnvPtr& env);

    void fire_window_event(const std::string& type);

    bool m_last_eval_failed{false};
    bool m_chain_short_circuit{false};
    bool m_load_notified{false};
    using WindowListeners = std::unordered_map<std::string, std::vector<JsValue>>;
    std::shared_ptr<WindowListeners> m_window_listeners;

    /// This interpreter's built-ins. Declared first: everything below creates
    /// objects that need its prototypes.
    std::unique_ptr<JsIntrinsics> m_realm;
    /// Cleared by the destructor. Every function value holds a copy, so a
    /// callback that outlives the interpreter (a timer still queued in the
    /// process-wide event loop) becomes a no-op instead of a dangling call.
    std::shared_ptr<std::atomic<bool>> m_alive;

    std::shared_ptr<JsEnvironment> m_global_env;
    JsValue m_global_object;
    std::shared_ptr<Document> m_document;
    std::shared_ptr<class History> m_history;
    std::vector<std::weak_ptr<struct GeneratorData>> m_generators;
    VM m_vm;
};

} // namespace litebird
