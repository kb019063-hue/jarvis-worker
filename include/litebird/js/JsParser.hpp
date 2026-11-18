#pragma once

#include "litebird/js/JsLexer.hpp"
#include "litebird/js/JsValue.hpp"
#include <memory>
#include <vector>
#include <string>

namespace litebird {

/// Every AST node carries a tag so the interpreter can dispatch with a switch
/// instead of a chain of dynamic_casts.
enum class NodeKind : uint8_t {
    // Expressions
    Literal, Identifier, TemplateLiteral, TaggedTemplate, ArrayLiteral, ObjectLiteral,
    Function, Class, Regex, Unary, Update, Binary, Assign, Conditional, Call, New,
    Member, Index, Sequence, Spread, Yield, Await, SuperCall, SuperMember,
    OptionalChain, NewTarget, PrivateIn,
    // Statements
    ExprStmt, VarDecl, Block, If, While, DoWhile, For, ForEach, Return, Break,
    Continue, Throw, Try, Switch, FunctionDecl, ClassDecl, Labeled, Empty,
    // Shared sub-trees
    FunctionNode, ClassNode
};

class AstNode {
public:
    virtual ~AstNode() = default;
    NodeKind kind;
    int line{0};
protected:
    explicit AstNode(NodeKind k) : kind(k) {}
};

class BlockStmt;
class FunctionDeclStmt;
class Expr;
class Stmt;
struct Pattern;

/// Deletes a node of the syntax tree without destroying its children from
/// inside its own destructor. `a + b + c + ...` parses into a tree as deep as
/// the expression is long, and the parser builds that chain in a loop, so its
/// depth is not limited by the parser's own stack check; deleting it the
/// ordinary way would recurse once per operator. See common/ReleaseQueue.hpp
/// for the idea; the queue here holds raw nodes.
void ast_delete(Expr* node) noexcept;
void ast_delete(Stmt* node) noexcept;
void ast_delete(Pattern* node) noexcept;

} // namespace litebird

// Every owning pointer to an expression, statement or pattern in the tree is a
// std::unique_ptr of one of these three types, or leads to one within a node
// or two, so routing their deleters through ast_delete() bounds the depth of
// destruction for the whole tree without touching the node classes. A
// unique_ptr to a derived node (what std::make_unique returns) converts as
// before; that is what the converting constructors are for.
namespace std {
template <>
struct default_delete<litebird::Expr> {
    constexpr default_delete() noexcept = default;
    template <typename U, typename = enable_if_t<is_convertible_v<U*, litebird::Expr*>>>
    default_delete(const default_delete<U>&) noexcept {}
    void operator()(litebird::Expr* node) const noexcept { litebird::ast_delete(node); }
};
template <>
struct default_delete<litebird::Stmt> {
    constexpr default_delete() noexcept = default;
    template <typename U, typename = enable_if_t<is_convertible_v<U*, litebird::Stmt*>>>
    default_delete(const default_delete<U>&) noexcept {}
    void operator()(litebird::Stmt* node) const noexcept { litebird::ast_delete(node); }
};
template <>
struct default_delete<litebird::Pattern> {
    constexpr default_delete() noexcept = default;
    void operator()(litebird::Pattern* node) const noexcept { litebird::ast_delete(node); }
};
} // namespace std

namespace litebird {

// ---------------------------------------------------------------------------
// Binding and assignment patterns: `x`, `[a, , b = 1, ...rest]`, `{a, b: c}`.
// ---------------------------------------------------------------------------

struct PatternElement {
    std::unique_ptr<Pattern> target;        // null for an array hole
    std::unique_ptr<Expr> default_value;
    bool rest{false};
    std::string key;                        // object patterns: property name
    std::unique_ptr<Expr> computed_key;     // object patterns: `[expr]: target`
};

struct Pattern {
    enum class Kind : uint8_t { Identifier, Array, Object, Target };
    Kind kind{Kind::Identifier};
    std::string name;                       // Identifier
    std::vector<PatternElement> elements;   // Array / Object
    std::unique_ptr<Expr> target;           // Target: `a.b` or `a[i]` in an assignment pattern

