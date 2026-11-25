// The tree-walking evaluator: statements, functions, classes and expressions.
//
// Design notes:
//  * Dispatch is a switch on AstNode::kind, not a chain of dynamic_casts.
//  * `return`, `break` and `continue` are completion values (Flow), not C++
//    exceptions. Only a JavaScript `throw` unwinds the C++ stack, as JsThrow.
//  * Every construct is evaluated here and nowhere else. There is no second
//    engine to fall back to, so a script can never run twice.

#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/IterationCursor.hpp"
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

bool parse_copy_index(const std::string& s, size_t& out) {
    if (s.empty() || (s.size() > 1 && s[0] == '0')) return false;
    size_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const size_t digit = static_cast<size_t>(c - '0');
        if (v > (std::numeric_limits<size_t>::max() - digit) / 10) return false;
        v = v * 10 + digit;
    }
    out = v;
    return true;
}

/// Checks whether `key` is currently an own enumerable property of `source`,
/// matching the live `[[GetOwnProperty]](nextKey)` + `desc.[[Enumerable]]`
/// check inside ECMA-262 §7.3.24 `CopyDataProperties`.
bool is_own_enumerable_for_copy(const JsValue& source, const std::string& key) {
    if (source.type == JsType::String) {
        size_t idx = 0;
        return parse_copy_index(key, idx) && idx < source.string_val.length();
    }
    JsObject* cell = source.property_cell();
    if (cell) {
        if (cell->klass == ObjectClass::String) {
            const auto* wrapped = static_cast<const JsValue*>(cell->internal.get());
            size_t idx = 0;
            if (wrapped && wrapped->type == JsType::String &&
                parse_copy_index(key, idx) && idx < wrapped->string_val.length()) {
                return true;
            }
        }
        if (cell->is_array) {
            if (key == "length") return false;
            size_t idx = 0;
            if (parse_copy_index(key, idx) &&
                idx < cell->array_elements.size() && !source.is_array_hole(idx)) {
                return true;
            }
        }
        auto it = cell->properties.find(key);
        if (it != cell->properties.end()) {
            return (it->flags & PropertyMap::Enumerable) != 0;
        }
        if (cell->exotic && cell->exotic->has(cell, key)) {
            bool enumerable = true;
            bool writable = false;
            if (cell->exotic->computed_property(cell, key, enumerable, writable)) {
                return enumerable;
            }
            return true;
        }
        return false;
    }
    if (const PropertyMap* map = source.loose_properties()) {
        auto it = map->find(key);
        if (it != map->end()) return (it->flags & PropertyMap::Enumerable) != 0;
    }
    return false;
}

// The helpers above stay in the anonymous namespace; this function does not:
// the bytecode VM's object-literal spread calls it, and it must keep the one
// §7.3.24 the tree-walker already ships.
} // namespace (anonymous helpers)

/// ECMA-262 §7.3.24 `CopyDataProperties(target, source, excludedItems)` — used
/// by both object spread (`{...source}`) and object rest destructuring
/// (`{...rest} = source`). Snapshots `[[OwnPropertyKeys]]` (string keys in
/// `own_keys` order followed by own symbol keys in creation order), skips any
/// key in `excluded`, checks live own-enumerability before reading, and writes
/// via `CreateDataPropertyOrThrow` (`define_property`) rather than `[[Set]]`.
void copy_data_properties(JsValue& target, const JsValue& source,
                          const std::vector<std::string>* excluded) {
    if (source.is_nullish()) return;

    auto is_excluded = [&](const std::string& key) {
        return excluded && std::find(excluded->begin(), excluded->end(), key) != excluded->end();
    };

    std::vector<std::string> cross_keys;
    if (litebird::dom::cross_origin_own_keys(source, false, cross_keys)) {
        for (const auto& key : cross_keys) {
            if (is_excluded(key)) continue;
            target.define_property(key, source.get_property(key), PropertyMap::Default);
        }
        return;
    }

    // Snapshot every own key (string keys first, then symbol keys in creation
    // order), including non-enumerable ones so a getter that flips a later
    // key's enumerability is observed when that key is reached.
    std::vector<std::string> keys = source.own_keys(/*include_non_enumerable=*/true);
    const PropertyMap* snap_map = source.property_cell() ? &source.property_cell()->properties
                                                         : source.loose_properties();
    if (snap_map) {
        for (const auto& entry : *snap_map) {
            JsValue symbol;
            if (!js_symbol_from_key(entry.first, symbol)) continue;
            keys.push_back(entry.first);
        }
    }

    for (const auto& key : keys) {
        if (is_excluded(key)) continue;
        if (!is_own_enumerable_for_copy(source, key)) continue;
        JsValue prop_val = source.get_property(key);
        target.define_property(key, std::move(prop_val), PropertyMap::Default);
    }
}

namespace {

// ---------------------------------------------------------------------------
// Active scopes
// ---------------------------------------------------------------------------
//
// The collector cannot scan native stacks, so every scope that has code running
// in it is listed here: the scopes of all the callers of the function that is
// executing, not just its own lexical chain. There is one list per thread,
// because a suspended generator or async function keeps its scopes on the
// stack of its own thread while other code runs.

struct ScopeEntry {
    JsEnvironment* env{nullptr};
    bool owned_local_ref{true};
};

struct ScopeStack {
    uint32_t agent{0};
    GeneratorData* owner_frame{nullptr};
    std::vector<ScopeEntry> scopes;
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
            // A suspended generator's scope stack is traced from the generator
            // object itself (`trace_extra`), not as an unconditional root —
            // otherwise an unreachable suspended generator roots its own
            // enclosing scope and can never be collected.
            const uint32_t current = Heap::current_agent();
            std::lock_guard<std::mutex> lock(mutex);
            for (ScopeStack* stack : stacks) {
                if (stack->agent != current) continue;
                if (stack->owner_frame && !stack->owner_frame->is_coroutine &&
                    !stack->owner_frame->is_running()) {
                    continue;
                }
                for (const ScopeEntry& entry : stack->scopes) {
                    Heap::default_heap().mark_environment(entry.env);
                }
            }
        });
    }
};

ScopeStackRegistry& scope_registry() {
    static ScopeStackRegistry* registry = new ScopeStackRegistry();     // never destroyed: threads may outlive main()
    return *registry;
}

