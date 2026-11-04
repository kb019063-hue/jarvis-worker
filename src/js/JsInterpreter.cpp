#include "litebird/js/JsInterpreter.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/js/EventLoop.hpp"
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
    try {
        auto fn = compile_to_bytecode(source);
        return m_vm.interpret(fn);
    } catch (const std::exception& ex) {
        std::cerr << "Bytecode VM Exception: " << ex.what() << ", falling back to AST interpreter\n";
        return eval_ast(source);
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

    // document object
    if (m_document) {
        JsValue doc_obj = JsValue::make_object();
        doc_obj.set_property("title", JsValue::make_string(m_document->title()));
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
        fn_val.body = fn_decl->body;
        fn_val.closure = env;

        auto body_ptr = fn_decl->body;
        auto params = fn_decl->params;
        auto closure = env;
        fn_val.native_fn = [this, body_ptr, params, closure](const std::vector<JsValue>& args) {
            auto fn_env = std::make_shared<JsEnvironment>(closure);
            for (size_t i = 0; i < params.size(); ++i) {
                JsValue arg = (i < args.size()) ? args[i] : JsValue::make_undefined();
                fn_env->define(params[i], arg);
            }
            try {
                return execute(body_ptr.get(), fn_env);
            } catch (const ReturnException& ret) {
                return ret.value;
            }
        };

        env->define(fn_decl->name, fn_val);
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
        fn_val.body = fn_expr->body;
        fn_val.closure = env;

        auto body_ptr = fn_expr->body;
        auto params = fn_expr->params;
        auto closure = env;
        fn_val.native_fn = [this, body_ptr, params, closure](const std::vector<JsValue>& args) {
            auto fn_env = std::make_shared<JsEnvironment>(closure);
            for (size_t i = 0; i < params.size(); ++i) {
                JsValue arg = (i < args.size()) ? args[i] : JsValue::make_undefined();
                fn_env->define(params[i], arg);
            }
            try {
                return execute(body_ptr.get(), fn_env);
            } catch (const ReturnException& ret) {
                return ret.value;
            }
        };

        return fn_val;
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
