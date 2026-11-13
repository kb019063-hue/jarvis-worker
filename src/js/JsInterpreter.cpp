#include "litebird/js/JsInterpreter.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Event.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/net/Fetch.hpp"
#include "litebird/net/WebSocket.hpp"
#include "litebird/js/Worker.hpp"
#include "litebird/js/Generator.hpp"
#include "litebird/dom/IntersectionObserver.hpp"
#include "litebird/dom/ResizeObserver.hpp"
#include "litebird/dom/History.hpp"
#include "litebird/net/AbortController.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include <cmath>
#include <limits>
#include <iostream>

namespace litebird {

namespace {

// The collector calls these to walk closures' scopes; see Heap::EnvironmentTracer.
bool environment_enter(void* env, uint64_t epoch) {
    auto* scope = static_cast<JsEnvironment*>(env);
    if (scope->gc_epoch == epoch) return false;
    scope->gc_epoch = epoch;
    return true;
}

void* environment_trace(Heap& heap, void* env) {
    auto* scope = static_cast<JsEnvironment*>(env);
    for (auto& binding : scope->bindings()) heap.mark_value(binding.value);
    return scope->parent_raw();
}

bool environment_reached(void* env, uint64_t epoch) {
    return static_cast<JsEnvironment*>(env)->gc_epoch == epoch;
}

void environment_clear(void* env) {
    static_cast<JsEnvironment*>(env)->clear_bindings();
}

} // namespace

namespace {
std::unique_ptr<JsIntrinsics> create_realm() {
    auto realm = std::make_unique<JsIntrinsics>();
    realm->install();
    // Objects created from here on, including the ones the members below
    // create while they are constructed, belong to this interpreter's realm.
    JsIntrinsics::set_current(realm.get());
    return realm;
}
}

JsInterpreter::JsInterpreter(std::shared_ptr<Document> doc)
    : m_realm(create_realm()), m_alive(std::make_shared<std::atomic<bool>>(true)),
      m_global_env(std::make_shared<JsEnvironment>()), m_document(std::move(doc)),
      m_history(std::make_shared<History>()), m_vm(m_document) {
    Heap::EnvironmentTracer tracer;
    tracer.enter = &environment_enter;
    tracer.trace = &environment_trace;
    tracer.snapshot = &JsEnvironment::snapshot;
    tracer.reached = &environment_reached;
    tracer.clear = &environment_clear;
    Heap::set_environment_tracer(tracer);

    init_builtins();

    // Everything a script can still name lives in an environment, so this
    // interpreter's environments must be roots. Without this registration a
    // collection with an empty root list (the DevTools path) frees objects the
    // interpreter is still holding, and the next property read is a UAF.
    Heap::default_heap().add_root_provider(this, [this](std::vector<JsValue*>& roots) {
        // Scopes with code running in them are reported separately (see
        // "Active scopes" in JsEval.cpp); the global scope is always live.
        if (m_global_env) m_global_env->collect_roots(roots);
        roots.push_back(&m_global_object);
        for (auto& [k, v] : m_vm.globals()) roots.push_back(&v);

        // A generator or async function parked mid-body keeps its local
        // variables on a thread stack the collector cannot scan; its scope is
        // reachable from here instead.
        for (auto& weak : m_generators) {
            auto frame = weak.lock();
            if (!frame || frame->is_done || !frame->frame_env) continue;
            Heap::default_heap().mark_environment(frame->frame_env.get());
        }
    });
}

JsInterpreter::~JsInterpreter() {
    // Callbacks that outlive this interpreter (timers and promise reactions in
    // the process-wide event loop, DOM listeners) must not run script any more.
    m_alive->store(false);

    // Suspended generators and async functions hold frames that point back
    // into this interpreter. Unwind them now, while it still exists. Their
    // `finally` blocks do not run: no script may run from a destructor.
    for (auto& weak : m_generators) {
        if (auto frame = weak.lock()) {
            frame->cancelled = true;
            frame->terminate(false);
        }
    }
    m_generators.clear();
    // The DOM side of the realm lets go of its objects; functions made for
    // it stay callable and do nothing.
    dom::shutdown_realm(*m_realm);
    Heap::default_heap().remove_root_provider(this);
}

// Calls the window's listeners for `type`, then its `on<type>` handler.
void JsInterpreter::fire_window_event(const std::string& type) {
    JsValue event = JsValue::make_object();
    event.set_property("type", JsValue::make_string(type));
    event.set_property("target", m_global_object);
    event.set_property("currentTarget", m_global_object);
    event.set_property("bubbles", JsValue::make_boolean(false));
    event.set_property("cancelable", JsValue::make_boolean(false));

    if (m_window_listeners) {
        auto found = m_window_listeners->find(type);
        if (found != m_window_listeners->end()) {
            const auto listeners = found->second;       // a listener may change the table
            for (const auto& listener : listeners) js_call_guarded(listener, m_global_object, {event});
        }
    }
    JsValue handler = m_global_object.get_property("on" + type);
    if (handler.is_function()) js_call_guarded(handler, m_global_object, {event});
}

void JsInterpreter::notify_document_loaded() {
    if (m_load_notified) return;
    m_load_notified = true;
    JsIntrinsics::set_current(m_realm.get());

    if (m_document) {
        // DOMContentLoaded at the document, readystatechange, then load at
        // the window: see fire_load_events.
        JsScriptScope scope;
        dom::fire_load_events(dom::realm_of(*m_realm));
        return;
    }
    // No page: the bare window still hears that loading is over.
    fire_window_event("DOMContentLoaded");
    fire_window_event("load");
}

void JsInterpreter::notify_document_interactive() {
    if (!m_document || m_document->ready_state() != "loading") return;
    JsIntrinsics::set_current(m_realm.get());
    JsScriptScope scope;
    dom::Realm& realm = dom::realm_of(*m_realm);
    m_document->set_ready_state("interactive");
    dom::fire(realm, *m_document, "readystatechange");
}

void JsInterpreter::set_document(std::shared_ptr<Document> doc) {
    m_document = std::move(doc);
    m_vm.set_document(m_document);
    init_builtins();
}

std::shared_ptr<BytecodeFunction> JsInterpreter::compile_to_bytecode(std::string_view source) {
    JsLexer lexer(source);
    auto tokens = lexer.tokenize_all();
    JsParser parser(tokens);
    auto program = parser.parse_program();
    BytecodeCompiler compiler;
    return compiler.compile(program.get());
}

// Scripts run on the tree-walking interpreter, once.
//
// This used to try the bytecode VM first and, on any exception, re-run the
// whole script from the top in the tree-walker. Everything the script had
// already done (logging, DOM changes, network requests) then happened twice.
// The VM is still available through eval_bytecode() for code that wants it.
JsValue JsInterpreter::eval(std::string_view source) {
    return eval_ast(source);
}

JsValue JsInterpreter::eval_bytecode(std::string_view source) {
    for (const auto& binding : m_global_env->bindings()) {
        m_vm.set_global(binding.name, binding.value);
    }
    auto fn = compile_to_bytecode(source);
    JsValue result = m_vm.interpret(fn);
    for (const auto& [k, v] : m_vm.globals()) {
        m_global_env->define(k, v);
    }
    return result;
}

JsValue JsInterpreter::eval_ast(std::string_view source) {
    m_last_eval_failed = false;
    // When this scope ends the script has finished and the JavaScript stack is
    // empty, so promise callbacks it queued run before control returns to the
    // host. That is what a browser does after every <script>.
    JsScriptScope script_scope;
    try {
        return run(source);
    } catch (const JsSyntaxError& error) {
        // A script with a syntax error does not run at all, not even the part
        // before the error.
        js_report_uncaught(error);
    } catch (const JsThrow& thrown) {
        js_report_uncaught(thrown.value);
    } catch (const GeneratorAbortException&) {
        throw;
    } catch (const std::exception& ex) {
        js_report_uncaught(ex);
    }
    m_last_eval_failed = true;
    return JsValue::make_undefined();
}

namespace {

/// setTimeout's delay argument: anything that is not a positive finite number
/// means "as soon as possible".
int timer_delay_from(const JsValue& value) {
    const double ms = value.is_undefined() ? 0.0 : js_to_number(value);
    if (!(ms > 0)) return 0;                        // also catches NaN
    if (ms > 2147483647.0) return 2147483647;
    return static_cast<int>(ms);
}

uint32_t timer_id_from(const JsValue& value) {
    const double id = js_to_number(value);
    if (!(id >= 1) || id > 4294967295.0) return 0;  // 0 is never a live timer
    return static_cast<uint32_t>(id);
}

} // namespace

void JsInterpreter::init_builtins() {
    // gc function
    m_global_env->define("gc", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        // Everything this interpreter can reach is reported by its root
        // provider, including the scopes of every function on the call stack.
        auto stats = Heap::default_heap().collect_garbage();
        JsValue res = JsValue::make_object();
        res.set_property("total_cells", JsValue::make_number(stats.total_cells));
        res.set_property("alive_cells", JsValue::make_number(stats.alive_cells));
        res.set_property("collected_cells", JsValue::make_number(stats.collected_cells));
        res.set_property("bytes_allocated", JsValue::make_number(stats.bytes_allocated));
        res.set_property("collections_count", JsValue::make_number(stats.collections_count));
        return res;
    }));

    // console object
    JsValue console_obj = JsValue::make_object();
    console_obj.set_property("log", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
            std::cout << js_output_text(args[i].inspect()) << (i + 1 < args.size() ? " " : "");
        }
        std::cout << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("warn", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cout << "\033[33m[WARN] ";
        for (const auto& a : args) std::cout << js_output_text(a.inspect()) << " ";
        std::cout << "\033[0m" << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("error", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cerr << "\033[31m[ERROR] ";
        for (const auto& a : args) std::cerr << js_output_text(a.inspect()) << " ";
        std::cerr << "\033[0m" << std::endl;
        return JsValue::make_undefined();
    }));
    // info/debug/table/assert/trace exist on every real console; scripts call
    // them without checking.
    console_obj.set_property("info", console_obj.get_property("log"));
    console_obj.set_property("debug", console_obj.get_property("log"));
    console_obj.set_property("table", console_obj.get_property("log"));
    console_obj.set_property("trace", console_obj.get_property("log"));
    console_obj.set_property("assert", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty() || !args[0].is_truthy()) {
            std::cerr << "Assertion failed";
            for (size_t i = 1; i < args.size(); ++i) std::cerr << (i == 1 ? ": " : " ") << js_output_text(args[i].inspect());
            std::cerr << std::endl;
        }
        return JsValue::make_undefined();
    }));
    // The rest of the console a page may call without checking. Groups are
    // not indented and timers print nothing: neither can be the same on
    // every run, and output that differs between runs cannot be tested.
    console_obj.set_property("dir", console_obj.get_property("log"));
    console_obj.set_property("group", console_obj.get_property("log"));
    console_obj.set_property("groupCollapsed", console_obj.get_property("log"));
    for (const char* name : {"groupEnd", "time", "timeEnd", "timeLog", "timeStamp", "clear", "profile", "profileEnd"}) {
        console_obj.set_property(name, JsValue::make_native_function([](const std::vector<JsValue>&) {
            return JsValue::make_undefined();
        }));
    }
    auto counters = std::make_shared<std::unordered_map<std::string, int>>();
    console_obj.set_property("count", JsValue::make_native_function([counters](const std::vector<JsValue>& args) {
        const std::string label = args.empty() || args[0].is_undefined() ? std::string("default") : args[0].to_string();
        std::cout << js_output_text(label) << ": " << ++(*counters)[label] << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("countReset", JsValue::make_native_function([counters](const std::vector<JsValue>& args) {
        const std::string label = args.empty() || args[0].is_undefined() ? std::string("default") : args[0].to_string();
        (*counters)[label] = 0;
        return JsValue::make_undefined();
    }));
    m_global_env->define("console", console_obj);

    JsValue win_obj = JsValue::make_object();

    // With a page, the global object is a Window: everything a window has
    // (events, location, history, storage, the document) comes from the DOM
    // bindings, attached at the end of this function. Without one, as in a
    // script run on its own, it is a bare object with the few members that
    // need no page.
    if (!m_document) {
        JsValue local_store = Storage::local_storage().to_js_value();
        JsValue session_store = Storage::session_storage().to_js_value();
        m_global_env->define("localStorage", local_store);
        m_global_env->define("sessionStorage", session_store);
        win_obj.set_property("localStorage", local_store);
        win_obj.set_property("sessionStorage", session_store);

        m_window_listeners = std::make_shared<WindowListeners>();
        auto window_listeners = m_window_listeners;
        JsValue win_add_el = JsValue::make_native_function([window_listeners](const std::vector<JsValue>& args) {
            if (args.size() < 2 || !args[1].is_function()) return JsValue::make_undefined();
            auto& listeners = (*window_listeners)[args[0].to_string()];
            // The same callback registered twice is one listener.
            for (const auto& existing : listeners) {
                if (existing.identity() == args[1].identity()) return JsValue::make_undefined();
            }
            listeners.push_back(args[1]);
            return JsValue::make_undefined();
        });
        JsValue win_remove_el = JsValue::make_native_function([window_listeners](const std::vector<JsValue>& args) {
            if (args.size() < 2) return JsValue::make_undefined();
            auto found = window_listeners->find(args[0].to_string());
            if (found == window_listeners->end()) return JsValue::make_undefined();
            auto& listeners = found->second;
            for (auto it = listeners.begin(); it != listeners.end(); ++it) {
                if (it->identity() == args[1].identity()) {
                    listeners.erase(it);
                    break;
                }
            }
            return JsValue::make_undefined();
        });
        JsValue win_dispatch = JsValue::make_native_function([window_listeners](const std::vector<JsValue>& args) {
            if (args.empty()) return JsValue::make_boolean(false);
            std::string type = args[0].is_object() ? args[0].get_property("type").to_string() : args[0].to_string();
            auto it = window_listeners->find(type);
            if (it != window_listeners->end()) {
                // A listener may add or remove listeners; iterate over a copy.
                const auto listeners = it->second;
                for (const auto& cb : listeners) {
                    js_call_guarded(cb, JsValue(), args);
                }
            }
            return JsValue::make_boolean(true);
        });
        win_obj.set_property("addEventListener", win_add_el);
        win_obj.set_property("removeEventListener", win_remove_el);
        win_obj.set_property("dispatchEvent", win_dispatch);

        if (!m_history) m_history = std::make_shared<History>();
        JsValue history_obj = JsValue::make_object();
        auto hist = m_history;
        history_obj.set_property("pushState", JsValue::make_native_function([hist](const std::vector<JsValue>& args) {
            JsValue state = args.size() > 0 ? args[0] : JsValue::make_undefined();
            std::string title = args.size() > 1 ? args[1].to_string() : "";
            std::string url = args.size() > 2 ? args[2].to_string() : "";
            hist->push_state(state, title, url);
            return JsValue::make_undefined();
        }));
        history_obj.set_property("replaceState", JsValue::make_native_function([hist](const std::vector<JsValue>& args) {
            JsValue state = args.size() > 0 ? args[0] : JsValue::make_undefined();
            std::string title = args.size() > 1 ? args[1].to_string() : "";
            std::string url = args.size() > 2 ? args[2].to_string() : "";
            hist->replace_state(state, title, url);
            return JsValue::make_undefined();
        }));
        history_obj.set_property("back", JsValue::make_native_function([hist](const std::vector<JsValue>&) {
            hist->back();
            return JsValue::make_undefined();
        }));
        history_obj.set_property("forward", JsValue::make_native_function([hist](const std::vector<JsValue>&) {
            hist->forward();
            return JsValue::make_undefined();
        }));
        history_obj.set_property("go", JsValue::make_native_function([hist](const std::vector<JsValue>& args) {
            int delta = args.empty() ? 0 : static_cast<int>(args[0].to_number());
            hist->go(delta);
            return JsValue::make_undefined();
        }));
        history_obj.set_property("__get_length__", JsValue::make_native_function([hist](const std::vector<JsValue>&) {
            return JsValue::make_number(hist->length());
        }));
        history_obj.set_property("__get_state__", JsValue::make_native_function([hist](const std::vector<JsValue>&) {
            return hist->state();
        }));
        history_obj.set_property("length", JsValue::make_number(hist->length()));
        history_obj.set_property("state", hist->state());

        m_history->set_popstate_dispatcher([window_listeners](const JsValue& state) {
            JsValue event = JsValue::make_object();
            event.set_property("type", JsValue::make_string("popstate"));
            event.set_property("state", state);
            auto it = window_listeners->find("popstate");
            if (it != window_listeners->end()) {
                for (const auto& cb : it->second) {
                    js_call_guarded(cb, JsValue(), {event});
                }
            }
        });

        m_global_env->define("history", history_obj);
        win_obj.set_property("history", history_obj);
    }

    // requestAnimationFrame(callback): there is no compositor to synchronise
    // with, so a frame is a 16 ms timer. The callback receives the time since
    // this interpreter started, like performance.now().
    const auto time_origin = EventLoop::default_loop().now();
    auto elapsed_ms = [time_origin]() {
        const auto elapsed = EventLoop::default_loop().now() - time_origin;
        return std::chrono::duration<double, std::milli>(elapsed).count();
    };
    JsValue raf = js_make_native("requestAnimationFrame", 1, [elapsed_ms](const std::vector<JsValue>& args) {
        const JsValue callback = js_arg(args, 0);
        if (!callback.is_function()) js_throw_type_error("requestAnimationFrame: the callback is not a function");
        uint32_t id = EventLoop::default_loop().set_timeout([callback, elapsed_ms]() {
            callback.call({JsValue::make_number(elapsed_ms())});
        }, 16, {js_roots({callback})});
        return JsValue::make_number(id);
    });
    m_global_env->define("requestAnimationFrame", raf);
    win_obj.set_property("requestAnimationFrame", raf);
    JsValue caf = js_make_native("cancelAnimationFrame", 1, [](const std::vector<JsValue>& args) {
        if (!args.empty()) EventLoop::default_loop().clear_timer(timer_id_from(args[0]));
        return JsValue::make_undefined();
    });
    m_global_env->define("cancelAnimationFrame", caf);
    win_obj.set_property("cancelAnimationFrame", caf);

    // performance.now(): milliseconds since this interpreter started.
    JsValue performance = JsValue::make_object();
    js_define_method(performance, "now", 0, [elapsed_ms](const std::vector<JsValue>&) {
        return JsValue::make_number(elapsed_ms());
    });
    performance.set_property("timeOrigin", JsValue::make_number(js_current_time_ms()));
    m_global_env->define("performance", performance);

    m_global_env->define("window", win_obj);

    // queueMicrotask(callback)
    JsValue qm = js_make_native("queueMicrotask", 1, [](const std::vector<JsValue>& args) {
        const JsValue callback = js_arg(args, 0);
        if (!callback.is_function()) js_throw_type_error("queueMicrotask: the callback is not a function");
        EventLoop::default_loop().post_microtask([callback]() { callback.call({}); }, {js_roots({callback})});
        return JsValue::make_undefined();
    });
    m_global_env->define("queueMicrotask", qm);
    win_obj.set_property("queueMicrotask", qm);

    // setTimeout(callback, delay, ...args) and setInterval(callback, delay, ...args).
    // The callback and its arguments sit in a std::function while they wait,
    // where the collector cannot see them, so they are handed over as roots.
    auto make_timer = [](const char* name, bool repeating) {
        return js_make_native(name, 2, [repeating](const std::vector<JsValue>& args) {
            const JsValue callback = js_arg(args, 0);
            // Browsers also accept a string of code here. That form is an
            // eval() in disguise and is not supported.
            if (!callback.is_function()) return JsValue::make_number(0);
            std::vector<JsValue> extra;
            if (args.size() > 2) extra.assign(args.begin() + 2, args.end());
            auto roots = std::make_shared<std::vector<JsValue>>(extra);
            roots->push_back(callback);
            auto fire = [callback, extra]() { callback.call(extra); };
            const int delay = timer_delay_from(js_arg(args, 1));
            auto& loop = EventLoop::default_loop();
            const uint32_t id = repeating ? loop.set_interval(fire, delay, {roots})
                                          : loop.set_timeout(fire, delay, {roots});
            return JsValue::make_number(id);
        });
    };
    JsValue clear_timer = js_make_native("clearTimeout", 1, [](const std::vector<JsValue>& args) {
        if (!args.empty()) EventLoop::default_loop().clear_timer(timer_id_from(args[0]));
        return JsValue::make_undefined();
    });
    m_global_env->define("setTimeout", make_timer("setTimeout", false));
    m_global_env->define("setInterval", make_timer("setInterval", true));
    // One id space, as in browsers: either function clears either kind.
    m_global_env->define("clearTimeout", clear_timer);
    m_global_env->define("clearInterval", clear_timer);

    // Function values produced by the bytecode compiler have no native entry
    // point; this runs their body on the tree-walker.
    JsValue::set_function_caller([this](const JsValue& fn, const std::vector<JsValue>& args, const JsValue* this_val) {
        return call_legacy_function(fn, args, this_val);
    });

    // Promise built-in
    m_global_env->define("Promise", Promise::create_constructor());

    // fetch, Headers, Response built-ins
    m_global_env->define("Headers", HeadersObject::create_constructor());
    m_global_env->define("Response", ResponseObject::create_constructor());
    m_global_env->define("fetch", JsValue::make_native_function(FetchAPI::fetch));

    // URL and URLSearchParams. A window finds every interface by its name
    // (see dom::attach_window); a worker, or a script run on its own, has
    // only these two.
    if (!m_document) {
        dom::Realm& dom_realm = dom::realm_of(*m_realm);
        m_global_env->define("URLSearchParams", dom_realm.ctor("URLSearchParams"));
        m_global_env->define("URL", dom_realm.ctor("URL"));
    }

    // Worker built-in
    m_global_env->define("Worker", WorkerObject::create_constructor());

    // WebSocket built-in
    m_global_env->define("WebSocket", WebSocketObject::create_constructor());

    // AbortController and AbortSignal
    JsValue abort_ctrl_ctor = JsValue::make_native_function([](const std::vector<JsValue>&) {
        auto ctrl = std::make_shared<AbortController>();
        return ctrl->to_js_value();
    });
    if (!m_document) m_global_env->define("AbortController", abort_ctrl_ctor);

    JsValue abort_sig_ctor = JsValue::make_native_function([](const std::vector<JsValue>&) {
        return JsValue::make_undefined();
    });
    abort_sig_ctor.set_property("abort", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue reason = args.empty() ? JsValue::make_string("AbortError") : args[0];
        auto sig = AbortSignal::abort(reason);
        return sig->to_js_value();
    }));
    abort_sig_ctor.set_property("timeout", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        uint32_t ms = args.empty() ? 0 : static_cast<uint32_t>(args[0].to_number());
        auto sig = AbortSignal::timeout(ms);
        return sig->to_js_value();
    }));
    // With a page these are DOM interfaces, whose signals are real event
    // targets (see DomEventBindings.cpp).
    if (!m_document) m_global_env->define("AbortSignal", abort_sig_ctor);

    // IntersectionObserver
    JsValue intersection_obs_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        JsValue callback = args[0];
        IntersectionObserverInit opts;
        if (args.size() > 1 && args[1].is_object()) {
            JsValue root_val = args[1].get_property("root");
            if (root_val.dom_node_value()) {
                opts.root = std::dynamic_pointer_cast<Element>(root_val.dom_node_value());
            }
            JsValue rm_val = args[1].get_property("rootMargin");
            if (rm_val.type == JsType::String) {
                opts.root_margin = rm_val.to_string();
            }
            JsValue th_val = args[1].get_property("threshold");
            if (th_val.type == JsType::Number) {
                opts.thresholds = {static_cast<float>(th_val.to_number())};
            } else if (th_val.is_array()) {
                double len = th_val.get_property("length").to_number();
                opts.thresholds.clear();
                for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                    opts.thresholds.push_back(static_cast<float>(th_val.get_property(std::to_string(i)).to_number()));
                }
            }
        }
        auto obs = std::make_shared<IntersectionObserver>(callback, opts);
        return obs->to_js_value();
    });
    m_global_env->define("IntersectionObserver", intersection_obs_ctor);

    // ResizeObserver
    JsValue resize_obs_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        JsValue callback = args[0];
        auto obs = std::make_shared<ResizeObserver>(callback);
        return obs->to_js_value();
    });
    m_global_env->define("ResizeObserver", resize_obs_ctor);

    init_language_builtins();

    if (m_document) {
        // The global object becomes the page's Window, and `document` its
        // document's script object.
        dom::Realm& realm = dom::realm_of(*m_realm);
        m_document->set_ready_state("loading");
        dom::attach_window(realm, m_global_object, m_document, m_global_env);
        m_global_env->define("document", dom::wrap(realm, m_document));
        std::shared_ptr<std::atomic<bool>> alive = m_alive;
        dom::set_script_runner(realm, [this, alive](const std::string& source) {
            if (alive->load()) eval_ast(source);
        });
    }
}