ScopeStack::ScopeStack() : agent(Heap::current_agent()) {
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
/// `owned_local_ref` records whether the enclosing C++ frame holds its own
/// local `shared_ptr<JsEnvironment>` to `env` (used when counting references
/// held on a suspended generator's parked stack).
struct EnvScope {
    ScopeStack& stack;
    bool pushed{false};
    explicit EnvScope(const std::shared_ptr<JsEnvironment>& env, bool owned_local_ref = true)
        : stack(active_scopes()), pushed(env != nullptr) {
        if (pushed) stack.scopes.push_back({env.get(), owned_local_ref});
    }
    ~EnvScope() {
        if (pushed) stack.scopes.pop_back();
    }
    EnvScope(const EnvScope&) = delete;
    EnvScope& operator=(const EnvScope&) = delete;
};

/// Associates a worker thread's ScopeStack with its GeneratorData for the
/// lifetime of the worker body, clearing the link before finish() wakes the
/// resumer.
struct FrameScopeGuard {
    GeneratorData* frame;
    ScopeStack& stack;
    FrameScopeGuard(GeneratorData* f, ScopeStack& s) : frame(f), stack(s) {
        stack.owner_frame = f;
        frame->active_scope_stack = &stack;
    }
    ~FrameScopeGuard() {
        frame->active_scope_stack = nullptr;
        stack.owner_frame = nullptr;
    }
    FrameScopeGuard(const FrameScopeGuard&) = delete;
    FrameScopeGuard& operator=(const FrameScopeGuard&) = delete;
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

JsValue js_create_async_from_sync_iterator(const JsValue& sync_iterator) {
    return js_make_async_from_sync(sync_iterator);
}

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
    return run_program(program, nullptr, false, false);
}

JsValue JsInterpreter::run_program(const Program& program, const std::shared_ptr<JsEnvironment>& scope,
                                   bool direct_eval, bool is_eval) {
    CallDepthGuard depth_guard(*this);
    JsIntrinsics::set_current(m_realm.get());
    const bool eval_mode = direct_eval || is_eval;
    EnvPtr target = scope ? scope : m_global_env;
    EnvScope env_scope(target);

    // A script's strictness is its own, and for as long as it runs it is the
    // global scope's too (the global scope is shared by every script). An
    // indirect eval runs in a fresh declarative child of the global scope with
    // strictness determined purely by its own text. A direct eval inherits the
    // strictness of its caller unless its own text opens with "use strict".
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
    } strict_scope(*target, program.is_strict, !eval_mode);

    // Every eval (direct or indirect) gets its own declarative environment
    // (`layer`) for `let`, `const`, and `class` (and for `var` / `function`
    // when strict).
    EnvPtr body = target;
    std::shared_ptr<JsEnvironment> layer;
    const bool strict_eval = eval_mode && (program.is_strict || (direct_eval && target->is_strict()));
    if (eval_mode) {
        layer = std::make_shared<JsEnvironment>(target);
        layer->strict_mode = direct_eval ? (program.is_strict ? 1 : -1) : (program.is_strict ? 1 : 0);
        body = layer;
    }
    EnvScope layer_scope(layer);

    EnvPtr var_target = strict_eval ? body : target;
    if (direct_eval && !strict_eval) var_target = target->var_scope();

    // EvalDeclarationInstantiation (ECMA-262 §19.2.1.1.1): reject `var` /
    // `function` hoisting that would collide with an intervening lexical
    // environment, catch parameter (unless simple catch under Annex B.3.5), or
    // global lexical / restricted property BEFORE mutating anything.
    if (eval_mode && !strict_eval) {
        for (JsEnvironment* s = target.get(); s && s != var_target.get(); s = s->parent_raw()) {
            for (const auto& name : program.var_names) {
                if (s->is_simple_catch && s->simple_catch_name == name) continue;
                if (s->has_own(name)) {
                    js_throw_error(JsErrorKind::SyntaxError,
                                   "Identifier '" + name + "' has already been declared");
                }
            }
            for (const auto* decl : program.functions) {
                if (s->is_simple_catch && s->simple_catch_name == decl->fn->name) continue;
                if (s->has_own(decl->fn->name)) {
                    js_throw_error(JsErrorKind::SyntaxError,
                                   "Identifier '" + decl->fn->name + "' has already been declared");
                }
            }
        }
    }

    const bool global_var_target = (var_target == m_global_env);
    JsObject* global_cell = global_var_target ? m_global_object.object_val : nullptr;

    auto check_global_lexical_conflict = [&](const std::string& name) {
        if (auto* b = m_global_env->find_own(name); b && b->is_lexical) {
            js_throw_error(JsErrorKind::SyntaxError, "Identifier '" + name + "' has already been declared");
        }
    };

    if (global_var_target) {
        for (const auto& name : program.var_names) {
            check_global_lexical_conflict(name);
            if (!m_global_env->has_own(name) && global_cell) {
                auto it = global_cell->properties.find(name);
                if (it == global_cell->properties.end() && !global_cell->extensible) {
                    js_throw_type_error("Cannot define property " + name + ", object is not extensible");
                }
            }
        }
        for (const auto* decl : program.functions) {
            const std::string& name = decl->fn->name;
            check_global_lexical_conflict(name);
            if (auto* b = m_global_env->find_own(name)) {
                if (b->is_readonly) {
                    js_throw_type_error("Cannot redefine non-configurable property '" + name + "'");
                }
                continue;
            }
            if (global_cell) {
                auto it = global_cell->properties.find(name);
                if (it != global_cell->properties.end()) {
                    if (!(it->flags & PropertyMap::Configurable) &&
                        ((it->flags & PropertyMap::Accessor) ||
                         !(it->flags & PropertyMap::Writable) ||
                         !(it->flags & PropertyMap::Enumerable))) {
                        js_throw_type_error("Cannot redefine property: " + name);
                    }
                } else if (!global_cell->extensible) {
                    js_throw_type_error("Cannot define property " + name + ", object is not extensible");
                }
            }
        }
    }

    // Hoist functions before `var`s (ECMA-262 §19.2.1.1.1 / §16.1.7) so a `var f`
    // without an initializer never overwrites a hoisted `function f`.
    for (const auto* decl : program.functions) {
        const std::string& name = decl->fn->name;
        JsValue fn = make_function(decl->fn, body);
        if (global_var_target) {
            if (auto* b = m_global_env->find_own(name)) {
                if (!b->is_readonly && !b->is_lexical) b->value = fn;
            } else if (global_cell) {
                auto it = global_cell->properties.find(name);
                uint8_t flags = eval_mode
                                    ? static_cast<uint8_t>(PropertyMap::Default)
                                    : static_cast<uint8_t>(PropertyMap::Writable | PropertyMap::Enumerable);
                if (it != global_cell->properties.end() && !(it->flags & PropertyMap::Configurable)) {
                    flags = it->flags;
                }
                m_global_object.define_property(name, fn, flags);
            } else {
                var_target->define(name, fn);
            }
        } else {
            const bool existed = var_target->has_own(name);
            var_target->define(name, fn);
            if (!existed && eval_mode && !strict_eval) {
                if (auto* b = var_target->find_own(name)) b->is_deletable = true;
            }
        }
    }

    for (const auto& name : program.var_names) {
        if (global_var_target) {
            if (m_global_env->has_own(name)) continue;
            if (global_cell) {
                if (global_cell->properties.find(name) == global_cell->properties.end()) {
                    const uint8_t flags =
                        eval_mode ? static_cast<uint8_t>(PropertyMap::Default)
                                  : static_cast<uint8_t>(PropertyMap::Writable | PropertyMap::Enumerable);
                    m_global_object.define_property(name, JsValue::make_undefined(), flags);
                }
            } else if (!var_target->has_own(name)) {
                var_target->define(name, JsValue::make_undefined());
            }
        } else if (!var_target->has_own(name)) {
            var_target->define(name, JsValue::make_undefined());
            if (eval_mode && !strict_eval) {
                if (auto* b = var_target->find_own(name)) b->is_deletable = true;
            }
        }
    }

    declare_lexicals(program.lexical_decls, body);

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
    if (!frame.direct || !frame.environment) {
        auto text = std::make_shared<const std::string>(source);
        JsLexer lexer(*text);
        auto tokens = lexer.tokenize_all();
        JsParser parser(std::move(tokens), text);
        auto program = parser.parse_program();
        return run_program(*program, nullptr, /*direct_eval=*/false, /*is_eval=*/true);
    }

    auto text = std::make_shared<const std::string>(source);
    JsLexer lexer(*text);
    auto tokens = lexer.tokenize_all();
    JsParserContext parser_context;
    parser_context.direct_eval = true;
    parser_context.strict = frame.environment->is_strict();
    parser_context.allow_super_property = frame.allow_super_property;
    parser_context.allow_super_call = frame.allow_super_call;
    parser_context.allow_new_target = frame.allow_new_target;
    parser_context.forbid_arguments = frame.forbid_arguments;
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
    return run_program(*program, running->shared_from_this(), /*direct_eval=*/true, /*is_eval=*/true);
}

void JsInterpreter::hoist_functions(const std::vector<const FunctionDeclStmt*>& functions, const EnvPtr& env) {
    for (const auto* decl : functions) {
        env->define(decl->fn->name, make_function(decl->fn, env));
    }
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

/// The `name` a *definition* gives the function it makes.
///
/// §15.4.4/§15.4.5 (SetFunctionName) put the name in the hands of the
/// production: a FunctionExpression written as the value of a property takes
/// the property key, and an accessor takes the key with `get `/`set ` in front
/// of it. A symbol key is not its string form -- there is no string form -- but
/// `[` + its description + `]`, and a symbol with no description contributes
/// nothing, so an anonymous symbol-keyed method is named `""`.
///
/// `key` is the key's string form and `computed` is the value a computed key
/// evaluated to, which is the only way the key can be a symbol. Both are
/// needed: the string form is what a symbol's opaque internal text looks like,
/// and it is the whole answer for every other kind of key.
static std::string defined_name(const std::string& key, const JsValue* computed, const char* prefix) {
    std::string name = prefix;
    if (computed && computed->type == JsType::Symbol) {
        // The description, not `toString()`: a symbol with none contributes
        // nothing at all, where `String(Symbol())` is the text "Symbol()".
        // It rides in the value's extra block (JsValue::make_symbol), which is
        // where Symbol.prototype.description reads it too.
        const std::string description = computed->symbol_description_text();
        if (!description.empty()) name += "[" + description + "]";
        return name;
    }
    return name + key;
}

/// `fd.name`, overridden by the definition that made the function.
///
/// The parser gives a method or an accessor its key as a name already, because
/// that is right by default; a definition that knows better -- a private
/// name's spelling, an accessor's `get `/`set `, a symbol key's description --
/// sets the name here instead of working out which case it is standing in.
void JsInterpreter::name_function(JsValue& fn, const std::string& name) const {
    if (auto* fd = const_cast<JsValue::FunctionData*>(fn.function_data())) fd->name = name;
    if (JsObject* cell = fn.property_cell()) {
        auto entry = cell->properties.find("name");
        if (entry != cell->properties.end()) entry->second = JsValue::make_string(name);
    }
}

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
    fd.is_strict = node->is_strict;
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

    // `name` and `length` are ordinary own properties of the function, not
    // values answered out of the payload: §10.2.9/§10.2.5 give them
    // `{ writable: false, enumerable: false, configurable: true }`, so a write
    // through a non-strict reference is ignored, a strict one throws, a delete
    // removes them, and `Object.getOwnPropertyNames` lists them. They are
    // installed here, in this order, because the standard's order *is*
    // insertion order and Node prints `["length","name",...]`.
    if (JsObject* cell = fn.property_cell(/*create=*/true)) {
        cell->properties.entry("length").second = JsValue::make_number(node->length);
        cell->properties.entry("length").flags = PropertyMap::Configurable;
        cell->properties.entry("name").second = JsValue::make_string(fd.name);
        cell->properties.entry("name").flags = PropertyMap::Configurable;
        // `arguments`, `caller` and `prototype` are the three an *ordinary*
        // function has without anyone making them (§10.2.4-6), and what the
        // first two are depends on strictness: `null` data properties in
        // sloppy code, and the *poison pill* accessors in strict code, where
        // reading either throws — that is how a strict script is refused the
        // caller of a function.
        //
        // The set is narrower than "every function". An arrow, a method, a
        // generator and an async function have an exotic `%Function.prototype%`
        // that carries the pair (that prototype is not modelled yet, so they
        // have neither own property and read as `undefined`), and a class
        // constructor is strict code that inherits the pair rather than owning
        // it — Node lists `["length","name","prototype","s"]` for a class and
        // nothing else.
        // Only the sloppy ordinary function gets them, and only as data
        // properties. Everything else — a strict function, an arrow, a method,
        // a generator, an async function, a class — must have *no* own property
        // of either name (§10.2.4: "must not be created with own properties
        // named caller or arguments"), and reading one throws because the
        // lookup refuses it, not because something is stored.
        const bool sloppy_ordinary = fd.is_constructor && !node->is_class_constructor &&
                                     !node->is_method && !node->is_arrow && !node->is_generator &&
                                     !node->is_async && !node->is_strict;
        if (sloppy_ordinary) {
            for (const char* key : {"arguments", "caller"}) {
                auto& entry = cell->properties.entry(key);
                entry.second = JsValue::make_null();
                entry.flags = 0;
            }
            auto& proto_entry = cell->properties.entry("prototype");
            JsValue proto = JsValue::make_object();
            // The same back-link the lazy path makes in
            // `function_implicit_property`: `p.constructor === P` is how an
            // instance finds the function it came from.
            proto.define_property("constructor", fn, PropertyMap::Hidden);
            proto_entry.second = proto;
            proto_entry.flags = PropertyMap::Writable;
        }
    }
    return fn;
}

