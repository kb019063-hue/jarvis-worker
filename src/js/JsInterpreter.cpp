#include "litebird/js/JsInterpreter.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/MutationObserver.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/net/URL.hpp"
#include "litebird/net/Fetch.hpp"
#include "litebird/net/WebSocket.hpp"
#include "litebird/js/Worker.hpp"
#include "litebird/js/Generator.hpp"
#include "litebird/dom/IntersectionObserver.hpp"
#include "litebird/dom/ResizeObserver.hpp"
#include "litebird/dom/History.hpp"
#include "litebird/net/AbortController.hpp"
#include <cmath>
#include <iostream>

namespace litebird {

JsInterpreter::JsInterpreter(std::shared_ptr<Document> doc)
    : m_global_env(std::make_shared<JsEnvironment>()), m_document(std::move(doc)), m_history(std::make_shared<History>()), m_vm(m_document) {
    init_builtins();
}

static JsValue create_generator_instance(JsInterpreter* interp,
                                         std::shared_ptr<AstNode> body_ptr,
                                         const std::vector<std::string>& params,
                                         const std::vector<std::shared_ptr<Expr>>& defs,
                                         std::shared_ptr<JsEnvironment> closure,
                                         const std::vector<JsValue>& args) {
    auto gen_data = std::make_shared<GeneratorData>();
    auto fn_env = std::make_shared<JsEnvironment>(closure ? closure : interp->global_env());
    for (size_t i = 0; i < params.size(); ++i) {
        JsValue arg = (i < args.size() && args[i].type != JsType::Undefined) ? args[i] : JsValue::make_undefined();
        if (arg.type == JsType::Undefined && i < defs.size() && defs[i]) {
            arg = interp->execute(defs[i].get(), fn_env);
        }
        fn_env->define(params[i], arg);
    }

    gen_data->worker = std::thread([interp, gen_data, body_ptr, fn_env]() {
        GeneratorContext::set_active(gen_data.get());
        {
            std::unique_lock<std::mutex> lock(gen_data->mtx);
            gen_data->cv_gen.wait(lock, [&] {
                return gen_data->started || gen_data->should_terminate;
            });
            if (gen_data->should_terminate) {
                GeneratorContext::set_active(nullptr);
                return;
            }
        }

        try {
            JsValue res = interp->execute(body_ptr.get(), fn_env);
            std::unique_lock<std::mutex> lock(gen_data->mtx);
            gen_data->yielded_value = res;
            gen_data->is_done = true;
            gen_data->finished = true;
            gen_data->cv_main.notify_one();
        } catch (const ReturnException& ret) {
            std::unique_lock<std::mutex> lock(gen_data->mtx);
            gen_data->yielded_value = ret.value;
            gen_data->is_done = true;
            gen_data->finished = true;
            gen_data->cv_main.notify_one();
        } catch (const GeneratorAbortException&) {
            // cleanly aborted
        } catch (...) {
            std::unique_lock<std::mutex> lock(gen_data->mtx);
            gen_data->is_done = true;
            gen_data->finished = true;
            gen_data->cv_main.notify_one();
        }
        GeneratorContext::set_active(nullptr);
    });

    JsValue gen_obj = JsValue::make_object();
    gen_obj.generator_data = gen_data;
    gen_obj.is_generator = true;

    gen_obj.set_property("next", JsValue::make_native_function([gen_data](const std::vector<JsValue>& nargs) {
        JsValue send_val = nargs.empty() ? JsValue::make_undefined() : nargs[0];
        return gen_data->step(send_val);
    }));

    gen_obj.set_property("return", JsValue::make_native_function([gen_data](const std::vector<JsValue>& rargs) {
        gen_data->terminate();
        gen_data->is_done = true;
        JsValue r = JsValue::make_object();
        r.set_property("value", rargs.empty() ? JsValue::make_undefined() : rargs[0]);
        r.set_property("done", JsValue::make_boolean(true));
        return r;
    }));

    gen_obj.set_property("throw", JsValue::make_native_function([gen_data](const std::vector<JsValue>& targs) -> JsValue {
        gen_data->terminate();
        gen_data->is_done = true;
        throw std::runtime_error(targs.empty() ? "Generator error" : targs[0].to_string());
        return JsValue::make_undefined();
    }));

    auto iter_fn = JsValue::make_native_function([gen_obj](const std::vector<JsValue>&) {
        return gen_obj;
    });
    gen_obj.set_property("@@iterator", iter_fn);
    gen_obj.set_property("Symbol.iterator", iter_fn);

    return gen_obj;
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

JsValue JsInterpreter::eval(std::string_view source) {
    for (const auto& [k, v] : m_global_env->values()) {
        m_vm.set_global(k, v);
    }
    try {
        auto fn = compile_to_bytecode(source);
        JsValue res = m_vm.interpret(fn);
        for (const auto& [k, v] : m_vm.globals()) {
            m_global_env->define(k, v);
        }
        return res;
    } catch (const std::exception&) {
        JsValue res = eval_ast(source);
        for (const auto& [k, v] : m_global_env->values()) {
            m_vm.set_global(k, v);
        }
        return res;
    }
}

JsValue JsInterpreter::eval_ast(std::string_view source) {
    try {
        JsLexer lexer(source);
        auto tokens = lexer.tokenize_all();
        JsParser parser(tokens);
        auto program = parser.parse_program();
        return execute(program.get(), m_global_env);
    } catch (const ReturnException& ret) {
        return ret.value;
    } catch (const std::exception& ex) {
        std::cerr << "JS Runtime Error: " << ex.what() << std::endl;
        return JsValue::make_undefined();
    } catch (...) {
        std::cerr << "JS Unknown Error in eval_ast!" << std::endl;
        return JsValue::make_undefined();
    }
}

void JsInterpreter::init_builtins() {
    // gc function
    m_global_env->define("gc", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        std::vector<JsValue*> roots;
        if (m_current_env) {
            m_current_env->collect_roots(roots);
        } else if (m_global_env) {
            m_global_env->collect_roots(roots);
        }
        for (auto& [k, v] : m_vm.globals()) {
            roots.push_back(&v);
        }
        auto stats = Heap::default_heap().collect_garbage(roots);
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
            std::cout << args[i].to_string() << (i + 1 < args.size() ? " " : "");
        }
        std::cout << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("warn", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cout << "\033[33m[WARN] ";
        for (const auto& a : args) std::cout << a.to_string() << " ";
        std::cout << "\033[0m" << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("error", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cerr << "\033[31m[ERROR] ";
        for (const auto& a : args) std::cerr << a.to_string() << " ";
        std::cerr << "\033[0m" << std::endl;
        return JsValue::make_undefined();
    }));
    m_global_env->define("console", console_obj);

    // Math object
    JsValue math_obj = JsValue::make_object();
    math_obj.set_property("PI", JsValue::make_number(3.141592653589793));
    math_obj.set_property("floor", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::floor(args[0].to_number()));
    }));
    math_obj.set_property("ceil", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::ceil(args[0].to_number()));
    }));
    math_obj.set_property("round", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::round(args[0].to_number()));
    }));
    math_obj.set_property("abs", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::abs(args[0].to_number()));
    }));
    m_global_env->define("Math", math_obj);

    // Web Storage API
    JsValue local_store = Storage::local_storage().to_js_value();
    JsValue session_store = Storage::session_storage().to_js_value();
    m_global_env->define("localStorage", local_store);
    m_global_env->define("sessionStorage", session_store);

    JsValue win_obj = JsValue::make_object();
    win_obj.set_property("localStorage", local_store);
    win_obj.set_property("sessionStorage", session_store);

    auto window_listeners = std::make_shared<std::unordered_map<std::string, std::vector<JsValue>>>();
    JsValue win_add_el = JsValue::make_native_function([window_listeners](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            (*window_listeners)[args[0].to_string()].push_back(args[1]);
        }
        return JsValue::make_undefined();
    });
    JsValue win_dispatch = JsValue::make_native_function([window_listeners](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_boolean(false);
        std::string type = args[0].is_object() ? args[0].get_property("type").to_string() : args[0].to_string();
        auto it = window_listeners->find(type);
        if (it != window_listeners->end()) {
            for (const auto& cb : it->second) {
                cb.call(args);
            }
        }
        return JsValue::make_boolean(true);
    });
    win_obj.set_property("addEventListener", win_add_el);
    win_obj.set_property("dispatchEvent", win_dispatch);

    if (!m_history) {
        m_history = std::make_shared<History>();
    }
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
                cb.call({event});
            }
        }
    });

    m_global_env->define("history", history_obj);
    win_obj.set_property("history", history_obj);

    JsValue raf = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_number(0);
        JsValue callback = args[0];
        uint32_t id = EventLoop::default_loop().set_timeout([callback]() {
            callback.call({JsValue::make_number(16.0)});
        }, 16);
        return JsValue::make_number(id);
    });
    m_global_env->define("requestAnimationFrame", raf);
    win_obj.set_property("requestAnimationFrame", raf);

    m_global_env->define("window", win_obj);

    // document object
    if (m_document) {
        JsValue doc_obj = JsValue::make_object();
        doc_obj.set_property("title", JsValue::make_string(m_document->title()));
        doc_obj.set_property("cookie", JsValue::make_string(CookieJar::default_jar().get_document_cookie()));
        doc_obj.set_property("getElementById", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
            if (args.empty() || !m_document) return JsValue::make_null();
            auto el = m_document->get_element_by_id(args[0].to_string());
            if (el) {
                return el->custom_element_instance().is_undefined() ? JsValue::make_dom_node(el) : el->custom_element_instance();
            }
            return JsValue::make_null();
        }));
        doc_obj.set_property("querySelector", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
            if (args.empty() || !m_document) return JsValue::make_null();
            auto el = m_document->query_selector(args[0].to_string());
            if (el) {
                return el->custom_element_instance().is_undefined() ? JsValue::make_dom_node(el) : el->custom_element_instance();
            }
            return JsValue::make_null();
        }));
        doc_obj.set_property("querySelectorAll", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
            JsValue arr = JsValue::make_array();
            if (!args.empty() && m_document) {
                auto list = m_document->query_selector_all(args[0].to_string());
                for (const auto& item : list) {
                    arr.array_elements().push_back(item->custom_element_instance().is_undefined() ? JsValue::make_dom_node(item) : item->custom_element_instance());
                }
            }
            return arr;
        }));
        doc_obj.set_property("createElement", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
            if (args.empty()) return JsValue::make_null();
            std::string tag = args[0].to_string();
            auto el = m_document ? m_document->create_element(tag) : std::make_shared<Element>(tag);
            CustomElementRegistry::default_registry().upgrade_element(el);
            return el->custom_element_instance().is_undefined() ? JsValue::make_dom_node(el) : el->custom_element_instance();
        }));
        doc_obj.set_property("createTextNode", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
            std::string txt = args.empty() ? "" : args[0].to_string();
            auto node = m_document ? m_document->create_text_node(txt) : std::make_shared<Text>(txt);
            return JsValue::make_dom_node(node);
        }));
        doc_obj.set_property("createDocumentFragment", JsValue::make_native_function([this](const std::vector<JsValue>&) {
            auto frag = m_document ? m_document->create_document_fragment() : std::make_shared<Node>(NodeType::DocumentFragment);
            return JsValue::make_dom_node(frag);
        }));
        doc_obj.set_property("__get_body__", JsValue::make_native_function([this](const std::vector<JsValue>&) {
            auto b = m_document ? m_document->body() : nullptr;
            return b ? JsValue::make_dom_node(b) : JsValue::make_null();
        }));
        doc_obj.set_property("__get_head__", JsValue::make_native_function([this](const std::vector<JsValue>&) {
            auto h = m_document ? m_document->head() : nullptr;
            return h ? JsValue::make_dom_node(h) : JsValue::make_null();
        }));
        doc_obj.set_property("__get_documentElement__", JsValue::make_native_function([this](const std::vector<JsValue>&) {
            auto d = m_document ? m_document->document_element() : nullptr;
            return d ? JsValue::make_dom_node(d) : JsValue::make_null();
        }));
        if (m_document->body()) doc_obj.set_property("body", JsValue::make_dom_node(m_document->body()));
        if (m_document->head()) doc_obj.set_property("head", JsValue::make_dom_node(m_document->head()));
        if (m_document->document_element()) doc_obj.set_property("documentElement", JsValue::make_dom_node(m_document->document_element()));

        m_global_env->define("document", doc_obj);
        win_obj.set_property("document", doc_obj);
    }

    // customElements registry
    JsValue ce_obj = JsValue::make_object();
    ce_obj.set_property("define", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() < 2) return JsValue::make_undefined();
        std::string name = args[0].to_string();
        JsValue constructor = args[1];
        bool ok = CustomElementRegistry::default_registry().define(name, constructor);
        if (ok && m_document) {
            CustomElementRegistry::default_registry().upgrade_tree(m_document);
        }
        return JsValue::make_boolean(ok);
    }));
    ce_obj.set_property("get", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        return CustomElementRegistry::default_registry().get(args[0].to_string());
    }));
    ce_obj.set_property("whenDefined", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return Promise::make_resolved(JsValue::make_undefined());
        return CustomElementRegistry::default_registry().when_defined(args[0].to_string());
    }));
    ce_obj.set_property("upgrade", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].dom_node) {
            CustomElementRegistry::default_registry().upgrade_tree(args[0].dom_node);
        }
        return JsValue::make_undefined();
    }));
    m_global_env->define("customElements", ce_obj);
    win_obj.set_property("customElements", ce_obj);

    // HTMLElement constructor base
    JsValue html_element_ctor = JsValue::make_native_function([](const std::vector<JsValue>&) {
        return JsValue::make_undefined();
    });
    JsValue html_proto = JsValue::make_object();
    html_element_ctor.set_property("prototype", html_proto);
    m_global_env->define("HTMLElement", html_element_ctor);
    win_obj.set_property("HTMLElement", html_element_ctor);

    // MutationObserver
    JsValue mo_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty() || !args[0].is_callable()) {
            return JsValue::make_undefined();
        }
        auto obs = std::make_shared<MutationObserver>(args[0]);
        return obs->to_js_value();
    });
    m_global_env->define("MutationObserver", mo_ctor);
    win_obj.set_property("MutationObserver", mo_ctor);

    // queueMicrotask(callback)
    JsValue qm = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].is_callable()) {
            JsValue cb = args[0];
            EventLoop::default_loop().post_microtask([cb]() {
                cb.call({});
            });
        }
        return JsValue::make_undefined();
    });
    m_global_env->define("queueMicrotask", qm);
    win_obj.set_property("queueMicrotask", qm);

    // setTimeout(callback, delay)
    m_global_env->define("setTimeout", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_number(0);
        JsValue callback = args[0];
        int delay = (args.size() > 1) ? static_cast<int>(args[1].to_number()) : 0;
        uint32_t id = EventLoop::default_loop().set_timeout([callback]() {
            callback.call({});
        }, delay);
        return JsValue::make_number(id);
    }));

    // setInterval(callback, interval)
    m_global_env->define("setInterval", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_number(0);
        JsValue callback = args[0];
        int interval = (args.size() > 1) ? static_cast<int>(args[1].to_number()) : 0;
        uint32_t id = EventLoop::default_loop().set_interval([callback]() {
            callback.call({});
        }, interval);
        return JsValue::make_number(id);
    }));

    // clearTimeout(id)
    m_global_env->define("clearTimeout", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            EventLoop::default_loop().clear_timer(static_cast<uint32_t>(args[0].to_number()));
        }
        return JsValue::make_undefined();
    }));

    // clearInterval(id)
    m_global_env->define("clearInterval", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            EventLoop::default_loop().clear_timer(static_cast<uint32_t>(args[0].to_number()));
        }
        return JsValue::make_undefined();
    }));

    // Function caller for callbacks
    JsValue::set_function_caller([this](const JsValue& fn, const std::vector<JsValue>& args, const JsValue* this_val) {
        if (fn.is_generator && fn.native_fn) {
            return fn.native_fn(args);
        }
        for (const auto& [k, v] : m_vm.globals()) {
            if (k != "document" && k != "window") {
                m_global_env->define(k, v);
            }
        }
        auto fn_env = std::make_shared<JsEnvironment>(fn.closure ? fn.closure : m_global_env);
        if (this_val && this_val->type != JsType::Undefined) {
            fn_env->define("this", *this_val);
        }
        for (size_t i = 0; i < fn.params.size(); ++i) {
            JsValue arg = (i < args.size()) ? args[i] : JsValue::make_undefined();
            fn_env->define(fn.params[i], arg);
        }
        try {
            JsValue res = execute(fn.body.get(), fn_env);
            for (const auto& [k, v] : m_global_env->values()) {
                m_vm.set_global(k, v);
            }
            return res;
        } catch (const ReturnException& ret) {
            for (const auto& [k, v] : m_global_env->values()) {
                m_vm.set_global(k, v);
            }
            return ret.value;
        } catch (...) {
            return JsValue::make_undefined();
        }
    });

    // Object built-ins
    JsValue object_obj = JsValue::make_object();
    object_obj.set_property("create", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue obj = JsValue::make_object();
        if (!args.empty() && args[0].type == JsType::Object && args[0].object_val) {
            obj.object_val->prototype = args[0].object_val;
        }
        return obj;
    }));
    object_obj.set_property("getPrototypeOf", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].type == JsType::Object && args[0].object_val && args[0].object_val->prototype) {
            return JsValue::make_object(args[0].object_val->prototype);
        }
        return JsValue::make_null();
    }));
    object_obj.set_property("setPrototypeOf", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && args[0].type == JsType::Object && args[0].object_val) {
            if (args[1].type == JsType::Object && args[1].object_val) {
                args[0].object_val->prototype = args[1].object_val;
            } else if (args[1].type == JsType::Null) {
                args[0].object_val->prototype = nullptr;
            }
        }
        return args.empty() ? JsValue::make_undefined() : args[0];
    }));
    m_global_env->define("Object", object_obj);

    // Array built-ins
    JsValue array_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_array(args);
    });
    array_ctor.set_property("isArray", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            return JsValue::make_boolean(args[0].is_array());
        }
        return JsValue::make_boolean(false);
    }));
    m_global_env->define("Array", array_ctor);

    // Promise built-in
    m_global_env->define("Promise", Promise::create_constructor());

    // JSON built-in
    JsValue json_obj = JsValue::make_object();
    json_obj.set_property("parse", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        try {
            return Json::parse(args[0].to_string());
        } catch (const std::exception& ex) {
            std::cerr << "JSON.parse Error: " << ex.what() << std::endl;
            return JsValue::make_null();
        }
    }));
    json_obj.set_property("stringify", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        return JsValue::make_string(Json::stringify(args[0]));
    }));
    m_global_env->define("JSON", json_obj);

    // fetch, Headers, Response built-ins
    m_global_env->define("Headers", HeadersObject::create_constructor());
    m_global_env->define("Response", ResponseObject::create_constructor());
    m_global_env->define("fetch", JsValue::make_native_function(FetchAPI::fetch));

    // URL & URLSearchParams built-ins
    m_global_env->define("URLSearchParams", URLSearchParamsObject::create_constructor());
    m_global_env->define("URL", URLObject::create_constructor());

    // Worker built-in
    m_global_env->define("Worker", WorkerObject::create_constructor());

    // WebSocket built-in
    m_global_env->define("WebSocket", WebSocketObject::create_constructor());

    // Map Constructor
    JsValue map_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue map_inst = JsValue::make_object();
        auto map_store = std::make_shared<std::vector<std::pair<std::string, JsValue>>>();

        map_inst.set_property("__get_size__", JsValue::make_native_function([map_store](const std::vector<JsValue>&) {
            return JsValue::make_number(map_store->size());
        }));

        map_inst.set_property("set", JsValue::make_native_function([map_inst, map_store](const std::vector<JsValue>& sargs) {
            if (!sargs.empty()) {
                std::string k = sargs[0].to_string();
                JsValue v = (sargs.size() > 1) ? sargs[1] : JsValue::make_undefined();
                bool found = false;
                for (auto& pair : *map_store) {
                    if (pair.first == k) {
                        pair.second = v;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    map_store->emplace_back(k, v);
                }
            }
            return map_inst;
        }));

        map_inst.set_property("get", JsValue::make_native_function([map_store](const std::vector<JsValue>& gargs) {
            if (gargs.empty()) return JsValue::make_undefined();
            std::string k = gargs[0].to_string();
            for (const auto& pair : *map_store) {
                if (pair.first == k) return pair.second;
            }
            return JsValue::make_undefined();
        }));

        map_inst.set_property("has", JsValue::make_native_function([map_store](const std::vector<JsValue>& hargs) {
            if (hargs.empty()) return JsValue::make_boolean(false);
            std::string k = hargs[0].to_string();
            for (const auto& pair : *map_store) {
                if (pair.first == k) return JsValue::make_boolean(true);
            }
            return JsValue::make_boolean(false);
        }));

        map_inst.set_property("delete", JsValue::make_native_function([map_store](const std::vector<JsValue>& dargs) {
            if (dargs.empty()) return JsValue::make_boolean(false);
            std::string k = dargs[0].to_string();
            for (auto it = map_store->begin(); it != map_store->end(); ++it) {
                if (it->first == k) {
                    map_store->erase(it);
                    return JsValue::make_boolean(true);
                }
            }
            return JsValue::make_boolean(false);
        }));

        map_inst.set_property("clear", JsValue::make_native_function([map_store](const std::vector<JsValue>&) {
            map_store->clear();
            return JsValue::make_undefined();
        }));

        // Populate entries if passed an array of pairs e.g. [["k", 1], ["k2", 2]]
        if (!args.empty() && args[0].is_array()) {
            JsValue arr = args[0];
            double len = arr.get_property("length").to_number();
            for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                JsValue entry = arr.get_property(std::to_string(i));
                if (entry.is_array()) {
                    std::string ek = entry.get_property("0").to_string();
                    JsValue ev = entry.get_property("1");
                    map_store->emplace_back(ek, ev);
                }
            }
        }

        return map_inst;
    });
    m_global_env->define("Map", map_ctor);

    // Set Constructor
    JsValue set_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue set_inst = JsValue::make_object();
        auto set_store = std::make_shared<std::vector<std::string>>();

        set_inst.set_property("__get_size__", JsValue::make_native_function([set_store](const std::vector<JsValue>&) {
            return JsValue::make_number(set_store->size());
        }));

        set_inst.set_property("add", JsValue::make_native_function([set_inst, set_store](const std::vector<JsValue>& sargs) {
            if (!sargs.empty()) {
                std::string k = sargs[0].to_string();
                if (std::find(set_store->begin(), set_store->end(), k) == set_store->end()) {
                    set_store->push_back(k);
                }
            }
            return set_inst;
        }));

        set_inst.set_property("has", JsValue::make_native_function([set_store](const std::vector<JsValue>& hargs) {
            if (hargs.empty()) return JsValue::make_boolean(false);
            std::string k = hargs[0].to_string();
            bool found = (std::find(set_store->begin(), set_store->end(), k) != set_store->end());
            return JsValue::make_boolean(found);
        }));

        set_inst.set_property("delete", JsValue::make_native_function([set_store](const std::vector<JsValue>& dargs) {
            if (dargs.empty()) return JsValue::make_boolean(false);
            std::string k = dargs[0].to_string();
            auto it = std::find(set_store->begin(), set_store->end(), k);
            if (it != set_store->end()) {
                set_store->erase(it);
                return JsValue::make_boolean(true);
            }
            return JsValue::make_boolean(false);
        }));

        set_inst.set_property("clear", JsValue::make_native_function([set_store](const std::vector<JsValue>&) {
            set_store->clear();
            return JsValue::make_undefined();
        }));

        // Populate elements if passed an array
        if (!args.empty() && args[0].is_array()) {
            JsValue arr = args[0];
            double len = arr.get_property("length").to_number();
            for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                std::string val = arr.get_property(std::to_string(i)).to_string();
                if (std::find(set_store->begin(), set_store->end(), val) == set_store->end()) {
                    set_store->push_back(val);
                }
            }
        }

        return set_inst;
    });
    m_global_env->define("Set", set_ctor);

    // Symbol
    JsValue symbol_obj = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::string desc = args.empty() ? "" : args[0].to_string();
        return JsValue::make_string("Symbol(" + desc + ")");
    });
    symbol_obj.set_property("iterator", JsValue::make_string("@@iterator"));
    symbol_obj.set_property("toStringTag", JsValue::make_string("@@toStringTag"));
    m_global_env->define("Symbol", symbol_obj);

    // AbortController and AbortSignal
    JsValue abort_ctrl_ctor = JsValue::make_native_function([](const std::vector<JsValue>&) {
        auto ctrl = std::make_shared<AbortController>();
        return ctrl->to_js_value();
    });
    m_global_env->define("AbortController", abort_ctrl_ctor);

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
    m_global_env->define("AbortSignal", abort_sig_ctor);

    // IntersectionObserver
    JsValue intersection_obs_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        JsValue callback = args[0];
        IntersectionObserverInit opts;
        if (args.size() > 1 && args[1].is_object()) {
            JsValue root_val = args[1].get_property("root");
            if (root_val.dom_node) {
                opts.root = std::dynamic_pointer_cast<Element>(root_val.dom_node);
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

    for (const auto& [k, v] : m_global_env->values()) {
        m_vm.set_global(k, v);
    }
}

JsValue JsInterpreter::execute(const AstNode* node, std::shared_ptr<JsEnvironment> env) {
    if (!node) return JsValue::make_undefined();
    m_current_env = env;

    if (auto block = dynamic_cast<const BlockStmt*>(node)) {
        JsValue last_val = JsValue::make_undefined();
        for (const auto& stmt : block->statements) {
            last_val = exec_stmt(stmt.get(), env);
        }
        return last_val;
    }

    if (auto stmt = dynamic_cast<const Stmt*>(node)) {
        return exec_stmt(stmt, env);
    }

    if (auto expr = dynamic_cast<const Expr*>(node)) {
        return eval_expr(expr, env);
    }

    return JsValue::make_undefined();
}

JsValue JsInterpreter::exec_stmt(const Stmt* stmt, std::shared_ptr<JsEnvironment> env) {
    if (!stmt) return JsValue::make_undefined();

    if (auto expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        return eval_expr(expr_stmt->expression.get(), env);
    } else if (auto var_decl = dynamic_cast<const VarDeclStmt*>(stmt)) {
        JsValue init_val = var_decl->initializer ? eval_expr(var_decl->initializer.get(), env) : JsValue::make_undefined();
        env->define(var_decl->name, init_val);
        return init_val;
    } else if (auto arr_destruct = dynamic_cast<const ArrayDestructuringDeclStmt*>(stmt)) {
        JsValue init_val = arr_destruct->initializer ? eval_expr(arr_destruct->initializer.get(), env) : JsValue::make_undefined();
        for (size_t i = 0; i < arr_destruct->names.size(); ++i) {
            JsValue item = init_val.get_property(std::to_string(i));
            env->define(arr_destruct->names[i], item);
        }
        return init_val;
    } else if (auto obj_destruct = dynamic_cast<const ObjectDestructuringDeclStmt*>(stmt)) {
        JsValue init_val = obj_destruct->initializer ? eval_expr(obj_destruct->initializer.get(), env) : JsValue::make_undefined();
        for (const auto& [prop, var_name] : obj_destruct->bindings) {
            JsValue item = init_val.get_property(prop);
            env->define(var_name, item);
        }
        return init_val;
    } else if (auto block_stmt = dynamic_cast<const BlockStmt*>(stmt)) {
        auto block_env = std::make_shared<JsEnvironment>(env);
        JsValue last = JsValue::make_undefined();
        for (const auto& s : block_stmt->statements) {
            last = exec_stmt(s.get(), block_env);
        }
        return last;
    } else if (auto if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
        JsValue cond = eval_expr(if_stmt->condition.get(), env);
        if (cond.is_truthy()) {
            return exec_stmt(if_stmt->then_branch.get(), env);
        } else if (if_stmt->else_branch) {
            return exec_stmt(if_stmt->else_branch.get(), env);
        }
    } else if (auto while_stmt = dynamic_cast<const WhileStmt*>(stmt)) {
        JsValue last = JsValue::make_undefined();
        while (eval_expr(while_stmt->condition.get(), env).is_truthy()) {
            last = exec_stmt(while_stmt->body.get(), env);
        }
        return last;
    } else if (auto for_of = dynamic_cast<const ForOfStmt*>(stmt)) {
        JsValue iterable = eval_expr(for_of->iterable.get(), env);
        std::vector<JsValue> items;
        if (iterable.is_array()) {
            double len = iterable.get_property("length").to_number();
            for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                items.push_back(iterable.get_property(std::to_string(i)));
            }
        } else if (iterable.is_generator || iterable.generator_data != nullptr || iterable.has_property("next")) {
            while (true) {
                JsValue next_fn = iterable.get_property("next");
                if (!next_fn.is_callable()) break;
                JsValue step = next_fn.call_with_this(iterable, {});
                if (step.get_property("done").is_truthy()) break;
                items.push_back(step.get_property("value"));
            }
        } else if (iterable.has_property("@@iterator") || iterable.has_property("Symbol.iterator")) {
            JsValue iter_fn = iterable.has_property("@@iterator") ? iterable.get_property("@@iterator") : iterable.get_property("Symbol.iterator");
            if (iter_fn.is_callable()) {
                JsValue iter = iter_fn.call_with_this(iterable, {});
                while (true) {
                    JsValue next_fn = iter.get_property("next");
                    if (!next_fn.is_callable()) break;
                    JsValue step = next_fn.call_with_this(iter, {});
                    if (step.get_property("done").is_truthy()) break;
                    items.push_back(step.get_property("value"));
                }
            }
        } else if (iterable.type == JsType::String) {
            std::string s = iterable.to_string();
            for (char c : s) {
                items.push_back(JsValue::make_string(std::string(1, c)));
            }
        }

        JsValue last = JsValue::make_undefined();
        for (const auto& item : items) {
            auto loop_env = std::make_shared<JsEnvironment>(env);
            loop_env->define(for_of->variable_name, item);
            last = exec_stmt(for_of->body.get(), loop_env);
        }
        return last;
    } else if (auto fn_decl = dynamic_cast<const FunctionDeclStmt*>(stmt)) {
        JsValue fn_val;
        fn_val.type = JsType::Function;
        fn_val.params = fn_decl->params;
        for (const auto& d : fn_decl->default_values) fn_val.default_values.push_back(d);
        fn_val.body = fn_decl->body;
        fn_val.closure = env;
        fn_val.is_async = fn_decl->is_async;
        fn_val.is_generator = fn_decl->is_generator;

        auto body_ptr = fn_decl->body;
        auto params = fn_decl->params;
        auto defs = fn_decl->default_values;
        auto closure = env;
        bool is_async = fn_decl->is_async;
        bool is_gen = fn_decl->is_generator;

        if (is_gen) {
            fn_val.native_fn = [this, body_ptr, params, defs, closure](const std::vector<JsValue>& args) {
                return create_generator_instance(this, body_ptr, params, defs, closure, args);
            };
        } else {
            fn_val.native_fn = [this, body_ptr, params, defs, closure, is_async](const std::vector<JsValue>& args) {
                for (const auto& [k, v] : m_vm.globals()) {
                    m_global_env->define(k, v);
                }
                auto fn_env = std::make_shared<JsEnvironment>(closure ? closure : m_global_env);
                for (size_t i = 0; i < params.size(); ++i) {
                    JsValue arg = (i < args.size() && args[i].type != JsType::Undefined) ? args[i] : JsValue::make_undefined();
                    if (arg.type == JsType::Undefined && i < defs.size() && defs[i]) {
                        arg = eval_expr(defs[i].get(), fn_env);
                    }
                    fn_env->define(params[i], arg);
                }

                if (is_async) {
                    auto p_data = std::make_shared<PromiseData>();
                    try {
                        JsValue res = execute(body_ptr.get(), fn_env);
                        Promise::resolve(p_data, res);
                    } catch (const ReturnException& ret) {
                        Promise::resolve(p_data, ret.value);
                    } catch (const std::exception& ex) {
                        Promise::reject(p_data, JsValue::make_string(ex.what()));
                    }
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return Promise::create_promise_instance(p_data);
                }

                try {
                    JsValue res = execute(body_ptr.get(), fn_env);
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return res;
                } catch (const ReturnException& ret) {
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return ret.value;
                }
            };
        }

        env->define(fn_decl->name, fn_val);
        if (env == m_global_env) {
            m_vm.set_global(fn_decl->name, fn_val);
        }
        return fn_val;
    } else if (auto class_decl = dynamic_cast<const ClassDeclStmt*>(stmt)) {
        JsValue super_ctor = JsValue::make_undefined();
        if (!class_decl->super_class.empty()) {
            super_ctor = env->get(class_decl->super_class);
        }

        const MethodDefinition* ctor_def = nullptr;
        for (const auto& m : class_decl->methods) {
            if (m.is_constructor) {
                ctor_def = &m;
                break;
            }
        }

        JsValue proto_obj = JsValue::make_object(Heap::default_heap().allocate_object());
        if (super_ctor.type == JsType::Function || super_ctor.type == JsType::NativeFunction) {
            JsValue super_proto = super_ctor.get_property("prototype");
            if (super_proto.type == JsType::Object && super_proto.object_val) {
                proto_obj.object_val->prototype = super_proto.object_val;
            }
        }

        JsValue ctor_fn;
        ctor_fn.type = JsType::Function;
        ctor_fn.closure = env;
        ctor_fn.params = ctor_def ? ctor_def->params : std::vector<std::string>{};
        if (ctor_def) {
            for (const auto& d : ctor_def->default_values) ctor_fn.default_values.push_back(d);
        }
        ctor_fn.body = ctor_def ? ctor_def->body : nullptr;

        auto ctor_params = ctor_fn.params;
        auto ctor_defs = ctor_fn.default_values;
        auto ctor_body = ctor_fn.body;
        auto closure = env;

        ctor_fn.native_fn = [this, ctor_body, ctor_params, ctor_defs, closure, super_ctor](const std::vector<JsValue>& args) {
            for (const auto& [k, v] : m_vm.globals()) {
                m_global_env->define(k, v);
            }
            auto fn_env = std::make_shared<JsEnvironment>(closure ? closure : m_global_env);

            for (size_t i = 0; i < ctor_params.size(); ++i) {
                JsValue arg = (i < args.size() && args[i].type != JsType::Undefined) ? args[i] : JsValue::make_undefined();
                if (arg.type == JsType::Undefined && i < ctor_defs.size() && ctor_defs[i]) {
                    arg = eval_expr(dynamic_cast<const Expr*>(ctor_defs[i].get()), fn_env);
                }
                fn_env->define(ctor_params[i], arg);
            }

            if (super_ctor.is_truthy()) {
                fn_env->define("__super_constructor__", super_ctor);
            }

            if (ctor_body) {
                try {
                    execute(ctor_body.get(), fn_env);
                } catch (const ReturnException& ret) {
                    return ret.value;
                }
            } else if (super_ctor.native_fn) {
                super_ctor.native_fn(args);
            }

            try {
                return fn_env->get("this");
            } catch (...) {
                return JsValue::make_undefined();
            }
        };

        if (super_ctor.is_truthy()) {
            ctor_fn.set_property("__super_constructor__", super_ctor);
        }
        proto_obj.set_property("constructor", ctor_fn);
        ctor_fn.set_property("prototype", proto_obj);

        for (const auto& m : class_decl->methods) {
            if (m.is_constructor) continue;

            auto m_body = m.body;
            auto m_params = m.params;
            auto m_defs = m.default_values;
            bool m_async = m.is_async;

            JsValue m_fn;
            m_fn.type = JsType::Function;
            m_fn.closure = env;
            m_fn.params = m.params;
            for (const auto& d : m.default_values) m_fn.default_values.push_back(d);
            m_fn.body = m.body;
            m_fn.is_async = m.is_async;
            m_fn.is_generator = m.is_generator;
            if (m.is_generator) {
                m_fn.native_fn = [this, m_body, m_params, m_defs, closure](const std::vector<JsValue>& args) {
                    return create_generator_instance(this, m_body, m_params, m_defs, closure, args);
                };
            } else {
                m_fn.native_fn = [this, m_body, m_params, m_defs, closure, m_async](const std::vector<JsValue>& args) {
                    for (const auto& [k, v] : m_vm.globals()) {
                        m_global_env->define(k, v);
                    }
                    auto fn_env = std::make_shared<JsEnvironment>(closure ? closure : m_global_env);
                    for (size_t i = 0; i < m_params.size(); ++i) {
                        JsValue arg = (i < args.size() && args[i].type != JsType::Undefined) ? args[i] : JsValue::make_undefined();
                        if (arg.type == JsType::Undefined && i < m_defs.size() && m_defs[i]) {
                            arg = eval_expr(m_defs[i].get(), fn_env);
                        }
                        fn_env->define(m_params[i], arg);
                    }

                    if (m_async) {
                        auto p_data = std::make_shared<PromiseData>();
                        try {
                            JsValue r = execute(m_body.get(), fn_env);
                            Promise::resolve(p_data, r);
                        } catch (const ReturnException& ret) {
                            Promise::resolve(p_data, ret.value);
                        } catch (const std::exception& ex) {
                            Promise::reject(p_data, JsValue::make_string(ex.what()));
                        }
                        return Promise::create_promise_instance(p_data);
                    }

                    try {
                        return execute(m_body.get(), fn_env);
                    } catch (const ReturnException& ret) {
                        return ret.value;
                    }
                };
            }

            if (m.is_static) {
                ctor_fn.set_property(m.name, m_fn);
            } else {
                if (super_ctor.is_truthy()) {
                    m_fn.set_property("__super_constructor__", super_ctor);
                }
                proto_obj.set_property(m.name, m_fn);
            }
        }

        env->define(class_decl->name, ctor_fn);
        if (env == m_global_env) {
            m_vm.set_global(class_decl->name, ctor_fn);
        }
        return ctor_fn;
    } else if (auto ret_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        JsValue ret_val = ret_stmt->value ? eval_expr(ret_stmt->value.get(), env) : JsValue::make_undefined();
        throw ReturnException(ret_val);
    }
    return JsValue::make_undefined();
}