void JsInterpreter::init_language_builtins() {
    // The language's own library (Object, Array, String, Math, JSON, ...) is
    // shared by every interpreter in the process.
    for (const auto& [name, value] : m_realm->globals) {
        m_global_env->define(name, value);
    }
    // The three value properties of the global object are not writable.
    m_global_env->define_readonly("undefined", JsValue::make_undefined());
    m_global_env->define_readonly("NaN", JsValue::make_number(std::numeric_limits<double>::quiet_NaN()));
    m_global_env->define_readonly("Infinity", JsValue::make_number(std::numeric_limits<double>::infinity()));

    // eval(source): runs in the global scope and propagates exceptions.
    m_global_env->define("eval", js_make_native("eval", 1, [this](const std::vector<JsValue>& args) {
        if (args.empty() || args[0].type != JsType::String) return js_arg(args, 0);
        try {
            return run(args[0].string_val.str());
        } catch (const JsSyntaxError& error) {
            js_throw_error(JsErrorKind::SyntaxError, error.what());
        }
    }));

    // new Function("a", "b", "return a + b")
    JsValue function_ctor = js_make_native("Function", 1, [this](const std::vector<JsValue>& args) {
        std::string params;
        std::string body;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i + 1 == args.size()) {
                body = js_to_string(args[i]);
            } else {
                if (!params.empty()) params += ",";
                params += js_to_string(args[i]);
            }
        }
        try {
            return run("(function anonymous(" + params + "\n) {\n" + body + "\n})");
        } catch (const JsSyntaxError& error) {
            js_throw_error(JsErrorKind::SyntaxError, error.what());
        }
    });
    function_ctor.define_property("prototype", js_object_value(m_realm->function_proto), 0);
    js_define_hidden(m_realm->function_proto, "constructor", function_ctor);
    m_global_env->define("Function", function_ctor);

    // The global object. `window`, `self` and `globalThis` are the same object,
    // and global variables are visible as its properties in both directions.
    auto* existing_window = m_global_env->find_own("window");
    if (existing_window && existing_window->value.object_val) {
        m_global_object = existing_window->value;
    } else {
        m_global_object = JsValue::make_object();
        m_global_env->define("window", m_global_object);
    }
    m_global_env->define("globalThis", m_global_object);
    m_global_env->define("self", m_global_object);

    // The global object does not own the global scope: it looks it up each
    // time, and finds nothing once the interpreter and its scope are gone.
    // What `let`, `const` and `class` declare is not a property of it.
    std::weak_ptr<JsEnvironment> global_scope = m_global_env;
    m_global_object.object_val->lookup_fallback =
        std::make_shared<std::function<bool(const std::string&, JsValue&)>>(
            [global_scope](const std::string& name, JsValue& out) {
                auto scope = global_scope.lock();
                if (!scope) return false;
                auto* binding = scope->find_own(name);
                if (!binding || binding->is_lexical) return false;
                out = binding->value;
                return true;
            });
    m_global_object.object_val->assign_hook =
        std::make_shared<std::function<bool(const std::string&, const JsValue&)>>(
            [global_scope](const std::string& name, const JsValue& value) {
                auto scope = global_scope.lock();
                if (!scope) return false;
                auto* binding = scope->find_own(name);
                if (!binding || binding->is_lexical) return false;
                if (!binding->is_const && !binding->is_readonly) binding->value = value;
                return true;
            });

    // At the top level of a script `this` is the global object.
    m_global_env->define("this", m_global_object);
}

} // namespace litebird
