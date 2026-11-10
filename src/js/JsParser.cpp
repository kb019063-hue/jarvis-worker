#include "litebird/js/JsParser.hpp"
#include <stdexcept>

namespace litebird {

JsParser::JsParser(std::vector<JsToken> tokens) : m_tokens(std::move(tokens)) {}

const JsToken& JsParser::peek() const {
    return m_tokens[m_current];
}

const JsToken& JsParser::previous() const {
    return m_tokens[m_current - 1];
}

bool JsParser::is_at_end() const {
    return peek().type == JsTokenType::Eof;
}

const JsToken& JsParser::advance() {
    if (!is_at_end()) m_current++;
    return previous();
}

bool JsParser::check(JsTokenType type) const {
    if (is_at_end()) return false;
    return peek().type == type;
}

bool JsParser::match(JsTokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

const JsToken& JsParser::consume(JsTokenType type, const std::string& err_msg) {
    if (check(type)) return advance();
    throw std::runtime_error("JS Parse Error: " + err_msg);
}

std::unique_ptr<BlockStmt> JsParser::parse_program() {
    auto block = std::make_unique<BlockStmt>();
    while (!is_at_end()) {
        auto stmt = parse_statement();
        if (stmt) {
            block->statements.push_back(std::move(stmt));
        }
    }
    return block;
}

std::unique_ptr<Stmt> JsParser::parse_statement() {
    if (check(JsTokenType::Var) || check(JsTokenType::Let) || check(JsTokenType::Const)) {
        const JsTokenType token = advance().type;
        const JsDeclKind kind = token == JsTokenType::Var ? JsDeclKind::Var
                              : token == JsTokenType::Const ? JsDeclKind::Const : JsDeclKind::Let;
        return parse_var_declaration(kind);
    }
    if (match(JsTokenType::If)) {
        return parse_if_statement();
    }
    if (match(JsTokenType::While)) {
        return parse_while_statement();
    }
    if (match(JsTokenType::Switch)) {
        return parse_switch_statement();
    }
    if (match(JsTokenType::Break)) {
        return parse_break_statement();
    }
    if (match(JsTokenType::For)) {
        return parse_for_statement();
    }
    if (match(JsTokenType::Return)) {
        return parse_return_statement();
    }
    if (match(JsTokenType::Function)) {
        return parse_function_declaration();
    }
    if (match(JsTokenType::Class)) {
        return parse_class_declaration();
    }
    if (match(JsTokenType::Async)) {
        consume(JsTokenType::Function, "Expected 'function' after 'async'.");
        auto fn = parse_function_declaration();
        fn->is_async = true;
        return fn;
    }
    if (match(JsTokenType::OpenBrace)) {
        return parse_block();
    }

    // Expression statement
    auto expr = parse_expression();
    match(JsTokenType::Semicolon);
    return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<Stmt> JsParser::parse_var_declaration(JsDeclKind kind) {
    if (match(JsTokenType::OpenBracket)) {
        std::vector<std::string> names;
        if (!check(JsTokenType::CloseBracket)) {
            do {
                if (check(JsTokenType::CloseBracket)) break;
                names.push_back(consume(JsTokenType::Identifier, "Expected variable name in array destructuring.").text);
            } while (match(JsTokenType::Comma));
        }
        consume(JsTokenType::CloseBracket, "Expected ']' after array destructuring pattern.");
        consume(JsTokenType::Equal, "Expected '=' in destructuring declaration.");
        auto init = parse_expression();
        match(JsTokenType::Semicolon);
        return std::make_unique<ArrayDestructuringDeclStmt>(std::move(names), std::move(init), kind);
    }

    if (match(JsTokenType::OpenBrace)) {
        std::vector<std::pair<std::string, std::string>> bindings;
        if (!check(JsTokenType::CloseBrace)) {
            do {
                if (check(JsTokenType::CloseBrace)) break;
                std::string prop = consume(JsTokenType::Identifier, "Expected property name in object destructuring.").text;
                std::string var_name = prop;
                if (match(JsTokenType::Colon)) {
                    var_name = consume(JsTokenType::Identifier, "Expected binding variable name.").text;
                }
                bindings.emplace_back(std::move(prop), std::move(var_name));
            } while (match(JsTokenType::Comma));
        }
        consume(JsTokenType::CloseBrace, "Expected '}' after object destructuring pattern.");
        consume(JsTokenType::Equal, "Expected '=' in destructuring declaration.");
        auto init = parse_expression();
        match(JsTokenType::Semicolon);
        return std::make_unique<ObjectDestructuringDeclStmt>(std::move(bindings), std::move(init), kind);
    }

    const auto& name_token = consume(JsTokenType::Identifier, "Expected variable name.");
    std::unique_ptr<Expr> init = nullptr;

    if (match(JsTokenType::Equal)) {
        init = parse_expression();
    }
    match(JsTokenType::Semicolon);
    return std::make_unique<VarDeclStmt>(name_token.text, std::move(init), kind);
}

std::unique_ptr<FunctionDeclStmt> JsParser::parse_function_declaration() {
    bool is_gen = match(JsTokenType::Star);
    const auto& name_token = consume(JsTokenType::Identifier, "Expected function name.");
    consume(JsTokenType::OpenParen, "Expected '(' after function name.");
    std::vector<std::string> params;
    std::vector<std::shared_ptr<Expr>> defs;
    if (!check(JsTokenType::CloseParen)) {
        do {
            params.push_back(consume(JsTokenType::Identifier, "Expected parameter name.").text);
            std::shared_ptr<Expr> def_val = nullptr;
            if (match(JsTokenType::Equal)) {
                def_val = parse_expression();
            }
            defs.push_back(std::move(def_val));
        } while (match(JsTokenType::Comma));
    }
    consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
    consume(JsTokenType::OpenBrace, "Expected '{' before function body.");
    auto body = parse_block();
    return std::make_unique<FunctionDeclStmt>(name_token.text, std::move(params), std::move(body), std::move(defs), false, is_gen);
}

std::unique_ptr<ClassDeclStmt> JsParser::parse_class_declaration() {
    std::string class_name = consume(JsTokenType::Identifier, "Expected class name.").text;
    std::string super_class;
    if (match(JsTokenType::Extends)) {
        super_class = consume(JsTokenType::Identifier, "Expected superclass name.").text;
    }
    consume(JsTokenType::OpenBrace, "Expected '{' before class body.");

    std::vector<MethodDefinition> methods;
    while (!check(JsTokenType::CloseBrace) && !is_at_end()) {
        bool is_static = match(JsTokenType::Static);
        bool is_async = match(JsTokenType::Async);
        bool is_gen = match(JsTokenType::Star);

        std::string method_name = consume(JsTokenType::Identifier, "Expected method name in class.").text;
        bool is_constructor = (method_name == "constructor");

        consume(JsTokenType::OpenParen, "Expected '(' after method name.");
        std::vector<std::string> params;
        std::vector<std::shared_ptr<Expr>> defs;
        if (!check(JsTokenType::CloseParen)) {
            do {
                params.push_back(consume(JsTokenType::Identifier, "Expected parameter name.").text);
                std::shared_ptr<Expr> def_val = nullptr;
                if (match(JsTokenType::Equal)) {
                    def_val = parse_expression();
                }
                defs.push_back(std::move(def_val));
            } while (match(JsTokenType::Comma));
        }
        consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
        consume(JsTokenType::OpenBrace, "Expected '{' before method body.");
        auto body = parse_block();

        MethodDefinition m;
        m.name = method_name;
        m.is_static = is_static;
        m.is_constructor = is_constructor;
        m.is_async = is_async;
        m.is_generator = is_gen;
        m.params = std::move(params);
        m.default_values = std::move(defs);
        m.body = std::move(body);
        methods.push_back(std::move(m));
    }
    consume(JsTokenType::CloseBrace, "Expected '}' after class body.");
    return std::make_unique<ClassDeclStmt>(class_name, super_class, std::move(methods));
}

std::unique_ptr<Stmt> JsParser::parse_if_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after if.");
    auto condition = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after if condition.");

    auto then_branch = parse_statement();
    std::unique_ptr<Stmt> else_branch = nullptr;
    if (match(JsTokenType::Else)) {
        else_branch = parse_statement();
    }
    return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<Stmt> JsParser::parse_switch_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after switch.");
    auto discriminant = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after switch expression.");
    consume(JsTokenType::OpenBrace, "Expected '{' before switch body.");

    std::vector<SwitchCase> cases;
    bool saw_default = false;
    while (!check(JsTokenType::CloseBrace) && !is_at_end()) {
        SwitchCase clause;
        if (match(JsTokenType::Case)) {
            clause.test = parse_expression();
            consume(JsTokenType::Colon, "Expected ':' after case expression.");
        } else if (match(JsTokenType::Default)) {
            if (saw_default) throw std::runtime_error("Duplicate default clause in switch.");
            saw_default = true;
            consume(JsTokenType::Colon, "Expected ':' after default.");
        } else {
            throw std::runtime_error("Expected 'case' or 'default' in switch body.");
        }

        while (!check(JsTokenType::Case) && !check(JsTokenType::Default) &&
               !check(JsTokenType::CloseBrace) && !is_at_end()) {
            clause.statements.push_back(parse_statement());
        }
        cases.push_back(std::move(clause));
    }
    consume(JsTokenType::CloseBrace, "Expected '}' after switch body.");
    return std::make_unique<SwitchStmt>(std::move(discriminant), std::move(cases));
}

std::unique_ptr<Stmt> JsParser::parse_break_statement() {
    match(JsTokenType::Semicolon);
    return std::make_unique<BreakStmt>();
}

std::unique_ptr<Stmt> JsParser::parse_while_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after while.");
    auto condition = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after while condition.");

    auto body = parse_statement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<Stmt> JsParser::parse_for_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after 'for'.");
    bool is_decl = false;
    JsDeclKind decl_kind = JsDeclKind::Let;
    if (check(JsTokenType::Var) || check(JsTokenType::Let) || check(JsTokenType::Const)) {
        const JsTokenType token = advance().type;
        is_decl = true;
        decl_kind = token == JsTokenType::Var ? JsDeclKind::Var
                  : token == JsTokenType::Const ? JsDeclKind::Const : JsDeclKind::Let;
    }
    std::string var_name = consume(JsTokenType::Identifier, "Expected variable name in for loop.").text;

    if (match(JsTokenType::Of)) {
        auto iterable = parse_expression();
        consume(JsTokenType::CloseParen, "Expected ')' after for-of expression.");
        auto body = parse_statement();
        return std::make_unique<ForOfStmt>(var_name, std::move(iterable), std::move(body), decl_kind, is_decl);
    }

    // Traditional for loop fallback: for (var_name = init; cond; update)
    consume(JsTokenType::Equal, "Expected '=' in for loop init.");
    auto init_val = parse_expression();
    consume(JsTokenType::Semicolon, "Expected ';' after for loop init.");
    auto condition = parse_expression();
    consume(JsTokenType::Semicolon, "Expected ';' after for loop condition.");
    auto update = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after for loop clause.");
    auto body = parse_statement();

    auto block = std::make_unique<BlockStmt>();
    if (is_decl) {
        block->statements.push_back(std::make_unique<VarDeclStmt>(var_name, std::move(init_val), decl_kind));
    } else {
        auto target_id = std::make_unique<IdentifierExpr>(var_name);
        auto assign = std::make_unique<AssignExpr>(std::move(target_id), std::move(init_val));
        block->statements.push_back(std::make_unique<ExprStmt>(std::move(assign)));
    }

    auto while_body = std::make_unique<BlockStmt>();
    if (body) {
        while_body->statements.push_back(std::move(body));
    }
    while_body->statements.push_back(std::make_unique<ExprStmt>(std::move(update)));

    block->statements.push_back(std::make_unique<WhileStmt>(std::move(condition), std::move(while_body)));
    return block;
}

std::unique_ptr<Stmt> JsParser::parse_return_statement() {
    std::unique_ptr<Expr> val = nullptr;
    if (!check(JsTokenType::Semicolon)) {
        val = parse_expression();
    }
    match(JsTokenType::Semicolon);
    return std::make_unique<ReturnStmt>(std::move(val));
}

std::unique_ptr<BlockStmt> JsParser::parse_block() {
    auto block = std::make_unique<BlockStmt>();
    while (!check(JsTokenType::CloseBrace) && !is_at_end()) {
        auto stmt = parse_statement();
        if (stmt) block->statements.push_back(std::move(stmt));
    }
    consume(JsTokenType::CloseBrace, "Expected '}' after block.");
    return block;
}

std::unique_ptr<Expr> JsParser::parse_expression() {
    return parse_assignment();
}

std::unique_ptr<Expr> JsParser::parse_assignment() {
    auto expr = parse_logical_or();

    // Right-associative, so `a = b = c` nests as `a = (b = c)`.
    if (match(JsTokenType::Equal) || match(JsTokenType::PlusEqual) ||
        match(JsTokenType::MinusEqual) || match(JsTokenType::StarEqual) ||
        match(JsTokenType::SlashEqual) || match(JsTokenType::PercentEqual)) {
        JsTokenType op = previous().type;
        auto value = parse_assignment();
        if (!is_assignable(expr.get())) {
            throw std::runtime_error("Invalid assignment target: left-hand side is not a variable or property.");
        }
        return std::make_unique<AssignExpr>(std::move(expr), std::move(value), op);
    }
    return expr;
}

// Valid assignment targets are identifiers, `a.b` and `a[b]`.
bool JsParser::is_assignable(const Expr* expr) {
    return dynamic_cast<const IdentifierExpr*>(expr) != nullptr ||
           dynamic_cast<const MemberExpr*>(expr) != nullptr ||
           dynamic_cast<const IndexExpr*>(expr) != nullptr;
}

namespace {
bool root_is_unparenthesized_operator(const Expr* expr, JsTokenType op) {
    if (!expr || expr->explicitly_grouped) return false;
    auto binary = dynamic_cast<const BinaryExpr*>(expr);
    return binary && binary->op == op;
}
}

std::unique_ptr<Expr> JsParser::parse_logical_or() {
    auto expr = parse_coalesce();
    while (match(JsTokenType::PipePipe)) {
        if (root_is_unparenthesized_operator(expr.get(), JsTokenType::Nullish)) {
            throw std::runtime_error("Cannot mix '??' with '||' without parentheses.");
        }
        JsTokenType op = previous().type;
        auto right = parse_coalesce();
        if (root_is_unparenthesized_operator(right.get(), JsTokenType::Nullish)) {
            throw std::runtime_error("Cannot mix '??' with '||' without parentheses.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_coalesce() {
    auto expr = parse_logical_and();
    while (match(JsTokenType::Nullish)) {
        if (root_is_unparenthesized_operator(expr.get(), JsTokenType::AmpAmp)) {
            throw std::runtime_error("Cannot mix '??' with '&&' without parentheses.");
        }
        JsTokenType op = previous().type;
        auto right = parse_logical_and();
        if (root_is_unparenthesized_operator(right.get(), JsTokenType::AmpAmp)) {
            throw std::runtime_error("Cannot mix '??' with '&&' without parentheses.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_logical_and() {
    auto expr = parse_equality();
    while (match(JsTokenType::AmpAmp)) {
        JsTokenType op = previous().type;
        auto right = parse_equality();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_equality() {
    auto expr = parse_comparison();
    while (match(JsTokenType::EqualEqual) || match(JsTokenType::StrictEqual) ||
           match(JsTokenType::NotEqual) || match(JsTokenType::StrictNotEqual)) {
        JsTokenType op = previous().type;
        auto right = parse_comparison();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_comparison() {
    auto expr = parse_term();
    while (match(JsTokenType::Greater) || match(JsTokenType::GreaterEqual) ||
           match(JsTokenType::Less) || match(JsTokenType::LessEqual) ||
           match(JsTokenType::InstanceOf)) {
        JsTokenType op = previous().type;
        auto right = parse_term();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_term() {
    auto expr = parse_factor();
    while (match(JsTokenType::Plus) || match(JsTokenType::Minus)) {
        JsTokenType op = previous().type;
        auto right = parse_factor();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_factor() {
    auto expr = parse_unary();
    while (match(JsTokenType::Star) || match(JsTokenType::Slash) || match(JsTokenType::Percent)) {
        JsTokenType op = previous().type;
        auto right = parse_unary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_unary() {
    if (match(JsTokenType::Typeof)) {
        return std::make_unique<UnaryExpr>(JsTokenType::Typeof, parse_unary());
    }
    if (match(JsTokenType::Await)) {
        auto operand = parse_unary();
        return std::make_unique<AwaitExpr>(std::move(operand));
    }
    if (match(JsTokenType::Bang) || match(JsTokenType::Minus)) {
        JsTokenType op = previous().type;
        auto right = parse_unary();
        return std::make_unique<UnaryExpr>(op, std::move(right));
    }
    // Prefix increment/decrement: `++i` yields the value *after* the change,
    // so it is prefix=true. Postfix is handled in parse_call_or_member.
    if (match(JsTokenType::PlusPlus) || match(JsTokenType::MinusMinus)) {
        JsTokenType op = previous().type;
        auto target = parse_unary();
        if (!is_assignable(target.get())) {
            throw std::runtime_error("Invalid increment/decrement target.");
        }
        return std::make_unique<UpdateExpr>(std::move(target), op, /*prefix=*/true);
    }
    if (match(JsTokenType::New)) {
        auto callee = parse_primary();
        std::vector<std::unique_ptr<Expr>> args;
        if (match(JsTokenType::OpenParen)) {
            if (!check(JsTokenType::CloseParen)) {
                do {
                    args.push_back(parse_expression());
                } while (match(JsTokenType::Comma));
            }
            consume(JsTokenType::CloseParen, "Expected ')' after constructor arguments.");
        }
        return std::make_unique<NewExpr>(std::move(callee), std::move(args));
    }
    return parse_exponentiation();
}

std::unique_ptr<Expr> JsParser::parse_exponentiation() {
    auto expr = parse_call_or_member();
    if (match(JsTokenType::Power)) {
        // Exponentiation is right-associative and accepts a unary expression
        // on the right (e.g. 2 ** -2).
        auto right = parse_unary();
        return std::make_unique<BinaryExpr>(std::move(expr), JsTokenType::Power, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_call_or_member() {
    auto expr = parse_primary();

    while (true) {
        if (match(JsTokenType::Dot)) {
            const auto& prop_token = consume(JsTokenType::Identifier, "Expected property name after '.'.");
            expr = std::make_unique<MemberExpr>(std::move(expr), prop_token.text);
        } else if (match(JsTokenType::OpenBracket)) {
            auto idx = parse_expression();
            consume(JsTokenType::CloseBracket, "Expected ']' after index expression.");
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(idx));
        } else if (match(JsTokenType::OpenParen)) {
            std::vector<std::unique_ptr<Expr>> args;
            if (!check(JsTokenType::CloseParen)) {
                do {
                    args.push_back(parse_expression());
                } while (match(JsTokenType::Comma));
            }
            consume(JsTokenType::CloseParen, "Expected ')' after call arguments.");
            expr = std::make_unique<CallExpr>(std::move(expr), std::move(args));
        } else if (match(JsTokenType::PlusPlus) || match(JsTokenType::MinusMinus)) {
            // Postfix: `i++` yields the value *before* the change (prefix=false).
            JsTokenType op = previous().type;
            if (!is_assignable(expr.get())) {
                throw std::runtime_error("Invalid increment/decrement target.");
            }
            expr = std::make_unique<UpdateExpr>(std::move(expr), op, /*prefix=*/false);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> JsParser::parse_primary() {
    if (match(JsTokenType::True)) return std::make_unique<LiteralExpr>(JsValue::make_boolean(true));
    if (match(JsTokenType::False)) return std::make_unique<LiteralExpr>(JsValue::make_boolean(false));
    if (match(JsTokenType::Null)) return std::make_unique<LiteralExpr>(JsValue::make_null());
    if (match(JsTokenType::Undefined)) return std::make_unique<LiteralExpr>(JsValue::make_undefined());

    if (match(JsTokenType::Super)) {
        if (match(JsTokenType::OpenParen)) {
            std::vector<std::unique_ptr<Expr>> args;
            if (!check(JsTokenType::CloseParen)) {
                do {
                    args.push_back(parse_expression());
                } while (match(JsTokenType::Comma));
            }
            consume(JsTokenType::CloseParen, "Expected ')' after super arguments.");
            return std::make_unique<SuperExpr>(std::move(args), "", true);
        } else if (match(JsTokenType::Dot)) {
            std::string method = consume(JsTokenType::Identifier, "Expected method name after super.").text;
            if (match(JsTokenType::OpenParen)) {
                std::vector<std::unique_ptr<Expr>> args;
                if (!check(JsTokenType::CloseParen)) {
                    do {
                        args.push_back(parse_expression());
                    } while (match(JsTokenType::Comma));
                }
                consume(JsTokenType::CloseParen, "Expected ')' after arguments.");
                return std::make_unique<SuperExpr>(std::move(args), method, true);
            }
            return std::make_unique<SuperExpr>(std::vector<std::unique_ptr<Expr>>{}, method, false);
        }
        throw std::runtime_error("Unexpected use of 'super'.");
    }

    if (match(JsTokenType::Async)) {
        if (match(JsTokenType::Function)) {
            std::string name;
            if (match(JsTokenType::Identifier)) {
                name = previous().text;
            }
            consume(JsTokenType::OpenParen, "Expected '(' after function.");
            std::vector<std::string> params;
            std::vector<std::shared_ptr<Expr>> defs;
            if (!check(JsTokenType::CloseParen)) {
                do {
                    params.push_back(consume(JsTokenType::Identifier, "Expected parameter name.").text);
                    std::shared_ptr<Expr> def_val = nullptr;
                    if (match(JsTokenType::Equal)) {
                        def_val = parse_expression();
                    }
                    defs.push_back(std::move(def_val));
                } while (match(JsTokenType::Comma));
            }
            consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
            consume(JsTokenType::OpenBrace, "Expected '{' before function body.");
            auto body = parse_block();
            return std::make_unique<FunctionExpr>(name, std::move(params), std::move(body), std::move(defs), true);
        }
        if (match(JsTokenType::Identifier)) {
            std::string id = previous().text;
            consume(JsTokenType::Arrow, "Expected '=>' after async identifier.");
            std::shared_ptr<BlockStmt> body;
            if (match(JsTokenType::OpenBrace)) {
                body = parse_block();
            } else {
                auto expr = parse_assignment();
                std::vector<std::unique_ptr<Stmt>> stmts;
                stmts.push_back(std::make_unique<ReturnStmt>(std::move(expr)));
                body = std::make_shared<BlockStmt>(std::move(stmts));
            }
            return std::make_unique<FunctionExpr>("", std::vector<std::string>{id}, std::move(body), std::vector<std::shared_ptr<Expr>>{}, true);
        }
        if (match(JsTokenType::OpenParen)) {
            std::vector<std::string> params;
            std::vector<std::shared_ptr<Expr>> defs;
            if (!check(JsTokenType::CloseParen)) {
                do {
                    params.push_back(consume(JsTokenType::Identifier, "Expected parameter name.").text);
                    std::shared_ptr<Expr> def_val = nullptr;
                    if (match(JsTokenType::Equal)) {
                        def_val = parse_expression();
                    }
                    defs.push_back(std::move(def_val));
                } while (match(JsTokenType::Comma));
            }
            consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
            consume(JsTokenType::Arrow, "Expected '=>' after async parameters.");
            std::shared_ptr<BlockStmt> body;
            if (match(JsTokenType::OpenBrace)) {
                body = parse_block();
            } else {
                auto expr = parse_assignment();
                std::vector<std::unique_ptr<Stmt>> stmts;
                stmts.push_back(std::make_unique<ReturnStmt>(std::move(expr)));
                body = std::make_shared<BlockStmt>(std::move(stmts));
            }
            return std::make_unique<FunctionExpr>("", std::move(params), std::move(body), std::move(defs), true);
        }
    }

    if (match(JsTokenType::Number)) {
        return std::make_unique<LiteralExpr>(JsValue::make_number(previous().number_value));
    }

    if (match(JsTokenType::String)) {
        return std::make_unique<LiteralExpr>(JsValue::make_string(previous().text));
    }

    if (match(JsTokenType::NoSubstitutionTemplate)) {
        std::vector<std::unique_ptr<Expr>> parts;
        parts.push_back(std::make_unique<LiteralExpr>(JsValue::make_string(previous().text)));
        return std::make_unique<TemplateLiteralExpr>(std::move(parts));
    }

    if (match(JsTokenType::TemplateHead)) {
        std::vector<std::unique_ptr<Expr>> parts;
        parts.push_back(std::make_unique<LiteralExpr>(JsValue::make_string(previous().text)));
        while (true) {
            auto expr = parse_expression();
            parts.push_back(std::move(expr));
            if (match(JsTokenType::TemplateMiddle)) {
                parts.push_back(std::make_unique<LiteralExpr>(JsValue::make_string(previous().text)));
            } else if (match(JsTokenType::TemplateTail)) {
                parts.push_back(std::make_unique<LiteralExpr>(JsValue::make_string(previous().text)));
                break;
            } else {
                throw std::runtime_error("Expected '}' or closing backtick in template literal.");
            }
        }
        return std::make_unique<TemplateLiteralExpr>(std::move(parts));
    }

    if (match(JsTokenType::Identifier)) {
        std::string id = previous().text;
        if (match(JsTokenType::Arrow)) {
            std::shared_ptr<BlockStmt> body;
            if (match(JsTokenType::OpenBrace)) {
                body = parse_block();
            } else {
                auto expr = parse_assignment();
                std::vector<std::unique_ptr<Stmt>> stmts;
                stmts.push_back(std::make_unique<ReturnStmt>(std::move(expr)));
                body = std::make_shared<BlockStmt>(std::move(stmts));
            }
            return std::make_unique<FunctionExpr>("", std::vector<std::string>{id}, std::move(body));
        }
        return std::make_unique<IdentifierExpr>(id);
    }

    if (match(JsTokenType::OpenParen)) {
        if (match(JsTokenType::CloseParen)) {
            consume(JsTokenType::Arrow, "Expected '=>' after empty parameter list.");
            std::shared_ptr<BlockStmt> body;
            if (match(JsTokenType::OpenBrace)) {
                body = parse_block();
            } else {
                auto expr = parse_assignment();
                std::vector<std::unique_ptr<Stmt>> stmts;
                stmts.push_back(std::make_unique<ReturnStmt>(std::move(expr)));
                body = std::make_shared<BlockStmt>(std::move(stmts));
            }
            return std::make_unique<FunctionExpr>("", std::vector<std::string>{}, std::move(body));
        }

        auto first_expr = parse_expression();
        std::shared_ptr<Expr> first_def = nullptr;
        std::string first_name;
        if (auto ident = dynamic_cast<IdentifierExpr*>(first_expr.get())) {
            first_name = ident->name;
        } else if (auto assign = dynamic_cast<AssignExpr*>(first_expr.get())) {
            if (auto ident_target = dynamic_cast<IdentifierExpr*>(assign->target.get())) {
                first_name = ident_target->name;
                first_def = std::move(assign->value);
            }
        }

        if (match(JsTokenType::Comma)) {
            std::vector<std::string> params;
            std::vector<std::shared_ptr<Expr>> defs;
            if (!first_name.empty()) {
                params.push_back(first_name);
                defs.push_back(first_def);
            } else {
                throw std::runtime_error("Expected identifier in arrow parameter list.");
            }
            do {
                std::string p = consume(JsTokenType::Identifier, "Expected parameter name in arrow function.").text;
                std::shared_ptr<Expr> d = nullptr;
                if (match(JsTokenType::Equal)) {
                    d = parse_expression();
                }
                params.push_back(p);
                defs.push_back(d);
            } while (match(JsTokenType::Comma));

            consume(JsTokenType::CloseParen, "Expected ')' after arrow parameters.");
            consume(JsTokenType::Arrow, "Expected '=>' after arrow parameters.");

            std::shared_ptr<BlockStmt> body;
            if (match(JsTokenType::OpenBrace)) {
                body = parse_block();
            } else {
                auto expr = parse_assignment();
                std::vector<std::unique_ptr<Stmt>> stmts;
                stmts.push_back(std::make_unique<ReturnStmt>(std::move(expr)));
                body = std::make_shared<BlockStmt>(std::move(stmts));
            }
            return std::make_unique<FunctionExpr>("", std::move(params), std::move(body), std::move(defs));
        }

        consume(JsTokenType::CloseParen, "Expected ')' after expression.");
        if (match(JsTokenType::Arrow)) {
            std::vector<std::string> params;
            std::vector<std::shared_ptr<Expr>> defs;
            if (!first_name.empty()) {
                params.push_back(first_name);
                defs.push_back(first_def);
            } else {
                throw std::runtime_error("Expected identifier in arrow parameter list.");
            }

            std::shared_ptr<BlockStmt> body;
            if (match(JsTokenType::OpenBrace)) {
                body = parse_block();
            } else {
                auto expr = parse_assignment();
                std::vector<std::unique_ptr<Stmt>> stmts;
                stmts.push_back(std::make_unique<ReturnStmt>(std::move(expr)));
                body = std::make_shared<BlockStmt>(std::move(stmts));
            }
            return std::make_unique<FunctionExpr>("", std::move(params), std::move(body), std::move(defs));
        }

        first_expr->explicitly_grouped = true;
        return first_expr;
    }

    if (match(JsTokenType::Yield)) {
        bool del = match(JsTokenType::Star);
        std::unique_ptr<Expr> arg = nullptr;
        if (!check(JsTokenType::Semicolon) && !check(JsTokenType::CloseParen) &&
            !check(JsTokenType::CloseBrace) && !check(JsTokenType::CloseBracket) &&
            !check(JsTokenType::Comma) && !is_at_end()) {
            arg = parse_expression();
        }
        return std::make_unique<YieldExpr>(std::move(arg), del);
    }

    if (match(JsTokenType::Function)) {
        bool is_gen = match(JsTokenType::Star);
        std::string name;
        if (match(JsTokenType::Identifier)) {
            name = previous().text;
        }
        consume(JsTokenType::OpenParen, "Expected '(' after function.");
        std::vector<std::string> params;
        std::vector<std::shared_ptr<Expr>> defs;
        if (!check(JsTokenType::CloseParen)) {
            do {
                params.push_back(consume(JsTokenType::Identifier, "Expected parameter name.").text);
                std::shared_ptr<Expr> def_val = nullptr;
                if (match(JsTokenType::Equal)) {
                    def_val = parse_expression();
                }
                defs.push_back(std::move(def_val));
            } while (match(JsTokenType::Comma));
        }
        consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
        consume(JsTokenType::OpenBrace, "Expected '{' before function body.");
        auto body = parse_block();
        return std::make_unique<FunctionExpr>(name, std::move(params), std::move(body), std::move(defs), false, is_gen);
    }

    if (match(JsTokenType::OpenBracket)) {
        std::vector<std::unique_ptr<Expr>> elements;
        if (!check(JsTokenType::CloseBracket)) {
            do {
                if (check(JsTokenType::CloseBracket)) break;
                elements.push_back(parse_expression());
            } while (match(JsTokenType::Comma));
        }
        consume(JsTokenType::CloseBracket, "Expected ']' after array elements.");
        return std::make_unique<ArrayLiteralExpr>(std::move(elements));
    }

    if (match(JsTokenType::OpenBrace)) {
        std::vector<std::pair<std::string, std::unique_ptr<Expr>>> entries;
        if (!check(JsTokenType::CloseBrace)) {
            do {
                if (check(JsTokenType::CloseBrace)) break;
                std::string key;
                if (match(JsTokenType::Identifier) || match(JsTokenType::String)) {
                    key = previous().text;
                } else {
                    throw std::runtime_error("Expected property name in object literal.");
                }
                consume(JsTokenType::Colon, "Expected ':' after property name.");
                auto val = parse_expression();
                entries.emplace_back(std::move(key), std::move(val));
            } while (match(JsTokenType::Comma));
        }
        consume(JsTokenType::CloseBrace, "Expected '}' after object literal.");
        return std::make_unique<ObjectLiteralExpr>(std::move(entries));
    }

    throw std::runtime_error("Unexpected token in JS expression: " + peek().text);
}

} // namespace litebird
