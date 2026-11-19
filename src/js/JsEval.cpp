// The tree-walking evaluator: statements, functions, classes and expressions.
//
// Design notes:
//  * Dispatch is a switch on AstNode::kind, not a chain of dynamic_casts.
//  * `return`, `break` and `continue` are completion values (Flow), not C++
//    exceptions. Only a JavaScript `throw` unwinds the C++ stack, as JsThrow.
//  * Every construct is evaluated here and nowhere else. There is no second
//    engine to fall back to, so a script can never run twice.

#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Generator.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/Promise.hpp"
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_set>

namespace litebird {

namespace {

const std::vector<const std::string*> kNoLabels;

bool label_matches(const std::string* label, const std::vector<const std::string*>& own) {
    if (!label) return true;                 // an unlabelled break/continue targets the nearest loop
    for (const auto* candidate : own) {
        if (*candidate == *label) return true;
    }
    return false;
}

enum class LoopAction { Next, Exit, Propagate };

struct FlagScope {
    bool& flag;
    bool saved;
    FlagScope(bool& f, bool value) : flag(f), saved(f) { flag = value; }
    ~FlagScope() { flag = saved; }
};

// ---------------------------------------------------------------------------
// Active scopes
// ---------------------------------------------------------------------------
//
// The collector cannot scan native stacks, so every scope that has code running
// in it is listed here: the scopes of all the callers of the function that is
// executing, not just its own lexical chain. There is one list per thread,
// because a suspended generator or async function keeps its scopes on the
// stack of its own thread while other code runs.

struct ScopeStack {
    std::vector<JsEnvironment*> scopes;
    ScopeStack();
    ~ScopeStack();
};

struct ScopeStackRegistry {
    std::mutex mutex;
    std::vector<ScopeStack*> stacks;

    ScopeStackRegistry() {
        Heap::default_heap().add_root_provider(this, [this](std::vector<JsValue*>&) {
            // Runs inside a collection. Script threads take turns, so every
            // other thread's list is at rest while this one reads it.
            std::lock_guard<std::mutex> lock(mutex);
            for (ScopeStack* stack : stacks) {
                for (JsEnvironment* scope : stack->scopes) Heap::default_heap().mark_environment(scope);
            }
        });
    }
};

ScopeStackRegistry& scope_registry() {
    static ScopeStackRegistry* registry = new ScopeStackRegistry();     // never destroyed: threads may outlive main()
    return *registry;
}

ScopeStack::ScopeStack() {
    auto& registry = scope_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.stacks.push_back(this);
}

ScopeStack::~ScopeStack() {
    auto& registry = scope_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.stacks.erase(std::remove(registry.stacks.begin(), registry.stacks.end(), this), registry.stacks.end());
}

ScopeStack& active_scopes() {
    static thread_local ScopeStack stack;
    return stack;
}

/// Lists `env` as active for the lifetime of this object. The caller keeps the
/// scope alive; this only makes it visible to the collector. Passing nullptr
/// is a no-op so callers can conditionally root lexical loop scopes.
struct EnvScope {
    ScopeStack& stack;
    bool pushed{false};
    explicit EnvScope(const std::shared_ptr<JsEnvironment>& env)
        : stack(active_scopes()), pushed(env != nullptr) {
        if (pushed) stack.scopes.push_back(env.get());
    }
    ~EnvScope() {
        if (pushed) stack.scopes.pop_back();
    }
    EnvScope(const EnvScope&) = delete;
    EnvScope& operator=(const EnvScope&) = delete;
};

JsTokenType binary_operator_for(JsTokenType assign_op) {
    switch (assign_op) {
        case JsTokenType::PlusEqual: return JsTokenType::Plus;
        case JsTokenType::MinusEqual: return JsTokenType::Minus;
        case JsTokenType::StarEqual: return JsTokenType::Star;
        case JsTokenType::SlashEqual: return JsTokenType::Slash;
        case JsTokenType::PercentEqual: return JsTokenType::Percent;
        case JsTokenType::PowerEqual: return JsTokenType::Power;
        case JsTokenType::ShiftLeftEqual: return JsTokenType::ShiftLeft;
        case JsTokenType::ShiftRightEqual: return JsTokenType::ShiftRight;
        case JsTokenType::ShiftRightUnsignedEqual: return JsTokenType::ShiftRightUnsigned;
        case JsTokenType::AmpEqual: return JsTokenType::Amp;
        case JsTokenType::PipeEqual: return JsTokenType::Pipe;
        case JsTokenType::CaretEqual: return JsTokenType::Caret;
        default: return JsTokenType::Eof;
    }
}

/// Property lookup that starts at `start` but runs getters against `receiver`:
/// what `super.x` needs. In a static method `start` is the cell of the parent
/// class, a function.
JsValue get_with_receiver(JsObject* start, const std::string& key, const JsValue& receiver) {
    return js_get_from_chain(start, key, receiver);
}

/// Pulls values out of an iterable one at a time, for array destructuring,
/// which must not consume more of an infinite generator than it binds.
class IterationCursor {
public:
    explicit IterationCursor(const JsValue& source) : m_source(source) {
        if (source.is_array()) {
            m_mode = Mode::Array;
        } else if (source.type == JsType::String) {
            m_mode = Mode::Buffered;
            m_buffer = js_iterable_to_vector(source);
        } else {
            m_mode = Mode::Protocol;
            JsValue method;
            if (source.is_object_like()) {
                method = source.get_property(kIteratorKey);
                if (!method.is_function()) method = source.get_property("Symbol.iterator");
            }
            if (method.is_function()) {
                m_iterator = method.call_with_this(source, {});
            } else if (source.is_object_like() && source.get_property("next").is_function()) {
                m_iterator = source;
            } else {
                js_throw_type_error(js_describe_thrown(source) + " is not iterable");
            }
            m_next = m_iterator.get_property("next");
        }
    }

    /// The next value, or undefined once the source is exhausted.
    JsValue pull() {
        if (m_done) return JsValue::make_undefined();
        switch (m_mode) {
            case Mode::Array: {
                const auto& elements = m_source.object_val->array_elements;
                if (m_index < elements.size()) return elements[m_index++];
                m_done = true;
                return JsValue::make_undefined();
            }
            case Mode::Buffered:
                if (m_index < m_buffer.size()) return m_buffer[m_index++];
                m_done = true;
                return JsValue::make_undefined();
            case Mode::Protocol: {
                JsValue step = js_call(m_next, m_iterator, {});
                if (!step.is_object_like()) js_throw_type_error("Iterator result is not an object");
                if (step.get_property("done").is_truthy()) {
                    m_done = true;
                    return JsValue::make_undefined();
                }
                return step.get_property("value");
            }
        }
        return JsValue::make_undefined();
    }

    bool done() const { return m_done; }