// Reads the current value behind an assignment target.
JsValue JsInterpreter::read_lvalue(const Expr* target, const std::shared_ptr<JsEnvironment>& env) {
    if (auto id = dynamic_cast<const IdentifierExpr*>(target)) {
        return env->get(id->name);
    }
    if (auto member = dynamic_cast<const MemberExpr*>(target)) {
        JsValue obj = eval_expr(member->object.get(), env);
        return obj.get_property(member->property);
    }
    if (auto index = dynamic_cast<const IndexExpr*>(target)) {
        JsValue obj = eval_expr(index->object.get(), env);
        JsValue idx = eval_expr(index->index.get(), env);
        std::string prop = (idx.type == JsType::Number)
            ? std::to_string(static_cast<int64_t>(idx.number_val))
            : idx.to_string();
        return obj.get_property(prop);
    }
    return JsValue::make_undefined();
}

// Stores into an assignment target.
void JsInterpreter::write_lvalue(const Expr* target, const std::shared_ptr<JsEnvironment>& env,
                                 const JsValue& value) {
    if (auto id = dynamic_cast<const IdentifierExpr*>(target)) {
        env->assign(id->name, value);
        return;
    }
    if (auto member = dynamic_cast<const MemberExpr*>(target)) {
        JsValue obj = eval_expr(member->object.get(), env);
        obj.set_property(member->property, value);
        return;
    }
    if (auto index = dynamic_cast<const IndexExpr*>(target)) {
        JsValue obj = eval_expr(index->object.get(), env);
        JsValue idx = eval_expr(index->index.get(), env);
        std::string prop = (idx.type == JsType::Number)
            ? std::to_string(static_cast<int64_t>(idx.number_val))
            : idx.to_string();
        obj.set_property(prop, value);
        return;
    }
}

