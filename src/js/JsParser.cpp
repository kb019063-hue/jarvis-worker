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
    if (match(JsTokenType::Var) || match(JsTokenType::Let) || match(JsTokenType::Const)) {
        return parse_var_declaration();
    }
    if (match(JsTokenType::If)) {
        return parse_if_statement();
    }
    if (match(JsTokenType::While)) {
        return parse_while_statement();
    }
    if (match(JsTokenType::Return)) {
        return parse_return_statement();
    }
    if (match(JsTokenType::Function)) {
        return parse_function_declaration();
    }
    if (match(JsTokenType::OpenBrace)) {
        return parse_block();
    }

    // Expression statement
    auto expr = parse_expression();
    match(JsTokenType::Semicolon);
    return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<Stmt> JsParser::parse_var_declaration() {
    const auto& name_token = consume(JsTokenType::Identifier, "Expected variable name.");
    std::unique_ptr<Expr> init = nullptr;

    if (match(JsTokenType::Equal)) {
        init = parse_expression();
    }
    match(JsTokenType::Semicolon);
    return std::make_unique<VarDeclStmt>(name_token.text, std::move(init));
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

std::unique_ptr<Stmt> JsParser::parse_while_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after while.");
    auto condition = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after while condition.");

    auto body = parse_statement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<Stmt> JsParser::parse_return_statement() {
    std::unique_ptr<Expr> val = nullptr;
    if (!check(JsTokenType::Semicolon)) {
        val = parse_expression();
    }
    match(JsTokenType::Semicolon);
    return std::make_unique<ReturnStmt>(std::move(val));
}

std::unique_ptr<FunctionDeclStmt> JsParser::parse_function_declaration() {
    const auto& name_token = consume(JsTokenType::Identifier, "Expected function name.");
    consume(JsTokenType::OpenParen, "Expected '(' after function name.");
    std::vector<std::string> params;
    if (!check(JsTokenType::CloseParen)) {
        do {
            params.push_back(consume(JsTokenType::Identifier, "Expected parameter name.").text);
        } while (match(JsTokenType::Comma));
    }
    consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
    consume(JsTokenType::OpenBrace, "Expected '{' before function body.");
    auto body = parse_block();
    return std::make_unique<FunctionDeclStmt>(name_token.text, std::move(params), std::move(body));
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

    if (match(JsTokenType::Equal)) {
        auto value = parse_assignment();
        return std::make_unique<AssignExpr>(std::move(expr), std::move(value));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_logical_or() {
    auto expr = parse_logical_and();
    while (match(JsTokenType::PipePipe)) {
        JsTokenType op = previous().type;
        auto right = parse_logical_and();
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
    while (match(JsTokenType::EqualEqual) || match(JsTokenType::NotEqual)) {
        JsTokenType op = previous().type;
        auto right = parse_comparison();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> JsParser::parse_comparison() {
    auto expr = parse_term();
    while (match(JsTokenType::Greater) || match(JsTokenType::GreaterEqual) ||
           match(JsTokenType::Less) || match(JsTokenType::LessEqual)) {
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
    if (match(JsTokenType::Bang) || match(JsTokenType::Minus)) {
        JsTokenType op = previous().type;
        auto right = parse_unary();
        return std::make_unique<UnaryExpr>(op, std::move(right));
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
    return parse_call_or_member();
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
        if (match(JsTokenType::Comma)) {
            std::vector<std::string> params;
            if (auto ident = dynamic_cast<IdentifierExpr*>(first_expr.get())) {
                params.push_back(ident->name);
            } else {
                throw std::runtime_error("Expected identifier in arrow parameter list.");
            }
            do {
                params.push_back(consume(JsTokenType::Identifier, "Expected parameter name in arrow function.").text);
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
            return std::make_unique<FunctionExpr>("", std::move(params), std::move(body));
        }

        consume(JsTokenType::CloseParen, "Expected ')' after expression.");
        if (match(JsTokenType::Arrow)) {
            std::vector<std::string> params;
            if (auto ident = dynamic_cast<IdentifierExpr*>(first_expr.get())) {
                params.push_back(ident->name);
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
            return std::make_unique<FunctionExpr>("", std::move(params), std::move(body));
        }

        return first_expr;
    }

    if (match(JsTokenType::Function)) {
        std::string name;
        if (match(JsTokenType::Identifier)) {
            name = previous().text;
        }
        consume(JsTokenType::OpenParen, "Expected '(' after function.");
        std::vector<std::string> params;
        if (!check(JsTokenType::CloseParen)) {
            do {
                params.push_back(consume(JsTokenType::Identifier, "Expected parameter name.").text);
            } while (match(JsTokenType::Comma));
        }
        consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
        consume(JsTokenType::OpenBrace, "Expected '{' before function body.");
        auto body = parse_block();
        return std::make_unique<FunctionExpr>(name, std::move(params), std::move(body));
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
