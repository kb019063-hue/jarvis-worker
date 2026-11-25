#pragma once

#include "litebird/js/Bytecode.hpp"
#include "litebird/js/JsParser.hpp"
#include <string>
#include <vector>
#include <memory>

namespace litebird {

struct Local {
    std::string name;
    int depth{-1};
};

class BytecodeCompiler {
public:
    explicit BytecodeCompiler(std::shared_ptr<BytecodeFunction> function = nullptr);

    std::shared_ptr<BytecodeFunction> compile(const BlockStmt* program);

private:
    void compile_stmt(const Stmt* stmt);
    // Only a bare identifier operand of typeof may quietly miss a global.
    void compile_expr(const Expr* expr, const std::string* name_hint = nullptr,
                      bool quiet_unresolvable = false);
    /// `switch (d) { … }` (ECMA-262 14.13): the discriminant is evaluated
    /// once, each clause's test is evaluated in source order until the first
    /// strict equality holds, and the body then runs from that clause to the
    /// end (fall-through), with a plain `break` leaving the switch. One block
    /// scope surrounds the whole switch, as in the tree-walker.
    void compile_switch(const SwitchStmt* stmt);
    static void require_simple_function(const FunctionNode& node);

    void begin_scope();
    void end_scope();

    void add_local(const std::string& name);
    /// The BindingIdentifier case of a declarator: `var a = v`, including
    /// where the binding lives (a function's local slot, or the global
    /// object's property at the script level). Consumes the value on top of
    /// the stack exactly once.
    void compile_binding(const std::string& name, JsDeclKind kind);
    /// Dispatch on the shape of a binding target: a name, or an array
    /// pattern. An object pattern is refused here, loudly, rather than
    /// compiled wrongly.
    void compile_pattern(const Pattern& pattern, JsDeclKind kind);
    /// The BindingPattern case of a declarator: `var [a, b = 1, ...rest] = v`,
    /// recursively. The value is on top of the stack; it consumes it. Array
    /// patterns only — object patterns stay refused rather than half-done.
    void compile_array_binding(const Pattern& pattern, JsDeclKind kind);
    /// Records a local at `depth` rather than at the scope being compiled, for
    /// a `var` nested in a block: it belongs to the enclosing function.
    void add_local_at(const std::string& name, int depth);
    int resolve_local(const std::string& name);

    size_t emit_byte(uint8_t byte);
    size_t emit_opcode(OpCode op);
    /// Maps `+=`, `-=`, `*=`, `/=`, `%=` onto the matching arithmetic opcode.
    static OpCode compound_opcode(JsTokenType op);
    size_t emit_constant(JsValue value);
    size_t emit_jump(OpCode op);
    void patch_jump(size_t offset);
    void emit_loop(size_t loop_start);
    /// Fills in an already-emitted jump that goes backwards to `target`.
    void patch_back_jump(size_t at, size_t target);
    /// Fills in an already-emitted forward jump so that it lands on `target`,
    /// which may be earlier than the code written since.
    void patch_forward_jump(size_t at, size_t target);

    /// One enclosing loop, for `break` and `continue`.
    ///
    /// A loop's exit jump is only known once its body is written, and a
    /// `break` inside the body has to jump to it -- so each `break` emits a
    /// jump whose target is filled in when the loop is finished. The same for
    /// `continue`, which jumps back to the loop's test (the update, in a
    /// `for`, where `continue` runs it first).
    struct Loop {
        size_t start{0};                   ///< the test's first instruction
        /// Where a `continue` goes. In a `while` the test is behind the body,
        /// so it is a backward jump; in a `for` and a `do`/`while` the update
        /// or the test is *after* the body, so it is a forward jump. Using the
        /// backward form for all three sent `continue` in a `for` off the end
        /// of the loop.
        bool continue_is_forward{false};
        std::vector<size_t> breaks;        ///< jumps out, patched when the loop ends
        std::vector<size_t> continues;     ///< jumps to the test/update
        /// A `switch` pushes a context so that a plain `break` jumps to the
        /// end of the switch. It is not a loop: an unlabelled `continue`
        /// inside a switch (ECMA-262 14.13.4.1) targets the enclosing loop,
        /// so a `continue` skips over this context.
        bool is_switch{false};
    };

    std::shared_ptr<BytecodeFunction> m_function;
    std::vector<Local> m_locals;
    int m_scope_depth{0};
    bool m_is_function{false};
    std::vector<Loop> m_loops;
};

} // namespace litebird