// Applies a compound-assignment operator to the value already read.
JsValue JsInterpreter::apply_compound(JsTokenType op, const JsValue& left, const JsValue& right) {
    switch (op) {
        case JsTokenType::PlusEqual:
            // `+=` concatenates when either operand is a string, exactly like `+`.
            if (left.type == JsType::String || right.type == JsType::String ||
                left.type == JsType::DomNode || right.type == JsType::DomNode) {
                return JsValue::make_string(left.to_string() + right.to_string());
            }
            return JsValue::make_number(left.to_number() + right.to_number());
        case JsTokenType::MinusEqual:
            return JsValue::make_number(left.to_number() - right.to_number());
        case JsTokenType::StarEqual:
            return JsValue::make_number(left.to_number() * right.to_number());
        case JsTokenType::SlashEqual:
            return JsValue::make_number(left.to_number() / right.to_number());
        case JsTokenType::PercentEqual:
            return JsValue::make_number(std::fmod(left.to_number(), right.to_number()));
        default:
            return right;
    }
}

JsValue JsInterpreter::eval_expr(const Expr* expr, std::shared_ptr<JsEnvironment> env) {
    if (!expr) return JsValue::make_undefined();

    if (auto lit = dynamic_cast<const LiteralExpr*>(expr)) {
        return lit->value;
    }

    if (auto id = dynamic_cast<const IdentifierExpr*>(expr)) {
        return env->get(id->name);
    }

    if (auto obj_lit = dynamic_cast<const ObjectLiteralExpr*>(expr)) {
        JsValue obj = JsValue::make_object();
        for (const auto& [k, v] : obj_lit->entries) {
            obj.set_property(k, eval_expr(v.get(), env));
        }
        return obj;
    }

    if (auto fn_expr = dynamic_cast<const FunctionExpr*>(expr)) {
        JsValue fn_val;
        fn_val.type = JsType::Function;
        fn_val.params = fn_expr->params;
        for (const auto& d : fn_expr->default_values) fn_val.default_values.push_back(d);
        fn_val.body = fn_expr->body;
        fn_val.closure = env;
        fn_val.is_async = fn_expr->is_async;
        fn_val.is_generator = fn_expr->is_generator;

        auto body_ptr = fn_expr->body;
        auto params = fn_expr->params;
        auto defs = fn_expr->default_values;
        auto closure = env;
        bool is_async = fn_expr->is_async;
        bool is_gen = fn_expr->is_generator;

        if (is_gen) {
            fn_val.native_fn = [this, body_ptr, params, defs, closure](const std::vector<JsValue>& args) {
                return create_generator_instance(this, body_ptr, params, defs, closure, args);
            };
        } else {
            fn_val.native_fn = [this, body_ptr, params, defs, closure, is_async](const std::vector<JsValue>& args) {
                for (const auto& [k, v] : m_vm.globals()) {
                    m_global_env->define(k, v);
                }
                auto fn_env = std::make_shared<JsEnvironment>(closure ? closure : m_global_env);
                for (size_t i = 0; i < params.size(); ++i) {
                    JsValue arg = (i < args.size() && args[i].type != JsType::Undefined) ? args[i] : JsValue::make_undefined();
                    if (arg.type == JsType::Undefined && i < defs.size() && defs[i]) {
                        arg = eval_expr(defs[i].get(), fn_env);
                    }
                    fn_env->define(params[i], arg);
                }

                if (is_async) {
                    auto p_data = std::make_shared<PromiseData>();
                    try {
                        JsValue res = execute(body_ptr.get(), fn_env);
                        Promise::resolve(p_data, res);
                    } catch (const ReturnException& ret) {
                        Promise::resolve(p_data, ret.value);
                    } catch (const std::exception& ex) {
                        Promise::reject(p_data, JsValue::make_string(ex.what()));
                    }
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return Promise::create_promise_instance(p_data);
                }

                try {
                    JsValue res = execute(body_ptr.get(), fn_env);
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return res;
                } catch (const ReturnException& ret) {
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return ret.value;
                }
            };
        }

        return fn_val;
    }

    if (auto yield_expr = dynamic_cast<const YieldExpr*>(expr)) {
        auto active_gen = GeneratorContext::get_active();
        if (!active_gen) {
            return JsValue::make_undefined();
        }

        if (yield_expr->delegate) {
            JsValue iterable = yield_expr->argument ? eval_expr(yield_expr->argument.get(), env) : JsValue::make_undefined();
            if (iterable.is_array()) {
                double len = iterable.get_property("length").to_number();
                for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                    active_gen->yield_to_main(iterable.get_property(std::to_string(i)));
                }
            } else if (iterable.is_generator || iterable.generator_data != nullptr || iterable.has_property("next")) {
                while (true) {
                    JsValue next_fn = iterable.get_property("next");
                    if (!next_fn.is_callable()) break;
                    JsValue step = next_fn.call_with_this(iterable, {});
                    if (step.get_property("done").is_truthy()) break;
                    active_gen->yield_to_main(step.get_property("value"));
                }
            } else if (iterable.has_property("@@iterator") || iterable.has_property("Symbol.iterator")) {
                JsValue iter_fn = iterable.has_property("@@iterator") ? iterable.get_property("@@iterator") : iterable.get_property("Symbol.iterator");
                if (iter_fn.is_callable()) {
                    JsValue iter = iter_fn.call_with_this(iterable, {});
                    while (true) {
                        JsValue next_fn = iter.get_property("next");
                        if (!next_fn.is_callable()) break;
                        JsValue step = next_fn.call_with_this(iter, {});
                        if (step.get_property("done").is_truthy()) break;
                        active_gen->yield_to_main(step.get_property("value"));
                    }
                }
            }
            return JsValue::make_undefined();
        }

        JsValue val = yield_expr->argument ? eval_expr(yield_expr->argument.get(), env) : JsValue::make_undefined();
        return active_gen->yield_to_main(val);
    }

    if (auto tle = dynamic_cast<const TemplateLiteralExpr*>(expr)) {
        std::string result;
        for (const auto& part : tle->parts) {
            JsValue val = eval_expr(part.get(), env);
            result += val.to_string();
        }
        return JsValue::make_string(result);
    }

    if (auto await_expr = dynamic_cast<const AwaitExpr*>(expr)) {
        JsValue val = eval_expr(await_expr->expression.get(), env);
        if (val.promise_data) {
            while (val.promise_data->state == PromiseState::Pending) {
                EventLoop::default_loop().tick();
            }
            if (val.promise_data->state == PromiseState::Fulfilled) {
                return val.promise_data->result;
            }
            if (val.promise_data->state == PromiseState::Rejected) {
                throw std::runtime_error(val.promise_data->result.to_string());
            }
        }
        return val;
    }

    if (auto sup = dynamic_cast<const SuperExpr*>(expr)) {
        JsValue this_val;
        try {
            this_val = env->get("this");
        } catch (...) {
            this_val = JsValue::make_undefined();
        }

        JsValue super_ctor;
        try {
            super_ctor = env->get("__super_constructor__");
        } catch (...) {
            super_ctor = JsValue::make_undefined();
        }
        if (!super_ctor.is_truthy() && this_val.type == JsType::Object) {
            JsValue ctor = this_val.get_property("constructor");
            if (ctor.is_truthy()) {
                super_ctor = ctor.get_property("__super_constructor__");
            }
        }

        std::vector<JsValue> arg_vals;
        for (const auto& a : sup->args) {
            arg_vals.push_back(eval_expr(a.get(), env));
        }

        if (sup->method_name.empty()) {
            if (super_ctor.type == JsType::Function || super_ctor.type == JsType::NativeFunction) {
                if (super_ctor.body) {
                    auto s_env = std::make_shared<JsEnvironment>(super_ctor.closure ? super_ctor.closure : m_global_env);
                    s_env->define("this", this_val);
                    JsValue parent_super = super_ctor.get_property("__super_constructor__");
                    if (parent_super.is_truthy()) {
                        s_env->define("__super_constructor__", parent_super);
                    }
                    for (size_t i = 0; i < super_ctor.params.size(); ++i) {
                        JsValue arg = (i < arg_vals.size() && arg_vals[i].type != JsType::Undefined) ? arg_vals[i] : JsValue::make_undefined();
                        if (arg.type == JsType::Undefined && i < super_ctor.default_values.size() && super_ctor.default_values[i]) {
                            arg = eval_expr(dynamic_cast<const Expr*>(super_ctor.default_values[i].get()), s_env);
                        }
                        s_env->define(super_ctor.params[i], arg);
                    }
                    try {
                        execute(super_ctor.body.get(), s_env);
                    } catch (...) {}
                } else if (super_ctor.native_fn) {
                    super_ctor.native_fn(arg_vals);
                }
            }
            return this_val;
        } else {
            JsValue super_proto = super_ctor.get_property("prototype");
            JsValue method = super_proto.get_property(sup->method_name);
            if (sup->is_call) {
                if (method.type == JsType::Function || method.type == JsType::NativeFunction) {
                    auto fn_env = std::make_shared<JsEnvironment>(method.closure ? method.closure : m_global_env);
                    fn_env->define("this", this_val);
                    JsValue p_super = super_ctor.get_property("__super_constructor__");
                    if (p_super.is_truthy()) {
                        fn_env->define("__super_constructor__", p_super);
                    }
                    for (size_t i = 0; i < method.params.size(); ++i) {
                        JsValue arg = (i < arg_vals.size() && arg_vals[i].type != JsType::Undefined) ? arg_vals[i] : JsValue::make_undefined();
                        fn_env->define(method.params[i], arg);
                    }
                    if (method.body) {
                        try {
                            return execute(method.body.get(), fn_env);
                        } catch (const ReturnException& ret) {
                            return ret.value;
                        }
                    }
                    if (method.native_fn) {
                        return method.native_fn(arg_vals);
                    }
                }
                return JsValue::make_undefined();
            }
            return method;
        }
    }

    if (auto assign = dynamic_cast<const AssignExpr*>(expr)) {
        JsValue right = eval_expr(assign->value.get(), env);

        // Compound assignment reads the current value first, then applies the
        // corresponding binary operator. `+=` must concatenate when either side
        // is a string, which is why it shares the binary `+` semantics rather
        // than doing arithmetic.
        if (assign->op != JsTokenType::Equal) {
            JsValue current = read_lvalue(assign->target.get(), env);
            right = apply_compound(assign->op, current, right);
        }

        write_lvalue(assign->target.get(), env, right);
        return right;
    }

    if (auto update = dynamic_cast<const UpdateExpr*>(expr)) {
        // The lvalue is evaluated exactly once, so `a[i++]++` does what it says.
        JsValue current = read_lvalue(update->target.get(), env);
        double before = current.to_number();
        double after = before + (update->op == JsTokenType::PlusPlus ? 1.0 : -1.0);
        write_lvalue(update->target.get(), env, JsValue::make_number(after));
        return JsValue::make_number(update->prefix ? after : before);
    }

    if (auto unary = dynamic_cast<const UnaryExpr*>(expr)) {
        JsValue operand = eval_expr(unary->operand.get(), env);
        if (unary->op == JsTokenType::Minus) {
            return JsValue::make_number(-operand.to_number());
        } else if (unary->op == JsTokenType::Bang) {
            return JsValue::make_boolean(!operand.is_truthy());
        }
        return operand;
    }

    if (auto binary = dynamic_cast<const BinaryExpr*>(expr)) {
        // Short-circuit logical operators
        if (binary->op == JsTokenType::PipePipe) {
            JsValue left = eval_expr(binary->left.get(), env);
            if (left.is_truthy()) return left;
            return eval_expr(binary->right.get(), env);
        }
        if (binary->op == JsTokenType::AmpAmp) {
            JsValue left = eval_expr(binary->left.get(), env);
            if (!left.is_truthy()) return left;
            return eval_expr(binary->right.get(), env);
        }

        JsValue left = eval_expr(binary->left.get(), env);
        JsValue right = eval_expr(binary->right.get(), env);

        switch (binary->op) {
            case JsTokenType::Plus: {
                if (left.type == JsType::String || right.type == JsType::String) {
                    return JsValue::make_string(left.to_string() + right.to_string());
                }
                return JsValue::make_number(left.to_number() + right.to_number());
            }
            case JsTokenType::Minus:
                return JsValue::make_number(left.to_number() - right.to_number());
            case JsTokenType::Star:
                return JsValue::make_number(left.to_number() * right.to_number());
            case JsTokenType::Slash:
                return JsValue::make_number(left.to_number() / right.to_number());
            case JsTokenType::Percent:
                return JsValue::make_number(std::fmod(left.to_number(), right.to_number()));
            case JsTokenType::EqualEqual: {
                if (left.type == right.type) {
                    if (left.type == JsType::Number) return JsValue::make_boolean(left.number_val == right.number_val);
                    if (left.type == JsType::String) return JsValue::make_boolean(left.string_val == right.string_val);
                    if (left.type == JsType::Boolean) return JsValue::make_boolean(left.bool_val == right.bool_val);
                }
                return JsValue::make_boolean(left.to_string() == right.to_string());
            }
            case JsTokenType::NotEqual:
                return JsValue::make_boolean(left.to_string() != right.to_string());
            case JsTokenType::StrictEqual:
                return JsValue::make_boolean(strict_equals(left, right));
            case JsTokenType::StrictNotEqual:
                return JsValue::make_boolean(!strict_equals(left, right));
            case JsTokenType::Less:
                return JsValue::make_boolean(left.to_number() < right.to_number());
            case JsTokenType::LessEqual:
                return JsValue::make_boolean(left.to_number() <= right.to_number());
            case JsTokenType::Greater:
                return JsValue::make_boolean(left.to_number() > right.to_number());
            case JsTokenType::GreaterEqual:
                return JsValue::make_boolean(left.to_number() >= right.to_number());
            case JsTokenType::InstanceOf: {
                if (left.type != JsType::Object || left.object_val == nullptr) {
                    return JsValue::make_boolean(false);
                }
                JsValue right_proto = right.get_property("prototype");
                if (right_proto.type != JsType::Object || right_proto.object_val == nullptr) {
                    return JsValue::make_boolean(false);
                }
                JsObject* curr = left.object_val->prototype;
                while (curr) {
                    if (curr == right_proto.object_val) return JsValue::make_boolean(true);
                    curr = curr->prototype;
                }
                return JsValue::make_boolean(false);
            }
            default:
                break;
        }
    }

    if (auto member = dynamic_cast<const MemberExpr*>(expr)) {
        JsValue obj = eval_expr(member->object.get(), env);
        return obj.get_property(member->property);
    }

    if (auto index_expr = dynamic_cast<const IndexExpr*>(expr)) {
        JsValue obj = eval_expr(index_expr->object.get(), env);
        JsValue idx_val = eval_expr(index_expr->index.get(), env);
        std::string prop = (idx_val.type == JsType::Number) ?
            std::to_string(static_cast<int64_t>(idx_val.number_val)) : idx_val.to_string();
        return obj.get_property(prop);
    }

    if (auto array_lit = dynamic_cast<const ArrayLiteralExpr*>(expr)) {
        std::vector<JsValue> elements;
        for (const auto& elem : array_lit->elements) {
            elements.push_back(eval_expr(elem.get(), env));
        }
        return JsValue::make_array(elements);
    }

    if (auto call = dynamic_cast<const CallExpr*>(expr)) {
        JsValue callee_val = eval_expr(call->callee.get(), env);
        std::vector<JsValue> arg_vals;
        for (const auto& a : call->args) {
            arg_vals.push_back(eval_expr(a.get(), env));
        }

        JsValue this_val = JsValue::make_undefined();
        if (auto mem = dynamic_cast<const MemberExpr*>(call->callee.get())) {
            this_val = eval_expr(mem->object.get(), env);
        }

        if (callee_val.type == JsType::NativeFunction && callee_val.native_fn) {
            return callee_val.native_fn(arg_vals);
        }

        if (callee_val.is_generator && callee_val.native_fn) {
            return callee_val.native_fn(arg_vals);
        }

        if (callee_val.type == JsType::Function) {
            auto fn_env = std::make_shared<JsEnvironment>(callee_val.closure ? callee_val.closure : m_global_env);
            if (this_val.type == JsType::Object || this_val.type == JsType::DomNode) {
                fn_env->define("this", this_val);
            }
            JsValue super_c = callee_val.get_property("__super_constructor__");
            if (super_c.is_truthy()) {
                fn_env->define("__super_constructor__", super_c);
            }
            for (size_t i = 0; i < callee_val.params.size(); ++i) {
                JsValue arg = (i < arg_vals.size() && arg_vals[i].type != JsType::Undefined) ? arg_vals[i] : JsValue::make_undefined();
                if (arg.type == JsType::Undefined && i < callee_val.default_values.size() && callee_val.default_values[i]) {
                    arg = eval_expr(dynamic_cast<const Expr*>(callee_val.default_values[i].get()), fn_env);
                }
                fn_env->define(callee_val.params[i], arg);
            }
            if (callee_val.is_async) {
                auto p_data = std::make_shared<PromiseData>();
                try {
                    JsValue res = execute(callee_val.body.get(), fn_env);
                    Promise::resolve(p_data, res);
                } catch (const ReturnException& ret) {
                    Promise::resolve(p_data, ret.value);
                } catch (const std::exception& ex) {
                    Promise::reject(p_data, JsValue::make_string(ex.what()));
                }
                for (const auto& [k, v] : m_global_env->values()) {
                    m_vm.set_global(k, v);
                }
                return Promise::create_promise_instance(p_data);
            }

            if (callee_val.body) {
                try {
                    JsValue res = execute(callee_val.body.get(), fn_env);
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return res;
                } catch (const ReturnException& ret) {
                    for (const auto& [k, v] : m_global_env->values()) {
                        m_vm.set_global(k, v);
                    }
                    return ret.value;
                }
            }
            if (callee_val.native_fn) {
                return callee_val.native_fn(arg_vals);
            }
            return JsValue::make_undefined();
        }
    }

    if (auto new_expr = dynamic_cast<const NewExpr*>(expr)) {
        JsValue callee_val = eval_expr(new_expr->callee.get(), env);
        std::vector<JsValue> arg_vals;
        for (const auto& a : new_expr->args) {
            arg_vals.push_back(eval_expr(a.get(), env));
        }

        if (callee_val.type == JsType::NativeFunction && callee_val.native_fn) {
            return callee_val.native_fn(arg_vals);
        }

        if (callee_val.type == JsType::Function) {
            JsValue instance = JsValue::make_object(Heap::default_heap().allocate_object());
            JsValue proto = callee_val.get_property("prototype");
            if (proto.type == JsType::Object && proto.object_val) {
                instance.object_val->prototype = proto.object_val;
            }

            auto fn_env = std::make_shared<JsEnvironment>(callee_val.closure ? callee_val.closure : m_global_env);
            fn_env->define("this", instance);

            JsValue super_c = callee_val.get_property("__super_constructor__");
            if (super_c.is_truthy()) {
                fn_env->define("__super_constructor__", super_c);
            }

            for (size_t i = 0; i < callee_val.params.size(); ++i) {
                JsValue arg = (i < arg_vals.size() && arg_vals[i].type != JsType::Undefined) ? arg_vals[i] : JsValue::make_undefined();
                if (arg.type == JsType::Undefined && i < callee_val.default_values.size() && callee_val.default_values[i]) {
                    arg = eval_expr(dynamic_cast<const Expr*>(callee_val.default_values[i].get()), fn_env);
                }
                fn_env->define(callee_val.params[i], arg);
            }

            if (callee_val.body) {
                try {
                    JsValue res = execute(callee_val.body.get(), fn_env);
                    if (res.type == JsType::Object && res.object_val != nullptr) return res;
                    return instance;
                } catch (const ReturnException& ret) {
                    if (ret.value.type == JsType::Object && ret.value.object_val != nullptr) return ret.value;
                    return instance;
                }
            } else if (super_c.type == JsType::Function && super_c.body) {
                auto s_env = std::make_shared<JsEnvironment>(super_c.closure ? super_c.closure : m_global_env);
                s_env->define("this", instance);
                for (size_t i = 0; i < super_c.params.size(); ++i) {
                    JsValue arg = (i < arg_vals.size()) ? arg_vals[i] : JsValue::make_undefined();
                    s_env->define(super_c.params[i], arg);
                }
                try {
                    execute(super_c.body.get(), s_env);
                } catch (...) {}
                return instance;
            } else if (callee_val.native_fn) {
                JsValue res = callee_val.native_fn(arg_vals);
                if (res.type == JsType::Object && res.object_val != nullptr) return res;
                return instance;
            }
            return instance;
        }
    }

    return JsValue::make_undefined();
}

} // namespace litebird