/// §10.4.4.2, the *mapped arguments object*: a sloppy function's `arguments`
/// shares a place with each of its simple parameters, so `a = 1` shows through
/// `arguments[0]` and `arguments[0] = 1` shows through `a`.
///
/// The two are one binding, not two values kept in step. `JsEnvironment` owns
/// the binding (§9.1.1.1's mutable binding), so the object holds its *name* and
/// looks it up in the environment that owns it: writing either side is the same
/// write, and there is no second copy that can drift. A name is enough because
/// the environment is the same one for the whole call and a parameter's binding
/// is never removed from it — only `eval`'s var declarations are deletable, and
/// a `delete a` of a parameter deletes the *mapping* below, not the binding.
///
/// §10.4.4.2 step 13 is the rest of it: `delete arguments[i]` removes that
/// index's mapping and leaves every other one. `mapped` is indexed by place, so
/// an index that is not below the count of simple parameters is never mapped.
class MappedArguments : public JsExotic {
public:
    MappedArguments(std::shared_ptr<JsEnvironment> env, std::vector<std::string> parameter_names)
        : m_env(std::move(env)), m_names(std::move(parameter_names)) {}


    /// The binding `index` is mapped onto, or null when it is not mapped.
    JsEnvironment::Binding* mapped_binding(size_t index) {
        if (index >= m_names.size() || m_names[index].empty()) return nullptr;
        return m_env->find_own(m_names[index]);
    }

    /// `arguments[i]` when the place is mapped: the parameter's value. False
    /// when the index is not mapped, and then the element is the answer.
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        const size_t index = mapped_index(name);
        JsEnvironment::Binding* binding = mapped_binding(index);
        if (binding == nullptr) return false;
        out = binding->value;
        return true;
    }

    /// `arguments[i] = v` when the place is mapped: the parameter is written.
    /// The element is written too, so a place that is later unmapped still
    /// reads back what was last put there.
    SetResult set(JsObject* self, const std::string& name, const JsValue& value) override {
        const size_t index = mapped_index(name);
        JsEnvironment::Binding* binding = mapped_binding(index);
        if (binding == nullptr) return SetResult::NotHandled;
        binding->value = value;
        if (self != nullptr && index < self->array_elements.size()) {
            // A place a descriptor defined is a hole with a table entry (see
            // `defining`), and that entry is what a key walk and the descriptor
            // read see; and an element that is still there is written too, so a
            // place later unmapped reads back what was last put in it.
            if (self->has_accessors) {
                if (auto it = self->properties.find(name); it != self->properties.end()) {
                    it->second = value;
                    return SetResult::Handled;
                }
            }
            if (!self->array_holes || !self->array_holes->empty_slot(index)) {
                self->array_elements[index] = value;
            }
        }
        return SetResult::Handled;
    }

    /// `i in arguments` is true for a mapped place even if the element went.
    bool has(JsObject*, const std::string& name) override {
        return mapped_binding(mapped_index(name)) != nullptr;
    }

    /// `delete arguments[i]`: the mapping goes, the place stays an element, and
    /// the answer is true because the element itself is configurable. §10.4.4.2
    /// step 13.e. Every *other* index keeps its mapping — which is why this is
    /// a per-place vector and not one flag.
    bool remove(JsObject* self, const std::string& name, bool& result) override {
        const size_t index = mapped_index(name);
        if (index >= m_names.size() || m_names[index].empty()) return false;
        m_names[index].clear();
        // The place goes with the mapping. §10.4.4.2 step 13 deletes the index
        // from the object after clearing the mapping, so afterwards
        // `0 in arguments` is false, `Object.keys` leaves it out, and a read
        // is the prototype's to answer — Node answers `undefined` and stays
        // `undefined` however the parameter is written later. An element left
        // behind would read the parameter's last value, which is what this
        // engine did first: `delete arguments[0]; a = 7` answered 7.
        if (self != nullptr) {
            result = true;
            return false;       // let the ordinary [[Delete]] take the element
        }
        result = true;
        return true;
    }

    /// [[DefineOwnProperty]] on a mapped place. The place is one binding with
    /// its parameter, so a descriptor is applied to the *binding*, and only
    /// what cannot be said of a binding breaks the mapping:
    ///
    /// - an accessor descriptor (§10.6 step 5.a.i) always unmaps;
    /// - `writable: false` (§10.6 step 4.c) always unmaps;
    /// - a value in the descriptor is written into the parameter, and the
    ///   place stays mapped (`{value: 5}` then `a = 9` reads 9);
    /// - `enumerable` and `configurable` narrow the place's attributes, which
    ///   live here because the dense elements carry none.
    ///
    /// Six test262 files (`15.2.3.6-4-291-1` and its relatives) define an
    /// accessor here and failed while the mapping answered the read instead.
    bool owns_place(JsObject*, const std::string& name) override {
        // A place this mapping still answers for. `defining` is what gives one
        // up, so it is never asked as a question: this is that question.
        return mapped_binding(mapped_index(name)) != nullptr;
    }

    Definition defining(JsObject* self, const std::string& name, const PropertyDefinition& definition) override {
        const size_t index = mapped_index(name);
        if (index >= m_names.size() || m_names[index].empty()) return Definition::NotMine;
        JsEnvironment::Binding* binding = mapped_binding(index);
        if (binding == nullptr) return Definition::NotMine;
        if (definition.accessor) {
            // An accessor descriptor always unmaps the place (§10.6 step
            // 5.a.i), and there is no value to leave behind: the table entry
            // the accessor itself is about to be written into is what answers.
            m_names[index].clear();
            return Definition::Given;
        }
        if (definition.unwritable) {
            // A place that cannot be written cannot follow its parameter any
            // more, so defining it not-writable is what unmaps it — with or
            // without a value in the descriptor (Node leaves the parameter's
            // value behind for `{writable: false}` alone).
            unmap(self, name, index, definition.value != nullptr ? *definition.value : binding->value);
            return Definition::Given;
        }
        // Everything else leaves the place mapped, and a value in the
        // descriptor is written *into the parameter*: `{value: 5}` on a place
        // whose parameter is 1 makes both 5, and a later `a = 9` is still
        // visible through `arguments[0]`. §10.6 step 5.b.i's SameValue rule,
        // which unmapped when the two differed, is not what engines do — Node
        // answers 9 for exactly that sequence.
        if (definition.value != nullptr) binding->value = *definition.value;
        // The place is still one binding with its parameter, and reads keep
        // coming from it (`get`). What a descriptor asks for besides the value
        // is an attribute, and the dense elements carry no attributes: so the
        // place becomes a hole that the property table answers for, holding the
        // descriptor's flags, and `set` mirrors every write of the parameter
        // into that entry so the table never goes stale. The value in the entry
        // is a placeholder — a read asks the binding — and the descriptor read
        // back by `Object.getOwnPropertyDescriptor` takes the value from
        // `computed_definition` below.
        if (self != nullptr && index < self->array_elements.size() &&
            (definition.enumerable.has_value() || definition.configurable.has_value())) {
            JsValue::make_object(self).set_array_hole(index);
            self->has_accessors = true;
            uint8_t flags = PropertyMap::Writable;
            if (!definition.enumerable.has_value() || *definition.enumerable) flags |= PropertyMap::Enumerable;
            if (!definition.configurable.has_value() || *definition.configurable) flags |= PropertyMap::Configurable;
            auto& entry = self->properties.entry(name);
            entry.second = JsValue::make_undefined();      // a placeholder: `get` reads the binding
            entry.setter.reset();
            entry.flags = flags;
        }
        return Definition::Kept;
    }

    /// Gives the place up: the element becomes a hole, the table holds the
    /// value left behind under the *index* (not the parameter's name — the
    /// place's name is what a property read, a key walk and a descriptor look
    /// up), and later writes to the parameter no longer reach it. Both halves
    /// are needed: the elements are dense and carry no attributes, so a
    /// defined descriptor lives in the table, and a hole is what makes the
    /// table's entry the property.
    void unmap(JsObject* self, const std::string& name, size_t index, const JsValue& kept) {
        if (self != nullptr && index < self->array_elements.size()) {
            JsValue::make_object(self).set_array_hole(index);
            self->has_accessors = true;
            self->properties.entry(name).second = kept;
        }
        m_names[index].clear();
    }

    /// The value of a mapped place, which is its parameter's and is in no
    /// table. `Object.getOwnPropertyDescriptor` asks, because the entry a
    /// defined place keeps holds a placeholder and not the value.
    bool host_value(JsObject*, const std::string& name, JsValue& value) override {
        JsEnvironment::Binding* binding = mapped_binding(mapped_index(name));
        if (binding == nullptr) return false;
        value = binding->value;
        return true;
    }

    /// The mapping does not add or remove keys: the places are already elements
    /// of the object, so `Object.keys` is answered by the object itself.
    void keys(JsObject*, std::vector<std::string>&) override {}

