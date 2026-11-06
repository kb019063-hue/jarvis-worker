#pragma once

#include "litebird/js/JsLexer.hpp"
#include "litebird/js/JsValue.hpp"
#include <memory>
#include <vector>
#include <string>

namespace litebird {

class AstNode {
public:
    virtual ~AstNode() = default;
};

class BlockStmt;

// Expressions
class Expr : public AstNode {};

class LiteralExpr : public Expr {
public:
    JsValue value;
    explicit LiteralExpr(JsValue val) : value(std::move(val)) {}
};

class IdentifierExpr : public Expr {
public:
    std::string name;
    explicit IdentifierExpr(std::string n) : name(std::move(n)) {}
};

class BinaryExpr : public Expr {
public:
    std::unique_ptr<Expr> left;
    JsTokenType op;
    std::unique_ptr<Expr> right;

    BinaryExpr(std::unique_ptr<Expr> l, JsTokenType o, std::unique_ptr<Expr> r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
};

class UnaryExpr : public Expr {
public:
    JsTokenType op;
    std::unique_ptr<Expr> operand;

    UnaryExpr(JsTokenType o, std::unique_ptr<Expr> expr)
        : op(o), operand(std::move(expr)) {}
};

class CallExpr : public Expr {
public:
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;

    CallExpr(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a)
        : callee(std::move(c)), args(std::move(a)) {}
};

class NewExpr : public Expr {
public:
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;

    NewExpr(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a)
        : callee(std::move(c)), args(std::move(a)) {}
};

class MemberExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::string property;

    MemberExpr(std::unique_ptr<Expr> obj, std::string prop)
        : object(std::move(obj)), property(std::move(prop)) {}
};

class IndexExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;

    IndexExpr(std::unique_ptr<Expr> obj, std::unique_ptr<Expr> idx)
        : object(std::move(obj)), index(std::move(idx)) {}
};

class ObjectLiteralExpr : public Expr {
public:
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> entries;
    explicit ObjectLiteralExpr(std::vector<std::pair<std::string, std::unique_ptr<Expr>>> e)
        : entries(std::move(e)) {}
};

class ArrayLiteralExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;
    explicit ArrayLiteralExpr(std::vector<std::unique_ptr<Expr>> elems)
        : elements(std::move(elems)) {}
};

class TemplateLiteralExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> parts;
    explicit TemplateLiteralExpr(std::vector<std::unique_ptr<Expr>> p)
        : parts(std::move(p)) {}
};

class FunctionExpr : public Expr {
public:
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<BlockStmt> body;

    FunctionExpr(std::string n, std::vector<std::string> p, std::shared_ptr<BlockStmt> b)
        : name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
};

class AssignExpr : public Expr {
public:
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;

    AssignExpr(std::unique_ptr<Expr> t, std::unique_ptr<Expr> v)
        : target(std::move(t)), value(std::move(v)) {}
};

// Statements
class Stmt : public AstNode {};

class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expression;
    explicit ExprStmt(std::unique_ptr<Expr> expr) : expression(std::move(expr)) {}
};

class VarDeclStmt : public Stmt {
public:
    std::string name;
    std::unique_ptr<Expr> initializer;

    VarDeclStmt(std::string n, std::unique_ptr<Expr> init)
        : name(std::move(n)), initializer(std::move(init)) {}
};

class BlockStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> statements;
    BlockStmt() = default;
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts)
        : statements(std::move(stmts)) {}
};

class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch;

    IfStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> th, std::unique_ptr<Stmt> el = nullptr)
        : condition(std::move(cond)), then_branch(std::move(th)), else_branch(std::move(el)) {}
};

class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;

    WhileStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> b)
        : condition(std::move(cond)), body(std::move(b)) {}
};

class ReturnStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;
    explicit ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
};

class FunctionDeclStmt : public Stmt {
public:
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<BlockStmt> body;

    FunctionDeclStmt(std::string n, std::vector<std::string> p, std::shared_ptr<BlockStmt> b)
        : name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
};

class JsParser {
public:
    explicit JsParser(std::vector<JsToken> tokens);

    std::unique_ptr<BlockStmt> parse_program();

private:
    const JsToken& peek() const;
    const JsToken& previous() const;
    bool is_at_end() const;
    const JsToken& advance();
    bool check(JsTokenType type) const;
    bool match(JsTokenType type);
    const JsToken& consume(JsTokenType type, const std::string& err_msg);

    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<Stmt> parse_var_declaration();
    std::unique_ptr<Stmt> parse_if_statement();
    std::unique_ptr<Stmt> parse_while_statement();
    std::unique_ptr<Stmt> parse_return_statement();
    std::unique_ptr<FunctionDeclStmt> parse_function_declaration();
    std::unique_ptr<BlockStmt> parse_block();

    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_assignment();
    std::unique_ptr<Expr> parse_logical_or();
    std::unique_ptr<Expr> parse_logical_and();
    std::unique_ptr<Expr> parse_equality();
    std::unique_ptr<Expr> parse_comparison();
    std::unique_ptr<Expr> parse_term();
    std::unique_ptr<Expr> parse_factor();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_call_or_member();
    std::unique_ptr<Expr> parse_primary();

    std::vector<JsToken> m_tokens;
    size_t m_current{0};
};

} // namespace litebird