    /// Tells a protocol iterator that it will not be drained.
    void close() {
        if (m_mode != Mode::Protocol || m_done) return;
        m_done = true;
        JsValue ret = m_iterator.get_property("return");
        if (ret.is_function()) ret.call_with_this(m_iterator, {});
    }

private:
    enum class Mode { Array, Buffered, Protocol };
    JsValue m_source;
    Mode m_mode{Mode::Array};
    std::vector<JsValue> m_buffer;
    JsValue m_iterator;
    JsValue m_next;
    size_t m_index{0};
    bool m_done{false};
};

[[noreturn]] void throw_tdz_error(const std::string& name) {
    if (name == "this") {
        js_throw_error(JsErrorKind::ReferenceError,
                       "Must call super constructor in derived class before accessing 'this' or returning from "
                       "derived constructor");
    }
    js_throw_error(JsErrorKind::ReferenceError, "Cannot access '" + name + "' before initialization");
}

void declare_lexicals(const std::vector<LexicalDecl>& decls, const std::shared_ptr<JsEnvironment>& env) {
    for (const auto& decl : decls) env->declare_lexical(decl.name, decl.is_const);
}

std::shared_ptr<JsEnvironment> copy_scope(const std::shared_ptr<JsEnvironment>& source,
                                          const std::shared_ptr<JsEnvironment>& parent) {
    auto copy = std::make_shared<JsEnvironment>(parent);
    for (const auto& binding : source->bindings()) {
        if (!binding.is_initialized) copy->declare_lexical(binding.name, binding.is_const);
        else if (binding.is_lexical) copy->initialize_lexical(binding.name, binding.value, binding.is_const);
        else if (binding.is_const) copy->define_const(binding.name, binding.value);
        else copy->define(binding.name, binding.value);
    }
    return copy;
}

// ---------------------------------------------------------------------------
// CreateAsyncFromSyncIterator
// ---------------------------------------------------------------------------
//
// When an async generator's `yield*` finds no `@@asyncIterator`, the spec
// (ES §7.4.3) wraps the sync iterator in an async one. The wrapper's
// `next`/`return`/`throw` call the sync iterator with the argument they
// were handed, and return a promise; a throwing call or a non-object result
// rejects that promise, and the result's value is awaited. The wrapper has
// its own `return` and `throw` methods even if the sync iterator lacks them;
// those methods handle the missing delegate method themselves.
JsValue iterator_result_pair(const JsValue& value, bool done) {
    JsValue object = JsValue::make_object();
    object.set_property("value", value);
    object.set_property("done", JsValue::make_boolean(done));
    return object;
}

// IteratorClose for the sync iterator an async-from-sync wrapper wraps:
// called when a yielded value rejects, and when the sync iterator is closed
// without a return method. Errors are swallowed (the close never passes an
// argument), matching IteratorClose with a throw completion.
void js_close_sync_inner(const JsValue& sync_iterator) {
    JsValue ret;
    JsValue error;
    if (!js_capture_throw([&] { ret = sync_iterator.get_property("return"); }, error)) return;
    if (!ret.is_function()) return;
    js_capture_throw([&] { ret.call_with_this(sync_iterator, {}); }, error);
}

// GetMethod for the spec's wrapper protocol: GetV, where undefined and null
// both mean "absent". Returns true when a method (or undefined) came back;
// false when the get or the callable check threw — in which case `thrown`
// carries the error and the wrapper rejects with it.
bool js_get_wrapper_method(const JsValue& sync_iterator, const char* key, JsValue& method, JsValue& thrown) {
    JsValue raw;
    if (!js_capture_throw([&] { raw = sync_iterator.get_property(key); }, thrown)) return false;
    if (raw.is_nullish()) {
        method = JsValue::make_undefined();
        return true;
    }
    if (!raw.is_function()) {
        thrown = js_make_error(JsErrorKind::TypeError, std::string("Iterator does not have a '") + key + "' method.");
        return false;
    }
    method = raw;
    return true;
}

// AsyncFromSyncIteratorContinuation: await the step result's done, then its
// value, then the value itself; a rejected value closes the sync iterator
// (closeOnRejection) before it reaches the consumer.
JsValue js_call_awaited(const JsValue& sync_iterator, const char* key, const JsValue& method,
                        bool arg_present, const JsValue& arg) {
    JsValue result;
    JsValue thrown;
    const bool call_ok = js_capture_throw([&] {
        if (!method.is_function())
            js_throw_type_error(std::string("Iterator does not have a '") + key + "' method.");
        result = arg_present ? method.call_with_this(sync_iterator, {arg})
                             : method.call_with_this(sync_iterator, {});
    }, thrown);
    if (!call_ok) return Promise::make_rejected(thrown);
    // The sync iterator's result must be an object, before either property
    // is read. In particular, numbers and strings must not be boxed here.
    if (!result.is_object_like())
        return Promise::make_rejected(js_make_error(JsErrorKind::TypeError, "Iterator result is not an object."));
    JsValue done_value, value_value;
    if (!js_capture_throw([&] {
            done_value = result.get_property("done");
            value_value = result.get_property("value");
        }, thrown)) {
        return Promise::make_rejected(thrown);
    }
    const bool done = done_value.is_truthy();
    const bool close_on_rejection = std::string(key) != "return";
    auto data = std::make_shared<PromiseData>();
    Promise::subscribe(Promise::to_promise(value_value),
                       [data, done, sync_iterator, close_on_rejection](bool rejected, const JsValue& settled) {
        if (rejected) {
            if (!done && close_on_rejection) js_close_sync_inner(sync_iterator);
            Promise::reject(data, settled);
            return;
        }
        Promise::resolve(data, iterator_result_pair(settled, done));
    });
    return Promise::create_promise_instance(data);
}

JsValue js_make_async_from_sync(const JsValue& sync_iterator) {
    // The iterator record caches `next` once, at creation time: a plain Get
    // (a throwing getter throws out of the record creation, i.e. out of
    // here). A missing next fails at the first call, not here.
    JsValue sync_next = sync_iterator.get_property("next");
    JsValue object = JsValue::make_object();

    js_define_method(object, "next", 1, [sync_iterator, sync_next](const std::vector<JsValue>& args) {
        const bool present = !args.empty();
        const JsValue arg = present ? args[0] : JsValue::make_undefined();
        return js_call_awaited(sync_iterator, "next", sync_next, present, arg);
    });

    js_define_method(object, "return", 1, [sync_iterator](const std::vector<JsValue>& args) {
        const bool present = !args.empty();
        const JsValue arg = present ? args[0] : JsValue::make_undefined();
        JsValue method, thrown;
        if (!js_get_wrapper_method(sync_iterator, "return", method, thrown))
            return Promise::make_rejected(thrown);
        if (method.is_undefined()) {
            // GetMethod returned undefined: the sync iterator is closed by
            // resolving with done: true — the value is returned to the
            // consumer without any call.
            auto data = std::make_shared<PromiseData>();
            Promise::resolve(data, iterator_result_pair(arg, /*done=*/true));
            return Promise::create_promise_instance(data);
        }
        return js_call_awaited(sync_iterator, "return", method, present, arg);
    });

    js_define_method(object, "throw", 1, [sync_iterator](const std::vector<JsValue>& args) {
        const bool present = !args.empty();
        const JsValue arg = present ? args[0] : JsValue::make_undefined();
        JsValue method, thrown;
        if (!js_get_wrapper_method(sync_iterator, "throw", method, thrown))
            return Promise::make_rejected(thrown);
        if (method.is_undefined()) {
            // No throw method: the sync iterator is given a chance to clean
            // up, then this rejects with a TypeError. The consumer never
            // sees a step.
            js_close_sync_inner(sync_iterator);
            return Promise::make_rejected(js_make_error(
                JsErrorKind::TypeError, "The iterator does not provide a 'throw' method."));
        }
        return js_call_awaited(sync_iterator, "throw", method, present, arg);
    });
    return object;
}

} // namespace

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

std::vector<JsInterpreter::EvalFrame>& JsInterpreter::eval_call_stack() {
    thread_local std::vector<EvalFrame> stack;
    return stack;
}

JsValue JsInterpreter::run(std::string_view source) {
    // Functions keep a reference to the text they were parsed from.
    auto text = std::make_shared<const std::string>(source);
    JsLexer lexer(*text);
    auto tokens = lexer.tokenize_all();
    JsParser parser(std::move(tokens), text);
    auto program = parser.parse_program();
    return run_program(*program);
}

JsValue JsInterpreter::run_program(const Program& program) {
    return run_program(program, nullptr, false);
}

JsValue JsInterpreter::run_program(const Program& program, const std::shared_ptr<JsEnvironment>& scope,
                                   bool direct_eval) {
    CallDepthGuard depth_guard(*this);
    JsIntrinsics::set_current(m_realm.get());
    EnvPtr target = scope ? scope : m_global_env;
    EnvScope env_scope(target);

    // A script's strictness is its own, and for as long as it runs it is the
    // global scope's too (the global scope is shared by every script). A
    // direct eval inherits the strictness of the code that called it, unless
    // its own text opens with "use strict" -- which is known only once it has
    // been parsed, so the flag here is left alone: the caller's environment
    // already has the right value, and run_program keeps it.
    struct StrictScope {
        JsEnvironment& env;
        int8_t saved;
        bool active;
        StrictScope(JsEnvironment& e, bool strict, bool is_active) : env(e), saved(e.strict_mode), active(is_active) {
            if (active) env.strict_mode = strict ? 1 : 0;
        }
        ~StrictScope() {
            if (active) env.strict_mode = saved;
        }
    } strict_scope(*target, program.is_strict, !direct_eval);

    // A direct eval's `let`, `const` and `class` are its own: they must not be
    // visible to the caller's code after it returns, nor to a later eval in
    // the same function. A layer over the caller's environment gives them that
    // lifetime, and gives the eval everything the caller has beneath it -- a
    // name in the layer shadows the caller's, so `eval('x')` still reads the
    // caller's `x` when the eval declares no `x` of its own. The `var`s go
    // elsewhere (below): into the caller's environment when the code is
    // sloppy, which is what leaves them behind afterwards, and into the layer
    // when it is strict, where they are the eval's alone.
    EnvPtr body = target;
    std::shared_ptr<JsEnvironment> layer;
    const bool var_scope_own = direct_eval && (program.is_strict || target->is_strict()); // its own text, or its caller's
    if (direct_eval) {
        layer = std::make_shared<JsEnvironment>(target);
        // The eval's text is as strict as it says, and otherwise as its
        // caller's -- the caller's environment already answers that: a
        // function's environment carries its own strictness, and so does a
        // strict script's global one.
        layer->strict_mode = program.is_strict ? 1 : -1;
        body = layer;
    }
    EnvScope layer_scope(layer);

    // Everything declared exists before the first statement runs. A sloppy
    // direct eval's `var`s skip any lexical loop, catch or block environment
    // at the call site and land in the caller's variable environment.
    EnvPtr var_target = var_scope_own ? body : target;
    if (direct_eval && !var_scope_own) var_target = target->var_scope();
    for (const auto& name : program.var_names) {
        if (!var_target->has_own(name)) var_target->define(name, JsValue::make_undefined());
    }
    declare_lexicals(program.lexical_decls, body);
    // A function declaration in sloppy eval text is var-scoped like a `var` is
    // (ECMA-262 B.3.3.1), so it goes to the caller's variable environment; in
    // strict text it is lexical to the eval, like a `let`.
    hoist_functions(program.functions, var_target);

    Frame frame;
    frame.track_completion = true;
    Flow flow = exec_list(program.statements, body, frame);
    if (flow == Flow::Return) return std::move(frame.return_value);
    return std::move(frame.completion);
}

JsValue JsInterpreter::eval_source(const std::string& source) {
    // A call from outside any call expression -- a host calling `eval`'s
    // function value, say -- has no call site to answer for it, and is as
    // indirect as `(0, eval)` is.
    const EvalFrame frame = eval_call_stack().empty() ? EvalFrame{} : eval_call_stack().back();
    if (!frame.direct || !frame.environment) return run(source);

    auto text = std::make_shared<const std::string>(source);
    JsLexer lexer(*text);
    auto tokens = lexer.tokenize_all();
    JsParserContext parser_context;
    parser_context.direct_eval = true;
    parser_context.allow_super_property = frame.allow_super_property;
    parser_context.allow_super_call = frame.allow_super_call;
    JsParser parser(std::move(tokens), text, parser_context);
    // A direct eval written inside a class body can name that class's private
    // fields; the class put them in scope when it was evaluated, so they are
    // found the same way a `let` of the caller's is found.
    {
        std::vector<std::string> visible;
        for (JsEnvironment* scope = const_cast<JsEnvironment*>(frame.environment); scope; scope = scope->parent_raw()) {
            for (const auto& binding : scope->bindings()) {
                if (!binding.name.empty() && binding.name[0] == '#') visible.push_back(binding.name);
            }
        }
        parser.seed_private_names(std::move(visible));
    }
    auto program = parser.parse_program();
    // The scope has to outlive this call: the bindings its `var`s make belong
    // to the function whose code called it, and are read after it returns.
    JsEnvironment* running = const_cast<JsEnvironment*>(frame.environment);
    return run_program(*program, running->shared_from_this(), /*direct_eval=*/true);
}

void JsInterpreter::hoist_functions(const std::vector<const FunctionDeclStmt*>& functions, const EnvPtr& env) {
    for (const auto* decl : functions) {
        env->define(decl->fn->name, make_function(decl->fn, env));
    }
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

JsValue JsInterpreter::make_function(const std::shared_ptr<FunctionNode>& node, const EnvPtr& env,
                                     const std::string& name_hint) {
    JsValue fn;
    fn.type = JsType::Function;
    auto& fd = fn.ensure_function_data();
    fd.node = node;
    fd.closure = env;
    fd.name = node->name.empty() ? name_hint : node->name;
    fd.length = node->length;
    fd.is_async = node->is_async;
    fd.is_generator = node->is_generator;
    fd.is_arrow = node->is_arrow;
    fd.is_class_constructor = node->is_class_constructor;
    fd.is_constructor = !node->is_arrow && !node->is_method && !node->is_async && !node->is_generator;
    fd.body = node->body;
    if (node->simple_params) fd.params = node->simple_param_names();

    // The entry point deliberately does not capture the function value itself:
    // that would be a reference cycle. The callee arrives through the call
    // context instead.
    std::shared_ptr<const FunctionNode> shared_node = node;
    // The scope is owned once, by `fd.closure` above. This callable lives in
    // the same payload, so a plain pointer is enough, and a second owner here
    // would hide from the collector how many closures really share a scope.
    JsEnvironment* closure = env.get();
    fd.native_fn = [this, alive = m_alive, shared_node, closure](const std::vector<JsValue>& args) {
        JsValue this_val;
        JsValue callee;
        // Whoever called construct() holds this for as long as the call runs.
        const JsValue* new_target = nullptr;
        bool is_construct = false;
        JsValue::take_call_context(this_val, is_construct, &callee, &new_target);
        // The interpreter that created this function is gone: a timer or a
        // listener outlived it. There is nothing left to run the body with.
        if (!alive->load()) return JsValue::make_undefined();
        return invoke_function(shared_node, closure ? closure->shared_from_this() : nullptr, callee, this_val,
                               is_construct, args, new_target);
    };
    return fn;
}

void JsInterpreter::bind_parameters(const FunctionNode& node, const EnvPtr& env, const std::vector<JsValue>& args) {
    if (node.simple_params) {
        for (size_t i = 0; i < node.params.size(); ++i) {
            env->define(node.params[i].target->name, i < args.size() ? args[i] : JsValue::make_undefined());
        }
        return;
    }
    // Pre-declare parameter names uninitialised so earlier defaults cannot
    // observe later parameters before their initialisers run.
    for (const auto& param : node.params) {
        if (!param.target) continue;
        std::vector<std::string> names;
        param.target->collect_names(names);
        for (const auto& name : names) {
            if (!env->has_own(name)) env->declare_lexical(name, false);
        }
    }
    for (size_t i = 0; i < node.params.size(); ++i) {
        const auto& param = node.params[i];
        if (param.rest) {
            std::vector<JsValue> rest;
            for (size_t j = i; j < args.size(); ++j) rest.push_back(args[j]);
            bind_pattern(*param.target, JsValue::make_array(rest), env, BindMode::Let);
            break;
        }
        JsValue value = i < args.size() ? args[i] : JsValue::make_undefined();
        // A default applies when the argument is missing or explicitly
        // undefined, and may refer to earlier parameters.
        if (value.is_undefined() && param.default_value) {
            value = param.target->kind == Pattern::Kind::Identifier
                ? evaluate_named(param.default_value.get(), env, param.target->name)
                : evaluate(param.default_value.get(), env);
        }
        bind_pattern(*param.target, value, env, BindMode::Let);
    }
}

JsValue JsInterpreter::invoke_function(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& closure,
                                       const JsValue& callee, const JsValue& this_val, bool is_construct,
                                       const std::vector<JsValue>& args, const JsValue* new_target) {
    CallDepthGuard depth_guard(*this);
    JsIntrinsics::set_current(m_realm.get());

    if (node->is_class_constructor && !is_construct) {
        js_throw_type_error("Class constructor " + node->name + " cannot be invoked without 'new'");
    }

    auto env = std::make_shared<JsEnvironment>(closure ? closure : m_global_env);
    env->mark_var_scope();
    env->strict_mode = node->is_strict ? 1 : 0;
    if (!node->is_arrow) {
        // Arrow functions bind none of these; lookups fall through to the
        // enclosing function's scope, which is exactly lexical `this`.
        if (node->is_strict || this_val.is_object_like()) {
            env->define("this", this_val);
        } else if (this_val.is_nullish()) {
            env->define("this", m_global_object);       // sloppy mode: no receiver means the global object
        } else {
            env->define("this", js_to_object(this_val)); // sloppy mode: a primitive receiver is wrapped
        }
        if (node->uses_arguments) {
            JsValue arguments = JsValue::make_array(args);
            arguments.object_val->klass = ObjectClass::Arguments;
            env->define("arguments", arguments);
        }
        if (node->is_class_constructor || node->uses_new_target) {
            env->define("%fn", callee);
            // The function `new` was applied to: this one, unless this call is
            // a `super(...)` further down, or a Reflect.construct that names
            // another. Undefined in a plain call.
            env->define("%new.target", !is_construct ? JsValue::make_undefined() : new_target ? *new_target : callee);
        }
        if (node->is_derived_constructor) {
            env->this_binding = JsEnvironment::ThisBinding::Unbound;
            env->mark_uninitialized("this");
        }
        if (node->uses_super && callee.is_function()) {
            env->define("%home", callee.get_property("%home"));
        }
    }

    EnvScope env_scope(env);

    // Everything an async function does wrong, including a throwing parameter
    // default, comes back as a rejected promise rather than a thrown error.
    auto prepare = [&] {
        bind_parameters(*node, env, args);
        for (const auto& name : node->var_names) {
            if (!env->has_own(name)) env->define(name, JsValue::make_undefined());
        }
        if (node->body) {
            declare_lexicals(node->body->lexical_decls, env);
            hoist_functions(node->body->functions, env);
        }
    };
    if (node->is_async && !node->is_generator) {
        JsValue thrown;
        if (!js_capture_throw(prepare, thrown)) return Promise::make_rejected(thrown);
        return run_async_body(node, env);
    }
    prepare();

    if (node->is_generator) return make_generator(node, env);

    // A base class runs its field initialisers before the constructor body; a
    // derived class runs them when super() returns.
    if (node->is_class_constructor && !node->is_derived_constructor) {
        initialize_fields(callee, this_val);
    }
    JsValue result = run_function_body(*node, env);
    // What a derived constructor made is its `this` as `super(...)` left it,
    // which need not be the object it was entered with: a parent's
    // constructor can return another (eval_super_call), and then that one
    // comes back from here. Whoever called construct() uses an object that
    // comes back in preference to its own. The constructor may return an
    // object of its choice instead, or nothing; and if it returns nothing,
    // `super(...)` has to have run.
    if (is_construct && node->is_derived_constructor && !result.is_object_like()) {
        if (!result.is_undefined()) {
            js_throw_type_error("Derived constructors may only return object or undefined");
        }
        if (env->this_binding == JsEnvironment::ThisBinding::Unbound) {
            js_throw_error(JsErrorKind::ReferenceError,
                           "Must call super constructor in derived class before accessing 'this' or returning from "
                           "derived constructor");
        }
        if (env->this_binding == JsEnvironment::ThisBinding::BoundToAnother) {
            if (const auto* self = env->find_own("this"); self && self->value.is_object_like()) return self->value;
        }
    }
    return result;
}

JsValue JsInterpreter::run_function_body(const FunctionNode& node, const EnvPtr& env) {
    Frame frame;
    Flow flow = exec_list(node.body->statements, env, frame);
    if (flow == Flow::Return) return std::move(frame.return_value);
    return JsValue::make_undefined();
}

// Starts a thread that will run `node`'s body as a suspendable frame. The
// thread parks immediately and runs only while someone is inside resume().
std::shared_ptr<GeneratorData> JsInterpreter::spawn_frame(const std::shared_ptr<const FunctionNode>& node,
                                                          const EnvPtr& env, bool is_coroutine) {
    auto frame = std::make_shared<GeneratorData>();
    frame->is_coroutine = is_coroutine;
    frame->frame_env = env;

    // Forget frames that no longer exist before remembering another one.
    if (m_generators.size() >= 64 && (m_generators.size() & (m_generators.size() - 1)) == 0) {
        m_generators.erase(std::remove_if(m_generators.begin(), m_generators.end(),
                                          [](const std::weak_ptr<GeneratorData>& weak) { return weak.expired(); }),
                           m_generators.end());
    }
    m_generators.push_back(frame);

    // The thread holds a raw pointer: GeneratorData joins the thread in its
    // destructor, so it always outlives the thread, and a shared_ptr here
    // would keep a frame that is never resumed alive forever.
    GeneratorData* raw = frame.get();
    JsIntrinsics* realm = m_realm.get();
    EventLoop* loop = &EventLoop::default_loop();
    const uint32_t agent = Heap::current_agent();
    auto body = [this, raw, realm, loop, agent, node = node, env = env]() mutable {
        // The frame is part of the agent that created it.
        GeneratorContext::set_active(raw);
        JsIntrinsics::set_current(realm);
        EventLoop::set_current(loop);
        Heap::join_agent(agent);

        JsValue result;
        JsValue error;
        bool failed = false;
        if (raw->wait_for_start()) {
            try {
                EnvScope env_scope(env);
                result = run_function_body(*node, env);
            } catch (const GeneratorAbortException&) {
                // Discarded while suspended: nobody is waiting for a result.
            } catch (const GeneratorReturnException& ret) {
                // The consumer called .return(value): the frame completes with
                // the value the completion carried (awaited at the resumption
                // boundary if the generator is async).
                result = ret.value;
            } catch (const JsThrow& thrown) {
                failed = true;
                error = thrown.value;
            } catch (const std::exception& ex) {
                failed = true;
                error = js_value_from_exception(ex);
            }
        }
        // Let go of the scope before handing control back: after finish() this
        // thread must not touch anything another thread can see.
        node.reset();
        env.reset();
        GeneratorContext::set_active(nullptr);
        raw->finish(std::move(result), failed, std::move(error));
    };

    try {
        frame->worker = std::thread(std::move(body));
    } catch (const std::system_error&) {
        // The system will not give us another thread. Each suspended function
        // holds one, so this is the engine's limit on how many can be pending.
        frame->finished = true;
        frame->is_done = true;
        js_throw_error(JsErrorKind::RangeError, "Too many suspended functions: out of threads");
    }
    return frame;
}

namespace {

// Runs an async function's frame until its next `await` (or its end), then
// arranges for itself to be called again when the awaited promise settles.
void drive_coroutine(const std::shared_ptr<GeneratorData>& frame, const std::shared_ptr<PromiseData>& promise,
                     const JsValue& sent, bool is_throw) {
    if (frame->cancelled) return;       // the interpreter is gone
    if (is_throw) frame->inject_throw(sent);
    frame->resume(is_throw ? JsValue::make_undefined() : sent);
    if (frame->cancelled) return;

    if (frame->is_done) {
        frame->reap();      // the body has ended; its thread exits now
        JsValue error;
        if (frame->take_error(error)) Promise::reject(promise, error);
        else Promise::resolve(promise, frame->yielded_value);
        return;
    }

    // Parked at `await value`. Whatever it settles to is sent back in, from a
    // microtask: the code after an await never runs synchronously.
    Promise::subscribe(Promise::to_promise(frame->yielded_value),
        [frame, promise](bool rejected, const JsValue& value) {
            drive_coroutine(frame, promise, value, rejected);
        });
}

} // namespace

JsValue JsInterpreter::run_async_body(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& env) {
    auto promise = std::make_shared<PromiseData>();

    if (!node->uses_await) {
        // No await in the body: it runs straight through on this stack.
        JsValue result;
        JsValue thrown;
        if (js_capture_throw([&] { result = run_function_body(*node, env); }, thrown)) {
            Promise::resolve(promise, result);
        } else {
            Promise::reject(promise, thrown);
        }
        return Promise::create_promise_instance(promise);
    }

    // The body may suspend, so it needs a stack of its own. Its first segment
    // still runs synchronously, as the language requires: the caller gets the
    // promise back only once the body has reached its first await.
    auto frame = spawn_frame(node, env, /*is_coroutine=*/true);
    drive_coroutine(frame, promise, JsValue::make_undefined(), false);
    return Promise::create_promise_instance(promise);
}

JsValue JsInterpreter::make_generator(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& env) {
    auto gen = spawn_frame(node, env, /*is_coroutine=*/false);
    gen->is_async = node->is_async;    // an async generator: the result objects cross to the consumer

    JsValue object = JsValue::make_object();
    object.object_val->klass = ObjectClass::Generator;
    object.set_generator_value(gen);

    auto result_object = [](const JsValue& value, bool done) {
        JsValue result = JsValue::make_object();
        result.set_property("value", value);
        result.set_property("done", JsValue::make_boolean(done));
        return result;
    };

    // Runs the body to its next yield and reports what happened.
    auto advance = [gen, result_object](const JsValue& sent) -> JsValue {
        if (gen->is_done) return result_object(JsValue::make_undefined(), true);
        if (!gen->resume(sent)) js_throw_type_error("Generator is already running");
        JsValue error;
        if (gen->take_error(error)) js_throw(error);        // the body threw: it surfaces in the caller of next()
        if (gen->is_done) {
            // The return value is reported once; later calls see undefined.
            JsValue returned = gen->yielded_value;
            gen->yielded_value = JsValue::make_undefined();
            gen->reap();
            return result_object(returned, true);
        }
        if (gen->yielded_raw) {
            // A sync generator's `yield*` passes the delegate's raw step
            // object out; the consumer reads value/done off it lazily.
            gen->yielded_raw = false;
            JsValue raw = std::move(gen->yielded_value);
            gen->yielded_value = JsValue::make_undefined();
            return raw;
        }
        return result_object(gen->yielded_value, false);
    };

    const bool is_async = node->is_async;
    // An async generator's methods return promises, and a failure is a
    // rejection rather than a thrown error. A `done` result object's value
    // crosses to the consumer through a promise — a thenable it returns or
    // is handed is awaited, as in an async function's `return`; a `yield`
    // result is passed through untouched (its operand was awaited when the
    // body yielded, and a hand-written iterator's value must not be).
    auto deliver = [is_async](const std::function<JsValue()>& body) -> JsValue {
        if (!is_async) return body();
        JsValue result;
        JsValue thrown;
        if (!js_capture_throw([&] { result = body(); }, thrown)) return Promise::make_rejected(thrown);
        if (result.is_object_like() && result.get_property("done").is_truthy()) {
            JsValue value = result.get_property("value");
            // A thenable value is adopted, as in an async function's return;
            // anything else settles the consumer's promise right away.
            JsValue then_fn, then_thrown;
            const bool then_ok = js_capture_throw([&] { then_fn = value.get_property("then"); }, then_thrown);
            if (!then_ok) return Promise::make_rejected(then_thrown);
            if (!then_fn.is_function()) return Promise::make_resolved(result);
            auto data = std::make_shared<PromiseData>();
            Promise::subscribe(Promise::to_promise(value), [data](bool rejected, const JsValue& settled) {
                if (rejected) {
                    Promise::reject(data, settled);
                    return;
                }
                Promise::resolve(data, iterator_result_pair(settled, /*done=*/true));
            });
            return Promise::create_promise_instance(data);
        }
        return Promise::make_resolved(result);
    };

    js_define_method(object, "next", 1, [advance, deliver](const std::vector<JsValue>& args) {
        const JsValue sent = js_arg(args, 0);
        return deliver([&] { return advance(sent); });
    });

    js_define_method(object, "return", 1, [gen, result_object, deliver, advance](const std::vector<JsValue>& args) {
        const JsValue value = js_arg(args, 0);
        return deliver([&] {
            if (gen->is_running()) js_throw_type_error("Generator is already running");
            if (!gen->started || gen->is_done) {
                // Nothing is suspended: the return is answered without
                // resuming the body. A yield already performed inside a
                // `finally` while closing cannot have happened yet, so this
                // settles immediately.
                gen->terminate(false);
                return result_object(value, true);
            }
            // Resume suspended at the yield with a return completion. The
            // body unwinds through its `finally` blocks with the value; a
            // `yield*` in the way routes the completion into the delegate's
            // `return` method first. Whatever the frame completes with is
            // handed to the consumer as an iterator result.
            gen->inject_return(value);
            return advance(JsValue::make_undefined());
        });
    });

    js_define_method(object, "throw", 1, [gen, advance, deliver](const std::vector<JsValue>& args) {
        const JsValue error = js_arg(args, 0);
        return deliver([&]() -> JsValue {
            if (gen->is_running()) js_throw_type_error("Generator is already running");
            if (!gen->started || gen->is_done) {
                // Nothing is suspended to receive it: the generator just ends.
                gen->terminate(false);
                js_throw(error);
            }
            // Raised inside the body at the suspended `yield`, where the
            // generator's own try/catch can handle it.
            gen->inject_throw(error);
            return advance(JsValue::make_undefined());
        });
    });

    JsObject* cell = object.object_val;
    auto self = [cell](const std::vector<JsValue>&) { return JsValue::make_object(cell); };
    js_define_method(object, is_async ? kAsyncIteratorKey : kIteratorKey, 0, self);
    if (!is_async) js_define_method(object, "Symbol.iterator", 0, self);
    return object;
}

JsValue JsInterpreter::call_legacy_function(const JsValue& fn, const std::vector<JsValue>& args,
                                            const JsValue* this_val) {
    // Function values produced by the bytecode compiler: parameters by name
    // and a body block, no FunctionNode.
    const auto* fd = fn.function_data();
    if (!fd || !fd->body) return JsValue::make_undefined();
    auto* body = dynamic_cast<const BlockStmt*>(fd->body.get());
    if (!body) return JsValue::make_undefined();

    CallDepthGuard depth_guard(*this);
    auto env = std::make_shared<JsEnvironment>(fd->closure ? fd->closure : m_global_env);
    env->define("this", this_val ? *this_val : JsValue::make_undefined());
    for (size_t i = 0; i < fd->params.size(); ++i) {
        env->define(fd->params[i], i < args.size() ? args[i] : JsValue::make_undefined());
    }
    hoist_functions(body->functions, env);
    Frame frame;
    Flow flow = exec_list(body->statements, env, frame);
    return flow == Flow::Return ? std::move(frame.return_value) : JsValue::make_undefined();
}

// ---------------------------------------------------------------------------
// Classes
// ---------------------------------------------------------------------------

JsValue JsInterpreter::define_class(const std::shared_ptr<ClassNode>& cls, const EnvPtr& env) {
    // The class name is in its own TDZ while `extends` and computed property
    // keys are evaluated, and stays immutable inside the class body.
    auto class_env = std::make_shared<JsEnvironment>(env);
    class_env->strict_mode = 1;     // class bodies are always strict code
    if (!cls->name.empty()) class_env->declare_lexical(cls->name, true);

    JsValue parent;
    bool has_parent = false;
    if (cls->super_class) {
        parent = evaluate(cls->super_class.get(), class_env);
        if (parent.type != JsType::Null) {
            // An arrow function, a method, an async or a generator function
            // cannot be constructed, so it cannot be a parent either.
            if (!js_is_constructor(parent)) {
                js_throw_type_error("Class extends value " + js_describe_thrown(parent) +
                                    " is not a constructor or null");
            }
            has_parent = true;
        }
    }

    JsValue proto = JsValue::make_object();
    if (has_parent) {
        JsValue parent_proto = parent.get_property("prototype");
        if (parent_proto.is_object_like()) {
            if (JsObject* target = parent_proto.as_prototype()) proto.object_val->prototype = target;
        } else if (parent_proto.type == JsType::Null) {
            proto.object_val->prototype = nullptr;
        }
    } else if (cls->super_class) {
        proto.object_val->prototype = nullptr;       // `extends null`
    }

    JsValue ctor = make_function(cls->constructor, class_env, cls->name);
    {
        // Still unshared: safe to finish building the payload.
        auto& fd = ctor.ensure_function_data();
        fd.class_node = cls;
        fd.is_class_constructor = true;
        fd.is_constructor = true;
    }
    ctor.define_property("prototype", proto, 0);
    proto.define_property("constructor", ctor, PropertyMap::Hidden);
    // The parent is the prototype of the class itself: static members are
    // found along that link, `Object.getPrototypeOf(Derived)` is `Base`, and
    // `super(...)` calls whatever is there when the constructor runs. The
    // class was made a moment ago, so it cannot be in its parent's chain and
    // there is no loop to look for (which would cost the length of the chain
    // for every class of a long one).
    if (has_parent) ctor.function_cell()->prototype = parent.as_prototype();
    ctor.define_property("%home", proto, PropertyMap::Hidden);

    // The private names this evaluation declares. The key carries a serial
    // number, so `#p` in two classes (or in two evaluations of one class
    // expression) is two slots, and no string a script can write is one of
    // them: they do not live in the property table at all.
    const std::string private_prefix = "\x01" + std::to_string(++m_next_private_id) + ":";
    for (const auto& member : cls->members) {
        if (member.is_private) class_env->define(member.key, JsValue::make_string(private_prefix + member.key));
    }

    std::vector<std::string> member_keys;
    member_keys.reserve(cls->members.size());
    for (const auto& member : cls->members) {
        member_keys.push_back(member.computed_key
                                  ? js_to_property_key(evaluate(member.computed_key.get(), class_env))
                                  : member.is_private ? private_prefix + member.key
                                                      : member.key);
    }

    if (!cls->name.empty()) class_env->initialize_lexical(cls->name, ctor, true);

    // A private method, getter or setter is not a property of the prototype:
    // it is part of the brand every instance gets when it is made, so it is
    // collected here and installed by initialize_fields(). A getter and a
    // setter for one name share the slot they are installed under.
    struct PrivateInstall {
        std::string key;
        JsValue value;          // the method, or the getter
        JsValue setter;
        bool accessor{false};
    };
    std::vector<PrivateInstall> private_installs;

    std::vector<JsValue> field_keys;
    for (size_t i = 0; i < cls->members.size(); ++i) {
        const auto& member = cls->members[i];
        JsValue target = member.is_static ? ctor : proto;
        const std::string& key = member_keys[i];
        // What the function is called: the source spelling for a private name,
        // so `#m` and `get #x` print as they were written.
        const std::string& fn_name = member.is_private ? member.key : key;

        switch (member.kind) {
            case ClassMember::Kind::Method: {
                JsValue fn = make_function(member.fn, class_env, fn_name);
                if (member.fn->uses_super) fn.define_property("%home", target, PropertyMap::Hidden);
                if (!member.is_private) {
                    target.define_property(key, fn, PropertyMap::Hidden);
                } else if (member.is_static) {
                    // A method is not writable: `this.#m = 1` is a TypeError.
                    ctor.define_private(key, fn, PropertyMap::Configurable);
                } else {
                    private_installs.push_back({key, fn, JsValue(), false});
                }
                break;
            }
            case ClassMember::Kind::Getter:
            case ClassMember::Kind::Setter: {
                JsValue fn = make_function(member.fn, class_env, fn_name);
                if (member.fn->uses_super) fn.define_property("%home", target, PropertyMap::Hidden);
                if (member.is_private) {
                    if (!member.is_static) {
                        PrivateInstall* slot = nullptr;
                        for (auto& install : private_installs) {
                            if (install.key == key && install.accessor) slot = &install;
                        }
                        if (!slot) {
                            private_installs.push_back({key, JsValue(), JsValue(), true});
                            slot = &private_installs.back();
                        }
                        if (member.kind == ClassMember::Kind::Getter) slot->value = fn;
                        else slot->setter = fn;
                        break;
                    }
                    // A static private accessor, paired in the class object's
                    // own private table the same way.
                    JsValue getter;
                    JsValue setter;
                    ctor.private_accessor(key, getter, setter);
                    if (member.kind == ClassMember::Kind::Getter) getter = fn;
                    else setter = fn;
                    ctor.define_private_accessor(key, getter, setter, PropertyMap::Configurable);
                    break;
                }
                // A getter and a setter for the same name share one property.
                JsValue getter;
                JsValue setter;
                JsObject* cell = target.property_cell(true);
                auto existing = cell->properties.find(key);
                if (existing != cell->properties.end() && (existing->flags & PropertyMap::Accessor)) {
                    getter = existing->second;
                    if (existing->setter) setter = *existing->setter;
                }
                if (member.kind == ClassMember::Kind::Getter) getter = fn;
                else setter = fn;
                target.define_accessor(key, getter, setter, PropertyMap::Configurable);
                break;
            }
            case ClassMember::Kind::Field: {
                if (!member.is_static) {
                    field_keys.push_back(JsValue::make_string(key));
                    break;
                }
                JsValue value;
                if (member.value) {
                    auto field_env = std::make_shared<JsEnvironment>(class_env);
                    field_env->define("this", ctor);
                    field_env->define("%home", ctor);
                    value = evaluate_named(member.value.get(), field_env, fn_name);
                }
                if (member.is_private) ctor.define_private(key, value, PropertyMap::Default);
                else ctor.define_property(key, value, PropertyMap::Default);
                break;
            }
            case ClassMember::Kind::StaticBlock: {
                JsValue block = make_function(member.fn, class_env);
                block.define_property("%home", ctor, PropertyMap::Hidden);
                block.call_with_this(ctor, {});
                break;
            }
        }
    }
    if (!field_keys.empty()) {
        ctor.define_property("%field_keys", JsValue::make_array(field_keys), PropertyMap::Hidden);
    }
    if (!private_installs.empty()) {
        std::vector<JsValue> rows;
        rows.reserve(private_installs.size());
        for (const auto& install : private_installs) {
            std::vector<JsValue> row;
            row.push_back(JsValue::make_string(install.key));
            row.push_back(install.value);
            row.push_back(install.setter);
            row.push_back(JsValue::make_boolean(install.accessor));
            rows.push_back(JsValue::make_array(std::move(row)));
        }
        ctor.define_property("%privates", JsValue::make_array(std::move(rows)), PropertyMap::Hidden);
    }
    return ctor;
}

void JsInterpreter::initialize_fields(const JsValue& ctor, const JsValue& instance) {
    const auto* fd = ctor.function_data();
    if (!fd || !fd->class_node || !instance.is_object_like()) return;
    JsObject* ctor_cell = ctor.property_cell();
    if (!ctor_cell) return;

    // The private methods and accessors first: they are the brand, and a field
    // initialiser may call one of them (`#area = this.#side() * ...`).
    auto privates_entry = ctor_cell->properties.find("%privates");
    if (privates_entry != ctor_cell->properties.end()) {
        for (const auto& row : privates_entry->second.array_elements()) {
            const auto& parts = row.array_elements();
            if (parts.size() < 4) continue;
            const std::string key = parts[0].string_val.str();
            JsValue target = instance;      // a handle: the slot is on the shared object
            if (parts[3].is_truthy()) target.define_private_accessor(key, parts[1], parts[2], PropertyMap::Configurable);
            else target.define_private(key, parts[1], PropertyMap::Configurable);   // a method: not writable
        }
    }

    auto keys_entry = ctor_cell->properties.find("%field_keys");
    if (keys_entry == ctor_cell->properties.end()) return;
    const auto& keys = keys_entry->second.array_elements();

    const auto* cls = static_cast<const ClassNode*>(fd->class_node.get());
    auto field_env = std::make_shared<JsEnvironment>(fd->closure);
    field_env->define("this", instance);
    auto home = ctor_cell->properties.find("%home");
    if (home != ctor_cell->properties.end()) field_env->define("%home", home->second);

    JsValue target = instance;
    size_t index = 0;
    for (const auto& member : cls->members) {
        if (member.kind != ClassMember::Kind::Field || member.is_static) continue;
        if (index >= keys.size()) break;
        const std::string key = keys[index++].string_val.str();
        JsValue value = member.value ? evaluate_named(member.value.get(), field_env, key)
                                     : JsValue::make_undefined();
        if (member.is_private) target.define_private(key, value, PropertyMap::Default);
        else target.define_property(key, value, PropertyMap::Default);
    }
}

JsValue JsInterpreter::super_base(const EnvPtr& env) {
    auto* home = env->lookup("%home");
    if (!home || !home->value.is_object_like()) {
        js_throw_error(JsErrorKind::SyntaxError, "'super' keyword unexpected here");
    }
    // One step up from where the method lives: the prototype of the class's
    // prototype object, or for a static method the parent class, a function.
    JsObject* proto = home->value.prototype_object();
    if (!proto) return JsValue::make_undefined();
    return JsValue::make_object(proto);
}

JsValue JsInterpreter::eval_super_call(const SuperCallExpr* expr, const EnvPtr& env) {
    auto* active = env->lookup("%fn");
    if (!active || !active->value.is_function()) {
        js_throw_error(JsErrorKind::SyntaxError, "'super' keyword unexpected here");
    }
    JsValue ctor = active->value;
    // `this` of the constructor super(...) stands in: the binding this call
    // may give another object, and through an arrow function too.
    JsEnvironment* const constructor_scope = env->derived_constructor_scope();
    JsValue this_val;
    if (constructor_scope) {
        if (auto* self = constructor_scope->find_own("this")) this_val = self->value;
    } else {
        this_val = lookup_identifier("this", env);
    }
    // new.target goes up unchanged: in a parent's constructor it is still the
    // class `new` was applied to. (A copy: the binding can move while the
    // parent's constructor runs.)
    JsValue new_target_value;
    if (auto* binding = env->lookup("%new.target")) new_target_value = binding->value;
    const JsValue* new_target = new_target_value.is_function() ? &new_target_value : nullptr;
    // The parent is the constructor's prototype at this moment, as the
    // language has it: `Object.setPrototypeOf(Derived, Other)` changes what
    // `super(...)` calls. A class that extends null has Function.prototype
    // there, a function that is not a constructor.
    JsValue parent;
    if (JsObject* link = ctor.prototype_object()) parent = JsValue::make_object(link);
    const auto* parent_fd = parent.function_data();
    if (!js_is_constructor(parent)) {
        js_throw_type_error("Super constructor is not a constructor");
    }

    std::vector<JsValue> args = eval_arguments(expr->args, env);

    // `this` is bound once. A second `super(...)` runs the parent all the
    // same, as the language has it, and is refused when the parent comes
    // back. It is given an object of its own to work on, so that what the
    // first one made is left as it was.
    JsValue spare;
    if (constructor_scope && constructor_scope->this_binding != JsEnvironment::ThisBinding::Unbound) {
        spare = JsValue::make_object();
        const JsValue proto = new_target ? new_target->get_property("prototype") : JsValue::make_undefined();
        if (proto.is_object_like()) {
            if (JsObject* target = proto.as_prototype()) spare.object_val->prototype = target;
        }
    }
    const JsValue& receiver = spare.is_object() ? spare : this_val;

    JsValue made;       // an object the parent gave back in place of the one it was given
    if ((parent_fd && parent_fd->node) || parent.forwards_construct()) {
        // A script parent initialises the object it is given, unless its
        // constructor returns another: then that one is `this` from here on,
        // as it is, with the prototype it has. A bound function constructs
        // its target on an object of its own, which comes back the same way.
        made = parent.construct(args, &receiver, new_target);
        if (!made.is_object_like() || made.identity() == receiver.identity()) made = JsValue::make_undefined();
    } else {
        // A built-in parent (Error, Array, Map, ...) builds its own object;
        // take over its state so `this` becomes that kind of object. A host
        // constructor (Event, HTMLElement) is handed `this` and makes it the
        // object it constructs, and then there is nothing to take over.
        const auto take_over = [](JsObject* from, JsObject* to) {
            for (const auto& entry : from->properties) {
                auto& slot = to->properties.entry(entry.first);
                slot.second = entry.second;
                slot.setter = entry.setter;
                slot.flags = entry.flags;
            }
            to->is_array = from->is_array;
            to->array_elements = from->array_elements;
            // Which of those places are holes travels with them: `class List
            // extends Array {}` with `new List(3)` is three holes, because the
            // Array constructor made holes and this is where they are handed on.
            to->array_holes = from->array_holes ? std::make_unique<JsObject::Holes>(*from->array_holes)
                                                : nullptr;
            to->klass = from->klass;
            to->internal = from->internal;
            to->exotic = from->exotic;
            to->trace_extra = from->trace_extra;
            to->has_accessors = to->has_accessors || from->has_accessors;
            to->has_legacy_getters = to->has_legacy_getters || from->has_legacy_getters;
            if (from->dom_node) to->dom_node = from->dom_node;
        };
        JsValue in_place;
        JsValue built = parent.construct_as_super(args, receiver, new_target, &in_place);
        if (in_place.is_object()) {
            // The interface built on an object of its own and handed it back:
            // it is the element an upgrade is running for, which is what this
            // construction makes (JsValue::hand_back_constructed_object). That
            // object is `this` from here; an interface that builds its own
            // object whatever it is offered (Text) has its state taken over
            // into the one it was given, as below.
            made = in_place;
            if (built.object_val && built.object_val != in_place.object_val) {
                take_over(built.object_val, in_place.object_val);
            }
        } else if (built.object_val && receiver.object_val && built.object_val != receiver.object_val) {
            if (receiver.object_val->dom_node) {
                // `this` is an element that is being upgraded, and the parent
                // is no interface of the DOM at all: a class that extends Map
                // was given to customElements.define. The element is not made
                // a map. What the parent built is `this`, with the prototype
                // new.target has, as Reflect.construct(Map, [], NewTarget)
                // makes it, and the upgrade fails because that is not the
                // element.
                const JsValue proto = new_target ? new_target->get_property("prototype") : JsValue::make_undefined();
                if (proto.is_object_like()) {
                    if (JsObject* target = proto.as_prototype()) js_set_prototype(built.object_val, target);
                }
                made = built;
            } else {
                take_over(built.object_val, receiver.object_val);
            }
        }
    }
    // The parent has run; now `this` is bound. (The flag is read again: the
    // parent's constructor may have bound `this` itself, through an arrow
    // function it was passed.)
    if (constructor_scope) {
        if (constructor_scope->this_binding != JsEnvironment::ThisBinding::Unbound) {
            js_throw_error(JsErrorKind::ReferenceError, "Super constructor may only be called once");
        }
        constructor_scope->this_binding = made.is_object_like() ? JsEnvironment::ThisBinding::BoundToAnother
                                                                : JsEnvironment::ThisBinding::Bound;
        if (auto* self = constructor_scope->find_own("this")) {
            if (made.is_object_like()) self->value = made;
            self->is_initialized = true;
        }
    }
    if (made.is_object_like()) {
        this_val = made;
    }
    initialize_fields(ctor, this_val);
    return this_val;        // `super(...)` is an expression, and its value is `this`
}

// ---------------------------------------------------------------------------
// Variables, properties and patterns
// ---------------------------------------------------------------------------

JsValue JsInterpreter::lookup_identifier(const std::string& name, const EnvPtr& env) {
    if (auto* binding = env->lookup(name)) {
        if (!binding->is_initialized) throw_tdz_error(name);
        return binding->value;
    }

    // Properties of the global object are global variables too: `window.x = 1`
    // makes `x` resolvable.
    if (JsObject* global = m_global_object.object_val) {
        int guard = 0;
        for (JsObject* obj = global; obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
            auto it = obj->properties.find(name);
            if (it == obj->properties.end()) continue;
            if (it->flags & PropertyMap::Accessor) {
                return it->second.is_function() ? it->second.call_with_this(m_global_object, {})
                                                : JsValue::make_undefined();
            }
            return it->second;
        }
        // What the global object computes: the elements of the page by id.
        if (global->exotic) {
            JsValue computed;
            if (global->exotic->get(global, name, computed) || global->exotic->get_last(global, name, computed)) {
                return computed;
            }
        }
    }
    js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
}

void JsInterpreter::assign_identifier(const std::string& name, const JsValue& value, const EnvPtr& env) {
    switch (env->try_assign(name, value)) {
        case JsEnvironment::AssignResult::Ok:
            return;
        case JsEnvironment::AssignResult::Uninitialized:
            throw_tdz_error(name);
        case JsEnvironment::AssignResult::Const:
            js_throw_type_error("Assignment to constant variable.");
        case JsEnvironment::AssignResult::ReadOnly:
            // `undefined = 1`: ignored in sloppy code, an error in strict code.
            if (env->is_strict()) {
                js_throw_type_error("Cannot assign to read only property '" + name + "' of object '#<Object>'");
            }
            return;
        case JsEnvironment::AssignResult::NotFound:
            break;
    }
    if (JsObject* global = m_global_object.object_val) {
        if (global->properties.find(name) != global->properties.end()) {
            JsValue target = m_global_object;
            if (!target.set_property(name, value) && env->is_strict()) {
                js_throw_type_error("Cannot set property '" + name + "' of this object");
            }
            return;
        }
    }
    if (env->is_strict()) js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
    m_global_env->define(name, value);      // sloppy mode: implicit global
}

std::string JsInterpreter::private_key_for(const std::string& display, const EnvPtr& env) const {
    // The class that declared the name put it in scope when it was evaluated;
    // the code that mentions it looks it up the way any other name is looked
    // up, so a method of an outer class can read its field from a nested class.
    auto* binding = env->lookup(display);
    if (binding && binding->value.type == JsType::String) return binding->value.string_val.str();
    js_throw_error(JsErrorKind::SyntaxError,
                   "Private field '" + display + "' must be declared in an enclosing class");
}

JsValue JsInterpreter::read_private_member(const JsValue& base, const std::string& display, const EnvPtr& env) {
    if (base.is_nullish()) {
        js_throw_type_error(std::string("Cannot read properties of ") +
                            (base.type == JsType::Null ? "null" : "undefined") + " (reading '" + display + "')");
    }
    return base.get_private(private_key_for(display, env), display);
}

JsValue JsInterpreter::get_member(const JsValue& base, const std::string& key) {
    if (base.is_nullish()) {
        js_throw_type_error(std::string("Cannot read properties of ") +
                            (base.type == JsType::Null ? "null" : "undefined") + " (reading '" + key + "')");
    }
    return base.get_property(key);
}

void JsInterpreter::set_member(const JsValue& base, const std::string& key, const JsValue& value) {
    if (base.is_nullish()) {
        js_throw_type_error(std::string("Cannot set properties of ") +
                            (base.type == JsType::Null ? "null" : "undefined") + " (setting '" + key + "')");
    }
    JsValue target = base;      // a handle: the write lands on the shared object
    target.set_property(key, value);
}

JsInterpreter::Reference JsInterpreter::resolve_reference(const Expr* target, const EnvPtr& env) {
    Reference ref;
    switch (target->kind) {
        case NodeKind::Identifier:
            ref.name = static_cast<const IdentifierExpr*>(target)->name;
            return ref;
        case NodeKind::Member: {
            auto* member = static_cast<const MemberExpr*>(target);
            ref.is_property = true;
            ref.base = evaluate(member->object.get(), env);
            ref.name = member->property;
            ref.is_private = member->private_name;
            if (ref.is_private) {
                if (ref.base.is_nullish()) {
                    js_throw_type_error(std::string("Cannot write properties of ") +
                                        (ref.base.type == JsType::Null ? "null" : "undefined") +
                                        " (writing '" + ref.name + "')");
                }
                ref.private_key = private_key_for(ref.name, env);
            }
            break;
        }
        case NodeKind::Index: {
            auto* index = static_cast<const IndexExpr*>(target);
            ref.is_property = true;
            ref.base = evaluate(index->object.get(), env);
            ref.name = js_to_property_key(evaluate(index->index.get(), env));
            break;
        }
        case NodeKind::SuperMember: {
            auto* super_member = static_cast<const SuperMemberExpr*>(target);
            ref.is_property = true;
            ref.base = lookup_identifier("this", env);
            ref.name = super_member->computed
                ? js_to_property_key(evaluate(super_member->computed.get(), env))
                : super_member->property;
            break;
        }
        default:
            js_throw_error(JsErrorKind::SyntaxError, "Invalid assignment target");
    }
    if (ref.base.is_nullish()) {
        js_throw_type_error(std::string("Cannot set properties of ") +
                            (ref.base.type == JsType::Null ? "null" : "undefined") +
                            " (setting '" + ref.name + "')");
    }
    return ref;
}

JsValue JsInterpreter::read_reference(const Reference& ref, const EnvPtr& env) {
    if (ref.is_private) return ref.base.get_private(ref.private_key, ref.name);
    if (ref.is_property) return ref.base.get_property(ref.name);
    return lookup_identifier(ref.name, env);
}

void JsInterpreter::write_reference(const Reference& ref, const JsValue& value, const EnvPtr& env) {
    if (ref.is_private) {
        JsValue target = ref.base;
        target.set_private(ref.private_key, value, ref.name);
        return;
    }
    if (ref.is_property) {
        JsValue target = ref.base;
        // A write to a property this object does not take (a getter-only
        // index of a collection, say) is refused; strict mode throws, as
        // [[Set]]'s false requires.
        if (!target.set_property(ref.name, value) && env->is_strict()) {
            js_throw_type_error("Cannot set property '" + ref.name + "' of this object");
        }
        return;
    }
    assign_identifier(ref.name, value, env);
}

void JsInterpreter::bind_pattern(const Pattern& pattern, const JsValue& value, const EnvPtr& env, BindMode mode) {
    js_check_stack();       // patterns nest: [[[[x]]]] = v
    switch (pattern.kind) {
        case Pattern::Kind::Identifier:
            switch (mode) {
                case BindMode::Let:
                    env->initialize_lexical(pattern.name, value, false);
                    return;
                case BindMode::Const:
                    env->initialize_lexical(pattern.name, value, true);
                    return;
                case BindMode::Var: {
                    // The binding was hoisted to the function scope on entry.
                    if (auto* binding = env->lookup(pattern.name)) {
                        if (binding->is_readonly) return;      // `var undefined = 1` at the top level
                        binding->value = value;
                        binding->is_const = false;
                    } else {
                        m_global_env->define(pattern.name, value);
                    }
                    return;
                }
                case BindMode::Assign:
                    assign_identifier(pattern.name, value, env);
                    return;
            }
            return;

        case Pattern::Kind::Target: {
            Reference ref = resolve_reference(pattern.target.get(), env);
            write_reference(ref, value, env);
            return;
        }

        case Pattern::Kind::Array: {
            if (value.is_nullish()) {
                js_throw_type_error(js_describe_thrown(value) + " is not iterable");
            }
            IterationCursor cursor(value);
            for (const auto& element : pattern.elements) {
                if (element.rest) {
                    std::vector<JsValue> rest;
                    while (true) {
                        JsValue item = cursor.pull();
                        if (cursor.done()) break;
                        rest.push_back(std::move(item));
                    }
                    bind_pattern(*element.target, JsValue::make_array(rest), env, mode);
                    break;
                }
                JsValue item = cursor.pull();
                if (!element.target) continue;       // hole: `[a, , b]`
                if (item.is_undefined() && element.default_value) {
                    item = element.target->kind == Pattern::Kind::Identifier
                        ? evaluate_named(element.default_value.get(), env, element.target->name)
                        : evaluate(element.default_value.get(), env);
                }
                bind_pattern(*element.target, item, env, mode);
            }
            cursor.close();
            return;
        }

        case Pattern::Kind::Object: {
            if (value.is_nullish()) {
                js_throw_type_error("Cannot destructure '" + js_describe_thrown(value) +
                                    "' as it is " + (value.type == JsType::Null ? "null" : "undefined") + ".");
            }
            std::vector<std::string> used;
            for (const auto& element : pattern.elements) {
                if (element.rest) {
                    // Everything not already picked out, own and enumerable.
                    JsValue rest = JsValue::make_object();
                    std::vector<std::string> rest_keys;
                    if (!litebird::dom::cross_origin_own_keys(value, false, rest_keys))
                        rest_keys = value.own_keys();
                    for (const auto& key : rest_keys) {
                        if (std::find(used.begin(), used.end(), key) != used.end()) continue;
                        rest.set_property(key, value.get_property(key));
                    }
                    bind_pattern(*element.target, rest, env, mode);
                    continue;
                }
                std::string key = element.computed_key
                    ? js_to_property_key(evaluate(element.computed_key.get(), env))
                    : element.key;
                used.push_back(key);
                JsValue item = value.get_property(key);
                if (item.is_undefined() && element.default_value) {
                    item = element.target->kind == Pattern::Kind::Identifier
                        ? evaluate_named(element.default_value.get(), env, element.target->name)
                        : evaluate(element.default_value.get(), env);
                }
                bind_pattern(*element.target, item, env, mode);
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

JsInterpreter::Flow JsInterpreter::exec_list(const std::vector<std::unique_ptr<Stmt>>& list,
                                             const EnvPtr& env, Frame& frame) {
    for (const auto& stmt : list) {
        Flow flow = exec(stmt.get(), env, frame);
        if (flow != Flow::Normal) return flow;
    }
    return Flow::Normal;
}

JsInterpreter::Flow JsInterpreter::exec_block(const BlockStmt* block, const EnvPtr& env, Frame& frame) {
    // A block only needs a scope of its own when it declares something.
    if (!block->has_lexical) return exec_list(block->statements, env, frame);
    auto scope = std::make_shared<JsEnvironment>(env);
    EnvScope env_scope(scope);
    declare_lexicals(block->lexical_decls, scope);
    hoist_functions(block->functions, scope);
    return exec_list(block->statements, scope, frame);
}

void JsInterpreter::exec_var_decl(const VarDeclStmt* stmt, const EnvPtr& env) {
    for (const auto& decl : stmt->decls) {
        const bool is_name = decl.target->kind == Pattern::Kind::Identifier;
        if (stmt->kind == JsDeclKind::Var) {
            if (!decl.init) continue;        // `var x;` was hoisted and has no effect here
            JsValue value = is_name ? evaluate_named(decl.init.get(), env, decl.target->name)
                                    : evaluate(decl.init.get(), env);
            bind_pattern(*decl.target, value, env, BindMode::Var);
        } else {
            JsValue value;
            if (decl.init) {
                value = is_name ? evaluate_named(decl.init.get(), env, decl.target->name)
                                : evaluate(decl.init.get(), env);
            }
            bind_pattern(*decl.target, value, env,
                         stmt->kind == JsDeclKind::Const ? BindMode::Const : BindMode::Let);
        }
    }
}

JsInterpreter::Flow JsInterpreter::exec(const Stmt* stmt, const EnvPtr& env, Frame& frame) {
    // Statements nest too (`if (a) if (b) ...`, an `else if` ladder, blocks in
    // blocks), with no expression in between for evaluate()'s guard to catch.
    js_check_stack();
    switch (stmt->kind) {
        case NodeKind::ExprStmt: {
            JsValue value = evaluate(static_cast<const ExprStmt*>(stmt)->expression.get(), env);
            if (frame.track_completion) frame.completion = std::move(value);
            return Flow::Normal;
        }
        case NodeKind::VarDecl:
            exec_var_decl(static_cast<const VarDeclStmt*>(stmt), env);
            return Flow::Normal;
        case NodeKind::Block:
            return exec_block(static_cast<const BlockStmt*>(stmt), env, frame);
        case NodeKind::If: {
            auto* if_stmt = static_cast<const IfStmt*>(stmt);
            if (evaluate(if_stmt->condition.get(), env).is_truthy()) {
                return exec(if_stmt->then_branch.get(), env, frame);
            }
            if (if_stmt->else_branch) return exec(if_stmt->else_branch.get(), env, frame);
            return Flow::Normal;
        }
        case NodeKind::While:
            return exec_while(static_cast<const WhileStmt*>(stmt), env, frame, kNoLabels);
        case NodeKind::DoWhile:
            return exec_do_while(static_cast<const DoWhileStmt*>(stmt), env, frame, kNoLabels);
        case NodeKind::For:
            return exec_for(static_cast<const ForStmt*>(stmt), env, frame, kNoLabels);
        case NodeKind::ForEach:
            return exec_for_each(static_cast<const ForEachStmt*>(stmt), env, frame, kNoLabels);
        case NodeKind::Return: {
            auto* ret = static_cast<const ReturnStmt*>(stmt);
            frame.return_value = ret->value ? evaluate(ret->value.get(), env) : JsValue::make_undefined();
            return Flow::Return;
        }
        case NodeKind::Break: {
            auto* brk = static_cast<const BreakStmt*>(stmt);
            frame.label = brk->label.empty() ? nullptr : &brk->label;
            return Flow::Break;
        }
        case NodeKind::Continue: {
            auto* cont = static_cast<const ContinueStmt*>(stmt);
            frame.label = cont->label.empty() ? nullptr : &cont->label;
            return Flow::Continue;
        }
        case NodeKind::Throw:
            js_throw(evaluate(static_cast<const ThrowStmt*>(stmt)->argument.get(), env));
        case NodeKind::Try:
            return exec_try(static_cast<const TryStmt*>(stmt), env, frame);
        case NodeKind::Switch:
            return exec_switch(static_cast<const SwitchStmt*>(stmt), env, frame);
        case NodeKind::Labeled: {
            std::vector<const std::string*> labels;
            return exec_labeled(stmt, env, frame, labels);
        }
        case NodeKind::ClassDecl: {
            auto* decl = static_cast<const ClassDeclStmt*>(stmt);
            env->initialize_lexical(decl->cls->name, define_class(decl->cls, env), false);
            return Flow::Normal;
        }
        case NodeKind::FunctionDecl:    // hoisted when the enclosing block was entered
        case NodeKind::Empty:
        default:
            return Flow::Normal;
    }
}

JsInterpreter::Flow JsInterpreter::exec_labeled(const Stmt* stmt, const EnvPtr& env, Frame& frame,
                                                std::vector<const std::string*>& labels) {
    auto* labeled = static_cast<const LabeledStmt*>(stmt);
    labels.push_back(&labeled->label);
    const Stmt* body = labeled->body.get();

    // A loop owns its labels so that `continue label` reaches it.
    switch (body->kind) {
        case NodeKind::Labeled:
            return exec_labeled(body, env, frame, labels);
        case NodeKind::While:
            return exec_while(static_cast<const WhileStmt*>(body), env, frame, labels);
        case NodeKind::DoWhile:
            return exec_do_while(static_cast<const DoWhileStmt*>(body), env, frame, labels);
        case NodeKind::For:
            return exec_for(static_cast<const ForStmt*>(body), env, frame, labels);
        case NodeKind::ForEach:
            return exec_for_each(static_cast<const ForEachStmt*>(body), env, frame, labels);
        default:
            break;
    }

    // Any other statement: only `break label` can target it.
    Flow flow = exec(body, env, frame);
    if (flow == Flow::Break && frame.label) {
        for (const auto* own : labels) {
            if (*own == *frame.label) {
                frame.label = nullptr;
                return Flow::Normal;
            }
        }
    }
    return flow;
}

namespace {
LoopAction loop_action(JsInterpreter*, int flow_value, const std::string*& label,
                       const std::vector<const std::string*>& labels) {
    // flow_value: 0 Normal, 1 Break, 2 Continue, 3 Return
    switch (flow_value) {
        case 0: return LoopAction::Next;
        case 1:
            if (label_matches(label, labels)) { label = nullptr; return LoopAction::Exit; }
            return LoopAction::Propagate;
        case 2:
            if (label_matches(label, labels)) { label = nullptr; return LoopAction::Next; }
            return LoopAction::Propagate;
        default:
            return LoopAction::Propagate;
    }
}
}

JsInterpreter::Flow JsInterpreter::exec_while(const WhileStmt* stmt, const EnvPtr& env, Frame& frame,
                                              const std::vector<const std::string*>& labels) {
    while (evaluate(stmt->condition.get(), env).is_truthy()) {
        Flow flow = exec(stmt->body.get(), env, frame);
        LoopAction action = loop_action(this, static_cast<int>(flow), frame.label, labels);
        if (action == LoopAction::Exit) break;
        if (action == LoopAction::Propagate) return flow;
    }
    return Flow::Normal;
}

JsInterpreter::Flow JsInterpreter::exec_do_while(const DoWhileStmt* stmt, const EnvPtr& env, Frame& frame,
                                                 const std::vector<const std::string*>& labels) {
    do {
        Flow flow = exec(stmt->body.get(), env, frame);
        LoopAction action = loop_action(this, static_cast<int>(flow), frame.label, labels);
        if (action == LoopAction::Exit) break;
        if (action == LoopAction::Propagate) return flow;
    } while (evaluate(stmt->condition.get(), env).is_truthy());
    return Flow::Normal;
}

JsInterpreter::Flow JsInterpreter::exec_for(const ForStmt* stmt, const EnvPtr& env, Frame& frame,
                                            const std::vector<const std::string*>& labels) {
    EnvPtr loop_env = stmt->has_lexical_init ? std::make_shared<JsEnvironment>(env) : env;
    EnvScope loop_scope(stmt->has_lexical_init ? loop_env : nullptr);
    if (stmt->has_lexical_init && stmt->init && stmt->init->kind == NodeKind::VarDecl) {
        auto* var_decl = static_cast<const VarDeclStmt*>(stmt->init.get());
        const bool is_const = var_decl->kind == JsDeclKind::Const;
        std::vector<std::string> names;
        for (const auto& d : var_decl->decls) {
            if (d.target) d.target->collect_names(names);
        }
        for (const auto& name : names) loop_env->declare_lexical(name, is_const);
    }
    if (stmt->init) exec(stmt->init.get(), loop_env, frame);

    // `for (let i ...)` gives every iteration its own `i`, which is what makes
    // closures created in the body see the value from their own iteration.
    const bool per_iteration = !stmt->per_iteration_names.empty();
    EnvPtr iter_env = per_iteration ? copy_scope(loop_env, env) : loop_env;

    while (true) {
        {
            EnvScope iter_scope(per_iteration ? iter_env : nullptr);
            if (stmt->test && !evaluate(stmt->test.get(), iter_env).is_truthy()) break;
            Flow flow = exec(stmt->body.get(), iter_env, frame);
            LoopAction action = loop_action(this, static_cast<int>(flow), frame.label, labels);
            if (action == LoopAction::Exit) break;
            if (action == LoopAction::Propagate) return flow;
        }
        if (per_iteration) {
            EnvPtr next_env = copy_scope(iter_env, env);
            EnvScope next_scope(next_env);
            if (stmt->update) evaluate(stmt->update.get(), next_env);
            iter_env = std::move(next_env);
        } else if (stmt->update) {
            evaluate(stmt->update.get(), iter_env);
        }
    }
    return Flow::Normal;
}

JsInterpreter::Flow JsInterpreter::exec_for_each(const ForEachStmt* stmt, const EnvPtr& env, Frame& frame,
                                                 const std::vector<const std::string*>& labels) {
    JsValue subject;
    if (stmt->declares && stmt->decl_kind != JsDeclKind::Var) {
        auto head_env = std::make_shared<JsEnvironment>(env);
        EnvScope head_scope(head_env);
        const bool is_const = stmt->decl_kind == JsDeclKind::Const;
        std::vector<std::string> names;
        if (stmt->target) stmt->target->collect_names(names);
        for (const auto& name : names) head_env->declare_lexical(name, is_const);
        subject = evaluate(stmt->iterable.get(), head_env);
    } else {
        subject = evaluate(stmt->iterable.get(), env);
    }

    // Binds the loop variable for one iteration and returns the scope the body
    // runs in.
    auto bind_iteration = [&](const JsValue& value) -> EnvPtr {
        if (!stmt->declares) {
            bind_pattern(*stmt->target, value, env, BindMode::Assign);
            return env;
        }
        if (stmt->decl_kind == JsDeclKind::Var) {
            bind_pattern(*stmt->target, value, env, BindMode::Var);
            return env;
        }
        auto iter_env = std::make_shared<JsEnvironment>(env);
        const bool is_const = stmt->decl_kind == JsDeclKind::Const;
        std::vector<std::string> names;
        if (stmt->target) stmt->target->collect_names(names);
        for (const auto& name : names) iter_env->declare_lexical(name, is_const);
        bind_pattern(*stmt->target, value, iter_env, is_const ? BindMode::Const : BindMode::Let);
        return iter_env;
    };

    Flow result = Flow::Normal;
    // Returns true to keep iterating.
    auto run_body = [&](const JsValue& value) -> bool {
        EnvPtr iter_env = bind_iteration(value);
        EnvScope iter_scope(iter_env != env ? iter_env : nullptr);
        Flow flow = exec(stmt->body.get(), iter_env, frame);
        LoopAction action = loop_action(this, static_cast<int>(flow), frame.label, labels);
        if (action == LoopAction::Next) return true;
        if (action == LoopAction::Propagate) result = flow;
        return false;
    };

    if (!stmt->is_of) {
        // for...in: enumerable string keys, own first, then inherited, each once.
        if (subject.is_nullish()) return Flow::Normal;
        std::vector<std::string> keys;
        std::unordered_set<std::string> seen;
        std::vector<std::string> cross_keys;
        const bool cross_origin = litebird::dom::cross_origin_own_keys(subject, true, cross_keys);
        if (cross_origin) {
            for (const auto& key : cross_keys) seen.insert(key);
            for (const auto& key : cross_keys) {
                // The cross-origin table names are not enumerable; only the
                // indices are seen for-in.
                if (!key.empty() && key.find_first_not_of("0123456789") == std::string::npos) keys.push_back(key);
            }
        } else {
            for (const auto& key : subject.own_keys(true)) {
                seen.insert(key);
            }
            for (auto& key : subject.own_keys(false)) keys.push_back(std::move(key));
        }
        int guard = 0;
        // A cross-origin WindowProxy or Location has no prototype (HTML
        // §7.2.3.1), so the chain contributes nothing.
        for (JsObject* proto = (cross_origin || subject.type == JsType::String) ? nullptr
                               : subject.prototype_object();
             proto && guard < kMaxPrototypeChain; proto = proto->prototype, ++guard) {
            JsValue holder = JsValue::make_object(proto);
            std::vector<std::string> all = holder.own_keys(true);
            for (auto& key : holder.own_keys(false)) {
                if (seen.count(key)) continue;
                keys.push_back(key);
            }
            for (auto& key : all) seen.insert(std::move(key));
        }
        for (const auto& key : keys) {
            if (!run_body(JsValue::make_string(key))) break;
        }
        return result;
    }

    if (stmt->is_await && subject.is_object_like() && subject.get_property(kAsyncIteratorKey).is_function()) {
        // for await over an async iterable: next() returns a promise.
        JsValue iterator = subject.get_property(kAsyncIteratorKey).call_with_this(subject, {});
        JsValue next = iterator.get_property("next");
        while (true) {
            JsValue step = eval_await(js_call(next, iterator, {}));
            if (!step.is_object_like()) js_throw_type_error("Iterator result is not an object");
            if (step.get_property("done").is_truthy()) break;
            if (!run_body(step.get_property("value"))) {
                JsValue ret = iterator.get_property("return");
                if (ret.is_function()) eval_await(ret.call_with_this(iterator, {}));
                break;
            }
        }
        return result;
    }

    js_iterate(subject, [&](const JsValue& value) {
        return run_body(stmt->is_await ? eval_await(value) : value);
    });
    return result;
}

JsInterpreter::Flow JsInterpreter::exec_try(const TryStmt* stmt, const EnvPtr& env, Frame& frame) {
    Flow flow = Flow::Normal;
    std::exception_ptr pending;

    auto run_handler = [&](const JsValue& thrown) {
        try {
            if (stmt->param || stmt->handler->has_lexical) {
                auto scope = std::make_shared<JsEnvironment>(env);
                EnvScope env_scope(scope);
                if (stmt->param) {
                    std::vector<std::string> param_names;
                    stmt->param->collect_names(param_names);
                    for (const auto& name : param_names) scope->declare_lexical(name, false);
                    bind_pattern(*stmt->param, thrown, scope, BindMode::Let);
                }
                declare_lexicals(stmt->handler->lexical_decls, scope);
                hoist_functions(stmt->handler->functions, scope);
                flow = exec_list(stmt->handler->statements, scope, frame);
            } else {
                flow = exec_list(stmt->handler->statements, env, frame);
            }
        } catch (...) {
            pending = std::current_exception();     // still has to pass through `finally`
        }
    };

    try {
        flow = exec_block(stmt->block.get(), env, frame);
    } catch (const GeneratorAbortException& abort) {
        // The frame is being closed while suspended in this block. `catch`
        // never sees that. `finally` runs when script asked for the close
        // (iterator.return()), not when the engine is discarding the frame.
        if (stmt->finalizer && abort.run_finalizers) {
            Frame scratch;
            exec_block(stmt->finalizer.get(), env, scratch);
        }
        throw;
    } catch (const GeneratorReturnException&) {
        // `.return(value)` was called by the consumer. `catch` never sees
        // that either; `finally` always runs. What carries the value on is
        // the frame's completion.
        if (stmt->finalizer) {
            Frame scratch;
            exec_block(stmt->finalizer.get(), env, scratch);
        }
        throw;
    } catch (const JsThrow& thrown) {
        if (stmt->has_handler) {
            JsValue value = thrown.value;
            run_handler(value);
        } else {
            pending = std::current_exception();
        }
    } catch (const std::exception& ex) {
        // Errors raised by native code (including stack exhaustion) are
        // catchable like any other.
        if (stmt->has_handler) {
            JsValue value = js_value_from_exception(ex);
            run_handler(value);
        } else {
            pending = std::current_exception();
        }
    }

    if (stmt->finalizer) {
        // The finalizer may contain loops or calls of its own; they must not
        // disturb a `return value` or `break label` that is passing through.
        JsValue saved_return = frame.return_value;
        const std::string* saved_label = frame.label;
        Flow final_flow = exec_block(stmt->finalizer.get(), env, frame);
        if (final_flow != Flow::Normal) return final_flow;     // overrides everything, even a throw
        frame.return_value = std::move(saved_return);
        frame.label = saved_label;
    }
    if (pending) std::rethrow_exception(pending);
    return flow;
}

JsInterpreter::Flow JsInterpreter::exec_switch(const SwitchStmt* stmt, const EnvPtr& env, Frame& frame) {
    JsValue discriminant = evaluate(stmt->discriminant.get(), env);
    auto scope = std::make_shared<JsEnvironment>(env);
    EnvScope env_scope(scope);
    declare_lexicals(stmt->lexical_decls, scope);
    hoist_functions(stmt->functions, scope);

    std::ptrdiff_t selected = -1;
    std::ptrdiff_t default_case = -1;
    for (std::size_t i = 0; i < stmt->cases.size(); ++i) {
        const auto& clause = stmt->cases[i];
        if (!clause.test) {
            default_case = static_cast<std::ptrdiff_t>(i);
            continue;
        }
        if (strict_equals(discriminant, evaluate(clause.test.get(), scope))) {
            selected = static_cast<std::ptrdiff_t>(i);
            break;
        }
    }
    if (selected < 0) selected = default_case;
    if (selected < 0) return Flow::Normal;

    for (std::size_t i = static_cast<std::size_t>(selected); i < stmt->cases.size(); ++i) {
        Flow flow = exec_list(stmt->cases[i].statements, scope, frame);
        if (flow == Flow::Break && !frame.label) return Flow::Normal;   // the switch consumes a plain break
        if (flow != Flow::Normal) return flow;
    }
    return Flow::Normal;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

JsValue JsInterpreter::evaluate_named(const Expr* expr, const EnvPtr& env, const std::string& name_hint) {
    // `const f = function () {}` names the function after the binding.
    if (expr->kind == NodeKind::Function) {
        auto* fn_expr = static_cast<const FunctionExpr*>(expr);
        if (fn_expr->fn->name.empty()) return make_function(fn_expr->fn, env, name_hint);
    } else if (expr->kind == NodeKind::Class) {
        auto* cls_expr = static_cast<const ClassExpr*>(expr);
        if (cls_expr->cls->name.empty()) {
            JsValue ctor = define_class(cls_expr->cls, env);
            return ctor;
        }
    }
    return evaluate(expr, env);
}

std::vector<JsValue> JsInterpreter::eval_arguments(const std::vector<std::unique_ptr<Expr>>& args,
                                                   const EnvPtr& env) {
    std::vector<JsValue> values;
    values.reserve(args.size());
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::Spread) {
            JsValue iterable = evaluate(static_cast<const SpreadExpr*>(arg.get())->argument.get(), env);
            js_iterate(iterable, [&](const JsValue& item) {
                values.push_back(item);
                return true;
            });
        } else {
            values.push_back(evaluate(arg.get(), env));
        }
    }
    return values;
}

JsValue JsInterpreter::apply_binary(JsTokenType op, const JsValue& left, const JsValue& right) {
    switch (op) {
        case JsTokenType::Plus:
            return js_add(left, right);
        case JsTokenType::Minus:
            return JsValue::make_number(js_to_number(left) - js_to_number(right));
        case JsTokenType::Star:
            return JsValue::make_number(js_to_number(left) * js_to_number(right));
        case JsTokenType::Slash:
            return JsValue::make_number(js_to_number(left) / js_to_number(right));
        case JsTokenType::Percent:
            return JsValue::make_number(std::fmod(js_to_number(left), js_to_number(right)));
        case JsTokenType::Power: {
            double base = js_to_number(left);
            double exponent = js_to_number(right);
            // C's pow(1, Infinity) is 1; ECMAScript says NaN.
            if (std::isnan(exponent) || (std::fabs(base) == 1.0 && std::isinf(exponent))) {
                return JsValue::make_number(std::numeric_limits<double>::quiet_NaN());
            }
            return JsValue::make_number(std::pow(base, exponent));
        }
        case JsTokenType::EqualEqual:
            return JsValue::make_boolean(js_loose_equals(left, right));
        case JsTokenType::NotEqual:
            return JsValue::make_boolean(!js_loose_equals(left, right));
        case JsTokenType::StrictEqual:
            return JsValue::make_boolean(strict_equals(left, right));
        case JsTokenType::StrictNotEqual:
            return JsValue::make_boolean(!strict_equals(left, right));
        case JsTokenType::Less:
            return JsValue::make_boolean(js_less_than(left, right) == 1);
        case JsTokenType::Greater:
            return JsValue::make_boolean(js_less_than(right, left) == 1);
        case JsTokenType::LessEqual:
            return JsValue::make_boolean(js_less_than(right, left) == 0);
        case JsTokenType::GreaterEqual:
            return JsValue::make_boolean(js_less_than(left, right) == 0);
        case JsTokenType::Amp:
            return JsValue::make_number(js_to_int32(js_to_number(left)) & js_to_int32(js_to_number(right)));
        case JsTokenType::Pipe:
            return JsValue::make_number(js_to_int32(js_to_number(left)) | js_to_int32(js_to_number(right)));
        case JsTokenType::Caret:
            return JsValue::make_number(js_to_int32(js_to_number(left)) ^ js_to_int32(js_to_number(right)));
        case JsTokenType::ShiftLeft: {
            uint32_t value = js_to_uint32(js_to_number(left));
            uint32_t count = js_to_uint32(js_to_number(right)) & 31u;
            return JsValue::make_number(static_cast<int32_t>(value << count));
        }
        case JsTokenType::ShiftRight: {
            int32_t value = js_to_int32(js_to_number(left));
            uint32_t count = js_to_uint32(js_to_number(right)) & 31u;
            // Arithmetic shift, spelled out so it does not depend on the compiler.
            int32_t shifted = value < 0 ? ~(~value >> count) : value >> count;
            return JsValue::make_number(shifted);
        }
        case JsTokenType::ShiftRightUnsigned: {
            uint32_t value = js_to_uint32(js_to_number(left));
            uint32_t count = js_to_uint32(js_to_number(right)) & 31u;
            return JsValue::make_number(value >> count);
        }
        case JsTokenType::In: {
            if (!right.is_object_like()) {
                js_throw_type_error("Cannot use 'in' operator to search for a key in " + js_describe_thrown(right));
            }
            return JsValue::make_boolean(right.has_property(js_to_property_key(left)));
        }
        case JsTokenType::InstanceOf:
            return JsValue::make_boolean(js_instance_of(left, right));
        default:
            break;
    }
    return JsValue::make_undefined();
}

JsValue JsInterpreter::eval_binary_chain(const BinaryExpr* expr, const EnvPtr& env) {
    // `a + b + c + ...` is ((a + b) + c) + ...: a chain of nodes linked through
    // `left`. Collect the chain, evaluate its innermost operand, then apply
    // the operators on the way back out. The order of evaluation is the same
    // as recursing; the native stack used does not grow with the chain.
    std::vector<const BinaryExpr*> chain;
    chain.reserve(expr->left_chain + 1);
    const BinaryExpr* node = expr;
    while (true) {
        chain.push_back(node);
        if (node->left->kind != NodeKind::Binary) break;
        node = static_cast<const BinaryExpr*>(node->left.get());
    }
    JsValue acc = evaluate(chain.back()->left.get(), env);
    for (size_t i = chain.size(); i-- > 0;) {
        acc = combine_binary(chain[i], std::move(acc), env);
    }
    return acc;
}

JsValue JsInterpreter::eval_binary(const BinaryExpr* expr, const EnvPtr& env) {
    // Short chains recurse (no allocation); long ones are walked in a loop.
    if (expr->left_chain >= 8) return eval_binary_chain(expr, env);
    return combine_binary(expr, evaluate(expr->left.get(), env), env);
}

/// Finishes one binary operator whose left operand is already evaluated.
JsValue JsInterpreter::combine_binary(const BinaryExpr* expr, JsValue left, const EnvPtr& env) {
    // Short-circuit operators evaluate their right side only when needed and
    // yield an operand, not a boolean.
    if (expr->op == JsTokenType::PipePipe) {
        if (left.is_truthy()) return left;
        return evaluate(expr->right.get(), env);
    }
    if (expr->op == JsTokenType::AmpAmp) {
        if (!left.is_truthy()) return left;
        return evaluate(expr->right.get(), env);
    }
    if (expr->op == JsTokenType::Nullish) {
        if (!left.is_nullish()) return left;
        return evaluate(expr->right.get(), env);
    }
    JsValue right = evaluate(expr->right.get(), env);
    if (left.type == JsType::Number && right.type == JsType::Number) {
        // The hot path for loops and arithmetic.
        const double a = left.number_val;
        const double b = right.number_val;
        switch (expr->op) {
            case JsTokenType::Plus: return JsValue::make_number(a + b);
            case JsTokenType::Minus: return JsValue::make_number(a - b);
            case JsTokenType::Star: return JsValue::make_number(a * b);
            case JsTokenType::Slash: return JsValue::make_number(a / b);
            case JsTokenType::Less: return JsValue::make_boolean(a < b);
            case JsTokenType::Greater: return JsValue::make_boolean(a > b);
            case JsTokenType::LessEqual: return JsValue::make_boolean(a <= b);
            case JsTokenType::GreaterEqual: return JsValue::make_boolean(a >= b);
            case JsTokenType::StrictEqual:
            case JsTokenType::EqualEqual: return JsValue::make_boolean(a == b);
            case JsTokenType::StrictNotEqual:
            case JsTokenType::NotEqual: return JsValue::make_boolean(a != b);
            default: break;
        }
    }
    return apply_binary(expr->op, left, right);
}

JsValue JsInterpreter::eval_unary(const UnaryExpr* expr, const EnvPtr& env) {
    const Expr* operand = expr->operand.get();
    switch (expr->op) {
        case JsTokenType::Typeof: {
            // `typeof neverDeclared` is "undefined", not a ReferenceError.
            if (operand->kind == NodeKind::Identifier) {
                const std::string& name = static_cast<const IdentifierExpr*>(operand)->name;
                if (!env->lookup(name) && !m_global_object.has_property(name)) {
                    return js_typeof_value(JsValue::make_undefined());
                }
            }
            return js_typeof_value(evaluate(operand, env));
        }
        case JsTokenType::Delete: {
            if (operand->kind == NodeKind::Member || operand->kind == NodeKind::Index) {
                JsValue base;
                std::string key;
                if (operand->kind == NodeKind::Member) {
                    auto* member = static_cast<const MemberExpr*>(operand);
                    base = evaluate(member->object.get(), env);
                    key = member->property;
                } else {
                    auto* index = static_cast<const IndexExpr*>(operand);
                    base = evaluate(index->object.get(), env);
                    key = js_to_property_key(evaluate(index->index.get(), env));
                }
                if (base.is_nullish()) {
                    js_throw_type_error("Cannot convert undefined or null to object");
                }
                return JsValue::make_boolean(base.delete_property(key));
            }
            if (operand->kind == NodeKind::Identifier) return JsValue::make_boolean(false);
            evaluate(operand, env);
            return JsValue::make_boolean(true);
        }
        case JsTokenType::Void:
            evaluate(operand, env);
            return JsValue::make_undefined();
        case JsTokenType::Bang:
            return JsValue::make_boolean(!evaluate(operand, env).is_truthy());
        case JsTokenType::Minus:
            return JsValue::make_number(-js_to_number(evaluate(operand, env)));
        case JsTokenType::Plus:
            return JsValue::make_number(js_to_number(evaluate(operand, env)));
        case JsTokenType::Tilde:
            return JsValue::make_number(~js_to_int32(js_to_number(evaluate(operand, env))));
        default:
            return evaluate(operand, env);
    }
}

JsValue JsInterpreter::eval_assign(const AssignExpr* expr, const EnvPtr& env) {
    if (expr->pattern) {
        JsValue value = evaluate(expr->value.get(), env);
        bind_pattern(*expr->pattern, value, env, BindMode::Assign);
        return value;
    }

    const Expr* target = expr->target.get();
    if (expr->op == JsTokenType::Equal) {
        if (target->kind == NodeKind::Identifier) {
            const std::string& name = static_cast<const IdentifierExpr*>(target)->name;
            JsValue value = evaluate_named(expr->value.get(), env, name);
            assign_identifier(name, value, env);
            return value;
        }
        // The object and key are evaluated before the right-hand side.
        Reference ref = resolve_reference(target, env);
        JsValue value = evaluate(expr->value.get(), env);
        write_reference(ref, value, env);
        return value;
    }

    Reference ref = resolve_reference(target, env);
    JsValue current = read_reference(ref, env);

    // Logical assignment only evaluates and stores when it has to.
    if (expr->op == JsTokenType::AmpAmpEqual || expr->op == JsTokenType::PipePipeEqual ||
        expr->op == JsTokenType::NullishEqual) {
        const bool assign = expr->op == JsTokenType::AmpAmpEqual ? current.is_truthy()
                          : expr->op == JsTokenType::PipePipeEqual ? !current.is_truthy()
                          : current.is_nullish();
        if (!assign) return current;
        JsValue value = evaluate(expr->value.get(), env);
        write_reference(ref, value, env);
        return value;
    }

    JsValue right = evaluate(expr->value.get(), env);
    JsValue result = apply_binary(binary_operator_for(expr->op), current, right);
    write_reference(ref, result, env);
    return result;
}

JsValue JsInterpreter::eval_call(const CallExpr* expr, const EnvPtr& env) {
    const Expr* callee = expr->callee.get();
    JsValue this_val;
    JsValue fn;
    const std::string* callee_name = nullptr;
    std::string computed_name;

    switch (callee->kind) {
        case NodeKind::Member: {
            auto* member = static_cast<const MemberExpr*>(callee);
            JsValue base = evaluate(member->object.get(), env);
            if (m_chain_short_circuit) return JsValue::make_undefined();
            if (member->optional && base.is_nullish()) {
                m_chain_short_circuit = true;
                return JsValue::make_undefined();
            }
            fn = member->private_name ? read_private_member(base, member->property, env)
                                      : get_member(base, member->property);
            this_val = std::move(base);
            callee_name = &member->property;
            break;
        }
        case NodeKind::Index: {
            auto* index = static_cast<const IndexExpr*>(callee);
            JsValue base = evaluate(index->object.get(), env);
            if (m_chain_short_circuit) return JsValue::make_undefined();
            if (index->optional && base.is_nullish()) {
                m_chain_short_circuit = true;
                return JsValue::make_undefined();
            }
            computed_name = js_to_property_key(evaluate(index->index.get(), env));
            fn = get_member(base, computed_name);
            this_val = std::move(base);
            callee_name = &computed_name;
            break;
        }
        case NodeKind::SuperMember: {
            auto* super_member = static_cast<const SuperMemberExpr*>(callee);
            this_val = lookup_identifier("this", env);
            computed_name = super_member->computed
                ? js_to_property_key(evaluate(super_member->computed.get(), env))
                : super_member->property;
            JsValue base = super_base(env);
            fn = get_with_receiver(base.property_cell(), computed_name, this_val);
            callee_name = &computed_name;
            break;
        }
        default:
            fn = evaluate(callee, env);
            if (m_chain_short_circuit) return JsValue::make_undefined();
            if (callee->kind == NodeKind::Identifier) {
                callee_name = &static_cast<const IdentifierExpr*>(callee)->name;
            }
            break;
    }

    if (expr->optional && fn.is_nullish()) {
        m_chain_short_circuit = true;
        return JsValue::make_undefined();
    }

    std::vector<JsValue> args = eval_arguments(expr->args, env);
    if (!fn.is_function()) {
        js_throw_type_error((callee_name ? *callee_name : std::string("expression")) + " is not a function");
    }
    // Whether `eval` is the direct one is decided here, not inside it: the
    // standard asks whether the callee reference is the identifier `eval`,
    // bound to the function the realm installed, and that is exactly a callee
    // written as a plain identifier named `eval`. `(0, eval)(x)`,
    // `window.eval(x)` and `var e = eval; e(x)` arrive by another kind of
    // expression -- or, for the alias, by a callee that is an identifier of
    // another name -- and are indirect: global code. A function of its own
    // that a script calls `eval` is compared by identity, so shadowing the
    // name does not make its calls direct, and reassigning the global one
    // does not either.
    const bool is_eval = callee->kind == NodeKind::Identifier &&
                         static_cast<const IdentifierExpr*>(callee)->name == "eval";
    const bool direct = !expr->optional && is_eval && m_eval_function.is_function() &&
                        strict_equals(m_eval_function, fn);
    // A frame is pushed only where one can matter: for the direct call itself,
    // and -- inside an eval's extent, where the stack is not empty -- for every
    // other call too, so that an indirect eval farther in can mask the direct
    // one it runs inside. Outside any eval the stack stays empty and a call
    // expression costs nothing.
    std::vector<EvalFrame>& stack = eval_call_stack();
    if (direct || !stack.empty()) {
        stack.push_back(EvalFrame{env.get(), direct,
                                  direct && expr->direct_eval_allows_super_property,
                                  direct && expr->direct_eval_allows_super_call});
        struct Pop {
            std::vector<EvalFrame>& stack;
            ~Pop() { stack.pop_back(); }
        } pop{stack};
        return fn.call_with_this(this_val, args);
    }
    return fn.call_with_this(this_val, args);
}

JsValue JsInterpreter::eval_new(const NewExpr* expr, const EnvPtr& env) {
    JsValue ctor = evaluate(expr->callee.get(), env);
    std::vector<JsValue> args = eval_arguments(expr->args, env);
    if (!ctor.is_function()) {
        std::string name = expr->callee->kind == NodeKind::Identifier
            ? static_cast<const IdentifierExpr*>(expr->callee.get())->name
            : js_describe_thrown(ctor);
        js_throw_type_error(name + " is not a constructor");
    }
    return js_construct(ctor, args);
}

JsValue JsInterpreter::eval_object_literal(const ObjectLiteralExpr* expr, const EnvPtr& env) {
    JsValue object = JsValue::make_object();
    for (const auto& prop : expr->properties) {
        if (prop.kind == ObjectProperty::Kind::Spread) {
            // {...source}: own enumerable properties, in order.
            JsValue source = evaluate(prop.value.get(), env);
            if (source.is_nullish()) continue;
            std::vector<std::string> spread_keys;
            if (!litebird::dom::cross_origin_own_keys(source, false, spread_keys))
                spread_keys = source.own_keys();
            for (const auto& key : spread_keys) {
                object.set_property(key, source.get_property(key));
            }
            continue;
        }

        std::string key = prop.computed_key
            ? js_to_property_key(evaluate(prop.computed_key.get(), env))
            : prop.key;

        if (prop.kind == ObjectProperty::Kind::Getter || prop.kind == ObjectProperty::Kind::Setter) {
            auto* fn_expr = static_cast<const FunctionExpr*>(prop.value.get());
            JsValue fn = make_function(fn_expr->fn, env, key);
            if (fn_expr->fn->uses_super) fn.define_property("%home", object, PropertyMap::Hidden);
            JsValue getter;
            JsValue setter;
            auto existing = object.object_val->properties.find(key);
            if (existing != object.object_val->properties.end() && (existing->flags & PropertyMap::Accessor)) {
                getter = existing->second;
                if (existing->setter) setter = *existing->setter;
            }
            if (prop.kind == ObjectProperty::Kind::Getter) getter = fn;
            else setter = fn;
            object.define_accessor(key, getter, setter, PropertyMap::Enumerable | PropertyMap::Configurable);
            continue;
        }

        if (prop.is_method) {
            auto* fn_expr = static_cast<const FunctionExpr*>(prop.value.get());
            JsValue fn = make_function(fn_expr->fn, env, key);
            if (fn_expr->fn->uses_super) fn.define_property("%home", object, PropertyMap::Hidden);
            object.define_property(key, fn, PropertyMap::Default);
            continue;
        }

        // `{ __proto__: base }` sets the prototype rather than a property, so
        // a function or a class written there is not named after the key.
        const bool sets_prototype = key == "__proto__" && !prop.computed_key && !prop.shorthand;
        JsValue value = sets_prototype ? evaluate(prop.value.get(), env)
                                       : evaluate_named(prop.value.get(), env, key);
        if (sets_prototype) {
            if (value.is_object_like()) {
                if (JsObject* target = value.as_prototype()) object.object_val->prototype = target;
            } else if (value.type == JsType::Null) {
                object.object_val->prototype = nullptr;
            }
            continue;
        }
        object.define_property(key, value, PropertyMap::Default);
    }
    return object;
}

JsValue JsInterpreter::eval_yield(const YieldExpr* expr, const EnvPtr& env) {
    auto* generator = GeneratorContext::get_active();
    if (!generator || generator->is_coroutine) {
        js_throw_error(JsErrorKind::SyntaxError, "yield is only valid in generator functions");
    }

    const bool async_gen = generator->is_async;

    // Hands a value to the consumer and waits to be resumed. throw() on the
    // generator object surfaces here, inside the body's own try/catch.
    auto suspend = [&](const JsValue& value) -> JsValue {
        JsValue sent = generator->yield_to_main(value);
        JsValue error;
        if (generator->take_pending_throw(error)) js_throw(error);
        // A return completion: unwind as a return, past every `catch`,
        // through every `finally`. The value of an async generator's return
        // completion is awaited at the resumption boundary first
        // (AsyncGeneratorUnwrapYieldResumption) — a rejected one is raised
        // at the yield point, where the body's own `catch` can see it.
        JsValue ret;
        if (generator->take_pending_return(ret)) {
            if (async_gen) ret = eval_await(ret);
            throw GeneratorReturnException(std::move(ret));
        }
        return sent;
    };

    if (expr->delegate) {
        JsValue iterable = expr->argument ? evaluate(expr->argument.get(), env) : JsValue::make_undefined();

        // Which protocol: in an async generator, the async one — a sync
        // iterator wrapped in an async one when the value has no `@@asyncIterator`
        // (CreateAsyncFromSyncIterator) — the sync one everywhere else.
        JsValue iterator;
        if (async_gen) {
            JsValue method =
                iterable.is_nullish() ? JsValue::make_undefined() : iterable.get_property(kAsyncIteratorKey);
            if (method.is_nullish()) {
                // Absent: GetMethod returned undefined, so the sync protocol
                // applies and a sync iterator is wrapped in an async one.
                JsValue sync_method =
                    iterable.is_nullish() ? JsValue::make_undefined() : iterable.get_property(kIteratorKey);
                if (!sync_method.is_function())
                    js_throw_type_error(js_describe_thrown(iterable) + " is not iterable");
                JsValue sync_iterator = js_call(sync_method, iterable, {});
                if (!sync_iterator.is_object_like()) js_throw_type_error("Iterator is not an object");
                iterator = js_make_async_from_sync(sync_iterator);
            } else if (method.is_function()) {
                iterator = js_call(method, iterable, {});
            } else {
                // GetMethod throws when the property exists but is not a
                // function — this is not "no asyncIterator", it is a TypeError.
                js_throw_type_error("Symbol.asyncIterator is not a function");
            }
        } else {
            JsValue method =
                iterable.is_nullish() ? JsValue::make_undefined() : iterable.get_property(kIteratorKey);
            if (!method.is_function())
                js_throw_type_error(js_describe_thrown(iterable) + " is not iterable");
            iterator = js_call(method, iterable, {});
        }
        if (!iterator.is_object_like()) js_throw_type_error("Iterator is not an object");
        // GetIteratorDirect: the next method is read exactly once, here,
        // before any of it runs.
        JsValue next_method = iterator.get_property("next");

        // IteratorClose for the delegated iterator, called when the frame is
        // being aborted (an outer `return()`, or a for-of that broke out):
        // the delegate is closed before the body's own finally blocks run.
        // Errors from `return` are swallowed — the completion that caused
        // the close wins — and a throwing `return` getter is treated as
        // "not callable", so an abort is never masked.
        auto close_inner = [&]() {
            JsValue ret;
            JsValue error;
            if (!js_capture_throw([&] { ret = iterator.get_property("return"); }, error)) return;
            if (!ret.is_function()) return;
            js_capture_throw([&] {
                JsValue closed = js_call(ret, iterator, {});
                if (async_gen) eval_await(closed);
            }, error);
        };

        // IteratorClose for the script-visible cleanup in the yield* throw
        // path: an abrupt get of `return`, an abrupt call, or a rejected
        // promise out of it propagates in place of the normal completion the
        // close was made with (the injected throw is then never raised).
        auto close_inner_propagating = [&]() {
            JsValue ret = iterator.get_property("return");
            if (ret.is_nullish()) return;
            if (!ret.is_function()) js_throw_type_error("Iterator return is not callable.");
            JsValue closed = js_call(ret, iterator, {});
            if (async_gen) closed = eval_await(closed);
            if (!closed.is_object_like()) js_throw_type_error("Iterator result is not an object.");
        };

        // The sent value flows into the delegate; the delegate's return value
        // is the value of the `yield*` expression; an injected throw is the
        // delegate's to receive (its `throw` method), not this body's.
        JsValue sent;    // undefined: the first next() is called with no argument
        bool first = true;
        try {
            for (;;) {
                JsValue pending;
                JsValue step;
                if (generator->take_pending_throw(pending)) {
                    // Throw into the delegate, not into this body. GetMethod
                    // semantics: a throwing `throw` getter propagates; an
                    // absent (or null) `throw` closes the iterator and then
                    // raises a TypeError.
                    JsValue thrown_method = iterator.get_property("throw");
                    if (thrown_method.is_nullish()) {
                        close_inner_propagating();
                        js_throw_type_error("The iterator does not provide a 'throw' method.");
                    }
                    if (!thrown_method.is_function()) js_throw_type_error("Iterator throw is not callable.");
                    step = js_call(thrown_method, iterator, {pending});
                } else if (generator->take_pending_return(pending)) {
                    // A return completion at the yield point: the value of an
                    // async generator's return is awaited first
                    // (AsyncGeneratorUnwrapYieldResumption); a rejected one
                    // is raised at the yield point, in this body.
                    JsValue received = async_gen ? eval_await(std::move(pending)) : std::move(pending);
                    JsValue return_method = iterator.get_property("return");
                    if (return_method.is_nullish()) {
                        // ReturnCompletion(received): an absent `return`
                        // completes the `yield*` with the received value.
                        throw GeneratorReturnException(std::move(received));
                    }
                    if (!return_method.is_function()) js_throw_type_error("Iterator return is not callable.");
                    JsValue return_result = js_call(return_method, iterator, {received});
                    if (async_gen) return_result = eval_await(std::move(return_result));
                    if (!return_result.is_object_like())
                        js_throw_type_error("Iterator result is not an object.");
                    const bool ret_done = return_result.get_property("done").is_truthy();
                    if (ret_done) {
                        // IteratorValue: the yield* completes with the value.
                        JsValue ret_value = return_result.get_property("value");   // abrupt propagates
                        throw GeneratorReturnException(std::move(ret_value));
                    }
                    // A yield again: an async generator must read the value
                    // (AsyncGeneratorYield(? IteratorValue(...))); a sync one
                    // hands the raw return-result object out untouched
                    // (GeneratorYield), and reads nothing more off it.
                    if (async_gen) {
                        JsValue ret_value = return_result.get_property("value");   // abrupt propagates
                        sent = generator->yield_to_main(std::move(ret_value));
                    } else {
                        sent = generator->yield_to_main(std::move(return_result), /*raw=*/true);
                    }
                    first = false;
                    continue;
                } else {
                    step = js_call(next_method, iterator,
                                   std::vector<JsValue>{first ? JsValue::make_undefined() : sent});
                }
                first = false;
                // Await the step promise (or, for a non-promise from a
                // hand-written sync iterator, take the object as it is).
                // The step's *value* is never awaited here: it crosses to
                // the consumer as it is.
                if (async_gen) step = eval_await(step);
                if (!step.is_object_like()) js_throw_type_error("Iterator result is not an object.");
                // IteratorComplete reads `done` — both kinds read it here.
                const bool done = step.get_property("done").is_truthy();
                if (done) return step.get_property("value");   // IteratorValue; a throwing getter propagates
                // A yield: an async generator hands the consumer a value
                // (never the promise); a sync one hands out the raw step
                // object, untouched, and reads nothing more off it.
                if (async_gen) {
                    sent = generator->yield_to_main(step.get_property("value"));
                } else {
                    sent = generator->yield_to_main(std::move(step), /*raw=*/true);
                }
            }
        } catch (const GeneratorAbortException&) {
            close_inner();
            throw;
        }
    }

    JsValue value = expr->argument ? evaluate(expr->argument.get(), env) : JsValue::make_undefined();
    // A direct `yield` in an async generator awaits its operand first; the
    // suspension carries the settled value. (An `await` inside a generator
    // is a blocking wait in this engine — see eval_await — so a thenable
    // whose settlement depends on the very `next()` that reached the yield
    // cannot be awaited here.)
    if (async_gen) value = eval_await(value);
    return suspend(value);
}

JsValue JsInterpreter::eval_await(const JsValue& value) {
    // Inside an async function: park this frame. drive_coroutine() resumes it,
    // from a microtask, with whatever the awaited value settles to.
    if (auto* frame = GeneratorContext::get_active(); frame && frame->is_coroutine) {
        JsValue settled = frame->yield_to_main(value);
        JsValue error;
        if (frame->take_pending_throw(error)) js_throw(error);
        return settled;
    }

    // Anywhere else (the top level of a script, an async generator body) there
    // is no frame to park, so the wait is a blocking one: run the event loop
    // here until the promise settles.
    auto promise = Promise::to_promise(value);
    promise->handled = true;
    auto& loop = EventLoop::default_loop();
    while (promise->state == PromiseState::Pending) {
        if (loop.spin_once() == 0) {
            if (!loop.has_pending_tasks()) {
                // Nothing left that could ever settle it.
                js_throw_type_error("await: the promise can never settle, nothing is pending");
            }
            loop.wait_for_work(false);
        }
    }
    if (promise->state == PromiseState::Rejected) js_throw(promise->result);
    return promise->result;
}

JsValue JsInterpreter::eval_tagged_template(const TaggedTemplateExpr* expr, const EnvPtr& env) {
    JsValue this_val;
    JsValue fn;
    const Expr* tag = expr->tag.get();
    if (tag->kind == NodeKind::Member) {
        auto* member = static_cast<const MemberExpr*>(tag);
        this_val = evaluate(member->object.get(), env);
        fn = get_member(this_val, member->property);
    } else {
        fn = evaluate(tag, env);
    }

    std::vector<JsValue> cooked;
    std::vector<JsValue> raw;
    for (const auto& s : expr->cooked) cooked.push_back(JsValue::make_string(s));
    for (const auto& s : expr->raw) raw.push_back(JsValue::make_string(s));
    JsValue strings = JsValue::make_array(cooked);
    strings.define_property("raw", JsValue::make_array(raw), PropertyMap::Hidden);

    std::vector<JsValue> args;
    args.push_back(strings);
    for (const auto& e : expr->expressions) args.push_back(evaluate(e.get(), env));
    return js_call(fn, this_val, args);
}

JsValue JsInterpreter::eval_update(const UpdateExpr* update, const EnvPtr& env) {
    // The target is resolved once, so `a[i++]++` evaluates `i++` once.
    Reference ref = resolve_reference(update->target.get(), env);
    double before = js_to_number(read_reference(ref, env));
    double after = before + (update->op == JsTokenType::PlusPlus ? 1.0 : -1.0);
    write_reference(ref, JsValue::make_number(after), env);
    return JsValue::make_number(update->prefix ? after : before);
}

JsValue JsInterpreter::eval_member(const MemberExpr* member, const EnvPtr& env) {
    JsValue base = evaluate(member->object.get(), env);
    if (m_chain_short_circuit) return JsValue::make_undefined();
    if (member->optional && base.is_nullish()) {
        m_chain_short_circuit = true;
        return JsValue::make_undefined();
    }
    if (member->private_name) return read_private_member(base, member->property, env);
    return get_member(base, member->property);
}

JsValue JsInterpreter::eval_index(const IndexExpr* index, const EnvPtr& env) {
    JsValue base = evaluate(index->object.get(), env);
    if (m_chain_short_circuit) return JsValue::make_undefined();
    if (index->optional && base.is_nullish()) {
        m_chain_short_circuit = true;
        return JsValue::make_undefined();
    }
    JsValue key = evaluate(index->index.get(), env);
    // Fast path: array[number].
    if (key.type == JsType::Number && base.is_array()) {
        double n = key.number_val;
        const auto& elements = base.object_val->array_elements;
        if (n >= 0 && n < static_cast<double>(elements.size()) && n == std::floor(n)) {
            return elements[static_cast<size_t>(n)];
        }
    }
    return get_member(base, js_to_property_key(key));
}

JsValue JsInterpreter::eval_private_in(const PrivateInExpr* private_in, const EnvPtr& env) {
    // `#p in obj`: is this object branded with the name? A private name is not
    // a property, so this is the one way to ask without reading.
    JsValue object = evaluate(private_in->object.get(), env);
    // Only an object can carry a brand: `#p in 1` is a TypeError, not `false`
    // (a private name is not a property, so `in` is not the HasProperty it is
    // for strings).
    if (!object.is_object_like()) {
        js_throw_type_error("Cannot use 'in' operator to search for '" + private_in->name + "' in " +
                            js_describe_thrown(object));
    }
    return JsValue::make_boolean(object.has_private(private_key_for(private_in->name, env)));
}

JsValue JsInterpreter::eval_sequence(const SequenceExpr* sequence, const EnvPtr& env) {
    JsValue last;
    for (const auto& item : sequence->expressions) {
        last = evaluate(item.get(), env);
    }
    return last;
}

JsValue JsInterpreter::eval_array_literal(const ArrayLiteralExpr* literal, const EnvPtr& env) {
    std::vector<JsValue> elements;
    std::vector<size_t> holes;      // an elision is a hole, not an element
    for (const auto& element : literal->elements) {
        if (!element) {
            holes.push_back(elements.size());
            elements.push_back(JsValue::make_undefined());
        } else if (element->kind == NodeKind::Spread) {
            JsValue iterable = evaluate(static_cast<const SpreadExpr*>(element.get())->argument.get(), env);
            js_iterate(iterable, [&](const JsValue& item) {
                elements.push_back(item);
                return true;
            });
        } else {
            elements.push_back(evaluate(element.get(), env));
        }
    }
    JsValue array = JsValue::make_array(elements);
    // `[1, , 3]` has three places and two elements. Marked after the array is
    // made, because a spread in the literal may have added places that shift the
    // ones after it.
    for (size_t index : holes) array.set_array_hole(index);
    return array;
}

JsValue JsInterpreter::eval_template_literal(const TemplateLiteralExpr* literal, const EnvPtr& env) {
    // parts alternates text, substitution, text, ..., text. Position is what
    // tells them apart: a substitution can itself be a literal (`${200000}`,
    // `${true}`), and that has to be converted, not read as if it were a piece
    // of the template's own text.
    const auto& parts = literal->parts;
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i % 2 == 0) {
            result += static_cast<const LiteralExpr*>(parts[i].get())->value.string_val.view();
        } else {
            JsValue part = evaluate(parts[i].get(), env);
            if (part.type == JsType::String) result += part.string_val.view();
            else result += js_to_string(part);
        }
    }
    return JsValue::make_string(result);
}

JsValue JsInterpreter::eval_super_member(const SuperMemberExpr* super_member, const EnvPtr& env) {
    JsValue receiver = lookup_identifier("this", env);
    std::string key = super_member->computed ? js_to_property_key(evaluate(super_member->computed.get(), env))
                                             : super_member->property;
    JsValue base = super_base(env);
    return get_with_receiver(base.property_cell(), key, receiver);
}

JsValue JsInterpreter::evaluate(const Expr* expr, const EnvPtr& env) {
    // Expression evaluation is itself recursive: `1 + (1 + (1 + ...))` and
    // `return 1 + f(n - 1)` both nest evaluate() once per level, so the stack
    // guard has to live here and not only at call sites.
    CallDepthGuard depth_guard(*this);

    switch (expr->kind) {
        case NodeKind::Literal:
            return static_cast<const LiteralExpr*>(expr)->value;

        case NodeKind::Identifier: {
            auto* id = static_cast<const IdentifierExpr*>(expr);
            if (id->is_undefined && !JsEnvironment::undefined_shadowed()) return JsValue::make_undefined();
            return lookup_identifier(id->name, env);
        }

        case NodeKind::Binary:
            return eval_binary(static_cast<const BinaryExpr*>(expr), env);

        case NodeKind::Unary:
            return eval_unary(static_cast<const UnaryExpr*>(expr), env);

        case NodeKind::Assign:
            return eval_assign(static_cast<const AssignExpr*>(expr), env);

        case NodeKind::Update:
            return eval_update(static_cast<const UpdateExpr*>(expr), env);

        case NodeKind::Conditional: {
            auto* cond = static_cast<const ConditionalExpr*>(expr);
            return evaluate(cond->test.get(), env).is_truthy() ? evaluate(cond->consequent.get(), env)
                                                               : evaluate(cond->alternate.get(), env);
        }

        case NodeKind::Sequence:
            return eval_sequence(static_cast<const SequenceExpr*>(expr), env);

        case NodeKind::Member:
            return eval_member(static_cast<const MemberExpr*>(expr), env);

        case NodeKind::PrivateIn:
            return eval_private_in(static_cast<const PrivateInExpr*>(expr), env);

        case NodeKind::Index:
            return eval_index(static_cast<const IndexExpr*>(expr), env);

        case NodeKind::OptionalChain: {
            // A short-circuit inside the chain ends the whole chain here.
            FlagScope scope(m_chain_short_circuit, false);
            JsValue value = evaluate(static_cast<const OptionalChainExpr*>(expr)->expression.get(), env);
            return m_chain_short_circuit ? JsValue::make_undefined() : value;
        }

        case NodeKind::Call:
            return eval_call(static_cast<const CallExpr*>(expr), env);

        case NodeKind::New:
            return eval_new(static_cast<const NewExpr*>(expr), env);

        case NodeKind::ObjectLiteral:
            return eval_object_literal(static_cast<const ObjectLiteralExpr*>(expr), env);

        case NodeKind::ArrayLiteral:
            return eval_array_literal(static_cast<const ArrayLiteralExpr*>(expr), env);

        case NodeKind::TemplateLiteral:
            return eval_template_literal(static_cast<const TemplateLiteralExpr*>(expr), env);

        case NodeKind::TaggedTemplate:
            return eval_tagged_template(static_cast<const TaggedTemplateExpr*>(expr), env);

        case NodeKind::Function: {
            auto* fn_expr = static_cast<const FunctionExpr*>(expr);
            const auto& node = fn_expr->fn;
            if (!node->name.empty() && !node->is_arrow && !node->is_method) {
                // A named function expression can call itself by name even if
                // nothing outside binds that name.
                auto scope = std::make_shared<JsEnvironment>(env);
                JsValue fn = make_function(node, scope);
                // The inner name of a named function expression is an
                // immutable binding: sloppy writes are ignored and strict
                // writes throw, including writes made by direct eval.
                scope->define_readonly(node->name, fn);
                return fn;
            }
            return make_function(node, env);
        }

        case NodeKind::Class:
            return define_class(static_cast<const ClassExpr*>(expr)->cls, env);

        case NodeKind::Regex: {
            auto* regex = static_cast<const RegexLiteralExpr*>(expr);
            return js_regexp_create(regex->pattern, regex->flags);
        }

        case NodeKind::Yield:
            return eval_yield(static_cast<const YieldExpr*>(expr), env);

        case NodeKind::Await:
            return eval_await(evaluate(static_cast<const AwaitExpr*>(expr)->expression.get(), env));

        case NodeKind::SuperCall:
            return eval_super_call(static_cast<const SuperCallExpr*>(expr), env);

        case NodeKind::SuperMember:
            return eval_super_member(static_cast<const SuperMemberExpr*>(expr), env);

        case NodeKind::NewTarget: {
            auto* binding = env->lookup("%new.target");
            return binding ? binding->value : JsValue::make_undefined();
        }

        case NodeKind::Spread:
            js_throw_error(JsErrorKind::SyntaxError, "Unexpected spread element");

        default:
            break;
    }
    return JsValue::make_undefined();
}

// ---------------------------------------------------------------------------
// Legacy entry point
// ---------------------------------------------------------------------------

JsValue JsInterpreter::execute(const AstNode* node, std::shared_ptr<JsEnvironment> env) {
    if (!node) return JsValue::make_undefined();
    EnvScope env_scope(env);

    switch (node->kind) {
        case NodeKind::FunctionNode:
        case NodeKind::ClassNode:
            return JsValue::make_undefined();
        default:
            break;
    }

    if (auto* stmt = dynamic_cast<const Stmt*>(node)) {
        Frame frame;
        frame.track_completion = true;
        Flow flow;
        if (stmt->kind == NodeKind::Block) {
            auto* block = static_cast<const BlockStmt*>(stmt);
            declare_lexicals(block->lexical_decls, env);
            hoist_functions(block->functions, env);
            flow = exec_list(block->statements, env, frame);
        } else {
            flow = exec(stmt, env, frame);
        }
        return flow == Flow::Return ? std::move(frame.return_value) : std::move(frame.completion);
    }
    if (auto* expr = dynamic_cast<const Expr*>(node)) return evaluate(expr, env);
    return JsValue::make_undefined();
}

} // namespace litebird