private:
    /// "0", "1", … as an index; anything else is not a mapped place.
    static size_t mapped_index(const std::string& name) {
        if (name.empty() || name.size() > 10) return static_cast<size_t>(-1);
        size_t index = 0;
        for (const char c : name) {
            if (c < '0' || c > '9') return static_cast<size_t>(-1);
            index = index * 10 + static_cast<size_t>(c - '0');
        }
        // A leading zero is not an index key ("01" is a name, not a place).
        if (name.size() > 1 && name[0] == '0') return static_cast<size_t>(-1);
        return index;
    }

    std::shared_ptr<JsEnvironment> m_env;
    /// The parameter name each place is mapped onto, or empty when it is not
    /// mapped. Built from the formal parameters, so `arguments[2]` in
    /// `function (a, b)` is not mapped — there is no third parameter.
    std::vector<std::string> m_names;
};

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
            // §10.4.4: an arguments object's [[Prototype]] is
            // %Object.prototype%, not %Array.prototype% — it is an ordinary
            // object that happens to have index properties and a `length`,
            // not an Array exotic object. `make_array` installed
            // Array.prototype, so `Object.getPrototypeOf(arguments)` was
            // wrong and every inherited name came out of the wrong place:
            // `arguments.push` was a function, `Array.isArray(arguments)` was
            // refused only by the class check above, and the `#<Array>` in a
            // delete refusal's message where V8 says `#<Object>` (Phase 123's
            // row, finding 286).
            //
            // §10.4.4.7 gives the object its own `@@iterator` — *the same
            // function object* as %Array.prototype.values% — because it no
            // longer inherits one. Without it `[...arguments]`, `for…of` over
            // `arguments` and `yield* arguments` all stop working, which is
            // what 75 test262 cases said when the prototype alone was changed.
            JsObject* object_proto = JsIntrinsics::get().object_proto;
            JsObject* array_proto = JsIntrinsics::get().array_proto;
            arguments.object_val->prototype = object_proto;
            if (array_proto != nullptr) {
                for (const char* key : {"@@iterator", "Symbol.iterator"}) {
                    const auto found = array_proto->properties.find(key);
                    if (found == array_proto->properties.end()) continue;
                    arguments.define_property(key, found->second, PropertyMap::Writable | PropertyMap::Configurable);
                }
            }
            // §10.4.4.6: an own `callee` holding the function being run. It is
            // writable and configurable and does not enumerate, and in strict
            // code the object does not have it at all — which the `else` below
            // leaves alone, so `arguments.callee` there is `undefined` because
            // nothing has the name, not because it was set to undefined.
            //
            // §10.4.4.2's mapping comes with it, for the sloppy case only: a
            // strict function's parameters and its `arguments` are separate
            // from the first line. It needs simple parameters (an identifier
            // each) because §10.4.4 says so, and a function value to name.
            if (!node->is_strict) {
                if (callee.is_function()) {
                    auto& callee_entry = arguments.object_val->properties.entry("callee");
                    callee_entry.second = callee;
                    callee_entry.flags = PropertyMap::Writable | PropertyMap::Configurable;
                }
                if (node->simple_params && !node->is_arrow && !node->is_method && !node->is_generator &&
                    !node->is_async) {
                    std::vector<std::string> parameter_names;
                    parameter_names.reserve(node->params.size());
                    for (const auto& param : node->params) {
                        parameter_names.push_back(param.target && param.target->kind == Pattern::Kind::Identifier
                                                      ? param.target->name
                                                      : std::string());
                    }
                    arguments.object_val->exotic =
                        std::make_shared<MappedArguments>(env, std::move(parameter_names));
                }
            }
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
    EnvPtr exec_env = env;
    auto prepare = [&] {
        EnvPtr param_env = env;
        if (node->has_parameter_expressions) {
            param_env = std::make_shared<JsEnvironment>(env);
            param_env->strict_mode = node->is_strict ? 1 : 0;
        }
        {
            EnvScope param_scope(param_env != env ? param_env : nullptr);
            bind_parameters(*node, param_env, args);
        }
        EnvPtr body_var_env = env;
        if (node->has_parameter_expressions) {
            body_var_env = std::make_shared<JsEnvironment>(param_env);
            body_var_env->mark_var_scope();
            body_var_env->strict_mode = node->is_strict ? 1 : 0;
            for (const auto& b : param_env->bindings()) {
                body_var_env->define(b.name, b.value);
            }
        }
        for (const auto& name : node->var_names) {
            if (!body_var_env->has_own(name)) body_var_env->define(name, JsValue::make_undefined());
        }
        if (node->body) {
            if (!node->body->lexical_decls.empty()) {
                auto body_lex_env = std::make_shared<JsEnvironment>(body_var_env);
                body_lex_env->strict_mode = node->is_strict ? 1 : 0;
                declare_lexicals(node->body->lexical_decls, body_lex_env);
                exec_env = body_lex_env;
            } else {
                exec_env = body_var_env;
            }
            for (const auto* decl : node->body->functions) {
                body_var_env->define(decl->fn->name, make_function(decl->fn, exec_env));
            }
        } else {
            exec_env = body_var_env;
        }
    };
    if (node->is_async && !node->is_generator) {
        JsValue thrown;
        if (!js_capture_throw(prepare, thrown)) return Promise::make_rejected(thrown);
        return run_async_body(node, exec_env);
    }
    prepare();

    if (node->is_generator) return make_generator(node, exec_env);

    // A base class runs its field initialisers before the constructor body; a
    // derived class runs them when super() returns.
    if (node->is_class_constructor && !node->is_derived_constructor) {
        initialize_fields(callee, this_val);
    }
    JsValue result = run_function_body(*node, exec_env);
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