    void collect_names(std::vector<std::string>& out) const;
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

class Expr : public AstNode {
public:
    bool explicitly_grouped{false};
protected:
    explicit Expr(NodeKind k) : AstNode(k) {}
};

class LiteralExpr : public Expr {
public:
    JsValue value;
    explicit LiteralExpr(JsValue val) : Expr(NodeKind::Literal), value(std::move(val)) {}
};

class IdentifierExpr : public Expr {
public:
    std::string name;
    /// The name is `undefined`. That is an ordinary identifier, not a keyword:
    /// `function (window, undefined) {...}` declares a parameter with that
    /// name. The evaluator may still answer without a scope lookup as long as
    /// no scope has ever declared it (see JsEnvironment::undefined_shadowed).
    bool is_undefined{false};
    explicit IdentifierExpr(std::string n)
        : Expr(NodeKind::Identifier), name(std::move(n)), is_undefined(name == "undefined") {}
};

/// Arithmetic, comparison, bitwise and the short-circuiting logical operators.
class BinaryExpr : public Expr {
public:
    std::unique_ptr<Expr> left;
    JsTokenType op;
    std::unique_ptr<Expr> right;
    /// How many binary operators are nested directly through `left`:
    /// 0 for `a + b`, 2 for `a + b + c + d`. A long concatenation is a deep
    /// tree in this direction only, and the evaluator walks such a chain in a
    /// loop instead of recursing down it.
    uint32_t left_chain{0};

    BinaryExpr(std::unique_ptr<Expr> l, JsTokenType o, std::unique_ptr<Expr> r)
        : Expr(NodeKind::Binary), left(std::move(l)), op(o), right(std::move(r)) {
        if (left && left->kind == NodeKind::Binary) {
            left_chain = static_cast<const BinaryExpr*>(left.get())->left_chain + 1;
        }
    }
};

class UnaryExpr : public Expr {
public:
    JsTokenType op;
    std::unique_ptr<Expr> operand;

    UnaryExpr(JsTokenType o, std::unique_ptr<Expr> expr)
        : Expr(NodeKind::Unary), op(o), operand(std::move(expr)) {}
};

class ConditionalExpr : public Expr {
public:
    std::unique_ptr<Expr> test;
    std::unique_ptr<Expr> consequent;
    std::unique_ptr<Expr> alternate;

    ConditionalExpr(std::unique_ptr<Expr> t, std::unique_ptr<Expr> c, std::unique_ptr<Expr> a)
        : Expr(NodeKind::Conditional), test(std::move(t)), consequent(std::move(c)), alternate(std::move(a)) {}
};

/// `a, b, c`: evaluates every operand and yields the last.
class SequenceExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> expressions;
    explicit SequenceExpr(std::vector<std::unique_ptr<Expr>> e)
        : Expr(NodeKind::Sequence), expressions(std::move(e)) {}
};

/// `...x` inside an array literal or an argument list.
class SpreadExpr : public Expr {
public:
    std::unique_ptr<Expr> argument;
    explicit SpreadExpr(std::unique_ptr<Expr> a) : Expr(NodeKind::Spread), argument(std::move(a)) {}
};

class CallExpr : public Expr {
public:
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
    bool optional{false};   // `f?.()`

    CallExpr(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a)
        : Expr(NodeKind::Call), callee(std::move(c)), args(std::move(a)) {}
};

class NewExpr : public Expr {
public:
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;

    NewExpr(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a)
        : Expr(NodeKind::New), callee(std::move(c)), args(std::move(a)) {}
};

class MemberExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::string property;
    bool optional{false};   // `a?.b`
    /// `a.#p`: the name is a private name, so `property` is the source spelling
    /// (`"#p"`) and what is read and written is the slot the class that
    /// declared it minted when it was evaluated, not a property called `#p`.
    bool private_name{false};

    MemberExpr(std::unique_ptr<Expr> obj, std::string prop)
        : Expr(NodeKind::Member), object(std::move(obj)), property(std::move(prop)) {}
};

/// `#p in obj`: the one place a private name appears on its own. It asks
/// whether `obj` carries the brand, which is a question no string key can ask.
class PrivateInExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::string name;       // with the '#', as written

    PrivateInExpr(std::unique_ptr<Expr> obj, std::string n)
        : Expr(NodeKind::PrivateIn), object(std::move(obj)), name(std::move(n)) {}
};

class IndexExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    bool optional{false};   // `a?.[i]`

    IndexExpr(std::unique_ptr<Expr> obj, std::unique_ptr<Expr> idx)
        : Expr(NodeKind::Index), object(std::move(obj)), index(std::move(idx)) {}
};

/// Marks where an optional chain ends: a short-circuit anywhere inside makes
/// the whole wrapped expression evaluate to undefined.
class OptionalChainExpr : public Expr {
public:
    std::unique_ptr<Expr> expression;
    explicit OptionalChainExpr(std::unique_ptr<Expr> e)
        : Expr(NodeKind::OptionalChain), expression(std::move(e)) {}
};

struct ObjectProperty {
    enum class Kind : uint8_t { Init, Getter, Setter, Spread };
    Kind kind{Kind::Init};
    std::string key;
    std::unique_ptr<Expr> computed_key;
    std::unique_ptr<Expr> value;
    bool shorthand{false};
    bool is_method{false};
};

class ObjectLiteralExpr : public Expr {
public:
    std::vector<ObjectProperty> properties;
    explicit ObjectLiteralExpr(std::vector<ObjectProperty> p)
        : Expr(NodeKind::ObjectLiteral), properties(std::move(p)) {}

    /// True when every property is a plain `key: value` pair.
    bool is_simple() const {
        for (const auto& p : properties) {
            if (p.kind != ObjectProperty::Kind::Init || p.computed_key || p.is_method) return false;
        }
        return true;
    }
};

class ArrayLiteralExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;   // null entries are holes
    explicit ArrayLiteralExpr(std::vector<std::unique_ptr<Expr>> elems)
        : Expr(NodeKind::ArrayLiteral), elements(std::move(elems)) {}
};

class TemplateLiteralExpr : public Expr {
public:
    /// Text and substitutions, strictly alternating and starting and ending
    /// with text: `a${x}b` is ["a", x, "b"] and `${x}` is ["", x, ""]. The
    /// text entries are string LiteralExprs. A substitution may be a literal
    /// too, so only the position says which is which.
    std::vector<std::unique_ptr<Expr>> parts;
    explicit TemplateLiteralExpr(std::vector<std::unique_ptr<Expr>> p)
        : Expr(NodeKind::TemplateLiteral), parts(std::move(p)) {}
};

class TaggedTemplateExpr : public Expr {
public:
    std::unique_ptr<Expr> tag;
    std::vector<std::string> cooked;
    std::vector<std::string> raw;
    std::vector<std::unique_ptr<Expr>> expressions;
    TaggedTemplateExpr() : Expr(NodeKind::TaggedTemplate) {}
};

class RegexLiteralExpr : public Expr {
public:
    std::string pattern;
    std::string flags;
    RegexLiteralExpr(std::string p, std::string f)
        : Expr(NodeKind::Regex), pattern(std::move(p)), flags(std::move(f)) {}
};

struct FunctionParam {
    std::unique_ptr<Pattern> target;
    std::shared_ptr<Expr> default_value;
    bool rest{false};
};

/// A function's parameters and body. Shared between the AST and every function
/// value created from it, because closures outlive the script that defined
/// them.
class FunctionNode : public AstNode {
public:
    FunctionNode() : AstNode(NodeKind::FunctionNode) {}

    std::string name;
    std::vector<FunctionParam> params;
    std::shared_ptr<BlockStmt> body;
    bool is_arrow{false};
    bool is_async{false};
    bool is_generator{false};
    bool is_method{false};               // methods, getters and setters cannot be constructed
    bool is_class_constructor{false};
    bool is_derived_constructor{false};
    bool is_strict{false};
    bool uses_arguments{false};
    bool uses_await{false};              // the body itself contains `await` or `for await`
    bool uses_super{false};              // `super.x` or `super(...)` appears in the body
    bool uses_new_target{false};
    bool simple_params{true};            // identifiers only: no defaults, rest or patterns
    int length{0};                       // parameters before the first default or rest
    std::vector<std::string> var_names;  // `var` bindings hoisted to this function

