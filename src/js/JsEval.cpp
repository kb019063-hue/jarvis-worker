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
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Generator.hpp"
#include "litebird/js/Promise.hpp"
#include <chrono>
#include <cmath>
#include <limits>
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

struct EnvScope {
    std::shared_ptr<JsEnvironment>& slot;
    std::shared_ptr<JsEnvironment> saved;
    EnvScope(std::shared_ptr<JsEnvironment>& s, const std::shared_ptr<JsEnvironment>& value) : slot(s), saved(s) {
        slot = value;
    }
    ~EnvScope() { slot = std::move(saved); }
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
/// what `super.x` needs.
JsValue get_with_receiver(JsObject* start, const std::string& key, const JsValue& receiver) {
    int guard = 0;
    for (JsObject* obj = start; obj && guard < 10000; obj = obj->prototype, ++guard) {
        auto it = obj->properties.find(key);
        if (it == obj->properties.end()) continue;
        if (it->flags & PropertyMap::Accessor) {
            if (it->second.is_function()) return it->second.call_with_this(receiver, {});
            return JsValue::make_undefined();
        }
        return it->second;
    }
    return JsValue::make_undefined();
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

std::shared_ptr<JsEnvironment> copy_scope(const std::shared_ptr<JsEnvironment>& source,
                                          const std::shared_ptr<JsEnvironment>& parent) {
    auto copy = std::make_shared<JsEnvironment>(parent);
    for (const auto& binding : source->bindings()) {
        if (binding.is_const) copy->define_const(binding.name, binding.value);
        else copy->define(binding.name, binding.value);
    }
    return copy;
}

} // namespace

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

JsValue JsInterpreter::run(std::string_view source) {
    JsLexer lexer(source);
    auto tokens = lexer.tokenize_all();
    JsParser parser(std::move(tokens));
    auto program = parser.parse_program();
    return run_program(*program);
}

JsValue JsInterpreter::run_program(const Program& program) {
    CallDepthGuard depth_guard(*this);
    FlagScope strict_scope(m_strict, program.is_strict);
    EnvScope env_scope(m_current_env, m_global_env);

    // `var` and function declarations exist before the first statement runs.
    for (const auto& name : program.var_names) {
        if (!m_global_env->has_own(name)) m_global_env->define(name, JsValue::make_undefined());
    }
    hoist_functions(program.functions, m_global_env);

    Frame frame;
    frame.track_completion = true;
    Flow flow = exec_list(program.statements, m_global_env, frame);
    if (flow == Flow::Return) return std::move(frame.return_value);
    return std::move(frame.completion);
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
    EnvPtr closure = env;
    fd.native_fn = [this, shared_node, closure](const std::vector<JsValue>& args) {
        JsValue this_val;
        JsValue callee;
        bool is_construct = false;
        JsValue::take_call_context(this_val, is_construct, &callee);
        return invoke_function(shared_node, closure, callee, this_val, is_construct, args);
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
                                       const std::vector<JsValue>& args) {
    CallDepthGuard depth_guard(*this);

    if (node->is_class_constructor && !is_construct) {
        js_throw_type_error("Class constructor " + node->name + " cannot be invoked without 'new'");
    }

    auto env = std::make_shared<JsEnvironment>(closure ? closure : m_global_env);
    if (!node->is_arrow) {
        // Arrow functions bind none of these; lookups fall through to the
        // enclosing function's scope, which is exactly lexical `this`.
        if (this_val.is_nullish() && !node->is_strict) env->define("this", m_global_object);
        else env->define("this", this_val);
        if (node->uses_arguments) {
            JsValue arguments = JsValue::make_array(args);
            arguments.object_val->klass = ObjectClass::Arguments;
            env->define("arguments", arguments);
        }
        if (node->is_class_constructor || node->uses_new_target) {
            env->define("%fn", callee);
            env->define("%new.target", is_construct ? callee : JsValue::make_undefined());
        }
        if (node->uses_super && callee.is_function()) {
            env->define("%home", callee.get_property("%home"));
        }
    }

    bind_parameters(*node, env, args);
    for (const auto& name : node->var_names) {
        if (!env->has_own(name)) env->define(name, JsValue::make_undefined());
    }
    hoist_functions(node->body->functions, env);

    FlagScope strict_scope(m_strict, node->is_strict);
    EnvScope env_scope(m_current_env, env);

    if (node->is_generator) return make_generator(node, env);
    if (node->is_async) return run_async_body(node, env);

    // A base class runs its field initialisers before the constructor body; a
    // derived class runs them when super() returns.
    if (node->is_class_constructor && !node->is_derived_constructor) {
        initialize_fields(callee, this_val);
    }
    return run_function_body(*node, env);
}

JsValue JsInterpreter::run_function_body(const FunctionNode& node, const EnvPtr& env) {
    Frame frame;
    Flow flow = exec_list(node.body->statements, env, frame);
    if (flow == Flow::Return) return std::move(frame.return_value);
    return JsValue::make_undefined();
}

JsValue JsInterpreter::run_async_body(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& env) {
    auto promise = std::make_shared<PromiseData>();
    try {
        JsValue result = run_function_body(*node, env);
        Promise::resolve(promise, result);
    } catch (const GeneratorAbortException&) {
        throw;
    } catch (const JsThrow& thrown) {
        Promise::reject(promise, thrown.value);
    } catch (const std::exception& ex) {
        Promise::reject(promise, js_value_from_exception(ex));
    }
    return Promise::create_promise_instance(promise);
}

JsValue JsInterpreter::make_generator(const std::shared_ptr<const FunctionNode>& node, const EnvPtr& env) {
    auto gen = std::make_shared<GeneratorData>();
    GeneratorData* raw = gen.get();
    m_generators.push_back(gen);

    // The body runs on its own thread, parked until the first next(). The
    // thread holds a raw pointer: GeneratorData joins it in its destructor, so
    // the data always outlives the thread, and a shared_ptr here would keep an
    // unfinished generator alive forever.
    gen->worker = std::thread([this, raw, node, env]() {
        GeneratorContext::set_active(raw);
        {
            std::unique_lock<std::mutex> lock(raw->mtx);
            raw->cv_gen.wait(lock, [&] { return raw->started || raw->should_terminate; });
            if (raw->should_terminate) {
                GeneratorContext::set_active(nullptr);
                return;
            }
        }

        JsValue result;
        JsValue error;
        bool failed = false;
        try {
            result = run_function_body(*node, env);
        } catch (const GeneratorAbortException&) {
            GeneratorContext::set_active(nullptr);
            return;
        } catch (const JsThrow& thrown) {
            failed = true;
            error = thrown.value;
        } catch (const std::exception& ex) {
            failed = true;
            error = js_value_from_exception(ex);
        }

        {
            std::unique_lock<std::mutex> lock(raw->mtx);
            raw->yielded_value = failed ? JsValue::make_undefined() : result;
            raw->has_error = failed;
            raw->error = error;
            raw->is_done = true;
            raw->finished = true;
            raw->cv_main.notify_one();
        }
        GeneratorContext::set_active(nullptr);
    });

    JsValue object = JsValue::make_object();
    object.object_val->klass = ObjectClass::Generator;
    object.set_generator_value(gen);

    // Resumes the body and surfaces an exception that escaped it.
    auto resume = [gen](const JsValue& sent) -> JsValue {
        JsValue step = gen->step(sent);
        if (gen->has_error) {
            gen->has_error = false;
            JsValue error = gen->error;
            gen->error = JsValue::make_undefined();
            js_throw(error);
        }
        return step;
    };

    const bool is_async = node->is_async;
    auto wrap = [is_async](JsValue step) {
        // An async generator's methods return promises.
        return is_async ? Promise::make_resolved(std::move(step)) : step;
    };

    js_define_method(object, "next", 1, [resume, wrap](const std::vector<JsValue>& args) {
        return wrap(resume(js_arg(args, 0)));
    });

    js_define_method(object, "return", 1, [gen, wrap](const std::vector<JsValue>& args) {
        // Unwinds the suspended body, which runs its `finally` blocks.
        gen->terminate();
        gen->is_done = true;
        JsValue result = JsValue::make_object();
        result.set_property("value", js_arg(args, 0));
        result.set_property("done", JsValue::make_boolean(true));
        return wrap(result);
    });

    js_define_method(object, "throw", 1, [gen, resume, wrap](const std::vector<JsValue>& args) -> JsValue {
        if (!gen->started || gen->is_done) {
            gen->terminate();
            gen->is_done = true;
            js_throw(js_arg(args, 0));
        }
        {
            // Raised inside the body at the suspended `yield`, where the
            // generator's own try/catch can handle it.
            std::unique_lock<std::mutex> lock(gen->mtx);
            gen->has_pending_throw = true;
            gen->pending_throw = js_arg(args, 0);
        }
        return wrap(resume(JsValue::make_undefined()));
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
    JsValue parent;
    bool has_parent = false;
    if (cls->super_class) {
        parent = evaluate(cls->super_class.get(), env);
        if (parent.type != JsType::Null) {
            if (!parent.is_function()) {
                js_throw_type_error("Class extends value " + js_describe_thrown(parent) +
                                    " is not a constructor or null");
            }
            has_parent = true;
        }
    }

    // The class name is visible inside the class body even if the outer
    // binding is later reassigned.
    auto class_env = std::make_shared<JsEnvironment>(env);

    JsValue proto = JsValue::make_object();
    if (has_parent) {
        JsValue parent_proto = parent.get_property("prototype");
        if (parent_proto.object_val && parent_proto.is_object_like()) {
            proto.object_val->prototype = parent_proto.object_val;
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
    if (has_parent) ctor.define_property("%super", parent, PropertyMap::Hidden);
    ctor.define_property("%home", proto, PropertyMap::Hidden);

    if (!cls->name.empty()) class_env->define_const(cls->name, ctor);

    std::vector<JsValue> field_keys;
    for (const auto& member : cls->members) {
        JsValue target = member.is_static ? ctor : proto;
        std::string key = member.computed_key
            ? js_to_property_key(evaluate(member.computed_key.get(), class_env))
            : member.key;

        switch (member.kind) {
            case ClassMember::Kind::Method: {
                JsValue fn = make_function(member.fn, class_env, key);
                if (member.fn->uses_super) fn.define_property("%home", target, PropertyMap::Hidden);
                target.define_property(key, fn, PropertyMap::Hidden);
                break;
            }
            case ClassMember::Kind::Getter:
            case ClassMember::Kind::Setter: {
                JsValue fn = make_function(member.fn, class_env, key);
                if (member.fn->uses_super) fn.define_property("%home", target, PropertyMap::Hidden);
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
                    value = evaluate_named(member.value.get(), field_env, key);
                }
                ctor.define_property(key, value, PropertyMap::Default);
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
    return ctor;
}

void JsInterpreter::initialize_fields(const JsValue& ctor, const JsValue& instance) {
    const auto* fd = ctor.function_data();
    if (!fd || !fd->class_node || !instance.is_object_like()) return;
    JsObject* ctor_cell = ctor.property_cell();
    if (!ctor_cell) return;
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
        const std::string& key = keys[index++].string_val;
        JsValue value = member.value ? evaluate_named(member.value.get(), field_env, key)
                                     : JsValue::make_undefined();
        target.define_property(key, value, PropertyMap::Default);
    }
}

JsValue JsInterpreter::super_base(const EnvPtr& env) {
    auto* home = env->lookup("%home");
    if (!home || !home->value.is_object_like()) {
        js_throw_error(JsErrorKind::SyntaxError, "'super' keyword unexpected here");
    }
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
    JsValue this_val = lookup_identifier("this", env);
    JsValue parent;
    if (JsObject* cell = ctor.property_cell()) {
        auto it = cell->properties.find("%super");
        if (it != cell->properties.end()) parent = it->second;
    }
    if (!parent.is_function()) js_throw_type_error("Super constructor is not a constructor");

    std::vector<JsValue> args = eval_arguments(expr->args, env);
    const auto* parent_fd = parent.function_data();
    if (parent_fd && parent_fd->node) {
        // A script parent initialises the same `this` object.
        parent.construct(args, &this_val);
    } else {
        // A built-in parent (Error, Array, Map, ...) builds its own object;
        // take over its state so `this` becomes that kind of object.
        JsValue built = parent.construct(args, nullptr);
        if (built.object_val && this_val.object_val && built.object_val != this_val.object_val) {
            JsObject* from = built.object_val;
            JsObject* to = this_val.object_val;
            for (const auto& entry : from->properties) {
                auto& slot = to->properties.entry(entry.first);
                slot.second = entry.second;
                slot.setter = entry.setter;
                slot.flags = entry.flags;
            }
            to->is_array = from->is_array;
            to->array_elements = from->array_elements;
            to->klass = from->klass;
            to->internal = from->internal;
            to->trace_extra = from->trace_extra;
            to->has_accessors = to->has_accessors || from->has_accessors;
            to->has_legacy_getters = to->has_legacy_getters || from->has_legacy_getters;
            if (from->dom_node) to->dom_node = from->dom_node;
        }
    }
    initialize_fields(ctor, this_val);
    return JsValue::make_undefined();
}

// ---------------------------------------------------------------------------
// Variables, properties and patterns
// ---------------------------------------------------------------------------

JsValue JsInterpreter::lookup_identifier(const std::string& name, const EnvPtr& env) {
    if (auto* binding = env->lookup(name)) return binding->value;

    // Properties of the global object are global variables too: `window.x = 1`
    // makes `x` resolvable.
    if (JsObject* global = m_global_object.object_val) {
        int guard = 0;
        for (JsObject* obj = global; obj && guard < 10000; obj = obj->prototype, ++guard) {
            auto it = obj->properties.find(name);
            if (it == obj->properties.end()) continue;
            if (it->flags & PropertyMap::Accessor) {
                return it->second.is_function() ? it->second.call_with_this(m_global_object, {})
                                                : JsValue::make_undefined();
            }
            return it->second;
        }
    }
    js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
}

void JsInterpreter::assign_identifier(const std::string& name, const JsValue& value, const EnvPtr& env) {
    switch (env->try_assign(name, value)) {
        case JsEnvironment::AssignResult::Ok:
            return;
        case JsEnvironment::AssignResult::Const:
            js_throw_type_error("Assignment to constant variable.");
        case JsEnvironment::AssignResult::NotFound:
            break;
    }
    if (JsObject* global = m_global_object.object_val) {
        if (global->properties.find(name) != global->properties.end()) {
            JsValue target = m_global_object;
            target.set_property(name, value);
            return;
        }
    }
    if (m_strict) js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
    m_global_env->define(name, value);      // sloppy mode: implicit global
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
    if (ref.is_property) return ref.base.get_property(ref.name);
    return lookup_identifier(ref.name, env);
}

void JsInterpreter::write_reference(const Reference& ref, const JsValue& value, const EnvPtr& env) {
    if (ref.is_property) {
        JsValue target = ref.base;
        target.set_property(ref.name, value);
        return;
    }
    assign_identifier(ref.name, value, env);
}

void JsInterpreter::bind_pattern(const Pattern& pattern, const JsValue& value, const EnvPtr& env, BindMode mode) {
    switch (pattern.kind) {
        case Pattern::Kind::Identifier:
            switch (mode) {
                case BindMode::Let:
                    env->define(pattern.name, value);
                    return;
                case BindMode::Const:
                    env->define_const(pattern.name, value);
                    return;
                case BindMode::Var: {
                    // The binding was hoisted to the function scope on entry.
                    if (auto* binding = env->lookup(pattern.name)) {
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
                    for (const auto& key : value.own_keys()) {
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
    EnvScope env_scope(m_current_env, scope);
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
            env->define(decl->cls->name, define_class(decl->cls, env));
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
    if (stmt->init) exec(stmt->init.get(), loop_env, frame);

    // `for (let i ...)` gives every iteration its own `i`, which is what makes
    // closures created in the body see the value from their own iteration.
    const bool per_iteration = !stmt->per_iteration_names.empty();
    EnvPtr iter_env = per_iteration ? copy_scope(loop_env, env) : loop_env;

    while (true) {
        if (stmt->test && !evaluate(stmt->test.get(), iter_env).is_truthy()) break;
        Flow flow = exec(stmt->body.get(), iter_env, frame);
        LoopAction action = loop_action(this, static_cast<int>(flow), frame.label, labels);
        if (action == LoopAction::Exit) break;
        if (action == LoopAction::Propagate) return flow;
        if (per_iteration) iter_env = copy_scope(iter_env, env);
        if (stmt->update) evaluate(stmt->update.get(), iter_env);
    }
    return Flow::Normal;
}

JsInterpreter::Flow JsInterpreter::exec_for_each(const ForEachStmt* stmt, const EnvPtr& env, Frame& frame,
                                                 const std::vector<const std::string*>& labels) {
    JsValue subject = evaluate(stmt->iterable.get(), env);

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
        bind_pattern(*stmt->target, value, iter_env,
                     stmt->decl_kind == JsDeclKind::Const ? BindMode::Const : BindMode::Let);
        return iter_env;
    };

    Flow result = Flow::Normal;
    // Returns true to keep iterating.
    auto run_body = [&](const JsValue& value) -> bool {
        EnvPtr iter_env = bind_iteration(value);
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
        for (const auto& key : subject.own_keys(true)) {
            seen.insert(key);
        }
        for (auto& key : subject.own_keys(false)) keys.push_back(std::move(key));
        int guard = 0;
        for (JsObject* proto = subject.type == JsType::String ? nullptr : subject.prototype_object();
             proto && guard < 10000; proto = proto->prototype, ++guard) {
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
                if (stmt->param) bind_pattern(*stmt->param, thrown, scope, BindMode::Let);
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
    } catch (const GeneratorAbortException&) {
        // The generator is being torn down: `finally` runs, `catch` does not.
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

JsValue JsInterpreter::eval_binary(const BinaryExpr* expr, const EnvPtr& env) {
    // Short-circuit operators evaluate their right side only when needed and
    // yield an operand, not a boolean.
    if (expr->op == JsTokenType::PipePipe) {
        JsValue left = evaluate(expr->left.get(), env);
        if (left.is_truthy()) return left;
        return evaluate(expr->right.get(), env);
    }
    if (expr->op == JsTokenType::AmpAmp) {
        JsValue left = evaluate(expr->left.get(), env);
        if (!left.is_truthy()) return left;
        return evaluate(expr->right.get(), env);
    }
    if (expr->op == JsTokenType::Nullish) {
        JsValue left = evaluate(expr->left.get(), env);
        if (!left.is_nullish()) return left;
        return evaluate(expr->right.get(), env);
    }
    JsValue left = evaluate(expr->left.get(), env);
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
                    return JsValue::make_string("undefined");
                }
            }
            return JsValue::make_string(js_typeof(evaluate(operand, env)));
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
            fn = get_member(base, member->property);
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
            fn = get_with_receiver(base.object_val, computed_name, this_val);
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
            for (const auto& key : source.own_keys()) {
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

        JsValue value = evaluate_named(prop.value.get(), env, key);
        if (key == "__proto__" && !prop.computed_key && !prop.shorthand) {
            // `{ __proto__: base }` sets the prototype rather than a property.
            if (value.object_val && value.is_object_like()) object.object_val->prototype = value.object_val;
            else if (value.type == JsType::Null) object.object_val->prototype = nullptr;
            continue;
        }
        object.define_property(key, value, PropertyMap::Default);
    }
    return object;
}

JsValue JsInterpreter::eval_yield(const YieldExpr* expr, const EnvPtr& env) {
    auto* generator = GeneratorContext::get_active();
    if (!generator) {
        js_throw_error(JsErrorKind::SyntaxError, "yield is only valid in generator functions");
    }

    // Hands a value to the consumer and waits to be resumed. throw() on the
    // generator object surfaces here, inside the body's own try/catch.
    auto suspend = [&](const JsValue& value) -> JsValue {
        JsValue sent = generator->yield_to_main(value);
        if (generator->has_pending_throw) {
            generator->has_pending_throw = false;
            JsValue error = generator->pending_throw;
            generator->pending_throw = JsValue::make_undefined();
            js_throw(error);
        }
        return sent;
    };

    if (expr->delegate) {
        JsValue iterable = expr->argument ? evaluate(expr->argument.get(), env) : JsValue::make_undefined();
        js_iterate(iterable, [&](const JsValue& item) {
            suspend(item);
            return true;
        });
        return JsValue::make_undefined();
    }

    JsValue value = expr->argument ? evaluate(expr->argument.get(), env) : JsValue::make_undefined();
    return suspend(value);
}

JsValue JsInterpreter::eval_await(const JsValue& value) {
    auto promise = value.promise_value();
    if (!promise) return value;

    // Blocking await: run the event loop until the promise settles.
    auto& loop = EventLoop::default_loop();
    while (promise->state == PromiseState::Pending) {
        if (loop.tick() == 0) {
            if (!loop.has_pending_tasks()) {
                // Nothing left that could ever settle it.
                js_throw_type_error("await on a promise that can never settle");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

JsValue JsInterpreter::evaluate(const Expr* expr, const EnvPtr& env) {
    // Expression evaluation is itself recursive: `1 + (1 + (1 + ...))` and
    // `return 1 + f(n - 1)` both nest evaluate() once per level, so the stack
    // guard has to live here and not only at call sites.
    CallDepthGuard depth_guard(*this);

    switch (expr->kind) {
        case NodeKind::Literal:
            return static_cast<const LiteralExpr*>(expr)->value;

        case NodeKind::Identifier:
            return lookup_identifier(static_cast<const IdentifierExpr*>(expr)->name, env);

        case NodeKind::Binary:
            return eval_binary(static_cast<const BinaryExpr*>(expr), env);

        case NodeKind::Unary:
            return eval_unary(static_cast<const UnaryExpr*>(expr), env);

        case NodeKind::Assign:
            return eval_assign(static_cast<const AssignExpr*>(expr), env);

        case NodeKind::Update: {
            auto* update = static_cast<const UpdateExpr*>(expr);
            // The target is resolved once, so `a[i++]++` evaluates `i++` once.
            Reference ref = resolve_reference(update->target.get(), env);
            double before = js_to_number(read_reference(ref, env));
            double after = before + (update->op == JsTokenType::PlusPlus ? 1.0 : -1.0);
            write_reference(ref, JsValue::make_number(after), env);
            return JsValue::make_number(update->prefix ? after : before);
        }

        case NodeKind::Conditional: {
            auto* cond = static_cast<const ConditionalExpr*>(expr);
            return evaluate(cond->test.get(), env).is_truthy() ? evaluate(cond->consequent.get(), env)
                                                               : evaluate(cond->alternate.get(), env);
        }

        case NodeKind::Sequence: {
            JsValue last;
            for (const auto& item : static_cast<const SequenceExpr*>(expr)->expressions) {
                last = evaluate(item.get(), env);
            }
            return last;
        }

        case NodeKind::Member: {
            auto* member = static_cast<const MemberExpr*>(expr);
            JsValue base = evaluate(member->object.get(), env);
            if (m_chain_short_circuit) return JsValue::make_undefined();
            if (member->optional && base.is_nullish()) {
                m_chain_short_circuit = true;
                return JsValue::make_undefined();
            }
            return get_member(base, member->property);
        }

        case NodeKind::Index: {
            auto* index = static_cast<const IndexExpr*>(expr);
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

        case NodeKind::ArrayLiteral: {
            std::vector<JsValue> elements;
            for (const auto& element : static_cast<const ArrayLiteralExpr*>(expr)->elements) {
                if (!element) {
                    elements.push_back(JsValue::make_undefined());      // hole
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
            return JsValue::make_array(elements);
        }

        case NodeKind::TemplateLiteral: {
            std::string result;
            for (const auto& part : static_cast<const TemplateLiteralExpr*>(expr)->parts) {
                if (part->kind == NodeKind::Literal) {
                    result += static_cast<const LiteralExpr*>(part.get())->value.string_val;
                } else {
                    result += js_to_string(evaluate(part.get(), env));
                }
            }
            return JsValue::make_string(std::move(result));
        }

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
                scope->define(node->name, fn);
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

        case NodeKind::SuperMember: {
            auto* super_member = static_cast<const SuperMemberExpr*>(expr);
            std::string key = super_member->computed
                ? js_to_property_key(evaluate(super_member->computed.get(), env))
                : super_member->property;
            JsValue base = super_base(env);
            return get_with_receiver(base.object_val, key, lookup_identifier("this", env));
        }

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
    EnvScope env_scope(m_current_env, env);

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
