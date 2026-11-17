#include "litebird/js/JsParser.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/JsOps.hpp"
#include <algorithm>

namespace litebird {

// ---------------------------------------------------------------------------
// AST helpers
// ---------------------------------------------------------------------------

namespace {

struct PendingNode {
    enum class Kind : uint8_t { Expr, Stmt, Pattern };
    Kind kind;
    void* node;
};

/// Nodes whose deletion was requested while another node on this thread was
/// being deleted. Points into the stack of the outermost ast_delete().
thread_local std::vector<PendingNode>* t_pending_nodes = nullptr;

void delete_now(const PendingNode& pending) {
    switch (pending.kind) {
        case PendingNode::Kind::Expr: delete static_cast<Expr*>(pending.node); break;
        case PendingNode::Kind::Stmt: delete static_cast<Stmt*>(pending.node); break;
        case PendingNode::Kind::Pattern: delete static_cast<Pattern*>(pending.node); break;
    }
}

void delete_iteratively(PendingNode first) noexcept {
    if (!first.node) return;
    if (t_pending_nodes) {
        // Inside another node's destructor: do not go deeper, queue it.
        t_pending_nodes->push_back(first);
        return;
    }
    std::vector<PendingNode> pending;
    t_pending_nodes = &pending;
    delete_now(first);                      // its children land in `pending`
    while (!pending.empty()) {
        PendingNode next = pending.back();
        pending.pop_back();
        delete_now(next);
    }
    t_pending_nodes = nullptr;
}

} // namespace

void ast_delete(Expr* node) noexcept { delete_iteratively({PendingNode::Kind::Expr, node}); }
void ast_delete(Stmt* node) noexcept { delete_iteratively({PendingNode::Kind::Stmt, node}); }
void ast_delete(Pattern* node) noexcept { delete_iteratively({PendingNode::Kind::Pattern, node}); }

void Pattern::collect_names(std::vector<std::string>& out) const {
    js_check_stack();
    switch (kind) {
        case Kind::Identifier:
            out.push_back(name);
            break;
        case Kind::Array:
        case Kind::Object:
            for (const auto& element : elements) {
                if (element.target) element.target->collect_names(out);
            }
            break;
        case Kind::Target:
            break;
    }
}

std::string js_function_source_text(const AstNode* node) {
    if (!node) return std::string();
    const std::string* source = nullptr;
    uint32_t start = 0;
    uint32_t end = 0;
    if (node->kind == NodeKind::FunctionNode) {
        auto* fn = static_cast<const FunctionNode*>(node);
        source = fn->source.get();
        start = fn->source_start;
        end = fn->source_end;
    } else if (node->kind == NodeKind::ClassNode) {
        auto* cls = static_cast<const ClassNode*>(node);
        source = cls->source.get();
        start = cls->source_start;
        end = cls->source_end;
    }
    if (!source || start >= end || end > source->size()) return std::string();
    return source->substr(start, end - start);
}

std::vector<std::string> FunctionNode::simple_param_names() const {
    std::vector<std::string> names;
    names.reserve(params.size());
    for (const auto& p : params) {
        names.push_back(p.target && p.target->kind == Pattern::Kind::Identifier ? p.target->name : std::string());
    }
    return names;
}

// ---------------------------------------------------------------------------
// Token plumbing
// ---------------------------------------------------------------------------

JsParser::JsParser(std::vector<JsToken> tokens, std::shared_ptr<const std::string> source)
    : m_tokens(std::move(tokens)), m_source(std::move(source)) {
    if (m_tokens.empty() || m_tokens.back().type != JsTokenType::Eof) {
        JsToken eof;
        eof.type = JsTokenType::Eof;
        eof.line = m_tokens.empty() ? 1 : m_tokens.back().line;
        m_tokens.push_back(eof);
    }

    // Bracket matching, computed once, lets the parser tell `(a, b) => x` from
    // a parenthesised expression with a single look at the token after `)`.
    m_match.assign(m_tokens.size(), -1);
    std::vector<size_t> stack;
    for (size_t i = 0; i < m_tokens.size(); ++i) {
        switch (m_tokens[i].type) {
            case JsTokenType::OpenParen:
            case JsTokenType::OpenBracket:
            case JsTokenType::OpenBrace:
                stack.push_back(i);
                break;
            case JsTokenType::CloseParen:
            case JsTokenType::CloseBracket:
            case JsTokenType::CloseBrace:
                if (!stack.empty()) {
                    m_match[stack.back()] = static_cast<int>(i);
                    stack.pop_back();
                }
                break;
            default:
                break;
        }
    }
}

const JsToken& JsParser::peek(size_t offset) const {
    size_t index = m_current + offset;
    if (index >= m_tokens.size()) index = m_tokens.size() - 1;
    return m_tokens[index];
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
    return peek().type == type;
}

bool JsParser::check_identifier(const char* text) const {
    return peek().type == JsTokenType::Identifier && !peek().has_escape && peek().text == text;
}

bool JsParser::match(JsTokenType type) {
    if (check(type) && type != JsTokenType::Eof) {
        advance();
        return true;
    }
    return false;
}

bool JsParser::match_identifier(const char* text) {
    if (check_identifier(text)) {
        advance();
        return true;
    }
    return false;
}

void JsParser::fail(const std::string& message) const {
    throw JsSyntaxError(message, peek().line);
}

void JsParser::fail_unexpected() const {
    if (is_at_end()) fail("Unexpected end of input");
    const JsToken& t = peek();
    switch (t.type) {
        case JsTokenType::Number: fail("Unexpected number");
        case JsTokenType::String: fail("Unexpected string");
        case JsTokenType::Identifier: fail("Unexpected identifier '" + t.text + "'");
        case JsTokenType::EscapedKeyword: fail("Keyword must not contain escaped characters");
        case JsTokenType::NoSubstitutionTemplate:
        case JsTokenType::TemplateHead:
        case JsTokenType::TemplateMiddle:
        case JsTokenType::TemplateTail: fail("Unexpected template string");
        default: fail("Unexpected token '" + t.text + "'");
    }
}

const JsToken& JsParser::consume(JsTokenType type, const std::string& err_msg) {
    if (check(type)) return advance();
    if (is_at_end()) fail("Unexpected end of input: " + err_msg);
    fail(err_msg);
}

// Automatic semicolon insertion (ECMA-262 12.10): a statement may end without
// a semicolon before `}`, at the end of input, or at a line break.
void JsParser::consume_semicolon() {
    if (match(JsTokenType::Semicolon)) return;
    if (check(JsTokenType::CloseBrace) || is_at_end() || peek().newline_before) return;
    fail_unexpected();
}

static bool is_strict_reserved_word(const std::string& name) {
    return name == "implements" || name == "interface" || name == "let" ||
           name == "package" || name == "private" || name == "protected" ||
           name == "public" || name == "static" || name == "yield";
}

bool JsParser::is_strict() const {
    return !m_functions.empty() && m_functions.back().strict;
}

void JsParser::validate_identifier(const std::string& name, bool strict_ctx) const {
    if (strict_ctx && is_strict_reserved_word(name)) {
        fail("Unexpected strict mode reserved word");
    }
    if (!m_functions.empty()) {
        const auto& ctx = m_functions.back();
        if ((ctx.is_generator || ctx.forbid_yield) && name == "yield") {
            fail("Unexpected reserved word");
        }
        if ((ctx.is_async || ctx.forbid_await) && name == "await") {
            fail("Unexpected reserved word");
        }
    }
}

std::string JsParser::consume_binding_identifier(const char* what) {
    if (check(JsTokenType::Identifier) || check(JsTokenType::Yield) ||
        check(JsTokenType::Await) || check(JsTokenType::Let)) {
        std::string name = advance().text;
        validate_identifier(name, is_strict());
        return name;
    }
    if (is_at_end()) fail(std::string("Unexpected end of input: expected ") + what);
    fail(std::string("Unexpected token '") + peek().text + "': expected " + what);
}

bool JsParser::is_property_name_token() const {
    const JsToken& t = peek();
    return t.type == JsTokenType::Identifier || t.type == JsTokenType::String ||
           t.type == JsTokenType::Number || t.type == JsTokenType::OpenBracket ||
           t.type == JsTokenType::PrivateName || t.is_keyword;
}

namespace {

/// Parentheses, brackets and argument lists re-enable the `in` operator that a
/// `for (init; ...)` header switches off.
struct FlagScope {
    bool& flag;
    bool saved;
    FlagScope(bool& f, bool value) : flag(f), saved(f) { flag = value; }
    ~FlagScope() { flag = saved; }
};

} // namespace

// ---------------------------------------------------------------------------
// Program and statements
// ---------------------------------------------------------------------------

std::unique_ptr<Program> JsParser::parse_program() {
    auto program = std::make_unique<Program>();
    FunctionContext ctx;
    ctx.var_names = &program->var_names;
    m_functions.push_back(ctx);
    m_blocks.push_back(program.get());
    parse_statement_list(*program, false);
    program->is_strict = m_functions.back().strict;
    m_blocks.pop_back();
    m_functions.pop_back();
    return program;
}

void JsParser::parse_statement_list(BlockStmt& block, bool until_close_brace) {
    // Directive prologue: "use strict" must be known before the body is parsed,
    // because nested functions inherit it.
    for (size_t k = 0; peek(k).type == JsTokenType::String; ) {
        if (peek(k).text == "use strict") m_functions.back().strict = true;
        const JsToken& next = peek(k + 1);
        if (next.type == JsTokenType::Semicolon) k += 2;
        else if (next.newline_before) k += 1;
        else break;
    }

    while (!is_at_end() && !(until_close_brace && check(JsTokenType::CloseBrace))) {
        auto stmt = parse_declaration_or_statement();
        if (stmt) block.statements.push_back(std::move(stmt));
    }
}

// The parser descends once per level of nesting in the source. Each function
// that can reach itself again checks how much native stack is left first, so
// that `((((...` or ten thousand nested blocks end in a RangeError the way
// they do in other engines, not in a dead process.
std::unique_ptr<Stmt> JsParser::parse_declaration_or_statement() {
    js_check_stack();
    const int line = peek().line;
    std::unique_ptr<Stmt> result;
    switch (peek().type) {
        case JsTokenType::Var: {
            advance();
            auto decl = parse_var_declaration(JsDeclKind::Var, false);
            consume_semicolon();
            result = std::move(decl);
            break;
        }
        case JsTokenType::Let:
        case JsTokenType::Const: {
            JsDeclKind kind = advance().type == JsTokenType::Const ? JsDeclKind::Const : JsDeclKind::Let;
            auto decl = parse_var_declaration(kind, false);
            consume_semicolon();
            m_blocks.back()->has_lexical = true;
            result = std::move(decl);
            break;
        }
        case JsTokenType::Function: {
            advance();
            auto decl = std::make_unique<FunctionDeclStmt>(parse_function(false, true));
            m_blocks.back()->functions.push_back(decl.get());
            m_blocks.back()->has_lexical = true;
            result = std::move(decl);
            break;
        }
        case JsTokenType::Class: {
            advance();
            auto decl = std::make_unique<ClassDeclStmt>(parse_class(true));
            m_blocks.back()->has_lexical = true;
            result = std::move(decl);
            break;
        }
        case JsTokenType::Identifier: {
            if (check_identifier("async") && peek(1).type == JsTokenType::Function && !peek(1).newline_before) {
                advance();
                advance();
                auto decl = std::make_unique<FunctionDeclStmt>(parse_function(true, true));
                m_blocks.back()->functions.push_back(decl.get());
                m_blocks.back()->has_lexical = true;
                result = std::move(decl);
                break;
            }
            if (peek(1).type == JsTokenType::Colon) {
                std::string label = advance().text;
                validate_identifier(label, is_strict());
                advance();
                auto body = parse_declaration_or_statement();
                result = std::make_unique<LabeledStmt>(std::move(label), std::move(body));
                break;
            }
            result = parse_statement();
            break;
        }
        default:
            result = parse_statement();
            break;
    }
    if (result) result->line = line;
    return result;
}

std::unique_ptr<Stmt> JsParser::parse_statement() {
    js_check_stack();
    switch (peek().type) {
        case JsTokenType::OpenBrace:
            advance();
            return parse_block();
        case JsTokenType::Semicolon:
            advance();
            return std::make_unique<EmptyStmt>();
        case JsTokenType::If:
            advance();
            return parse_if_statement();
        case JsTokenType::While:
            advance();
            return parse_while_statement();
        case JsTokenType::Do:
            advance();
            return parse_do_while_statement();
        case JsTokenType::For:
            advance();
            return parse_for_statement();
        case JsTokenType::Switch:
            advance();
            return parse_switch_statement();
        case JsTokenType::Try:
            advance();
            return parse_try_statement();
        case JsTokenType::Return:
            advance();
            return parse_return_statement();
        case JsTokenType::Break:
        case JsTokenType::Continue: {
            bool is_break = advance().type == JsTokenType::Break;
            std::string label;
            if ((check(JsTokenType::Identifier) || check(JsTokenType::Yield) ||
                 check(JsTokenType::Await) || check(JsTokenType::Let)) && !peek().newline_before) {
                label = advance().text;
                validate_identifier(label, is_strict());
            }
            consume_semicolon();
            if (is_break) return std::make_unique<BreakStmt>(std::move(label));
            return std::make_unique<ContinueStmt>(std::move(label));
        }
        case JsTokenType::Throw: {
            advance();
            if (peek().newline_before) fail("Illegal newline after throw");
            auto argument = parse_expression();
            consume_semicolon();
            return std::make_unique<ThrowStmt>(std::move(argument));
        }
        case JsTokenType::Debugger:
            advance();
            consume_semicolon();
            return std::make_unique<EmptyStmt>();
        case JsTokenType::Import:
        case JsTokenType::Export:
            fail("import and export are only valid in modules, which are not supported");
        case JsTokenType::With:
            fail("'with' statements are not supported");
        default:
            break;
    }

    auto expr = parse_expression();
    consume_semicolon();
    return std::make_unique<ExprStmt>(std::move(expr));
}

void JsParser::declare_var_names(const Pattern& pattern) {
    std::vector<std::string> names;
    pattern.collect_names(names);
    auto* target = m_functions.back().var_names;
    if (!target) return;
    for (auto& name : names) {
        if (std::find(target->begin(), target->end(), name) == target->end()) target->push_back(std::move(name));
    }
}

std::unique_ptr<VarDeclStmt> JsParser::parse_var_declaration(JsDeclKind kind, bool no_in) {
    auto decl = std::make_unique<VarDeclStmt>(kind);
    do {
        VarDeclarator d;
        d.target = parse_binding_pattern();
        if (match(JsTokenType::Equal)) {
            FlagScope scope(m_no_in, no_in);
            d.init = parse_assignment();
        } else if (d.target->kind != Pattern::Kind::Identifier && !no_in) {
            fail("Missing initializer in destructuring declaration");
        } else if (kind == JsDeclKind::Const && !no_in) {
            fail("Missing initializer in const declaration");
        }
        if (kind == JsDeclKind::Var) {
            declare_var_names(*d.target);
        } else {
            std::vector<std::string> names;
            d.target->collect_names(names);
            for (const auto& name : names) {
                if (name == "let") fail("let is disallowed as a lexically bound name");
            }
        }
        decl->decls.push_back(std::move(d));
    } while (match(JsTokenType::Comma));
    return decl;
}

std::unique_ptr<Stmt> JsParser::parse_if_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after if.");
    auto condition = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after if condition.");