    /// The script this function was parsed from and the function's extent in
    /// it, for Function.prototype.toString. The text is shared by every
    /// function of the script and lives as long as any of them does.
    std::shared_ptr<const std::string> source;
    uint32_t source_start{0};
    uint32_t source_end{0};

    /// Parameter names when `simple_params` is true.
    std::vector<std::string> simple_param_names() const;
};

class FunctionExpr : public Expr {
public:
    std::shared_ptr<FunctionNode> fn;
    explicit FunctionExpr(std::shared_ptr<FunctionNode> f)
        : Expr(NodeKind::Function), fn(std::move(f)) {}
};

struct ClassMember {
    enum class Kind : uint8_t { Method, Getter, Setter, Field, StaticBlock };
    Kind kind{Kind::Method};
    bool is_static{false};
    /// `#p`: `key` is then the source spelling and the member lives in the
    /// object's private slots rather than in its properties.
    bool is_private{false};
    std::string key;
    std::shared_ptr<Expr> computed_key;
    std::shared_ptr<FunctionNode> fn;    // Method, Getter, Setter, StaticBlock
    std::shared_ptr<Expr> value;         // Field initialiser, may be null
};

class ClassNode : public AstNode {
public:
    ClassNode() : AstNode(NodeKind::ClassNode) {}
    std::string name;
    std::shared_ptr<Expr> super_class;
    std::shared_ptr<FunctionNode> constructor;
    std::vector<ClassMember> members;

    /// The class's own source text; see FunctionNode::source.
    std::shared_ptr<const std::string> source;
    uint32_t source_start{0};
    uint32_t source_end{0};
};

class ClassExpr : public Expr {
public:
    std::shared_ptr<ClassNode> cls;
    explicit ClassExpr(std::shared_ptr<ClassNode> c) : Expr(NodeKind::Class), cls(std::move(c)) {}
};

class YieldExpr : public Expr {
public:
    std::unique_ptr<Expr> argument;
    bool delegate{false}; // yield*
    explicit YieldExpr(std::unique_ptr<Expr> arg, bool del = false)
        : Expr(NodeKind::Yield), argument(std::move(arg)), delegate(del) {}
};

class AwaitExpr : public Expr {
public:
    std::unique_ptr<Expr> expression;
    explicit AwaitExpr(std::unique_ptr<Expr> expr)
        : Expr(NodeKind::Await), expression(std::move(expr)) {}
};

/// `super(args)` inside a derived class constructor.
class SuperCallExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> args;
    explicit SuperCallExpr(std::vector<std::unique_ptr<Expr>> a)
        : Expr(NodeKind::SuperCall), args(std::move(a)) {}
};

/// `super.name` or `super[expr]`.
class SuperMemberExpr : public Expr {
public:
    std::string property;
    std::unique_ptr<Expr> computed;
    SuperMemberExpr(std::string p, std::unique_ptr<Expr> c)
        : Expr(NodeKind::SuperMember), property(std::move(p)), computed(std::move(c)) {}
};

class NewTargetExpr : public Expr {
public:
    NewTargetExpr() : Expr(NodeKind::NewTarget) {}
};

/// `i++`, `i--`, `++i`, `--i` (ECMA-262 13.4 UpdateExpression).
/// `prefix` decides which value the expression yields: the pre- or post-change one.
class UpdateExpr : public Expr {
public:
    std::unique_ptr<Expr> target;
    JsTokenType op;
    bool prefix;

    UpdateExpr(std::unique_ptr<Expr> t, JsTokenType o, bool pre)
        : Expr(NodeKind::Update), target(std::move(t)), op(o), prefix(pre) {}
};

class AssignExpr : public Expr {
public:
    std::unique_ptr<Expr> target;        // identifier, `a.b` or `a[b]`; null when `pattern` is set
    std::unique_ptr<Pattern> pattern;    // destructuring assignment: `[a, b] = ...`, `({x} = ...)`
    std::unique_ptr<Expr> value;
    /// `Equal` for plain assignment, or one of the compound operators.
    JsTokenType op{JsTokenType::Equal};