// Prepares a suspendable frame for `node`'s body. The OS thread is started
// lazily on the first resume() so an unstarted generator owns no thread at
// all; once started, the thread parks at each suspension point and runs only
// while someone is inside resume().
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
    // would keep a frame that is never resumed alive forever. It also reads
    // `raw->frame_env` by reference instead of capturing a second `EnvPtr`,
    // so the frame's own `frame_env` is the single reference to `env`.
    GeneratorData* raw = frame.get();
    JsIntrinsics* realm = m_realm.get();
    EventLoop* loop = &EventLoop::default_loop();
    const uint32_t agent = Heap::current_agent();
    frame->start_thunk = [this, raw, realm, loop, agent, node = node]() mutable {
        // The frame is part of the agent that created it.
        GeneratorContext::set_active(raw);
        JsIntrinsics::set_current(realm);
        EventLoop::set_current(loop);
        Heap::join_agent(agent);

        JsValue result;
        JsValue error;
        bool failed = false;
        {
            FrameScopeGuard scope_guard(raw, active_scopes());
            if (raw->wait_for_start()) {
                try {
                    const EnvPtr& frame_env = raw->frame_env;
                    EnvScope env_scope(frame_env, /*owned_local_ref=*/false);
                    result = run_function_body(*node, frame_env);
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
        }
        // Let go of the AST node before handing control back: after finish()
        // this thread must not touch anything another thread can see.
        node.reset();
        GeneratorContext::set_active(nullptr);
        raw->finish(std::move(result), failed, std::move(error));
    };
    return frame;
}

namespace {

bool resume_frame(GeneratorData& frame, JsValue sent) {
    try {
        return frame.resume(std::move(sent));
    } catch (const std::system_error&) {
        js_throw_error(JsErrorKind::RangeError, "Too many suspended functions: out of threads");
    }
}

// Runs an async function's frame until its next `await` (or its end), then
// arranges for itself to be called again when the awaited promise settles.
void drive_coroutine(const std::shared_ptr<GeneratorData>& frame, const std::shared_ptr<PromiseData>& promise,
                     const JsValue& sent, bool is_throw) {
    if (frame->cancelled) return;       // the interpreter is gone
    if (is_throw) frame->inject_throw(sent);
    resume_frame(*frame, is_throw ? JsValue::make_undefined() : sent);
    if (frame->cancelled) return;

    if (frame->is_done) {
        frame->reap();      // the body has ended; its thread exits now
        JsValue error;
        if (frame->take_error(error)) {
            frame->yielded_value = JsValue::make_undefined();
            Promise::reject(promise, error);
        } else {
            JsValue value = std::move(frame->yielded_value);
            frame->yielded_value = JsValue::make_undefined();
            Promise::resolve(promise, value);
        }
        return;
    }

    // Parked at `await value`. Whatever it settles to is sent back in, from a
    // microtask: the code after an await never runs synchronously.
    JsValue awaited = std::move(frame->yielded_value);
    frame->yielded_value = JsValue::make_undefined();
    Promise::subscribe(Promise::to_promise(awaited),
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

    JsObject* cell = Heap::default_heap().allocate_object();
    cell->klass = ObjectClass::Generator;
    cell->internal = gen;
    cell->trace_extra = [cell](Heap& heap) {
        if (!cell->internal) return;
        auto* frame = static_cast<GeneratorData*>(cell->internal.get());
        const bool sole_owner = (cell->internal.use_count() == 1);
        if (heap.counting()) {
            // Only count the frame's references when this cell is the sole
            // owner of the GeneratorData and the frame is not actively running
            // on its worker thread. Otherwise native code (an in-progress
            // resume() or interpreter teardown) holds it, so it stays rooted.
            if (!sole_owner || frame->is_running()) return;
            if (!heap.first_visit(frame)) return;
            if (frame->frame_env) heap.count_environment(frame->frame_env.get(), 1);
            if (frame->active_scope_stack) {
                auto* stack = static_cast<ScopeStack*>(frame->active_scope_stack);
                for (const ScopeEntry& entry : stack->scopes) {
                    if (entry.owned_local_ref) heap.count_environment(entry.env, 1);
                }
            }
            heap.mark_value(frame->send_value);
            heap.mark_value(frame->yielded_value);
            heap.mark_value(frame->error);
            heap.mark_value(frame->pending_throw);
            heap.mark_value(frame->pending_return);
            return;
        }
        if (!heap.first_visit(frame)) return;
        if (frame->frame_env) heap.mark_environment(frame->frame_env.get());
        if (frame->active_scope_stack) {
            auto* stack = static_cast<ScopeStack*>(frame->active_scope_stack);
            for (const ScopeEntry& entry : stack->scopes) {
                heap.mark_environment(entry.env);
            }
        }
        heap.mark_value(frame->send_value);
        heap.mark_value(frame->yielded_value);
        heap.mark_value(frame->error);
        heap.mark_value(frame->pending_throw);
        heap.mark_value(frame->pending_return);
    };
    JsValue object = JsValue::make_object(cell);

    auto result_object = [](const JsValue& value, bool done) {
        JsValue result = JsValue::make_object();
        result.set_property("value", value);
        result.set_property("done", JsValue::make_boolean(done));
        return result;
    };

    auto current_gen = []() -> std::shared_ptr<GeneratorData> {
        const auto& cap = JsValue::current_captured();
        if (cap.empty() || !cap[0].object_val) return nullptr;
        return std::static_pointer_cast<GeneratorData>(cap[0].object_val->internal);
    };

    // Runs the body to its next yield and reports what happened.
    auto advance = [result_object](const std::shared_ptr<GeneratorData>& frame, const JsValue& sent) -> JsValue {
        if (!frame || frame->is_done) return result_object(JsValue::make_undefined(), true);
        if (!resume_frame(*frame, sent)) js_throw_type_error("Generator is already running");
        JsValue error;
        if (frame->take_error(error)) {
            frame->yielded_value = JsValue::make_undefined();
            frame->reap();
            js_throw(error);        // the body threw: it surfaces in the caller of next()
        }
        if (frame->is_done) {
            // The return value is reported once; later calls see undefined.
            JsValue returned = std::move(frame->yielded_value);
            frame->yielded_value = JsValue::make_undefined();
            frame->reap();
            return result_object(returned, true);
        }
        if (frame->yielded_raw) {
            // A sync generator's `yield*` passes the delegate's raw step
            // object out; the consumer reads value/done off it lazily.
            frame->yielded_raw = false;
            JsValue raw = std::move(frame->yielded_value);
            frame->yielded_value = JsValue::make_undefined();
            return raw;
        }
        JsValue yielded = std::move(frame->yielded_value);
        frame->yielded_value = JsValue::make_undefined();
        return result_object(yielded, false);
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

    // Methods keep the generator object in Extra::captured rather than
    // capturing `shared_ptr<GeneratorData>` inside their std::function, so the
    // collector accounts for the object <-> method cycle and `cell->internal`
    // remains the sole owner of the frame.
    auto define_gen_method = [&](const char* name, int length, JsValue::NativeFn fn) {
        JsValue method = js_make_method(name, length, std::move(fn));
        method.set_payload_captured({object});
        object.define_property(name, std::move(method), PropertyMap::Hidden);
    };

    define_gen_method("next", 1, [current_gen, advance, deliver](const std::vector<JsValue>& args) {
        const JsValue sent = js_arg(args, 0);
        return deliver([&] { return advance(current_gen(), sent); });
    });

    define_gen_method("return", 1, [current_gen, result_object, deliver, advance](const std::vector<JsValue>& args) {
        const JsValue value = js_arg(args, 0);
        return deliver([&] {
            auto frame = current_gen();
            if (!frame) return result_object(value, true);
            if (frame->is_running()) js_throw_type_error("Generator is already running");
            if (!frame->started || frame->is_done) {
                // Nothing is suspended: the return is answered without
                // resuming the body. A yield already performed inside a
                // `finally` while closing cannot have happened yet, so this
                // settles immediately.
                frame->terminate(false);
                return result_object(value, true);
            }
            // Resume suspended at the yield with a return completion. The
            // body unwinds through its `finally` blocks with the value; a
            // `yield*` in the way routes the completion into the delegate's
            // `return` method first. Whatever the frame completes with is
            // handed to the consumer as an iterator result.
            frame->inject_return(value);
            return advance(frame, JsValue::make_undefined());
        });
    });

    define_gen_method("throw", 1, [current_gen, advance, deliver](const std::vector<JsValue>& args) {
        const JsValue error = js_arg(args, 0);
        return deliver([&]() -> JsValue {
            auto frame = current_gen();
            if (!frame) js_throw(error);
            if (frame->is_running()) js_throw_type_error("Generator is already running");
            if (!frame->started || frame->is_done) {
                // Nothing is suspended to receive it: the generator just ends.
                frame->terminate(false);
                js_throw(error);
            }
            // Raised inside the body at the suspended `yield`, where the
            // generator's own try/catch can handle it.
            frame->inject_throw(error);
            return advance(frame, JsValue::make_undefined());
        });
    });

    auto self_fn = [](const std::vector<JsValue>&) {
        const auto& cap = JsValue::current_captured();
        if (cap.empty()) return JsValue::make_undefined();
        return cap[0];
    };
    define_gen_method(is_async ? kAsyncIteratorKey : kIteratorKey, 0, self_fn);
    if (!is_async) define_gen_method("Symbol.iterator", 0, self_fn);
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

JsValue JsInterpreter::define_class(const std::shared_ptr<ClassNode>& cls, const EnvPtr& env,
                                    const std::string& name_hint) {
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
        } else {
            // §15.7.14 step 5: the parent's `prototype` must be an object or
            // null, and anything else is a TypeError. A *bound* function is
            // the case this exists for: it is a constructor (§10.4.1.3 gives
            // it [[Construct]]) but it has no `prototype` property at all, so
            // `class X extends f.bind(null) {}` has nothing to put in the
            // chain and Node refuses it. Reading the property and quietly
            // ignoring a missing one accepted the class and left its instances
            // inheriting straight from `%Object.prototype%`.
            js_throw_type_error("Class extends value does not have valid prototype property " +
                                js_describe_thrown(parent_proto));
        }
    } else if (cls->super_class) {
        proto.object_val->prototype = nullptr;       // `extends null`
    }

    // NamedEvaluation (§8.6.3 applies to a class as it does to a function):
    // `cls->name` is the name the source *spells*, and is empty for
    // `class {}`. A class written as an initialiser takes the name of what it
    // is bound to instead — `var a = class {}` is named `a`, and so is
    // `{ p: class {} }` and `function f(x = class {})`.
    //
    // It takes it as a *name*, not as a binding. That distinction is the whole
    // of this phase: a class that spells a name gets a binding for it inside
    // its own body, but one that is merely *inferred* does not, so the body
    // still reads whatever `a` means where the class was written. Two cases in
    // the corpus turn on it: `for (...) C = class extends C {}` has to keep
    // extending the *previous* `C`, and a static method's `this === C` has to
    // answer against the outer binding, not a copy of the class. An inner
    // binding here shadows both.
    const std::string inferred = cls->name.empty() ? name_hint : cls->name;
    JsValue ctor = make_function(cls->constructor, class_env, inferred);
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
    // What each computed key evaluated to, for the `name` of the function the
    // member defines: a symbol key is named from its description, which the
    // string form has already thrown away.
    std::vector<JsValue> member_key_values;
    member_keys.reserve(cls->members.size());
    member_key_values.reserve(cls->members.size());
    for (const auto& member : cls->members) {
        JsValue host_value;
        if (member.computed_key) host_value = evaluate(member.computed_key.get(), class_env);
        member_keys.push_back(member.computed_key
                                  ? js_to_property_key(host_value)
                                  : member.is_private ? private_prefix + member.key
                                                      : member.key);
        member_key_values.push_back(std::move(host_value));
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
        // What the function is called. A method is named after the key it was
        // defined by; an accessor with `get `/`set ` in front of it. A private
        // name is spelled as it was written, so `#m` stays `#m` and
        // `get #x` stays `get #x` — it can only be written literally.
        const bool key_computed = !member.is_private && member.computed_key != nullptr;

        switch (member.kind) {
            case ClassMember::Kind::Method: {
                if (!member.is_private && member.is_static && key == "prototype") {
                    js_throw_type_error("Classes may not have a static property named 'prototype'");
                }
                JsValue fn = make_function(member.fn, class_env);
                // A private name is spelled as written (`#m`), never with the
                // prefix the environment uses to keep it apart from a string.
                name_function(fn, member.is_private ? member.key
                                                    : defined_name(key, key_computed ? &member_key_values[i] : nullptr, ""));
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
                if (!member.is_private && member.is_static && key == "prototype") {
                    js_throw_type_error("Classes may not have a static property named 'prototype'");
                }
                JsValue fn = make_function(member.fn, class_env);
                name_function(fn, member.is_private
                                      ? std::string(member.kind == ClassMember::Kind::Getter ? "get " : "set ") +
                                            member.key
                                      : defined_name(key, key_computed ? &member_key_values[i] : nullptr,
                                                     member.kind == ClassMember::Kind::Getter ? "get " : "set "));
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
                    field_env->mark_var_scope();
                    field_env->strict_mode = 1;
                    field_env->define("this", ctor);
                    field_env->define("%home", ctor);
                    field_env->define("%new.target", JsValue::make_undefined());
                    // A function or arrow written *in* the initialiser is
                    // named by its field, as `x = function () {}` is named
                    // `x`; that is a different question from what the member's
                    // own function is called, above.
                    value = evaluate_named(member.value.get(), field_env, member.key);
                }
                if (!member.is_private && key == "prototype") {
                    js_throw_type_error("Classes may not have a static property named 'prototype'");
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
    field_env->mark_var_scope();
    field_env->strict_mode = 1;
    field_env->define("this", instance);
    field_env->define("%new.target", JsValue::make_undefined());
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

namespace {

/// The tag V8 prints for a value inside `[object ...]` in a refused `delete`:
/// the three kinds a browser shows by tag — an array, a Date, a RegExp — and
/// the wrapper objects. Empty for everything else, which prints by class name.
std::string printed_tag(const JsValue& value) {
    const JsObject* cell = value.property_cell();
    if (!cell) return {};
    switch (cell->klass) {
        case ObjectClass::Array: return "Array";
        case ObjectClass::Date: return "Date";
        case ObjectClass::RegExp: return "RegExp";
        case ObjectClass::String: return "String";
        case ObjectClass::Number: return "Number";
        case ObjectClass::Boolean: return "Boolean";
        case ObjectClass::Symbol: return "Symbol";
        default: return {};
    }
}

/// V8's words for a refused `delete`, which is what the corpus records: a
/// function and an Error print as themselves, an array, a Date, a RegExp and
/// the wrappers by their tag, and every other object as `#<Constructor>`.
std::string delete_refusal_message(const JsValue& base, const std::string& key) {
    std::string described;
    const JsObject* cell = base.property_cell();
    const bool is_error = cell && cell->klass == ObjectClass::Error;
    if (base.is_function() || is_error) {
        described = base.to_string();
    } else {
        const std::string tag = printed_tag(base);
        if (!tag.empty()) {
            described = "[object " + tag + "]";
        } else if (!base.prototype_object()) {
            // An object with no prototype prints by tag: there is no
            // `Object.prototype` to give it a class name.
            described = "[object Object]";
        } else {
            // The name of the class, read off the prototype's own `constructor`
            // without running a getter or anything a script could observe.
            std::string name = "Object";
            if (const JsObject* proto = base.prototype_object()) {
                auto it = proto->properties.find("constructor");
                if (it != proto->properties.end() && !(it->flags & PropertyMap::Accessor) && it->second.is_function()) {
                    JsValue declared = it->second.get_property("name");
                    if (declared.type == JsType::String && declared.string_val.length()) name = declared.string_val.str();
                }
            }
            described = "#<" + name + ">";
        }
    }
    return "Cannot delete property '" + key + "' of " + described;
}

/// ECMA-262 §9.1.1.2.1 Object Environment Record HasBinding(N) with
/// `[[IsWithEnvironment]] = true`. Checks `HasProperty(O, N)` first and only
/// consults `O[@@unscopables]` when the property exists.
bool with_object_has_binding(const JsValue& obj, const std::string& name) {
    // `this` and internal synthetic slots (`%fn`, `%home`, `%new.target`, `#...`)
    // are never resolved against a `with` binding object.
    if (name == "this" || (!name.empty() && (name[0] == '%' || name[0] == '#'))) return false;
    if (!obj.has_property(name)) return false;
    JsValue unscopables = obj.get_property("@@unscopables");
    if (unscopables.is_object_like()) {
        if (unscopables.get_property(name).is_truthy()) return false;
    }
    return true;
}

} // namespace

bool JsInterpreter::try_lookup_identifier(const std::string& name, const EnvPtr& env, JsValue& out_value,
                                          JsValue* out_with_base) {
    if (env->has_with_ancestor()) {
        for (JsEnvironment* cur = env.get(); cur; cur = cur->parent_raw()) {
            if (cur->is_with_scope()) {
                if (with_object_has_binding(cur->with_object(), name)) {
                    if (out_with_base) *out_with_base = cur->with_object();
                    out_value = cur->with_object().get_property(name);
                    return true;
                }
                continue;
            }
            if (auto* binding = cur->find_own(name)) {
                if (!binding->is_initialized) throw_tdz_error(name);
                out_value = binding->value;
                return true;
            }
        }
    } else if (auto* binding = env->lookup(name)) {
        if (!binding->is_initialized) throw_tdz_error(name);
        out_value = binding->value;
        return true;
    }

    // Properties of the global object are global variables too: `window.x = 1`
    // makes `x` resolvable.
    if (JsObject* global = m_global_object.object_val) {
        int guard = 0;
        for (JsObject* obj = global; obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
            auto it = obj->properties.find(name);
            if (it == obj->properties.end()) continue;
            if (it->flags & PropertyMap::Accessor) {
                out_value = it->second.is_function() ? it->second.call_with_this(m_global_object, {})
                                                     : JsValue::make_undefined();
                return true;
            }
            out_value = it->second;
            return true;
        }
        // What the global object computes: the elements of the page by id.
        if (global->exotic) {
            JsValue computed;
            if (global->exotic->get(global, name, computed) || global->exotic->get_last(global, name, computed)) {
                out_value = std::move(computed);
                return true;
            }
        }
    }
    return false;
}

JsInterpreter::Reference JsInterpreter::resolve_identifier_reference(const std::string& name, const EnvPtr& env) {
    Reference ref;
    ref.name = name;
    if (!env->has_with_ancestor()) return ref;
    for (JsEnvironment* cur = env.get(); cur; cur = cur->parent_raw()) {
        if (cur->is_with_scope()) {
            if (with_object_has_binding(cur->with_object(), name)) {
                ref.is_property = true;
                ref.is_with = true;
                ref.base = cur->with_object();
                return ref;
            }
            continue;
        }
        if (cur->find_own(name)) {
            ref.resolved_env = cur;
            return ref;
        }
    }
    return ref;
}

JsValue JsInterpreter::lookup_identifier(const std::string& name, const EnvPtr& env) {
    JsValue value;
    if (try_lookup_identifier(name, env, value, nullptr)) return value;
    js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
}

void JsInterpreter::assign_identifier(const std::string& name, const JsValue& value, const EnvPtr& env) {
    if (env->has_with_ancestor()) {
        for (JsEnvironment* cur = env.get(); cur; cur = cur->parent_raw()) {
            if (cur->is_with_scope()) {
                if (with_object_has_binding(cur->with_object(), name)) {
                    JsValue target = cur->with_object();
                    if (!target.set_property(name, value) && env->is_strict()) {
                        js_throw_type_error("Cannot set property '" + name + "' of this object");
                    }
                    return;
                }
                continue;
            }
            if (auto* b = cur->find_own(name)) {
                if (!b->is_initialized) throw_tdz_error(name);
                if (b->is_const) js_throw_type_error("Assignment to constant variable.");
                if (b->is_readonly) {
                    if (env->is_strict()) {
                        js_throw_type_error("Cannot assign to read only property '" + name + "' of object '#<Object>'");
                    }
                    return;
                }
                b->value = value;
                return;
            }
        }
    } else {
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
    }
    if (JsObject* global = m_global_object.object_val) {
        if (auto property = global->properties.find(name); property != global->properties.end()) {
            // `try_assign` above found no environment binding. An own writable
            // data property on an unfrozen global object is the global-object
            // record for a script `var`; the WindowExotic and assign-hook would
            // only repeat that failed binding lookup before ordinary [[Set]]
            // stores it. Skip those misses on the hot path; accessors,
            // read-only properties, and frozen globals still need the generic
            // setter, including its strict-mode refusal.
            if (!global->frozen && !(property->flags & PropertyMap::Accessor) &&
                (property->flags & PropertyMap::Writable)) {
                property->second = value;
                return;
            }
            JsValue target = m_global_object;
            if (!target.set_property(name, value) && env->is_strict()) {
                js_throw_type_error("Cannot set property '" + name + "' of this object");
            }
            return;
        }
    }
    if (env->is_strict()) js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
    // Sloppy mode: an implicit global. §9.1.1.4 gives it
    // `{writable: true, enumerable: true, configurable: true}` — it is neither
    // a built-in nor a declaration, so it is the one kind of global that is
    // deletable and enumerable both. `define` is what a built-in's binding
    // went through, and it cannot tell the two apart, so the name was left
    // looking like a library global: `zz = 3` reported `configurable: false`
    // and `delete globalThis.zz` refused. `define_script_global` says which
    // it is (Phase 431's remainder, closed here).
    m_global_env->define_script_global(name, value);
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

JsInterpreter::Reference JsInterpreter::resolve_reference(const Expr* target, const EnvPtr& env,
                                                            bool throw_on_nullish) {
    Reference ref;
    switch (target->kind) {
        case NodeKind::Identifier: {
            const std::string& name = static_cast<const IdentifierExpr*>(target)->name;
            if (env->has_with_ancestor()) return resolve_identifier_reference(name, env);
            ref.name = name;
            return ref;
        }
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
    if (throw_on_nullish) check_reference_base(ref);
    return ref;
}

void JsInterpreter::check_reference_base(const Reference& ref) {
    if (!ref.is_property || !ref.base.is_nullish()) return;
    js_throw_type_error(std::string("Cannot set properties of ") +
                        (ref.base.type == JsType::Null ? "null" : "undefined") +
                        " (setting '" + ref.name + "')");
}

JsValue JsInterpreter::read_reference(const Reference& ref, const EnvPtr& env) {
    if (ref.is_private) return ref.base.get_private(ref.private_key, ref.name);
    if (ref.is_property) return ref.base.get_property(ref.name);
    if (ref.resolved_env) {
        if (auto* b = ref.resolved_env->find_own(ref.name)) {
            if (!b->is_initialized) throw_tdz_error(ref.name);
            return b->value;
        }
    }
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
        // ECMA-262 §9.1.1.2.5 SetMutableBinding(N, V, S): if the property was
        // deleted from the `with` binding object between ResolveBinding and
        // PutValue, strict mode throws ReferenceError instead of recreating it.
        if (ref.is_with && env->is_strict() && target.object_val && !target.object_val->exotic &&
            !target.has_property(ref.name)) {
            js_throw_error(JsErrorKind::ReferenceError, ref.name + " is not defined");
        }
        // A write to a property this object does not take (a getter-only
        // index of a collection, say) is refused; strict mode throws, as
        // [[Set]]'s false requires.
        if (!target.set_property(ref.name, value) && env->is_strict()) {
            js_throw_type_error("Cannot set property '" + ref.name + "' of this object");
        }
        return;
    }
    if (ref.resolved_env) {
        if (auto* b = ref.resolved_env->find_own(ref.name)) {
            if (!b->is_initialized) throw_tdz_error(ref.name);
            if (b->is_const) js_throw_type_error("Assignment to constant variable.");
            if (b->is_readonly) {
                if (env->is_strict()) {
                    js_throw_type_error("Cannot assign to read only property '" + ref.name + "' of object '#<Object>'");
                }
                return;
            }
            b->value = value;
            return;
        }
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
                    // Inside a `with` scope, `var x = v` / `var [x] = ...`
                    // resolves through the `with` object environment record
                    // before falling back to the hoisted variable binding.
                    if (env->has_with_ancestor()) {
                        Reference ref = resolve_identifier_reference(pattern.name, env);
                        if (ref.is_with) {
                            write_reference(ref, value, env);
                            return;
                        }
                    }
                    // The binding was hoisted to the function scope on entry,
                    // or lives as a property on the global object.
                    if (auto* binding = env->lookup(pattern.name)) {
                        if (binding->is_readonly) return;      // `var undefined = 1` at the top level
                        binding->value = value;
                        binding->is_const = false;
                    } else if (JsObject* global = m_global_object.object_val) {
                        if (global->properties.find(pattern.name) != global->properties.end()) {
                            JsValue target = m_global_object;
                            if (!target.set_property(pattern.name, value) && env->is_strict()) {
                                js_throw_type_error("Cannot assign to read only property '" + pattern.name + "'");
                            }
                        } else {
                            m_global_object.define_property(pattern.name, value,
                                                            PropertyMap::Writable | PropertyMap::Enumerable);
                        }
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
            // Every way out of this loop other than `cursor.close()` at the
            // end — a throwing default, a target that throws, a `return` that
            // cannot be reached — must still tell the iterator it will not be
            // drained (7.4.11, an abrupt completion), and must let the
            // original throw through even when that close throws too.
            try {
                for (const auto& element : pattern.elements) {
                    if (element.rest) {
                        // AssignmentRestEvaluation resolves a simple target
                        // before draining the iterator. Nested array/object
                        // patterns instead receive the collected rest array
                        // and evaluate their own targets while binding it.
                        Reference target_ref;
                        const bool has_target_ref = mode == BindMode::Assign && element.target &&
                                                    element.target->kind == Pattern::Kind::Target;
                        if (has_target_ref) {
                            target_ref = resolve_reference(element.target->target.get(), env,
                                                           /*throw_on_nullish=*/false);
                        }
                        std::vector<JsValue> rest;
                        while (true) {
                            JsValue item = cursor.pull();
                            if (cursor.done()) break;
                            rest.push_back(std::move(item));
                        }
                        JsValue rest_value = JsValue::make_array(rest);
                        if (has_target_ref) {
                            check_reference_base(target_ref);
                            write_reference(target_ref, rest_value, env);
                        } else {
                            bind_pattern(*element.target, rest_value, env, mode);
                        }
                        break;
                    }
                    // An assignment target is evaluated before IteratorStep:
                    // `[object[yield]] = iterable` can suspend or throw
                    // while resolving the reference, and IteratorClose must
                    // see that completion before `next` is called. Defer the
                    // nullish-base check until the value (and any default) is
                    // ready, just as plain `=` does.
                    Reference target_ref;
                    const bool has_target_ref = mode == BindMode::Assign && element.target &&
                                                element.target->kind == Pattern::Kind::Target;
                    if (has_target_ref) {
                        target_ref = resolve_reference(element.target->target.get(), env,
                                                       /*throw_on_nullish=*/false);
                    }
                    JsValue item = cursor.pull();
                    if (!element.target) continue;       // hole: `[a, , b]`
                    if (item.is_undefined() && element.default_value) {
                        item = element.target->kind == Pattern::Kind::Identifier
                            ? evaluate_named(element.default_value.get(), env, element.target->name)
                            : evaluate(element.default_value.get(), env);
                    }
                    if (has_target_ref) {
                        check_reference_base(target_ref);
                        write_reference(target_ref, item, env);
                    } else {
                        bind_pattern(*element.target, item, env, mode);
                    }
                }
            } catch (const GeneratorReturnException&) {
                // IteratorClose observes the original completion. A throwing
                // `return` method, or a non-object result, replaces a Return
                // completion (unlike a Throw completion, which wins over the
                // close's result). `close_quietly()` is only correct for an
                // existing throw, so let this close report its own failure.
                if (!cursor.next_threw()) cursor.close();
                throw;
            } catch (const GeneratorAbortException& abort) {
                if (abort.run_finalizers && !cursor.next_threw()) cursor.close_quietly();
                throw;
            } catch (...) {
                if (!cursor.next_threw()) cursor.close_quietly();
                throw;
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
                    // ECMA-262 §7.3.24 CopyDataProperties: everything own and
                    // enumerable (string and symbol keys) not already picked out.
                    JsValue rest = JsValue::make_object();
                    copy_data_properties(rest, value, &used);
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
            if (is_name && env->has_with_ancestor()) {
                // ECMA-262 §14.3.2.1: ResolveBinding(bindingId) runs before the initializer.
                Reference lhs = resolve_identifier_reference(decl.target->name, env);
                JsValue value = evaluate_named(decl.init.get(), env, decl.target->name);
                if (lhs.is_with) {
                    write_reference(lhs, value, env);
                } else {
                    bind_pattern(*decl.target, value, env, BindMode::Var);
                }
                continue;
            }
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

void JsInterpreter::gc_stress_point() {
    Heap& heap = Heap::default_heap();
    if (!heap.take_gc_stress_request()) return;
    // The collector finds its own roots, so nothing needs naming here; what
    // the stress mode adds is that the moment is chosen by an allocation count
    // rather than by the script.
    heap.collect_garbage();
}

JsInterpreter::Flow JsInterpreter::exec(const Stmt* stmt, const EnvPtr& env, Frame& frame) {
    // Statements nest too (`if (a) if (b) ...`, an `else if` ladder, blocks in
    // blocks), with no expression in between for evaluate()'s guard to catch.
    js_check_stack();
    gc_stress_point();
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
        case NodeKind::With:
            return exec_with(static_cast<const WithStmt*>(stmt), env, frame);
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

JsInterpreter::Flow JsInterpreter::exec_with(const WithStmt* stmt, const EnvPtr& env, Frame& frame) {
    // ECMA-262 §14.11.2: ToObject(GetValue(expr)) throws TypeError on null/undefined.
    JsValue obj = js_to_object(evaluate(stmt->object.get(), env));
    auto with_env = JsEnvironment::make_with_scope(env, std::move(obj));
    EnvScope env_scope(with_env);
    // UpdateEmpty(C, undefined): an empty body or `var` clears the prior completion.
    if (frame.track_completion) frame.completion = JsValue::make_undefined();
    return exec(stmt->body.get(), with_env, frame);
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
    if (frame.track_completion) frame.completion = JsValue::make_undefined();
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
    if (frame.track_completion) frame.completion = JsValue::make_undefined();
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
    EnvPtr owned_loop_env = stmt->has_lexical_init ? std::make_shared<JsEnvironment>(env) : nullptr;
    const EnvPtr& loop_env = owned_loop_env ? owned_loop_env : env;
    EnvScope loop_scope(owned_loop_env);
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
    if (frame.track_completion) frame.completion = JsValue::make_undefined();

    // `for (let i ...)` gives every iteration its own `i`, which is what makes
    // closures created in the body see the value from their own iteration.
    const bool per_iteration = !stmt->per_iteration_names.empty();
    EnvPtr owned_iter_env = per_iteration ? copy_scope(loop_env, env) : nullptr;

    while (true) {
        {
            const EnvPtr& iter_env = owned_iter_env ? owned_iter_env : loop_env;
            EnvScope iter_scope(owned_iter_env);
            if (stmt->test && !evaluate(stmt->test.get(), iter_env).is_truthy()) break;
            Flow flow = exec(stmt->body.get(), iter_env, frame);
            LoopAction action = loop_action(this, static_cast<int>(flow), frame.label, labels);
            if (action == LoopAction::Exit) break;
            if (action == LoopAction::Propagate) return flow;
        }
        if (per_iteration) {
            EnvPtr next_env = copy_scope(owned_iter_env, env);
            EnvScope next_scope(next_env);
            if (stmt->update) evaluate(stmt->update.get(), next_env);
            owned_iter_env = std::move(next_env);
        } else if (stmt->update) {
            evaluate(stmt->update.get(), loop_env);
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
    if (frame.track_completion) frame.completion = JsValue::make_undefined();

    // Binds the loop variable for one iteration and returns a newly allocated
    // lexical scope if needed (or nullptr when the enclosing `env` is used).
    auto bind_iteration = [&](const JsValue& value) -> EnvPtr {
        if (!stmt->declares) {
            bind_pattern(*stmt->target, value, env, BindMode::Assign);
            return nullptr;
        }
        if (stmt->decl_kind == JsDeclKind::Var) {
            bind_pattern(*stmt->target, value, env, BindMode::Var);
            return nullptr;
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
        EnvPtr owned_iter = bind_iteration(value);
        const EnvPtr& iter_env = owned_iter ? owned_iter : env;
        EnvScope iter_scope(owned_iter);
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
                    if (stmt->param->kind == Pattern::Kind::Identifier) {
                        scope->is_simple_catch = true;
                        scope->simple_catch_name = stmt->param->name;
                    }
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
        } catch (const GeneratorAbortException& abort) {
            if (!abort.run_finalizers) throw;
            pending = std::current_exception();
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
    // `const f = function () {}` names the function after the binding. What
    // the parentheses around the *initialiser* are does not matter: test262
    // asserts `[cover = (function () {})]` names it `cover`, so the named
    // evaluation is chosen by the context, not by the expression's own
    // grouping. Only a grouped *assignment target* changes the answer, and
    // that is decided where the target is (see eval_assign).
    if (expr->kind == NodeKind::Function) {
        auto* fn_expr = static_cast<const FunctionExpr*>(expr);
        if (fn_expr->fn->name.empty()) return make_function(fn_expr->fn, env, name_hint);
    } else if (expr->kind == NodeKind::Class) {
        auto* cls_expr = static_cast<const ClassExpr*>(expr);
        if (cls_expr->cls->name.empty()) return define_class(cls_expr->cls, env, name_hint);
    }
    return evaluate(expr, env);
}

std::vector<JsValue> JsInterpreter::eval_arguments(const std::vector<std::unique_ptr<Expr>>& args,
                                                   const EnvPtr& env) {
    std::vector<JsValue> values;
    values.reserve(args.size());
    for (const auto& arg : args) {
        if (arg->kind == NodeKind::Spread) {
            const auto* spread = static_cast<const SpreadExpr*>(arg.get());
            JsValue iterable = evaluate(spread->argument.get(), env);
            if (spread->synthesised && iterable.is_array()) {
                // The implicit `constructor(...args) { super(...args); }` of a
                // derived class forwards the arguments list; the language does
                // not iterate it (ECMA-262 15.7.14), so a script that patched
                // `Array.prototype[Symbol.iterator]` is not called by `new
                // Derived(1, 2)`. Copying the elements is what the arguments
                // list already holds. A spread a script wrote keeps the
                // js_iterate path below, whatever the constructor looks like.
                for (const JsValue& item : iterable.array_elements()) values.push_back(item);
            } else {
                js_iterate(iterable, [&](const JsValue& item) {
                    values.push_back(item);
                    return true;
                });
            }
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
                if (env->has_with_ancestor()) {
                    JsValue val;
                    if (!try_lookup_identifier(name, env, val, nullptr)) {
                        return js_typeof_value(JsValue::make_undefined());
                    }
                    return js_typeof_value(val);
                }
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
                if (base.delete_property(key)) return JsValue::make_boolean(true);
                // [[Delete]] answers false for a property that is not
                // configurable. Sloppy code reads that as the answer `false`;
                // strict code makes it a TypeError, which is DeletePropertyOrThrow.
                if (env->is_strict()) js_throw_type_error(delete_refusal_message(base, key));
                return JsValue::make_boolean(false);
            }
            if (operand->kind == NodeKind::Identifier) {
                const std::string& name = static_cast<const IdentifierExpr*>(operand)->name;
                // `this` is kept as an identifier by the parser and is not a
                // reference, so there is no property to refuse.
                if (name == "this") return JsValue::make_boolean(true);
                // The parser refuses this in strict code; a direct eval
                // inherits its caller's strictness only once it runs.
                if (env->is_strict()) {
                    js_throw_error(JsErrorKind::SyntaxError, "Delete of an unqualified identifier in strict mode.");
                }
                // A declarative binding is not a property and is not
                // deletable. A name the global object carries is one: a
                // `globalThis.x = 1` goes, a `var` is not configurable and
                // stays; and a name that resolves to nothing is deletable,
                // which is what `delete notDeclared` answers rather than a
                // ReferenceError.
                if (env->has_with_ancestor()) {
                    for (JsEnvironment* e = env.get(); e; e = e->parent_raw()) {
                        if (e->is_with_scope()) {
                            if (with_object_has_binding(e->with_object(), name)) {
                                return JsValue::make_boolean(e->with_object().delete_property(name));
                            }
                            continue;
                        }
                        if (JsEnvironment::Binding* own = e->find_own(name)) {
                            if (own->is_deletable) {
                                e->remove_own(name);
                                return JsValue::make_boolean(true);
                            }
                            return JsValue::make_boolean(false);
                        }
                    }
                } else if (JsEnvironment::Binding* b = env->lookup(name)) {
                    if (b->is_deletable) {
                        for (JsEnvironment* e = env.get(); e; e = e->parent_raw()) {
                            if (JsEnvironment::Binding* own = e->find_own(name)) {
                                if (own == b) {
                                    e->remove_own(name);
                                    return JsValue::make_boolean(true);
                                }
                                break;
                            }
                        }
                    }
                    return JsValue::make_boolean(false);
                }
                if (JsObject* global = m_global_object.object_val) {
                    if (global->properties.find(name) != global->properties.end()) {
                        return JsValue::make_boolean(m_global_object.delete_property(name));
                    }
                }
                return JsValue::make_boolean(!m_global_object.has_property(name));
            }
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
            if (env->has_with_ancestor()) {
                // §13.15.2: ResolveBinding(name) runs before evaluating the RHS.
                Reference ref = resolve_identifier_reference(name, env);
                JsValue value = target->explicitly_grouped ? evaluate(expr->value.get(), env)
                                                           : evaluate_named(expr->value.get(), env, name);
                write_reference(ref, value, env);
                return value;
            }
            // `(f) = function () {}` leaves the function anonymous: the target
            // is a parenthesised expression, not an IdentifierReference, so
            // this is not a named evaluation (see evaluate_named).
            JsValue value = target->explicitly_grouped ? evaluate(expr->value.get(), env)
                                                       : evaluate_named(expr->value.get(), env, name);
            assign_identifier(name, value, env);
            return value;
        }
            // The object and key are evaluated before the right-hand side, and
            // a base that is null or undefined is only an error *then*: the
            // right-hand side still runs, because the reference it would be
            // assigned to is resolved before it (§13.15.2). `base.prop =
            // count += 1` throws and leaves `count` at 1.
            Reference ref = resolve_reference(target, env, /*throw_on_nullish=*/false);
            JsValue value = evaluate(expr->value.get(), env);
            check_reference_base(ref);
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
        case NodeKind::Identifier: {
            const std::string& name = static_cast<const IdentifierExpr*>(callee)->name;
            callee_name = &name;
            if (env->has_with_ancestor()) {
                // ECMA-262 §13.3.6.1 step 4.b: calling a binding resolved on a
                // `with` object environment record passes `WithBaseObject()` as `this`.
                if (!try_lookup_identifier(name, env, fn, &this_val)) {
                    js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
                }
            } else {
                fn = lookup_identifier(name, env);
            }
            break;
        }
        default:
            fn = evaluate(callee, env);
            if (m_chain_short_circuit) return JsValue::make_undefined();
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
                                  direct && expr->direct_eval_allows_super_call,
                                  direct && expr->direct_eval_allows_new_target,
                                  direct && expr->direct_eval_forbid_arguments});
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
            // {...source}: ECMA-262 §7.3.24 CopyDataProperties (own enumerable
            // string and symbol keys, written via CreateDataProperty).
            JsValue source = evaluate(prop.value.get(), env);
            copy_data_properties(object, source, nullptr);
            continue;
        }

        // The key, and — for a computed one — the value it evaluated to, which
        // is what an anonymous function defined by this property is named
        // after when the key is a symbol (see defined_name).
        JsValue key_value;
        if (prop.computed_key) key_value = evaluate(prop.computed_key.get(), env);
        std::string key = prop.computed_key ? js_to_property_key(key_value) : prop.key;

        if (prop.kind == ObjectProperty::Kind::Getter || prop.kind == ObjectProperty::Kind::Setter) {
            auto* fn_expr = static_cast<const FunctionExpr*>(prop.value.get());
            JsValue fn = make_function(fn_expr->fn, env);
            name_function(fn, defined_name(key, prop.computed_key ? &key_value : nullptr,
                                           prop.kind == ObjectProperty::Kind::Getter ? "get " : "set "));
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
            JsValue fn = make_function(fn_expr->fn, env);
            name_function(fn, defined_name(key, prop.computed_key ? &key_value : nullptr, ""));
            if (fn_expr->fn->uses_super) fn.define_property("%home", object, PropertyMap::Hidden);
            object.define_property(key, fn, PropertyMap::Default);
            continue;
        }

        // `{ __proto__: base }` sets the prototype rather than a property, so
        // a function or a class written there is not named after the key.
        const bool sets_prototype = key == "__proto__" && !prop.computed_key && !prop.shorthand;
        // A function or class written as the value is named after the key too
        // (the third form SetFunctionName is applied to), and a symbol key
        // needs the `[description]` the string form has thrown away.
        JsValue value;
        if (sets_prototype) {
            value = evaluate(prop.value.get(), env);
        } else if (prop.computed_key && key_value.type == JsType::Symbol &&
                   (prop.value->kind == NodeKind::Function || prop.value->kind == NodeKind::Class)) {
            value = evaluate(prop.value.get(), env);
            if (prop.value->kind == NodeKind::Function &&
                static_cast<const FunctionExpr*>(prop.value.get())->fn->name.empty()) {
                name_function(value, defined_name(key, &key_value, ""));
            }
        } else {
            value = evaluate_named(prop.value.get(), env, key);
        }
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
        } catch (const GeneratorAbortException& abort) {
            if (abort.run_finalizers) close_inner();
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
    } else if (tag->kind == NodeKind::Identifier && env->has_with_ancestor()) {
        const std::string& name = static_cast<const IdentifierExpr*>(tag)->name;
        if (!try_lookup_identifier(name, env, fn, &this_val)) {
            js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
        }
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
    const double delta = update->op == JsTokenType::PlusPlus ? 1.0 : -1.0;
    // `i++` is the counter of every loop ever written. For a scope binding,
    // one lookup finds it and the write lands there instead of constructing a
    // `Reference` and walking the chain again. A top-level `var` is a global
    // object's own property in this tree, so its primitive data-property case
    // gets the same treatment below. With scopes, accessors, read-only
    // properties, coercions that can call user code and unresolved names keep
    // the general reference path (and its refusal behavior).
    if (update->target->kind == NodeKind::Identifier && !env->has_with_ancestor()) {
        const std::string& name = static_cast<const IdentifierExpr*>(update->target.get())->name;
        if (JsEnvironment::Binding* binding = env->lookup(name)) {
            if (!binding->is_initialized) throw_tdz_error(name);
            if (binding->is_const) js_throw_type_error("Assignment to constant variable.");
            if (binding->is_readonly) {
                if (env->is_strict()) {
                    js_throw_type_error("Cannot assign to read only property '" + name +
                                        "' of object '#<Object>'");
                }
                return JsValue::make_number(js_to_number(binding->value));
            }
            const double before = js_to_number(binding->value);
            const double after = before + delta;
            binding->value = JsValue::make_number(after);
            return JsValue::make_number(update->prefix ? after : before);
        }
        // A global `var` declared by a script is an own property of the
        // global object, not a JsEnvironment binding. Keep its numeric update
        // on the one-walk path too, but not when the global is frozen. Only
        // primitive values are safe here: ToNumeric on an object can call user
        // code that deletes, replaces or makes the property non-writable, so
        // those targets take the general Reference path below. Accessors and
        // read-only properties do too.
        if (JsObject* global = m_global_object.object_val) {
            if (auto property = global->properties.find(name); property != global->properties.end() &&
                !global->frozen && !(property->flags & PropertyMap::Accessor) &&
                (property->flags & PropertyMap::Writable)) {
                switch (property->second.type) {
                    case JsType::Undefined:
                    case JsType::Null:
                    case JsType::Boolean:
                    case JsType::Number:
                    case JsType::String: {
                        const double before = js_to_number(property->second);
                        const double after = before + delta;
                        property->second = JsValue::make_number(after);
                        return JsValue::make_number(update->prefix ? after : before);
                    }
                    case JsType::Object:
                    case JsType::Function:
                    case JsType::NativeFunction:
                    case JsType::Symbol:
                        break;
                }
            }
        }
    }
    // The target is resolved once, so `a[i++]++` evaluates `i++` once.
    Reference ref = resolve_reference(update->target.get(), env);
    double before = js_to_number(read_reference(ref, env));
    double after = before + delta;
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
    // Fast path: array[number]. A hole is not one of the array's properties,
    // so it is not an answer here: `[, ][0]` has to reach the prototype with
    // `get_member`, as `HasProperty` says it does. An element still answers
    // without the call, which is the case this path exists for.
    // A host object is excluded: its `[[Get]]` is asked before its elements
    // (JsValue::get_property says so), so an `arguments` with a mapped place
    // must not be read past the mapping — `function (a) { a = 9; return
    // arguments[0] }` reads the parameter through the hook, and this path
    // answered the stale element instead.
    if (key.type == JsType::Number && base.is_array() && !base.object_val->exotic) {
        double n = key.number_val;
        const auto& elements = base.object_val->array_elements;
        if (n >= 0 && n < static_cast<double>(elements.size()) && n == std::floor(n)) {
            const size_t i = static_cast<size_t>(n);
            if (!base.is_array_hole(i)) return elements[i];
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
            if (id->is_undefined && !JsEnvironment::undefined_shadowed() && !env->has_with_ancestor()) {
                return JsValue::make_undefined();
            }
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

        case NodeKind::ImportCall: {
            // §13.3.12 step 6 runs ToString on the specifier first and throws
            // its error at the call site; step 7 hands the result to the host,
            // which answers a Promise. This engine has no module loader, so
            // what the host answers is a rejection -- but the rejection still
            // has to arrive as a promise, not as a throw, and the specifier is
            // still coerced, which is the order a page can observe.
            JsValue specifier = evaluate(static_cast<const ImportCallExpr*>(expr)->specifier.get(), env);
            const std::string text = js_to_string(specifier);
            JsValue error = js_make_error(JsErrorKind::TypeError,
                                          "'import()' is not available: this engine does not load modules (asked for '" +
                                              text + "')");
            return Promise::make_rejected(error);
        }

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