    auto then_branch = parse_declaration_or_statement();
    std::unique_ptr<Stmt> else_branch = nullptr;
    if (match(JsTokenType::Else)) {
        else_branch = parse_declaration_or_statement();
    }
    return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<Stmt> JsParser::parse_switch_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after switch.");
    auto discriminant = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after switch expression.");
    consume(JsTokenType::OpenBrace, "Expected '{' before switch body.");

    // Declarations inside the cases share one scope; a scratch block collects
    // the function declarations to hoist.
    BlockStmt scratch;
    m_blocks.push_back(&scratch);

    std::vector<SwitchCase> cases;
    bool saw_default = false;
    while (!check(JsTokenType::CloseBrace) && !is_at_end()) {
        SwitchCase clause;
        if (match(JsTokenType::Case)) {
            clause.test = parse_expression();
            consume(JsTokenType::Colon, "Expected ':' after case expression.");
        } else if (match(JsTokenType::Default)) {
            if (saw_default) fail("More than one default clause in switch statement");
            saw_default = true;
            consume(JsTokenType::Colon, "Expected ':' after default.");
        } else {
            fail_unexpected();
        }

        while (!check(JsTokenType::Case) && !check(JsTokenType::Default) &&
               !check(JsTokenType::CloseBrace) && !is_at_end()) {
            clause.statements.push_back(parse_declaration_or_statement());
        }
        cases.push_back(std::move(clause));
    }
    m_blocks.pop_back();
    consume(JsTokenType::CloseBrace, "Expected '}' after switch body.");
    auto stmt = std::make_unique<SwitchStmt>(std::move(discriminant), std::move(cases));
    stmt->functions = std::move(scratch.functions);
    return stmt;
}

