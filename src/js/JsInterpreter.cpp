#include "litebird/js/JsInterpreter.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <cmath>
#include <iostream>

namespace litebird {

JsInterpreter::JsInterpreter(std::shared_ptr<Document> doc)
    : m_global_env(std::make_shared<JsEnvironment>()), m_document(std::move(doc)), m_vm(m_document) {
    init_builtins();
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
                return JsValue::make_dom_node(el);
            }
            return JsValue::make_null();
        }));
        m_global_env->define("document", doc_obj);
    }

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
    JsValue::set_function_caller([this](const JsValue& fn, const std::vector<JsValue>& args) {
        for (const auto& [k, v] : m_vm.globals()) {
            m_global_env->define(k, v);
        }
        auto fn_env = std::make_shared<JsEnvironment>(fn.closure ? fn.closure : m_global_env);
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

    // fetch(url, options) built-in
    m_global_env->define("fetch", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) {
            return Promise::make_rejected(JsValue::make_string("fetch requires a URL argument"));
        }
        std::string url = args[0].to_string();
        auto pdata = std::make_shared<PromiseData>();
        JsValue pval = Promise::create_promise_instance(pdata);

        EventLoop::default_loop().post_task([url, pdata]() {
            ResourceResponse resp = ResourceLoader::load(url);
            if (resp.success) {
                JsValue res_obj = JsValue::make_object();
                res_obj.set_property("ok", JsValue::make_boolean(resp.status_code >= 200 && resp.status_code < 300));
                res_obj.set_property("status", JsValue::make_number(resp.status_code));
                res_obj.set_property("statusText", JsValue::make_string(resp.status_code == 200 ? "OK" : "Error"));
                res_obj.set_property("url", JsValue::make_string(url));

                JsValue headers = JsValue::make_object();
                headers.set_property("content-type", JsValue::make_string(resp.content_type));
                res_obj.set_property("headers", headers);

                std::string body = resp.body;
                res_obj.set_property("text", JsValue::make_native_function([body](const std::vector<JsValue>&) {
                    return Promise::make_resolved(JsValue::make_string(body));
                }));

                res_obj.set_property("json", JsValue::make_native_function([body](const std::vector<JsValue>&) {
                    try {
                        JsValue parsed = Json::parse(body);
                        return Promise::make_resolved(parsed);
                    } catch (const std::exception& ex) {
                        return Promise::make_rejected(JsValue::make_string(ex.what()));
                    }
                }));

                Promise::resolve(pdata, res_obj);
            } else {
                Promise::reject(pdata, JsValue::make_string(resp.error_message.empty() ? "Network request failed" : resp.error_message));
            }
        });

        return pval;
    }));

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
    } else if (auto fn_decl = dynamic_cast<const FunctionDeclStmt*>(stmt)) {
        JsValue fn_val;
        fn_val.type = JsType::Function;
        fn_val.params = fn_decl->params;
        for (const auto& d : fn_decl->default_values) fn_val.default_values.push_back(d);
        fn_val.body = fn_decl->body;
        fn_val.closure = env;

        auto body_ptr = fn_decl->body;
        auto params = fn_decl->params;
        auto defs = fn_decl->default_values;
        auto closure = env;
        fn_val.native_fn = [this, body_ptr, params, defs, closure](const std::vector<JsValue>& args) {
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

        env->define(fn_decl->name, fn_val);
        if (env == m_global_env) {
            m_vm.set_global(fn_decl->name, fn_val);
        }
        return fn_val;
    } else if (auto ret_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        JsValue ret_val = ret_stmt->value ? eval_expr(ret_stmt->value.get(), env) : JsValue::make_undefined();
        throw ReturnException(ret_val);
    }
    return JsValue::make_undefined();
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

        auto body_ptr = fn_expr->body;
        auto params = fn_expr->params;
        auto defs = fn_expr->default_values;
        auto closure = env;
        fn_val.native_fn = [this, body_ptr, params, defs, closure](const std::vector<JsValue>& args) {
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

        return fn_val;
    }

    if (auto tle = dynamic_cast<const TemplateLiteralExpr*>(expr)) {
        std::string result;
        for (const auto& part : tle->parts) {
            JsValue val = eval_expr(part.get(), env);
            result += val.to_string();
        }
        return JsValue::make_string(result);
    }

    if (auto assign = dynamic_cast<const AssignExpr*>(expr)) {
        JsValue right = eval_expr(assign->value.get(), env);
        if (auto id_target = dynamic_cast<const IdentifierExpr*>(assign->target.get())) {
            env->assign(id_target->name, right);
            return right;
        } else if (auto member_target = dynamic_cast<const MemberExpr*>(assign->target.get())) {
            JsValue obj = eval_expr(member_target->object.get(), env);
            obj.set_property(member_target->property, right);
            return right;
        } else if (auto index_target = dynamic_cast<const IndexExpr*>(assign->target.get())) {
            JsValue obj = eval_expr(index_target->object.get(), env);
            JsValue idx_val = eval_expr(index_target->index.get(), env);
            std::string prop = (idx_val.type == JsType::Number) ?
                std::to_string(static_cast<int64_t>(idx_val.number_val)) : idx_val.to_string();
            obj.set_property(prop, right);
            return right;
        }
        return right;
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
            case JsTokenType::Less:
                return JsValue::make_boolean(left.to_number() < right.to_number());
            case JsTokenType::LessEqual:
                return JsValue::make_boolean(left.to_number() <= right.to_number());
            case JsTokenType::Greater:
                return JsValue::make_boolean(left.to_number() > right.to_number());
            case JsTokenType::GreaterEqual:
                return JsValue::make_boolean(left.to_number() >= right.to_number());
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

        if (callee_val.type == JsType::NativeFunction && callee_val.native_fn) {
            return callee_val.native_fn(arg_vals);
        }

        if (callee_val.type == JsType::Function) {
            auto fn_env = std::make_shared<JsEnvironment>(callee_val.closure ? callee_val.closure : m_global_env);
            for (size_t i = 0; i < callee_val.params.size(); ++i) {
                JsValue arg = (i < arg_vals.size() && arg_vals[i].type != JsType::Undefined) ? arg_vals[i] : JsValue::make_undefined();
                if (arg.type == JsType::Undefined && i < callee_val.default_values.size() && callee_val.default_values[i]) {
                    arg = eval_expr(dynamic_cast<const Expr*>(callee_val.default_values[i].get()), fn_env);
                }
                fn_env->define(callee_val.params[i], arg);
            }
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
            auto fn_env = std::make_shared<JsEnvironment>(callee_val.closure);
            for (size_t i = 0; i < callee_val.params.size(); ++i) {
                JsValue arg = (i < arg_vals.size()) ? arg_vals[i] : JsValue::make_undefined();
                fn_env->define(callee_val.params[i], arg);
            }
            try {
                return execute(callee_val.body.get(), fn_env);
            } catch (const ReturnException& ret) {
                return ret.value;
            }
        }
    }

    return JsValue::make_undefined();
}

} // namespace litebird