    AssignExpr(std::unique_ptr<Expr> t, std::unique_ptr<Expr> v,
               JsTokenType o = JsTokenType::Equal)
        : Expr(NodeKind::Assign), target(std::move(t)), value(std::move(v)), op(o) {}
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

enum class JsDeclKind { Var, Let, Const };

class Stmt : public AstNode {
protected:
    explicit Stmt(NodeKind k) : AstNode(k) {}
};

class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expression;
    explicit ExprStmt(std::unique_ptr<Expr> expr)
        : Stmt(NodeKind::ExprStmt), expression(std::move(expr)) {}
};

struct VarDeclarator {
    std::unique_ptr<Pattern> target;
    std::unique_ptr<Expr> init;
};

/// `var`/`let`/`const` with one or more declarators, each a name or a pattern.
class VarDeclStmt : public Stmt {
public:
    JsDeclKind kind{JsDeclKind::Let};
    std::vector<VarDeclarator> decls;
    explicit VarDeclStmt(JsDeclKind k) : Stmt(NodeKind::VarDecl), kind(k) {}

    /// The name when this is a single `let x = ...` style declaration.
    const std::string* simple_name() const {
        if (decls.size() == 1 && decls[0].target && decls[0].target->kind == Pattern::Kind::Identifier) {
            return &decls[0].target->name;
        }
        return nullptr;
    }
};

struct LexicalDecl {
    std::string name;
    bool is_const{false};
};

class BlockStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> statements;
    /// Function declarations directly inside this block, hoisted on entry.
    std::vector<const FunctionDeclStmt*> functions;
    /// Lexical declarations (`let`, `const`, `class`) directly inside this
    /// block, instantiated uninitialised in the scope's TDZ on entry.
    std::vector<LexicalDecl> lexical_decls;
    /// True when the block declares let/const/class/function bindings and so
    /// needs an environment of its own.
    bool has_lexical{false};

    BlockStmt() : Stmt(NodeKind::Block) {}
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts)
        : Stmt(NodeKind::Block), statements(std::move(stmts)) {}
};

/// A whole script: the top-level statements plus what must be hoisted.
class Program : public BlockStmt {
public:
    std::vector<std::string> var_names;
    bool is_strict{false};
};

class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch;

    IfStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> th, std::unique_ptr<Stmt> el = nullptr)
        : Stmt(NodeKind::If), condition(std::move(cond)), then_branch(std::move(th)), else_branch(std::move(el)) {}
};

class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;

    WhileStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> b)
        : Stmt(NodeKind::While), condition(std::move(cond)), body(std::move(b)) {}
};

class DoWhileStmt : public Stmt {
public:
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Expr> condition;

    DoWhileStmt(std::unique_ptr<Stmt> b, std::unique_ptr<Expr> cond)
        : Stmt(NodeKind::DoWhile), body(std::move(b)), condition(std::move(cond)) {}
};

/// `for (init; test; update) body`.
class ForStmt : public Stmt {
public:
    std::unique_ptr<Stmt> init;      // VarDeclStmt, ExprStmt or null
    std::unique_ptr<Expr> test;
    std::unique_ptr<Expr> update;
    std::unique_ptr<Stmt> body;
    /// `let` loop variables captured by a closure need a fresh binding per
    /// iteration; this lists them. Empty when no closure can observe it.
    std::vector<std::string> per_iteration_names;
    bool has_lexical_init{false};

    ForStmt() : Stmt(NodeKind::For) {}
};

/// `for (target in object)` and `for (target of iterable)`.
class ForEachStmt : public Stmt {
public:
    bool is_of{false};
    bool is_await{false};
    bool declares{false};                 // `for (let x of ...)` vs `for (x of ...)`
    JsDeclKind decl_kind{JsDeclKind::Let};
    std::unique_ptr<Pattern> target;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<Stmt> body;

    ForEachStmt() : Stmt(NodeKind::ForEach) {}
};

class ReturnStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;
    explicit ReturnStmt(std::unique_ptr<Expr> v) : Stmt(NodeKind::Return), value(std::move(v)) {}
};

class BreakStmt : public Stmt {
public:
    std::string label;
    explicit BreakStmt(std::string l = "") : Stmt(NodeKind::Break), label(std::move(l)) {}
};