std::unique_ptr<Stmt> JsParser::parse_while_statement() {
    consume(JsTokenType::OpenParen, "Expected '(' after while.");
    auto condition = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after while condition.");
    auto body = parse_declaration_or_statement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<Stmt> JsParser::parse_do_while_statement() {
    auto body = parse_declaration_or_statement();
    consume(JsTokenType::While, "Expected 'while' after do body.");
    consume(JsTokenType::OpenParen, "Expected '(' after while.");
    auto condition = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after do-while condition.");
    match(JsTokenType::Semicolon);     // always optional here
    return std::make_unique<DoWhileStmt>(std::move(body), std::move(condition));
}

std::unique_ptr<Stmt> JsParser::parse_for_statement() {
    bool is_await = match(JsTokenType::Await);
    if (is_await && !m_functions.empty() && m_functions.back().fn) m_functions.back().fn->uses_await = true;
    consume(JsTokenType::OpenParen, "Expected '(' after 'for'.");

    const int closures_before = m_closure_count;
    std::unique_ptr<Stmt> init;
    std::unique_ptr<VarDeclStmt> decl;
    std::unique_ptr<Expr> init_expr;

    auto finish_for_each = [&](std::unique_ptr<Pattern> target, bool declares, JsDeclKind kind, bool is_of) {
        auto stmt = std::make_unique<ForEachStmt>();
        stmt->is_of = is_of;
        stmt->is_await = is_await;
        stmt->declares = declares;
        stmt->decl_kind = kind;
        stmt->target = std::move(target);
        // `for (x of a, b)` is not allowed; `for (x in a, b)` is.
        stmt->iterable = is_of ? parse_assignment() : parse_expression();
        consume(JsTokenType::CloseParen, "Expected ')' after for-in/of clause.");
        stmt->body = parse_declaration_or_statement();
        return stmt;
    };

    if (check(JsTokenType::Var) || check(JsTokenType::Let) || check(JsTokenType::Const)) {
        JsTokenType token = advance().type;
        JsDeclKind kind = token == JsTokenType::Var ? JsDeclKind::Var
                        : token == JsTokenType::Const ? JsDeclKind::Const : JsDeclKind::Let;
        decl = parse_var_declaration(kind, /*no_in=*/true);
        if (decl->decls.size() == 1 && !decl->decls[0].init &&
            (check(JsTokenType::In) || check_identifier("of"))) {
            bool is_of = advance().type != JsTokenType::In;
            return finish_for_each(std::move(decl->decls[0].target), true, kind, is_of);
        }
        for (const auto& d : decl->decls) {
            if (!d.init && (kind == JsDeclKind::Const || d.target->kind != Pattern::Kind::Identifier)) {
                fail("Missing initializer in for-loop declaration");
            }
        }
        init = std::move(decl);
    } else if (!check(JsTokenType::Semicolon)) {
        {
            FlagScope scope(m_no_in, true);
            init_expr = parse_expression();
        }
        if (check(JsTokenType::In) || check_identifier("of")) {
            bool is_of = advance().type != JsTokenType::In;
            return finish_for_each(expr_to_pattern(std::move(init_expr)), false, JsDeclKind::Let, is_of);
        }
        init = std::make_unique<ExprStmt>(std::move(init_expr));
    }

    auto stmt = std::make_unique<ForStmt>();
    consume(JsTokenType::Semicolon, "Expected ';' after for-loop initializer.");
    if (!check(JsTokenType::Semicolon)) stmt->test = parse_expression();
    consume(JsTokenType::Semicolon, "Expected ';' after for-loop condition.");
    if (!check(JsTokenType::CloseParen)) stmt->update = parse_expression();
    consume(JsTokenType::CloseParen, "Expected ')' after for-loop clauses.");
    stmt->body = parse_declaration_or_statement();

    if (init && init->kind == NodeKind::VarDecl) {
        auto* var_decl = static_cast<VarDeclStmt*>(init.get());
        if (var_decl->kind != JsDeclKind::Var) {
            stmt->has_lexical_init = true;
            // A fresh binding per iteration only matters if a closure created
            // inside the loop can capture it.
            if (m_closure_count != closures_before) {
                for (const auto& d : var_decl->decls) d.target->collect_names(stmt->per_iteration_names);
            }
        }
    }
    stmt->init = std::move(init);
    return stmt;
}

std::unique_ptr<Stmt> JsParser::parse_try_statement() {
    auto stmt = std::make_unique<TryStmt>();
    consume(JsTokenType::OpenBrace, "Expected '{' after try.");
    stmt->block = parse_block();
    if (match(JsTokenType::Catch)) {
        stmt->has_handler = true;
        if (match(JsTokenType::OpenParen)) {
            stmt->param = parse_binding_pattern();
            consume(JsTokenType::CloseParen, "Expected ')' after catch parameter.");
        }
        consume(JsTokenType::OpenBrace, "Expected '{' after catch.");
        stmt->handler = parse_block();
    }
    if (match(JsTokenType::Finally)) {
        consume(JsTokenType::OpenBrace, "Expected '{' after finally.");
        stmt->finalizer = parse_block();
    }
    if (!stmt->has_handler && !stmt->finalizer) fail("Missing catch or finally after try");
    return stmt;
}

std::unique_ptr<Stmt> JsParser::parse_return_statement() {
    std::unique_ptr<Expr> value;
    // A line break after `return` ends the statement (restricted production).
    if (!check(JsTokenType::Semicolon) && !check(JsTokenType::CloseBrace) && !is_at_end() &&
        !peek().newline_before) {
        value = parse_expression();
    }
    consume_semicolon();
    return std::make_unique<ReturnStmt>(std::move(value));
}

// Called with the opening brace already consumed.
std::unique_ptr<BlockStmt> JsParser::parse_block() {
    auto block = std::make_unique<BlockStmt>();
    m_blocks.push_back(block.get());
    while (!check(JsTokenType::CloseBrace) && !is_at_end()) {
        auto stmt = parse_declaration_or_statement();
        if (stmt) block->statements.push_back(std::move(stmt));
    }
    m_blocks.pop_back();
    consume(JsTokenType::CloseBrace, "Expected '}' after block.");
    return block;
}

// ---------------------------------------------------------------------------
// Functions and classes
// ---------------------------------------------------------------------------

void JsParser::finish_params(FunctionNode& fn) {
    fn.simple_params = true;
    fn.length = 0;
    bool counting = true;
    for (const auto& p : fn.params) {
        if (p.rest || p.default_value || !p.target || p.target->kind != Pattern::Kind::Identifier) {
            fn.simple_params = false;
        }
        if (p.rest || p.default_value) counting = false;
        if (counting) fn.length++;
    }
}

void JsParser::parse_params(FunctionNode& fn) {
    consume(JsTokenType::OpenParen, "Expected '(' before parameters.");
    FlagScope scope(m_no_in, false);
    while (!check(JsTokenType::CloseParen)) {
        FunctionParam param;
        if (match(JsTokenType::Ellipsis)) {
            param.rest = true;
            param.target = parse_binding_pattern();
        } else {
            param.target = parse_binding_pattern();
            if (match(JsTokenType::Equal)) param.default_value = parse_assignment();
        }
        bool was_rest = param.rest;
        fn.params.push_back(std::move(param));
        if (was_rest) break;
        if (!match(JsTokenType::Comma)) break;
    }
    consume(JsTokenType::CloseParen, "Expected ')' after parameters.");
    finish_params(fn);
}

void JsParser::parse_function_body(FunctionNode& fn) {
    consume(JsTokenType::OpenBrace, "Expected '{' before function body.");
    auto body = std::make_shared<BlockStmt>();
    FlagScope scope(m_no_in, false);
    m_blocks.push_back(body.get());
    parse_statement_list(*body, true);
    m_blocks.pop_back();
    consume(JsTokenType::CloseBrace, "Expected '}' after function body.");
    fn.body = std::move(body);
    fn.is_strict = m_functions.back().strict;
}

void JsParser::parse_params_and_body(FunctionNode& fn) {
    FunctionContext ctx;
    ctx.fn = &fn;
    ctx.var_names = &fn.var_names;
    ctx.is_arrow = fn.is_arrow;
    ctx.is_async = fn.is_async;
    ctx.is_generator = fn.is_generator;
    ctx.strict = m_functions.empty() ? false : m_functions.back().strict;
    m_functions.push_back(ctx);
    parse_params(fn);
    parse_function_body(fn);
    m_functions.pop_back();
    m_closure_count++;
}

// Called with `function` already consumed.
std::shared_ptr<FunctionNode> JsParser::parse_function(bool is_async, bool is_declaration) {
    auto fn = std::make_shared<FunctionNode>();
    fn->line = previous().line;
    // Called with `function` consumed, and `async` before it if there was one.
    const size_t first_token = m_current - (is_async && m_current >= 2 ? 2 : 1);
    fn->is_async = is_async;
    fn->is_generator = match(JsTokenType::Star);
    if (check(JsTokenType::Identifier) || check(JsTokenType::Yield) || check(JsTokenType::Await) ||
        check(JsTokenType::Let)) {
        fn->name = advance().text;
        if (is_declaration) {
            validate_identifier(fn->name, is_strict());
        } else {
            if (is_strict() && is_strict_reserved_word(fn->name)) {
                fail("Unexpected strict mode reserved word");
            }
            if (fn->is_generator && fn->name == "yield") fail("Unexpected reserved word");
            if (fn->is_async && fn->name == "await") fail("Unexpected reserved word");
        }
    } else if (is_declaration) {
        fail("Function statements require a function name");
    }
    parse_params_and_body(*fn);
    set_source(*fn, first_token);
    return fn;
}

std::shared_ptr<FunctionNode> JsParser::parse_method(bool is_async, bool is_generator) {
    auto fn = std::make_shared<FunctionNode>();
    fn->line = peek().line;
    fn->is_async = is_async;
    fn->is_generator = is_generator;
    fn->is_method = true;
    parse_params_and_body(*fn);
    return fn;
}

std::shared_ptr<FunctionNode> JsParser::parse_arrow_function(bool is_async) {
    auto fn = std::make_shared<FunctionNode>();
    fn->line = peek().line;
    const size_t first_token = m_current - (is_async && m_current >= 1 ? 1 : 0);
    fn->is_arrow = true;
    fn->is_async = is_async;

    FunctionContext ctx;
    ctx.fn = fn.get();
    ctx.var_names = &fn->var_names;
    ctx.is_arrow = true;
    ctx.is_async = is_async;
    ctx.strict = m_functions.empty() ? false : m_functions.back().strict;
    ctx.forbid_arguments = m_functions.empty() ? false : m_functions.back().forbid_arguments;
    // ArrowParameters inherit [?Yield, ?Await] from the enclosing scope (ECMA-262 §15.3).
    ctx.forbid_await = is_async || (!m_functions.empty() && (m_functions.back().is_async || m_functions.back().forbid_await));
    ctx.forbid_yield = !m_functions.empty() && (m_functions.back().is_generator || m_functions.back().forbid_yield);
    m_functions.push_back(ctx);

    if (check(JsTokenType::OpenParen)) {
        parse_params(*fn);
    } else {
        FunctionParam param;
        param.target = std::make_unique<Pattern>();
        param.target->name = consume_binding_identifier("an arrow function parameter");
        fn->params.push_back(std::move(param));
        finish_params(*fn);
    }
    consume(JsTokenType::Arrow, "Expected '=>' after arrow function parameters.");
    // ConciseBody resets [~Yield] and uses only the arrow's own [Await].
    m_functions.back().forbid_await = is_async;
    m_functions.back().forbid_yield = false;

    if (check(JsTokenType::OpenBrace)) {
        parse_function_body(*fn);
    } else {
        // Expression body: `x => x * 2` returns the expression.
        FlagScope scope(m_no_in, m_no_in);
        auto expr = parse_assignment();
        auto body = std::make_shared<BlockStmt>();
        body->statements.push_back(std::make_unique<ReturnStmt>(std::move(expr)));
        fn->body = std::move(body);
        fn->is_strict = m_functions.back().strict;
    }
    m_functions.pop_back();
    m_closure_count++;
    set_source(*fn, first_token);
    return fn;
}

std::string JsParser::parse_property_name(std::unique_ptr<Expr>& computed) {
    const JsToken& t = peek();
    if (t.type == JsTokenType::OpenBracket) {
        advance();
        FlagScope scope(m_no_in, false);
        computed = parse_assignment();
        consume(JsTokenType::CloseBracket, "Expected ']' after computed property name.");
        return std::string();
    }
    if (t.type == JsTokenType::Number) {
        double value = advance().number_value;
        return js_number_to_string(value);
    }
    if (t.type == JsTokenType::String || t.type == JsTokenType::Identifier ||
        t.type == JsTokenType::PrivateName || t.is_keyword) {
        return advance().text;
    }
    fail_unexpected();
}

// Called with `class` already consumed.
std::shared_ptr<ClassNode> JsParser::parse_class(bool is_declaration) {
    auto cls = std::make_shared<ClassNode>();
    cls->line = previous().line;
    const size_t class_token = m_current - 1;       // called with `class` consumed

    // All parts of a ClassDeclaration or ClassExpression are strict mode code.
    // The context is addressed by index because parsing the members pushes onto m_functions.
    const size_t enclosing = m_functions.size() - 1;
    const bool enclosing_strict = m_functions[enclosing].strict;
    m_functions[enclosing].strict = true;

    if (check(JsTokenType::Identifier) || check(JsTokenType::Yield) ||
        check(JsTokenType::Await) || check(JsTokenType::Let)) {
        cls->name = advance().text;
        validate_identifier(cls->name, true);
    } else if (is_declaration) {
        fail("Class declarations require a class name");
    }
    if (match(JsTokenType::Extends)) {
        cls->super_class = parse_call_or_member();
    }
    consume(JsTokenType::OpenBrace, "Expected '{' before class body.");

    FlagScope in_scope(m_no_in, false);

    auto can_follow_modifier = [&]() {
        const JsToken& next = peek(1);
        return next.type != JsTokenType::OpenParen && next.type != JsTokenType::Equal &&
               next.type != JsTokenType::Semicolon && next.type != JsTokenType::CloseBrace;
    };

    while (!check(JsTokenType::CloseBrace) && !is_at_end()) {
        if (match(JsTokenType::Semicolon)) continue;

        ClassMember member;
        if (check_identifier("static") && can_follow_modifier()) {
            advance();
            member.is_static = true;
            if (check(JsTokenType::OpenBrace)) {
                // static { ... } initialisation block
                auto fn = std::make_shared<FunctionNode>();
                fn->is_method = true;
                FunctionContext ctx;
                ctx.fn = fn.get();
                ctx.var_names = &fn->var_names;
                ctx.strict = true;
                ctx.forbid_arguments = true;
                ctx.forbid_await = true;
                m_functions.push_back(ctx);
                parse_function_body(*fn);
                m_functions.pop_back();
                m_closure_count++;
                member.kind = ClassMember::Kind::StaticBlock;
                member.fn = std::move(fn);
                cls->members.push_back(std::move(member));
                continue;
            }
        }

        bool is_async = false;
        bool is_generator = false;
        const size_t member_token = m_current;       // after `static`, as engines print it
        if (check_identifier("async") && can_follow_modifier() && !peek(1).newline_before) {
            advance();
            is_async = true;
        }
        if (match(JsTokenType::Star)) is_generator = true;
        if ((check_identifier("get") || check_identifier("set")) && can_follow_modifier()) {
            member.kind = advance().text == "get" ? ClassMember::Kind::Getter : ClassMember::Kind::Setter;
        }

        std::unique_ptr<Expr> computed;
        member.key = parse_property_name(computed);
        member.computed_key = std::move(computed);

        if (check(JsTokenType::OpenParen)) {
            auto fn = parse_method(is_async, is_generator);
            set_source(*fn, member_token);
            fn->name = member.key;
            if (member.kind == ClassMember::Kind::Method && !member.is_static &&
                !member.computed_key && member.key == "constructor") {
                fn->is_method = false;
                fn->is_class_constructor = true;
                fn->is_derived_constructor = static_cast<bool>(cls->super_class);
                fn->name = cls->name;
                if (cls->constructor) fail("A class may only have one constructor");
                cls->constructor = std::move(fn);
                continue;
            }
            member.fn = std::move(fn);
        } else {
            if (member.kind != ClassMember::Kind::Method || is_async || is_generator) fail_unexpected();
            member.kind = ClassMember::Kind::Field;
            if (match(JsTokenType::Equal)) {
                // The initialiser runs later with `this` bound to the instance;
                // treat it as its own function scope so `arguments` and `var`
                // bookkeeping do not leak into the enclosing function.
                FunctionNode scratch;
                FunctionContext ctx;
                ctx.fn = &scratch;
                ctx.var_names = &scratch.var_names;
                ctx.strict = true;
                ctx.forbid_arguments = true;
                m_functions.push_back(ctx);
                member.value = parse_assignment();
                m_functions.pop_back();
                m_closure_count++;
            }
            consume_semicolon();
        }
        cls->members.push_back(std::move(member));
    }
    consume(JsTokenType::CloseBrace, "Expected '}' after class body.");
    set_source(*cls, class_token);
    m_functions[enclosing].strict = enclosing_strict;

    if (!cls->constructor) {
        // Default constructor: `constructor(...args) { super(...args); }` for a
        // derived class, an empty one otherwise.
        auto fn = std::make_shared<FunctionNode>();
        fn->name = cls->name;
        fn->is_class_constructor = true;
        fn->is_strict = true;
        fn->body = std::make_shared<BlockStmt>();
        if (cls->super_class) {
            fn->is_derived_constructor = true;
            FunctionParam rest;
            rest.rest = true;
            rest.target = std::make_unique<Pattern>();
            rest.target->name = "args";
            fn->params.push_back(std::move(rest));
            std::vector<std::unique_ptr<Expr>> args;
            args.push_back(std::make_unique<SpreadExpr>(std::make_unique<IdentifierExpr>("args")));
            fn->body->statements.push_back(
                std::make_unique<ExprStmt>(std::make_unique<SuperCallExpr>(std::move(args))));
        }
        finish_params(*fn);
        cls->constructor = std::move(fn);
    }
    m_closure_count++;
    return cls;
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

std::unique_ptr<Pattern> JsParser::parse_binding_pattern() {
    js_check_stack();
    auto pattern = std::make_unique<Pattern>();
    FlagScope scope(m_no_in, false);
    if (match(JsTokenType::OpenBracket)) {
        pattern->kind = Pattern::Kind::Array;
        while (!check(JsTokenType::CloseBracket)) {
            PatternElement element;
            if (check(JsTokenType::Comma)) {
                advance();
                pattern->elements.push_back(std::move(element));   // hole
                continue;
            }
            if (match(JsTokenType::Ellipsis)) {
                element.rest = true;
                element.target = parse_binding_pattern();
            } else {
                element.target = parse_binding_pattern();
                if (match(JsTokenType::Equal)) element.default_value = parse_assignment();
            }
            pattern->elements.push_back(std::move(element));
            if (!check(JsTokenType::CloseBracket)) consume(JsTokenType::Comma, "Expected ',' in array pattern.");
        }
        consume(JsTokenType::CloseBracket, "Expected ']' after array pattern.");
        return pattern;
    }
    if (match(JsTokenType::OpenBrace)) {
        pattern->kind = Pattern::Kind::Object;
        while (!check(JsTokenType::CloseBrace)) {
            PatternElement element;
            if (match(JsTokenType::Ellipsis)) {
                element.rest = true;
                element.target = std::make_unique<Pattern>();
                element.target->name = consume_binding_identifier("a rest property name");
            } else {
                bool key_is_identifier = check(JsTokenType::Identifier) || check(JsTokenType::Yield) ||
                                         check(JsTokenType::Await) || check(JsTokenType::Let);
                std::unique_ptr<Expr> computed;
                element.key = parse_property_name(computed);
                element.computed_key = std::move(computed);
                if (match(JsTokenType::Colon)) {
                    element.target = parse_binding_pattern();
                } else {
                    if (!key_is_identifier || element.computed_key) fail_unexpected();
                    validate_identifier(element.key, is_strict());
                    element.target = std::make_unique<Pattern>();
                    element.target->name = element.key;
                }
                if (match(JsTokenType::Equal)) element.default_value = parse_assignment();
            }
            pattern->elements.push_back(std::move(element));
            if (!check(JsTokenType::CloseBrace)) consume(JsTokenType::Comma, "Expected ',' in object pattern.");
        }
        consume(JsTokenType::CloseBrace, "Expected '}' after object pattern.");
        return pattern;
    }
    pattern->name = consume_binding_identifier("a variable name");
    return pattern;
}

// Destructuring assignment is parsed as an array/object literal first and
// reinterpreted as a pattern once the `=` is seen (the "cover grammar").
std::unique_ptr<Pattern> JsParser::expr_to_pattern(std::unique_ptr<Expr> expr) {
    js_check_stack();
    auto pattern = std::make_unique<Pattern>();
    switch (expr->kind) {
        case NodeKind::Identifier:
            pattern->name = static_cast<IdentifierExpr*>(expr.get())->name;
            return pattern;
        case NodeKind::Member:
        case NodeKind::Index:
            pattern->kind = Pattern::Kind::Target;
            pattern->target = std::move(expr);
            return pattern;
        case NodeKind::Assign: {
            // Only reachable for a nested pattern that already carries its own
            // default; handled by the callers below.
            break;
        }
        case NodeKind::ArrayLiteral: {
            pattern->kind = Pattern::Kind::Array;
            auto* array = static_cast<ArrayLiteralExpr*>(expr.get());
            for (auto& item : array->elements) {
                PatternElement element;
                if (!item) {
                    pattern->elements.push_back(std::move(element));
                    continue;
                }
                if (item->kind == NodeKind::Spread) {
                    element.rest = true;
                    element.target = expr_to_pattern(std::move(static_cast<SpreadExpr*>(item.get())->argument));
                } else if (item->kind == NodeKind::Assign &&
                           static_cast<AssignExpr*>(item.get())->op == JsTokenType::Equal) {
                    auto* assign = static_cast<AssignExpr*>(item.get());
                    element.target = assign->pattern ? std::move(assign->pattern)
                                                     : expr_to_pattern(std::move(assign->target));
                    element.default_value = std::move(assign->value);
                } else {
                    element.target = expr_to_pattern(std::move(item));
                }
                pattern->elements.push_back(std::move(element));
            }
            return pattern;
        }
        case NodeKind::ObjectLiteral: {
            pattern->kind = Pattern::Kind::Object;
            auto* object = static_cast<ObjectLiteralExpr*>(expr.get());
            for (auto& prop : object->properties) {
                PatternElement element;
                if (prop.kind == ObjectProperty::Kind::Spread) {
                    element.rest = true;
                    element.target = expr_to_pattern(std::move(prop.value));
                    pattern->elements.push_back(std::move(element));
                    continue;
                }
                if (prop.kind != ObjectProperty::Kind::Init || prop.is_method) {
                    fail("Invalid destructuring assignment target");
                }
                element.key = prop.key;
                element.computed_key = std::move(prop.computed_key);
                if (prop.value->kind == NodeKind::Assign &&
                    static_cast<AssignExpr*>(prop.value.get())->op == JsTokenType::Equal) {
                    auto* assign = static_cast<AssignExpr*>(prop.value.get());
                    element.target = assign->pattern ? std::move(assign->pattern)
                                                     : expr_to_pattern(std::move(assign->target));
                    element.default_value = std::move(assign->value);
                } else {
                    element.target = expr_to_pattern(std::move(prop.value));
                }
                pattern->elements.push_back(std::move(element));
            }
            return pattern;
        }
        default:
            break;
    }
    fail("Invalid destructuring assignment target");
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> JsParser::parse_expression() {
    auto first = parse_assignment();
    if (!check(JsTokenType::Comma)) return first;
    std::vector<std::unique_ptr<Expr>> list;
    list.push_back(std::move(first));
    while (match(JsTokenType::Comma)) list.push_back(parse_assignment());
    return std::make_unique<SequenceExpr>(std::move(list));
}

// `arguments`, `super` and `new.target` belong to the nearest function that is
// not an arrow function.
FunctionNode* JsParser::nearest_non_arrow_function() {
    for (auto it = m_functions.rbegin(); it != m_functions.rend(); ++it) {
        if (!it->is_arrow) return it->fn;
    }
    return nullptr;
}

bool JsParser::is_arrow_ahead() const {
    if (!check(JsTokenType::OpenParen)) return false;
    int close = m_match[m_current];
    return close >= 0 && static_cast<size_t>(close) + 1 < m_tokens.size() &&
           m_tokens[static_cast<size_t>(close) + 1].type == JsTokenType::Arrow;
}

// Valid simple assignment targets are identifiers, `a.b` and `a[b]`.
bool JsParser::is_assignable(const Expr* expr) {
    return expr && (expr->kind == NodeKind::Identifier || expr->kind == NodeKind::Member ||
                    expr->kind == NodeKind::Index || expr->kind == NodeKind::SuperMember);
}

std::unique_ptr<Expr> JsParser::parse_assignment() {
    js_check_stack();
    const int line = peek().line;

    // Arrow functions.
    if (check(JsTokenType::Identifier)) {
        if (peek(1).type == JsTokenType::Arrow && !peek(1).newline_before) {
            auto fn = parse_arrow_function(false);
            return std::make_unique<FunctionExpr>(std::move(fn));
        }
        if (check_identifier("async") && !peek(1).newline_before) {
            if ((peek(1).type == JsTokenType::Identifier || peek(1).type == JsTokenType::Yield ||
                 peek(1).type == JsTokenType::Await || peek(1).type == JsTokenType::Let) &&
                peek(2).type == JsTokenType::Arrow && !peek(2).newline_before) {
                advance();
                return std::make_unique<FunctionExpr>(parse_arrow_function(true));
            }
            if (peek(1).type == JsTokenType::OpenParen) {
                int close = m_match[m_current + 1];
                if (close >= 0 && m_tokens[static_cast<size_t>(close) + 1].type == JsTokenType::Arrow &&
                    !m_tokens[static_cast<size_t>(close) + 1].newline_before) {
                    advance();
                    return std::make_unique<FunctionExpr>(parse_arrow_function(true));
                }
            }
        }
    }
    if (is_arrow_ahead()) {
        return std::make_unique<FunctionExpr>(parse_arrow_function(false));
    }

    if (check(JsTokenType::Yield) && !m_functions.empty() && m_functions.back().is_generator) {
        advance();
        bool delegate = !peek().newline_before && match(JsTokenType::Star);
        std::unique_ptr<Expr> argument;
        switch (peek().type) {
            case JsTokenType::Semicolon:
            case JsTokenType::CloseParen:
            case JsTokenType::CloseBrace:
            case JsTokenType::CloseBracket:
            case JsTokenType::Comma:
            case JsTokenType::Colon:
            case JsTokenType::Eof:
            case JsTokenType::TemplateMiddle:
            case JsTokenType::TemplateTail:
                break;
            default:
                if (!peek().newline_before || delegate) argument = parse_assignment();
                break;
        }
        return std::make_unique<YieldExpr>(std::move(argument), delegate);
    }

    auto left = parse_conditional();

    switch (peek().type) {
        case JsTokenType::Equal:
        case JsTokenType::PlusEqual:
        case JsTokenType::MinusEqual:
        case JsTokenType::StarEqual:
        case JsTokenType::SlashEqual:
        case JsTokenType::PercentEqual:
        case JsTokenType::PowerEqual:
        case JsTokenType::ShiftLeftEqual:
        case JsTokenType::ShiftRightEqual:
        case JsTokenType::ShiftRightUnsignedEqual:
        case JsTokenType::AmpEqual:
        case JsTokenType::PipeEqual:
        case JsTokenType::CaretEqual:
        case JsTokenType::AmpAmpEqual:
        case JsTokenType::PipePipeEqual:
        case JsTokenType::NullishEqual:
            break;
        default:
            return left;
    }

    JsTokenType op = advance().type;
    // Right-associative, so `a = b = c` nests as `a = (b = c)`.
    if (op == JsTokenType::Equal && !left->explicitly_grouped &&
        (left->kind == NodeKind::ArrayLiteral || left->kind == NodeKind::ObjectLiteral)) {
        auto pattern = expr_to_pattern(std::move(left));
        auto value = parse_assignment();
        auto assign = std::make_unique<AssignExpr>(nullptr, std::move(value), op);
        assign->pattern = std::move(pattern);
        assign->line = line;
        return assign;
    }
    if (!is_assignable(left.get())) fail("Invalid left-hand side in assignment");
    auto value = parse_assignment();
    auto assign = std::make_unique<AssignExpr>(std::move(left), std::move(value), op);
    assign->line = line;
    return assign;
}

std::unique_ptr<Expr> JsParser::parse_conditional() {
    auto test = parse_binary(1);
    if (!match(JsTokenType::Question)) return test;
    std::unique_ptr<Expr> consequent;
    {
        FlagScope scope(m_no_in, false);
        consequent = parse_assignment();
    }
    consume(JsTokenType::Colon, "Expected ':' in conditional expression.");
    auto alternate = parse_assignment();
    return std::make_unique<ConditionalExpr>(std::move(test), std::move(consequent), std::move(alternate));
}

int JsParser::binary_precedence(JsTokenType type) {
    switch (type) {
        case JsTokenType::Nullish: return 1;
        case JsTokenType::PipePipe: return 2;
        case JsTokenType::AmpAmp: return 3;
        case JsTokenType::Pipe: return 4;
        case JsTokenType::Caret: return 5;
        case JsTokenType::Amp: return 6;
        case JsTokenType::EqualEqual:
        case JsTokenType::NotEqual:
        case JsTokenType::StrictEqual:
        case JsTokenType::StrictNotEqual: return 7;
        case JsTokenType::Less:
        case JsTokenType::LessEqual:
        case JsTokenType::Greater:
        case JsTokenType::GreaterEqual:
        case JsTokenType::InstanceOf:
        case JsTokenType::In: return 8;
        case JsTokenType::ShiftLeft:
        case JsTokenType::ShiftRight:
        case JsTokenType::ShiftRightUnsigned: return 9;
        case JsTokenType::Plus:
        case JsTokenType::Minus: return 10;
        case JsTokenType::Star:
        case JsTokenType::Slash:
        case JsTokenType::Percent: return 11;
        case JsTokenType::Power: return 12;
        default: return 0;
    }
}

namespace {
bool is_unparenthesized(const Expr* expr, JsTokenType op) {
    return expr && !expr->explicitly_grouped && expr->kind == NodeKind::Binary &&
           static_cast<const BinaryExpr*>(expr)->op == op;
}
}

std::unique_ptr<Expr> JsParser::parse_binary(int min_precedence) {
    auto left = parse_unary();
    while (true) {
        JsTokenType type = peek().type;
        int precedence = binary_precedence(type);
        if (precedence == 0 || precedence < min_precedence) break;
        if (type == JsTokenType::In && m_no_in) break;
        // `-2 ** 2` could mean (-2) ** 2 or -(2 ** 2), so the grammar allows
        // neither: a unary operator may not sit directly left of `**`.
        if (type == JsTokenType::Power && !left->explicitly_grouped &&
            (left->kind == NodeKind::Unary || left->kind == NodeKind::Await)) {
            fail("Unary operator used immediately before exponentiation expression. "
                 "Parenthesis must be used to disambiguate operator precedence");
        }
        advance();
        // `**` is right-associative; everything else is left-associative.
        auto right = parse_binary(type == JsTokenType::Power ? precedence : precedence + 1);

        if (type == JsTokenType::Nullish || type == JsTokenType::PipePipe || type == JsTokenType::AmpAmp) {
            bool mixes;
            if (type == JsTokenType::Nullish) {
                mixes = is_unparenthesized(left.get(), JsTokenType::PipePipe) ||
                        is_unparenthesized(left.get(), JsTokenType::AmpAmp) ||
                        is_unparenthesized(right.get(), JsTokenType::PipePipe) ||
                        is_unparenthesized(right.get(), JsTokenType::AmpAmp);
            } else {
                mixes = is_unparenthesized(left.get(), JsTokenType::Nullish) ||
                        is_unparenthesized(right.get(), JsTokenType::Nullish);
            }
            if (mixes) fail("Cannot mix '?\?' with '||' or '&&' without parentheses.");
        }
        left = std::make_unique<BinaryExpr>(std::move(left), type, std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> JsParser::parse_unary() {
    js_check_stack();
    switch (peek().type) {
        case JsTokenType::Bang:
        case JsTokenType::Minus:
        case JsTokenType::Plus:
        case JsTokenType::Tilde:
        case JsTokenType::Typeof:
        case JsTokenType::Void:
        case JsTokenType::Delete: {
            JsTokenType op = advance().type;
            auto operand = parse_unary();
            return std::make_unique<UnaryExpr>(op, std::move(operand));
        }
        // Prefix increment/decrement: `++i` yields the value *after* the change.
        case JsTokenType::PlusPlus:
        case JsTokenType::MinusMinus: {
            JsTokenType op = advance().type;
            auto target = parse_unary();
            if (!is_assignable(target.get())) fail("Invalid left-hand side expression in prefix operation");
            return std::make_unique<UpdateExpr>(std::move(target), op, /*prefix=*/true);
        }
        case JsTokenType::Await: {
            if (!m_functions.empty() && m_functions.back().is_async) {
                advance();
                // Unlike `arguments`, await belongs to the innermost function,
                // arrow or not.
                if (m_functions.back().fn) m_functions.back().fn->uses_await = true;
                auto operand = parse_unary();
                return std::make_unique<AwaitExpr>(std::move(operand));
            }
            return parse_postfix();
        }
        default:
            return parse_postfix();
    }
}

std::unique_ptr<Expr> JsParser::parse_postfix() {
    auto expr = parse_call_or_member();
    // Postfix: `i++` yields the value *before* the change. A line break before
    // the operator ends the expression instead (restricted production).
    if ((check(JsTokenType::PlusPlus) || check(JsTokenType::MinusMinus)) && !peek().newline_before) {
        JsTokenType op = advance().type;
        if (!is_assignable(expr.get())) fail("Invalid left-hand side expression in postfix operation");
        return std::make_unique<UpdateExpr>(std::move(expr), op, /*prefix=*/false);
    }
    return expr;
}

std::vector<std::unique_ptr<Expr>> JsParser::parse_arguments() {
    consume(JsTokenType::OpenParen, "Expected '(' before arguments.");
    FlagScope scope(m_no_in, false);
    std::vector<std::unique_ptr<Expr>> args;
    while (!check(JsTokenType::CloseParen)) {
        if (match(JsTokenType::Ellipsis)) {
            args.push_back(std::make_unique<SpreadExpr>(parse_assignment()));
        } else {
            args.push_back(parse_assignment());
        }
        if (!match(JsTokenType::Comma)) break;
    }
    consume(JsTokenType::CloseParen, "Expected ')' after arguments.");
    return args;
}

// Called at a `new` token.
std::unique_ptr<Expr> JsParser::parse_new() {
    js_check_stack();
    advance();
    if (match(JsTokenType::Dot)) {
        if (!match_identifier("target")) fail_unexpected();
        if (auto* fn = nearest_non_arrow_function()) fn->uses_new_target = true;
        return std::make_unique<NewTargetExpr>();
    }
    std::unique_ptr<Expr> callee = check(JsTokenType::New) ? parse_new() : parse_primary();
    // Member accesses bind tighter than the argument list: `new a.b.C(x)`.
    while (true) {
        if (match(JsTokenType::Dot)) {
            if (!(check(JsTokenType::Identifier) || check(JsTokenType::PrivateName) || peek().is_keyword)) fail_unexpected();
            callee = std::make_unique<MemberExpr>(std::move(callee), advance().text);
        } else if (check(JsTokenType::OpenBracket)) {
            advance();
            FlagScope scope(m_no_in, false);
            auto index = parse_expression();
            consume(JsTokenType::CloseBracket, "Expected ']' after index expression.");
            callee = std::make_unique<IndexExpr>(std::move(callee), std::move(index));
        } else {
            break;
        }
    }
    std::vector<std::unique_ptr<Expr>> args;
    if (check(JsTokenType::OpenParen)) args = parse_arguments();
    return std::make_unique<NewExpr>(std::move(callee), std::move(args));
}

std::unique_ptr<Expr> JsParser::parse_call_or_member() {
    const int line = peek().line;
    std::unique_ptr<Expr> expr = check(JsTokenType::New) ? parse_new() : parse_primary();
    bool has_optional_link = false;

    while (true) {
        if (match(JsTokenType::Dot)) {
            if (!(check(JsTokenType::Identifier) || check(JsTokenType::PrivateName) || peek().is_keyword)) fail_unexpected();
            expr = std::make_unique<MemberExpr>(std::move(expr), advance().text);
        } else if (match(JsTokenType::QuestionDot)) {
            has_optional_link = true;
            if (check(JsTokenType::OpenParen)) {
                auto call = std::make_unique<CallExpr>(std::move(expr), parse_arguments());
                call->optional = true;
                expr = std::move(call);
            } else if (match(JsTokenType::OpenBracket)) {
                FlagScope scope(m_no_in, false);
                auto index = parse_expression();
                consume(JsTokenType::CloseBracket, "Expected ']' after index expression.");
                auto node = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
                node->optional = true;
                expr = std::move(node);
            } else {
                if (!(check(JsTokenType::Identifier) || check(JsTokenType::PrivateName) || peek().is_keyword)) fail_unexpected();
                auto node = std::make_unique<MemberExpr>(std::move(expr), advance().text);
                node->optional = true;
                expr = std::move(node);
            }
        } else if (check(JsTokenType::OpenBracket)) {
            advance();
            FlagScope scope(m_no_in, false);
            auto index = parse_expression();
            consume(JsTokenType::CloseBracket, "Expected ']' after index expression.");
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
        } else if (check(JsTokenType::OpenParen)) {
            expr = std::make_unique<CallExpr>(std::move(expr), parse_arguments());
        } else if (check(JsTokenType::NoSubstitutionTemplate) || check(JsTokenType::TemplateHead)) {
            expr = parse_template(std::move(expr));
        } else {
            break;
        }
        expr->line = line;
    }

    if (has_optional_link) return std::make_unique<OptionalChainExpr>(std::move(expr));
    return expr;
}

// Called at a NoSubstitutionTemplate or TemplateHead token.
std::unique_ptr<Expr> JsParser::parse_template(std::unique_ptr<Expr> tag) {
    std::vector<std::string> cooked;
    std::vector<std::string> raw;
    std::vector<std::unique_ptr<Expr>> expressions;

    const JsToken& first = advance();
    cooked.push_back(first.text);
    raw.push_back(first.raw);
    if (first.type == JsTokenType::TemplateHead) {
        while (true) {
            {
                FlagScope scope(m_no_in, false);
                expressions.push_back(parse_expression());
            }
            if (check(JsTokenType::TemplateMiddle)) {
                const JsToken& t = advance();
                cooked.push_back(t.text);
                raw.push_back(t.raw);
            } else if (check(JsTokenType::TemplateTail)) {
                const JsToken& t = advance();
                cooked.push_back(t.text);
                raw.push_back(t.raw);
                break;
            } else {
                fail("Expected '}' or the closing backtick in template literal.");
            }
        }
    }

    if (tag) {
        auto node = std::make_unique<TaggedTemplateExpr>();
        node->tag = std::move(tag);
        node->cooked = std::move(cooked);
        node->raw = std::move(raw);
        node->expressions = std::move(expressions);
        return node;
    }

    std::vector<std::unique_ptr<Expr>> parts;
    for (size_t i = 0; i < cooked.size(); ++i) {
        parts.push_back(std::make_unique<LiteralExpr>(JsValue::make_string(cooked[i])));
        if (i < expressions.size()) parts.push_back(std::move(expressions[i]));
    }
    return std::make_unique<TemplateLiteralExpr>(std::move(parts));
}

// Called with the opening bracket already consumed.
std::unique_ptr<Expr> JsParser::parse_array_literal() {
    FlagScope scope(m_no_in, false);
    std::vector<std::unique_ptr<Expr>> elements;
    while (!check(JsTokenType::CloseBracket)) {
        if (check(JsTokenType::Comma)) {
            advance();
            elements.push_back(nullptr);      // hole: `[1, , 3]`
            continue;
        }
        if (match(JsTokenType::Ellipsis)) {
            elements.push_back(std::make_unique<SpreadExpr>(parse_assignment()));
        } else {
            elements.push_back(parse_assignment());
        }
        if (!check(JsTokenType::CloseBracket)) consume(JsTokenType::Comma, "Expected ',' or ']' in array literal.");
    }
    consume(JsTokenType::CloseBracket, "Expected ']' after array elements.");
    return std::make_unique<ArrayLiteralExpr>(std::move(elements));
}

// Called with the opening brace already consumed.
std::unique_ptr<Expr> JsParser::parse_object_literal() {
    FlagScope scope(m_no_in, false);
    std::vector<ObjectProperty> properties;

    auto can_follow_modifier = [&]() {
        const JsToken& next = peek(1);
        return next.type != JsTokenType::OpenParen && next.type != JsTokenType::Colon &&
               next.type != JsTokenType::Comma && next.type != JsTokenType::CloseBrace &&
               next.type != JsTokenType::Equal;
    };

    while (!check(JsTokenType::CloseBrace)) {
        ObjectProperty prop;
        if (match(JsTokenType::Ellipsis)) {
            prop.kind = ObjectProperty::Kind::Spread;
            prop.value = parse_assignment();
            properties.push_back(std::move(prop));
            if (!check(JsTokenType::CloseBrace)) consume(JsTokenType::Comma, "Expected ',' or '}' in object literal.");
            continue;
        }

        bool is_async = false;
        bool is_generator = false;
        const size_t property_token = m_current;
        if (check_identifier("async") && can_follow_modifier() && !peek(1).newline_before) {
            advance();
            is_async = true;
        }
        if (match(JsTokenType::Star)) is_generator = true;
        if ((check_identifier("get") || check_identifier("set")) && can_follow_modifier()) {
            prop.kind = advance().text == "get" ? ObjectProperty::Kind::Getter : ObjectProperty::Kind::Setter;
        }

        const bool key_is_identifier = check(JsTokenType::Identifier) ||
                                       check(JsTokenType::Yield) || check(JsTokenType::Await) || check(JsTokenType::Let);
        if (!is_property_name_token()) fail_unexpected();
        prop.key = parse_property_name(prop.computed_key);

        if (prop.kind != ObjectProperty::Kind::Init) {
            auto fn = parse_method(false, false);
            set_source(*fn, property_token);
            fn->name = prop.key;
            prop.value = std::make_unique<FunctionExpr>(std::move(fn));
        } else if (check(JsTokenType::OpenParen) || is_async || is_generator) {
            auto fn = parse_method(is_async, is_generator);
            set_source(*fn, property_token);
            fn->name = prop.key;
            prop.is_method = true;
            prop.value = std::make_unique<FunctionExpr>(std::move(fn));
        } else if (match(JsTokenType::Colon)) {
            prop.value = parse_assignment();
        } else {
            // Shorthand `{a}`; `{a = 1}` is only meaningful as a pattern.
            if (!key_is_identifier || prop.computed_key) fail_unexpected();
            validate_identifier(prop.key, is_strict());
            prop.shorthand = true;
            if (prop.key == "arguments") {
                if (!m_functions.empty() && m_functions.back().forbid_arguments) {
                    fail("'arguments' is not allowed in class field initializer or static initialization block");
                }
                if (auto* fn = nearest_non_arrow_function()) fn->uses_arguments = true;
            }
            auto ident = std::make_unique<IdentifierExpr>(prop.key);
            if (match(JsTokenType::Equal)) {
                prop.value = std::make_unique<AssignExpr>(std::move(ident), parse_assignment());
            } else {
                prop.value = std::move(ident);
            }
        }
        properties.push_back(std::move(prop));
        if (!check(JsTokenType::CloseBrace)) consume(JsTokenType::Comma, "Expected ',' or '}' in object literal.");
    }
    consume(JsTokenType::CloseBrace, "Expected '}' after object literal.");
    return std::make_unique<ObjectLiteralExpr>(std::move(properties));
}

std::unique_ptr<Expr> JsParser::parse_primary() {
    js_check_stack();
    const JsToken& token = peek();
    const int line = token.line;
    std::unique_ptr<Expr> result;

    switch (token.type) {
        case JsTokenType::Number:
            result = std::make_unique<LiteralExpr>(JsValue::make_number(advance().number_value));
            break;
        case JsTokenType::String:
            result = std::make_unique<LiteralExpr>(JsValue::make_string(advance().text));
            break;
        case JsTokenType::True:
            advance();
            result = std::make_unique<LiteralExpr>(JsValue::make_boolean(true));
            break;
        case JsTokenType::False:
            advance();
            result = std::make_unique<LiteralExpr>(JsValue::make_boolean(false));
            break;
        case JsTokenType::Null:
            advance();
            result = std::make_unique<LiteralExpr>(JsValue::make_null());
            break;
        case JsTokenType::Undefined:
            advance();
            result = std::make_unique<LiteralExpr>(JsValue::make_undefined());
            break;
        case JsTokenType::This:
            advance();
            result = std::make_unique<IdentifierExpr>("this");
            break;
        case JsTokenType::Identifier: {
            if (check_identifier("async") && peek(1).type == JsTokenType::Function && !peek(1).newline_before) {
                advance();
                advance();
                result = std::make_unique<FunctionExpr>(parse_function(true, false));
                break;
            }
            std::string name = advance().text;
            validate_identifier(name, is_strict());
            if (name == "arguments") {
                if (!m_functions.empty() && m_functions.back().forbid_arguments) {
                    fail("'arguments' is not allowed in class field initializer or static initialization block");
                }
                if (auto* fn = nearest_non_arrow_function()) fn->uses_arguments = true;
            }
            result = std::make_unique<IdentifierExpr>(std::move(name));
            break;
        }
        case JsTokenType::Yield:
        case JsTokenType::Await:
        case JsTokenType::Let: {
            // Outside the contexts that reserve them these are plain names.
            std::string name = advance().text;
            validate_identifier(name, is_strict());
            result = std::make_unique<IdentifierExpr>(std::move(name));
            break;
        }
        case JsTokenType::OpenParen: {
            advance();
            FlagScope scope(m_no_in, false);
            result = parse_expression();
            consume(JsTokenType::CloseParen, "Expected ')' after expression.");
            result->explicitly_grouped = true;
            return result;
        }
        case JsTokenType::OpenBracket:
            advance();
            result = parse_array_literal();
            break;
        case JsTokenType::OpenBrace:
            advance();
            result = parse_object_literal();
            break;
        case JsTokenType::Function:
            advance();
            result = std::make_unique<FunctionExpr>(parse_function(false, false));
            break;
        case JsTokenType::Class:
            advance();
            result = std::make_unique<ClassExpr>(parse_class(false));
            break;
        case JsTokenType::NoSubstitutionTemplate:
        case JsTokenType::TemplateHead:
            result = parse_template(nullptr);
            break;
        case JsTokenType::Regex: {
            const JsToken& t = advance();
            std::string regex_err;
            if (!js_regexp_validate(t.text, t.flags, regex_err)) {
                fail(regex_err);
            }
            result = std::make_unique<RegexLiteralExpr>(t.text, t.flags);
            break;
        }
        case JsTokenType::Super: {
            advance();
            if (auto* fn = nearest_non_arrow_function()) fn->uses_super = true;
            if (check(JsTokenType::OpenParen)) {
                result = std::make_unique<SuperCallExpr>(parse_arguments());
            } else if (match(JsTokenType::Dot)) {
                if (!(check(JsTokenType::Identifier) || peek().is_keyword)) fail_unexpected();
                result = std::make_unique<SuperMemberExpr>(advance().text, nullptr);
            } else if (match(JsTokenType::OpenBracket)) {
                FlagScope scope(m_no_in, false);
                auto computed = parse_expression();
                consume(JsTokenType::CloseBracket, "Expected ']' after super index.");
                result = std::make_unique<SuperMemberExpr>(std::string(), std::move(computed));
            } else {
                fail("'super' keyword unexpected here");
            }
            break;
        }
        default:
            fail_unexpected();
    }
    result->line = line;
    return result;
}

} // namespace litebird