class ContinueStmt : public Stmt {
public:
    std::string label;
    explicit ContinueStmt(std::string l = "") : Stmt(NodeKind::Continue), label(std::move(l)) {}
};

class ThrowStmt : public Stmt {
public:
    std::unique_ptr<Expr> argument;
    explicit ThrowStmt(std::unique_ptr<Expr> a) : Stmt(NodeKind::Throw), argument(std::move(a)) {}
};

class TryStmt : public Stmt {
public:
    std::unique_ptr<BlockStmt> block;
    bool has_handler{false};
    std::unique_ptr<Pattern> param;          // may be null: `catch { ... }`
    std::unique_ptr<BlockStmt> handler;
    std::unique_ptr<BlockStmt> finalizer;

    TryStmt() : Stmt(NodeKind::Try) {}
};

class LabeledStmt : public Stmt {
public:
    std::string label;
    std::unique_ptr<Stmt> body;
    LabeledStmt(std::string l, std::unique_ptr<Stmt> b)
        : Stmt(NodeKind::Labeled), label(std::move(l)), body(std::move(b)) {}
};

class EmptyStmt : public Stmt {
public:
    EmptyStmt() : Stmt(NodeKind::Empty) {}
};

struct SwitchCase {
    std::unique_ptr<Expr> test; // null for default
    std::vector<std::unique_ptr<Stmt>> statements;
};

class SwitchStmt : public Stmt {
public:
    std::unique_ptr<Expr> discriminant;
    std::vector<SwitchCase> cases;
    std::vector<const FunctionDeclStmt*> functions;
    std::vector<LexicalDecl> lexical_decls;
    SwitchStmt(std::unique_ptr<Expr> expr, std::vector<SwitchCase> c)
        : Stmt(NodeKind::Switch), discriminant(std::move(expr)), cases(std::move(c)) {}
};

class FunctionDeclStmt : public Stmt {
public:
    std::shared_ptr<FunctionNode> fn;
    explicit FunctionDeclStmt(std::shared_ptr<FunctionNode> f)
        : Stmt(NodeKind::FunctionDecl), fn(std::move(f)) {}
};

class ClassDeclStmt : public Stmt {
public:
    std::shared_ptr<ClassNode> cls;
    explicit ClassDeclStmt(std::shared_ptr<ClassNode> c)
        : Stmt(NodeKind::ClassDecl), cls(std::move(c)) {}
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class JsParser {
public:
    /// `source` is the text the tokens came from. When given, functions and
    /// classes record their extent in it (Function.prototype.toString).
    explicit JsParser(std::vector<JsToken> tokens, std::shared_ptr<const std::string> source = nullptr);

    /// Parses a whole script. Throws JsSyntaxError on malformed input.
    std::unique_ptr<Program> parse_program();

    /// The private names already in scope, for code that is parsed where they
    /// are: a direct eval inside a class body reads that class's fields
    /// (`v = eval("this.#m()")`), so its text is parsed with the names of the
    /// class it was written in.
    void seed_private_names(std::vector<std::string> names);

private:
    struct FunctionContext {
        FunctionNode* fn{nullptr};          // null for the program
        std::vector<std::string>* var_names{nullptr};
        bool is_arrow{false};
        bool is_async{false};
        bool is_generator{false};
        bool strict{false};
        bool forbid_arguments{false};
        bool forbid_await{false};
        bool forbid_yield{false};
    };

    const JsToken& peek(size_t offset = 0) const;
    const JsToken& previous() const;
    bool is_at_end() const;
    const JsToken& advance();
    bool check(JsTokenType type) const;
    bool check_identifier(const char* text) const;
    bool match(JsTokenType type);
    bool match_identifier(const char* text);
    const JsToken& consume(JsTokenType type, const std::string& err_msg);
    [[noreturn]] void fail(const std::string& message) const;
    [[noreturn]] void fail_unexpected() const;
    void consume_semicolon();
    bool is_strict() const;
    void validate_identifier(const std::string& name, bool strict_ctx) const;
    std::string consume_binding_identifier(const char* what);
    bool is_property_name_token() const;

    // Statements
    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<Stmt> parse_declaration_or_statement();
    std::unique_ptr<VarDeclStmt> parse_var_declaration(JsDeclKind kind, bool no_in);
    std::unique_ptr<Stmt> parse_if_statement();
    std::unique_ptr<Stmt> parse_switch_statement();
    std::unique_ptr<Stmt> parse_while_statement();
    std::unique_ptr<Stmt> parse_do_while_statement();
    std::unique_ptr<Stmt> parse_for_statement();
    std::unique_ptr<Stmt> parse_try_statement();
    std::unique_ptr<Stmt> parse_return_statement();
    std::unique_ptr<BlockStmt> parse_block();
    void parse_statement_list(BlockStmt& block, bool until_close_brace);

    // Functions and classes
    std::shared_ptr<FunctionNode> parse_function(bool is_async, bool is_declaration);
    void parse_params_and_body(FunctionNode& fn);
    void parse_params(FunctionNode& fn);
    void parse_function_body(FunctionNode& fn);
    void finish_params(FunctionNode& fn);
    std::shared_ptr<FunctionNode> parse_method(bool is_async, bool is_generator);
    /// Records [token `first_token`, the token just consumed] as the source of `node`.
    template <typename Node> void set_source(Node& node, size_t first_token) const {
        if (!m_source || m_current == 0 || first_token >= m_current) return;
        node.source = m_source;
        node.source_start = m_tokens[first_token].start;
        node.source_end = m_tokens[m_current - 1].end;
    }
    std::shared_ptr<FunctionNode> parse_arrow_function(bool is_async);
    std::shared_ptr<ClassNode> parse_class(bool is_declaration);
    void detect_use_strict(FunctionNode* fn, const BlockStmt& body);

    // Patterns
    std::unique_ptr<Pattern> parse_binding_pattern();
    std::unique_ptr<Pattern> expr_to_pattern(std::unique_ptr<Expr> expr);
    void declare_var_names(const Pattern& pattern);

    // Expressions
    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_assignment();
    std::unique_ptr<Expr> parse_conditional();
    std::unique_ptr<Expr> parse_binary(int min_precedence);
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_call_or_member();
    std::unique_ptr<Expr> parse_new();
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<Expr> parse_array_literal();
    std::unique_ptr<Expr> parse_object_literal();
    std::unique_ptr<Expr> parse_template(std::unique_ptr<Expr> tag);
    std::vector<std::unique_ptr<Expr>> parse_arguments();
    std::string parse_property_name(std::unique_ptr<Expr>& computed, bool allow_private = false);
    /// Private names in scope: one entry per class body being parsed, outermost
    /// first, holding the names as written (`"#p"`). A method may use a name its
    /// own class declares or one an enclosing class does, and nothing else.
    bool private_declared(const std::string& name) const;
    /// Resolves the names one class body mentioned against every class body
    /// open around it, and reports what nothing declares. A method may read a
    /// field the class declares further down, so the names of a body are only
    /// complete when its closing brace is.
    void finish_private_scope();
    /// True when `name` is a private name, and takes note of a use that no
    /// class body has declared yet. False for an ordinary property name.
    bool check_private_name(const std::string& name);
    /// How many `#p` declarations in the innermost class body: 0 if it is new,
    /// 1 for a getter, 2 for a setter, 3 when that pair is already complete.
    int private_declaration_kind(const std::string& name) const;
    std::vector<std::vector<std::string>> m_private_scopes;
    std::vector<std::vector<uint8_t>> m_private_kinds;
    /// Names mentioned in a body that nothing had declared at the time, one
    /// list per open body, with the line each was written on.
    std::vector<std::vector<std::pair<std::string, int>>> m_private_pending;
    bool is_arrow_ahead() const;
    FunctionNode* nearest_non_arrow_function();
    static bool is_assignable(const Expr* expr);
    static int binary_precedence(JsTokenType type);

    std::vector<JsToken> m_tokens;
    std::shared_ptr<const std::string> m_source;
    std::vector<int> m_match;        // index of the matching bracket for each opener, or -1
    size_t m_current{0};
    bool m_no_in{false};
    int m_closure_count{0};
    std::vector<FunctionContext> m_functions;
    std::vector<BlockStmt*> m_blocks;
};

} // namespace litebird
