#include "litebird/js/BytecodeCompiler.hpp"
#include <stdexcept>
#include <algorithm>

namespace litebird {

BytecodeCompiler::BytecodeCompiler(std::shared_ptr<BytecodeFunction> function)
    : m_function(function ? function : std::make_shared<BytecodeFunction>("__main__", 0)) {
    // Slot 0 is reserved for function/closure receiver or stack alignment
    add_local("");
}

OpCode BytecodeCompiler::compound_opcode(JsTokenType op) {
    switch (op) {
        case JsTokenType::PlusEqual:    return OpCode::OpAdd;
        case JsTokenType::MinusEqual:   return OpCode::OpSubtract;
        case JsTokenType::StarEqual:    return OpCode::OpMultiply;
        case JsTokenType::SlashEqual:   return OpCode::OpDivide;
        case JsTokenType::PercentEqual: return OpCode::OpModulo;
        default:                        return OpCode::OpAdd;
    }
}

size_t BytecodeCompiler::emit_byte(uint8_t byte) {
    return m_function->chunk.write(byte);
}

size_t BytecodeCompiler::emit_opcode(OpCode op) {
    return m_function->chunk.write_opcode(op);
}

size_t BytecodeCompiler::emit_constant(JsValue value) {
    size_t idx = m_function->chunk.add_constant(std::move(value));
    if (idx > 255) {
        throw std::runtime_error("Too many constants in one chunk (>255)");
    }
    emit_opcode(OpCode::OpConstant);
    emit_byte(static_cast<uint8_t>(idx));
    return idx;
}

size_t BytecodeCompiler::emit_jump(OpCode op) {
    emit_opcode(op);
    emit_byte(0xFF);
    emit_byte(0xFF);
    return m_function->chunk.code.size() - 2;
}

void BytecodeCompiler::patch_jump(size_t offset) {
    // Jump distance is from end of jump instruction to current code size
    size_t jump = m_function->chunk.code.size() - (offset + 2);
    if (jump > 65535) {
        throw std::runtime_error("Too much code to jump over (>65535 bytes)");
    }
    m_function->chunk.code[offset] = static_cast<uint8_t>((jump >> 8) & 0xFF);
    m_function->chunk.code[offset + 1] = static_cast<uint8_t>(jump & 0xFF);
}

/// Fills in a jump that has already been emitted and goes *backwards* to
/// `target`. `patch_jump` is the forward case and cannot do this: its operand
/// is measured from the end of the jump to the code after it, and a backward
/// jump's operand is measured from the end of the jump back to the target (see
/// `VM::run`'s OpLoop, which subtracts it).
void BytecodeCompiler::patch_back_jump(size_t at, size_t target) {
    // `at` is the offset of the OpLoop opcode; its two operand bytes follow.
    const size_t end = at + 3;
    if (target > end) {
        throw std::runtime_error("A backward jump cannot point forwards");
    }
    const size_t distance = end - target;
    if (distance > 65535) {
        throw std::runtime_error("Too much code to jump back over (>65535 bytes)");
    }
    m_function->chunk.code[at + 1] = static_cast<uint8_t>((distance >> 8) & 0xFF);
    m_function->chunk.code[at + 2] = static_cast<uint8_t>(distance & 0xFF);
}

/// Fills in an already-emitted *forward* jump so that it lands exactly on
/// `target`, which may be earlier than the code written since.
///
/// `patch_jump` cannot do this: it targets wherever the code cursor is when it
/// is called, which is right for jumping *over* a branch and wrong for jumping
/// *into* code already written. A `continue` in a `for` needs the second: it
/// jumps forward to the update, which is emitted before the patch but is not
/// the current position. Using `patch_jump` there made `continue` skip the
/// update and land on the loop's back edge -- `for (var i = 0; i < 3; i++)
/// { continue; }` never terminated, and nothing reported it.
void BytecodeCompiler::patch_forward_jump(size_t at, size_t target) {
    // `at` comes from `emit_jump`, which answers the offset of the jump's two
    // *operand* bytes -- not of its opcode. The distance is measured from the
    // end of the operand, so `at + 2` rather than `at + 3`; `patch_jump` uses
    // the same convention and `patch_back_jump` does not, because its `at` is
    // taken before the opcode is written.
    const size_t end = at + 2;
    if (target < end) {
        throw std::runtime_error("A forward jump cannot point backwards");
    }
    const size_t distance = target - end;
    if (distance > 65535) {
        throw std::runtime_error("Too much code to jump over (>65535 bytes)");
    }
    // The operand bytes *are* `at` and `at + 1`, because `at` is where they
    // start. Writing at `at + 1`/`at + 2` (as `patch_back_jump` does, whose
    // `at` is the opcode) put the high byte in the following instruction and
    // wrapped the distance to 65297 -- a jump into nowhere.
    m_function->chunk.code[at]     = static_cast<uint8_t>((distance >> 8) & 0xFF);
    m_function->chunk.code[at + 1] = static_cast<uint8_t>(distance & 0xFF);
}

void BytecodeCompiler::emit_loop(size_t loop_start) {
    emit_opcode(OpCode::OpLoop);
    size_t jump = m_function->chunk.code.size() - loop_start + 2;
    if (jump > 65535) {
        throw std::runtime_error("Loop body too large (>65535 bytes)");
    }
    emit_byte(static_cast<uint8_t>((jump >> 8) & 0xFF));
    emit_byte(static_cast<uint8_t>(jump & 0xFF));
}

void BytecodeCompiler::begin_scope() {
    m_scope_depth++;
}

void BytecodeCompiler::end_scope() {
    m_scope_depth--;
    while (!m_locals.empty() && m_locals.back().depth > m_scope_depth) {
        emit_opcode(OpCode::OpPop);
        m_locals.pop_back();
    }
}

void BytecodeCompiler::add_local(const std::string& name) {
    add_local_at(name, m_scope_depth);
}

/// Records a local as belonging to `depth` rather than to the scope being
/// compiled. A `var` nested in a block is a local of the enclosing function,
/// so it is recorded at that function's depth and survives the block.
void BytecodeCompiler::add_local_at(const std::string& name, int depth) {
    if (m_locals.size() >= 256) {
        throw std::runtime_error("Too many local variables in scope (>256)");
    }
    m_locals.push_back({name, depth});
}

int BytecodeCompiler::resolve_local(const std::string& name) {
    for (int i = static_cast<int>(m_locals.size()) - 1; i >= 0; --i) {
        if (m_locals[i].name == name) {
            return i;
        }
    }
    return -1;
}

std::shared_ptr<BytecodeFunction> BytecodeCompiler::compile(const BlockStmt* program) {
    if (program && !program->statements.empty()) {
        for (size_t i = 0; i < program->statements.size(); ++i) {
            const auto& stmt = program->statements[i];
            bool is_last = (i + 1 == program->statements.size());
            if (is_last) {
                if (auto expr_stmt = dynamic_cast<const ExprStmt*>(stmt.get())) {
                    compile_expr(expr_stmt->expression.get());
                    emit_opcode(OpCode::OpReturn);
                    return m_function;
                }
            }
            compile_stmt(stmt.get());
        }
    }

    emit_opcode(OpCode::OpUndefined);
    emit_opcode(OpCode::OpReturn);
    return m_function;
}

void BytecodeCompiler::compile_stmt(const Stmt* stmt) {
    if (!stmt) return;
    js_check_stack();

    if (auto expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        compile_expr(expr_stmt->expression.get());
        emit_opcode(OpCode::OpPop);
    } else if (dynamic_cast<const EmptyStmt*>(stmt)) {
        // nothing to emit
    } else if (auto var_decl = dynamic_cast<const VarDeclStmt*>(stmt)) {
        // `var a = 1, b = 2` is one statement declaring two bindings, each
        // initialized in the order written (ECMA-262 14.3.2). Each declarator
        // is compiled exactly as a single one is, which is also what the
        // tree-walker does for them, so a pair of declarators cannot disagree
        // with the single form.
        for (const auto& declarator : var_decl->decls) {
            // Only a plain name is a binding this compiler can place. `var
            // [a, b] = ...` is ECMA-262's BindingPattern, which the tree-walker
            // destructures; there is no opcode here that does that, so it is
            // still refused rather than half-done.
            if (!declarator.target || declarator.target->kind != Pattern::Kind::Identifier) {
                throw std::runtime_error(
                    "Destructuring declarations are not supported by the bytecode compiler");
            }
            const std::string& name = declarator.target->name;
            if (declarator.init) {
                compile_expr(declarator.init.get());
            } else {
                emit_opcode(OpCode::OpUndefined);
            }

            // ECMA-262 14.3.2: a `let`/`const` belongs to the block it is
            // declared in, and `end_scope` pops it when that block ends. A
            // `var` belongs to the enclosing *function*, however deeply it is
            // nested, so it is not a local of the inner block and must not be
            // popped with it.
            //
            // Where a `var` can live depends on what this compiler has a place
            // for. A function's own body has var slots -- its parameters are
            // the first of them. The script body has none: its bindings are
            // properties of the global object, which is what `OpSetGlobal`
            // writes. So a `var` goes to a local only when the enclosing
            // function has already given that name a slot (a parameter, or a
            // `var` at the function's top level); otherwise it goes where a
            // top-level `var` goes, and the VM keeps it for the rest of the
            // script.
            //
            // Recording the name with `add_local_at` instead was tried and is
            // wrong: the slot is a position on the VM stack, and leaving the
            // block pops it, so the binding was gone by the time anything read
            // it -- `var a = 1; { var b = 2; } a + b` answered 1 where the
            // tree-walker answers 3.
            const int slot = resolve_local(name);
            if (var_decl->kind == JsDeclKind::Var && slot == -1) {
                size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(name));
                emit_opcode(OpCode::OpSetGlobal);
                emit_byte(static_cast<uint8_t>(name_idx));
                emit_opcode(OpCode::OpPop);
            } else if (m_scope_depth > 0 && var_decl->kind != JsDeclKind::Var) {
                add_local(name);
            } else if (m_scope_depth > 0) {
                // A block-nested `var` whose function already has the slot.
                add_local_at(name, 0);
            } else {
                size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(name));
                emit_opcode(OpCode::OpSetGlobal);
                emit_byte(static_cast<uint8_t>(name_idx));
                emit_opcode(OpCode::OpPop);
            }
        }
    } else if (auto for_stmt = dynamic_cast<const ForStmt*>(stmt)) {
        if (!for_stmt->per_iteration_names.empty()) {
            throw std::runtime_error("Per-iteration loop bindings are not supported by the bytecode compiler");
        }
        // for (init; test; update) body  ==>  { init; while (test) { body; update; } }
        begin_scope();
        if (for_stmt->init) compile_stmt(for_stmt->init.get());
        size_t loop_start = m_function->chunk.code.size();
        m_loops.push_back({loop_start, true, {}, {}});
        if (for_stmt->test) compile_expr(for_stmt->test.get());
        else emit_opcode(OpCode::OpTrue);
        size_t exit_jump = emit_jump(OpCode::OpJumpIfFalse);
        emit_opcode(OpCode::OpPop);
        begin_scope();
        compile_stmt(for_stmt->body.get());
        // `continue` in a `for` runs the update before the test, so its jumps
        // point at the update's first instruction -- which is where the code
        // cursor is now, before the update is emitted. The patch can only be
        // written once the update *is* emitted, because `patch_back_jump`
        // measures backwards from the jump's own end: a jump whose target is
        // still in the future is the error it refuses.
        const std::vector<size_t> continues = m_loops.back().continues;
        const size_t update_start = m_function->chunk.code.size();
        if (for_stmt->update) {
            compile_expr(for_stmt->update.get());
            emit_opcode(OpCode::OpPop);
        }
        // To the update's first instruction, not to wherever the cursor is
        // now: `continue` runs the update before the next test.
        for (size_t at : continues) patch_forward_jump(at, update_start);
        end_scope();
        // The same order as in `while`: `break` must jump past the back edge,
        // not to it.
        const std::vector<size_t> breaks = std::move(m_loops.back().breaks);
        m_loops.pop_back();
        emit_loop(loop_start);
        for (size_t at : breaks) patch_jump(at);
        patch_jump(exit_jump);
        emit_opcode(OpCode::OpPop);
        end_scope();
    } else if (auto switch_stmt = dynamic_cast<const SwitchStmt*>(stmt)) {
        compile_switch(switch_stmt);
    } else if (auto block_stmt = dynamic_cast<const BlockStmt*>(stmt)) {
        begin_scope();
        for (const auto& s : block_stmt->statements) {
            compile_stmt(s.get());
        }
        end_scope();
    } else if (auto if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
        compile_expr(if_stmt->condition.get());
        size_t then_jump = emit_jump(OpCode::OpJumpIfFalse);
        emit_opcode(OpCode::OpPop);

        compile_stmt(if_stmt->then_branch.get());

        size_t else_jump = emit_jump(OpCode::OpJump);
        patch_jump(then_jump);
        emit_opcode(OpCode::OpPop);

        if (if_stmt->else_branch) {
            compile_stmt(if_stmt->else_branch.get());
        }
        patch_jump(else_jump);
    } else if (auto while_stmt = dynamic_cast<const WhileStmt*>(stmt)) {
        size_t loop_start = m_function->chunk.code.size();
        m_loops.push_back({loop_start, false, {}, {}});
        compile_expr(while_stmt->condition.get());

        size_t exit_jump = emit_jump(OpCode::OpJumpIfFalse);
        emit_opcode(OpCode::OpPop);

        compile_stmt(while_stmt->body.get());
        // A `continue` goes back to the test, which is `loop_start`, so those
        // jumps are resolved against it first. The `break`s are patched only
        // *after* `emit_loop`: patching them before it pointed them at the
        // back edge itself, so `while (true) { ... break; }` jumped into
        // `OpLoop` and went round for ever.
        for (size_t at : m_loops.back().continues) patch_back_jump(at, loop_start);
        const std::vector<size_t> breaks = std::move(m_loops.back().breaks);
        m_loops.pop_back();
        emit_loop(loop_start);
        for (size_t at : breaks) patch_jump(at);

        patch_jump(exit_jump);
        emit_opcode(OpCode::OpPop);
    } else if (auto do_stmt = dynamic_cast<const DoWhileStmt*>(stmt)) {
        // `do body while (test)` (ECMA-262 14.6.2): the body runs first, then
        // the test decides whether to go round again. Two jumps are patched
        // backwards here, which `emit_loop` does not do -- it emits OpLoop for
        // a known start, and both the test and the exit sit after the body.
        //
        //   start: body; test; JumpIfFalse exit; Pop; Jump start; exit: Pop
        //
        // The test's value is popped on both paths: by the OpPop before the
        // loop jump when it is true, and by the OpPop after `exit` when it is
        // false. `OpJumpIfFalse` leaves it on the stack, so the exit path has
        // to discard it.
        size_t loop_start = m_function->chunk.code.size();
        m_loops.push_back({loop_start, true, {}, {}});
        compile_stmt(do_stmt->body.get());
        // `continue` in a `do`/`while` goes to the test, which follows.
        const std::vector<size_t> do_continues = m_loops.back().continues;
        const size_t test_start = m_function->chunk.code.size();
        for (size_t at : do_continues) patch_forward_jump(at, test_start);
        compile_expr(do_stmt->condition.get());
        size_t exit_jump = emit_jump(OpCode::OpJumpIfFalse);
        emit_opcode(OpCode::OpPop);
        // Back to the top: distance from here to `loop_start`, which is what
        // the OpLoop operand means (see `run`, where the offset is *added*).
        emit_opcode(OpCode::OpLoop);
        size_t back = m_function->chunk.code.size() - loop_start + 2;
        if (back > 65535) throw std::runtime_error("Loop body too large (>65535 bytes)");
        emit_byte(static_cast<uint8_t>((back >> 8) & 0xFF));
        emit_byte(static_cast<uint8_t>(back & 0xFF));
        const std::vector<size_t> do_breaks = std::move(m_loops.back().breaks);
        m_loops.pop_back();
        for (size_t at : do_breaks) patch_jump(at);
        patch_jump(exit_jump);
        emit_opcode(OpCode::OpPop);
    } else if (auto break_stmt = dynamic_cast<const BreakStmt*>(stmt)) {
        // ECMA-262 13.8. A labelled `break` is left to the tree-walker: labels
        // reach across statements, which this compiler has no table for, and
        // compiling an unlabelled jump for a labelled `break` would jump to
        // the wrong place -- silently, which is worse than refusing.
        if (!break_stmt->label.empty()) {
            throw std::runtime_error("A labelled break is not supported by the bytecode compiler");
        }
        if (m_loops.empty()) {
            throw std::runtime_error("`break` outside a loop or a switch is not supported by the bytecode compiler");
        }
        // The jump's target is the loop's exit, which is not known yet; the
        // loop patches it when its body has been compiled.
        m_loops.back().breaks.push_back(emit_jump(OpCode::OpJump));
    } else if (auto continue_stmt = dynamic_cast<const ContinueStmt*>(stmt)) {
        if (!continue_stmt->label.empty()) {
            throw std::runtime_error("A labelled continue is not supported by the bytecode compiler");
        }
        // The innermost loop, not the innermost loop-or-switch: a plain
        // `continue` inside a `switch` reaches the enclosing loop (ECMA-262
        // 14.13.4.1). A `switch` pushes a context so that a plain `break`
        // can find the end of the switch, and it is skipped here.
        size_t target = static_cast<size_t>(-1);
        for (size_t i = m_loops.size(); i-- > 0;) {
            if (!m_loops[i].is_switch) { target = i; break; }
        }
        if (target == static_cast<size_t>(-1)) {
            throw std::runtime_error("`continue` outside a loop is not supported by the bytecode compiler");
        }
        // To the loop's test (a `while`) or its update/test (a `for`, a
        // `do`/`while`). Which way that is decides the opcode: OpLoop carries
        // a distance backwards, OpJump one forwards, and the loop fills in
        // whichever it emitted when it knows its target.
        if (m_loops[target].continue_is_forward) {
            m_loops[target].continues.push_back(emit_jump(OpCode::OpJump));
        } else {
            m_loops[target].continues.push_back(m_function->chunk.code.size());
            emit_opcode(OpCode::OpLoop);
            emit_byte(0xFF);
            emit_byte(0xFF);
        }
    } else if (auto ret_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        if (ret_stmt->value) {
            compile_expr(ret_stmt->value.get());
        } else {
            emit_opcode(OpCode::OpUndefined);
        }
        emit_opcode(OpCode::OpReturn);
    } else if (auto fn_decl = dynamic_cast<const FunctionDeclStmt*>(stmt)) {
        const FunctionNode& node = *fn_decl->fn;
        require_simple_function(node);
        const std::vector<std::string> params = node.simple_param_names();
        auto sub_fn = std::make_shared<BytecodeFunction>(node.name, static_cast<int>(params.size()));
        BytecodeCompiler sub_compiler(sub_fn);

        // Parameters are local variables in the function scope
        for (const auto& p : params) {
            sub_compiler.add_local(p);
        }

        sub_compiler.compile(node.body.get());

        // Create a function object holding the compiled bytecode function
        JsValue fn_obj;
        fn_obj.type = JsType::Function;
        fn_obj.set_function_params(params);
        fn_obj.set_function_body(node.body);
        fn_obj.set_function_bytecode(sub_fn);
        m_function->sub_functions.push_back(sub_fn);
        // Embed the compiled BytecodeFunction pointer into properties
        fn_obj.set_property("__fn_ptr__", JsValue::make_number(reinterpret_cast<uintptr_t>(sub_fn.get())));
        // Keep a strong reference in properties so it isn't freed
        fn_obj.set_property("__fn_name__", JsValue::make_string(node.name));

        size_t const_idx = m_function->chunk.add_constant(fn_obj);
        emit_opcode(OpCode::OpConstant);
        emit_byte(static_cast<uint8_t>(const_idx));

        if (m_scope_depth > 0) {
            add_local(node.name);
        } else {
            size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(node.name));
            emit_opcode(OpCode::OpSetGlobal);
            emit_byte(static_cast<uint8_t>(name_idx));
            emit_opcode(OpCode::OpPop);
        }
    } else {
        // A statement this compiler does not understand must never be dropped
        // silently: the program would run with a hole in it.
        throw std::runtime_error("Statement is not supported by the bytecode compiler");
    }
}

void BytecodeCompiler::compile_switch(const SwitchStmt* stmt) {
    // ECMA-262 14.13.4: the discriminant is evaluated exactly once, kept on
    // the stack, and the clause tests run in source order until the first
    // strict equality holds. A test that would never be reached is never
    // evaluated, which matters: clause tests may have side effects. The
    // matched clause's body then runs from that clause to the end of the
    // switch (fall-through) unless a plain `break` leaves it.
    compile_expr(stmt->discriminant.get());
    begin_scope();

    const std::size_t clause_count = stmt->cases.size();
    int default_index = -1;
    for (std::size_t i = 0; i < clause_count; ++i) {
        if (!stmt->cases[i].test) default_index = static_cast<int>(i);
    }

    // Forward jumps that land on each clause's body, filled in when the body
    // is emitted. SIZE_MAX marks a clause nobody jumps to (or an absent one).
    std::vector<size_t> entry_jumps(clause_count, static_cast<size_t>(-1));
    for (std::size_t i = 0; i < clause_count; ++i) {
        const auto& clause = stmt->cases[i];
        if (!clause.test) continue;
        emit_opcode(OpCode::OpDup);            // [d, d]
        compile_expr(clause.test.get());       // [d, d, t]
        emit_opcode(OpCode::OpStrictEqual);    // [d, eq]
        size_t no_match = emit_jump(OpCode::OpJumpIfFalse);
        emit_opcode(OpCode::OpPop);            // the condition, matched path
        emit_opcode(OpCode::OpPop);            // the discriminant
        entry_jumps[i] = emit_jump(OpCode::OpJump);
        patch_jump(no_match);                  // lands on the next test
        emit_opcode(OpCode::OpPop);            // the condition, no-match path
    }

    // No clause matched.
    emit_opcode(OpCode::OpPop);                // the discriminant
    size_t no_clause_exit = static_cast<size_t>(-1);
    if (default_index >= 0) {
        entry_jumps[static_cast<size_t>(default_index)] = emit_jump(OpCode::OpJump);
    } else {
        no_clause_exit = emit_jump(OpCode::OpJump);
    }

    // A switch is not a loop: a plain `break` ends it, and an unlabelled
    // `continue` inside it targets the enclosing loop (ECMA-262 14.13.4.1),
    // so `continue` skips this context.
    m_loops.push_back({0, false, {}, {}, true});
    for (std::size_t i = 0; i < clause_count; ++i) {
        if (entry_jumps[i] != static_cast<size_t>(-1)) patch_jump(entry_jumps[i]);
        for (const auto& s : stmt->cases[i].statements) compile_stmt(s.get());
    }
    const std::vector<size_t> breaks = std::move(m_loops.back().breaks);
    m_loops.pop_back();
    for (size_t at : breaks) patch_jump(at);
    if (no_clause_exit != static_cast<size_t>(-1)) patch_jump(no_clause_exit);
    end_scope();
}

// The VM has no closures, default values, `this` or `arguments`; refuse anything
// that would need them rather than compile it wrongly.
void BytecodeCompiler::require_simple_function(const FunctionNode& node) {
    if (node.is_generator || node.is_async) {
        throw std::runtime_error("Generators and async functions are not supported by the bytecode compiler");
    }
    if (!node.simple_params || node.uses_arguments || node.uses_super || node.uses_new_target) {
        throw std::runtime_error("This function form is not supported by the bytecode compiler");
    }
}

void BytecodeCompiler::compile_expr(const Expr* expr) {
    if (!expr) {
        emit_opcode(OpCode::OpUndefined);
        return;
    }
    js_check_stack();

    if (auto lit = dynamic_cast<const LiteralExpr*>(expr)) {
        switch (lit->value.type) {
            case JsType::Null:      emit_opcode(OpCode::OpNull); break;
            case JsType::Undefined: emit_opcode(OpCode::OpUndefined); break;
            case JsType::Boolean:   emit_opcode(lit->value.bool_val ? OpCode::OpTrue : OpCode::OpFalse); break;
            default:                emit_constant(lit->value); break;
        }
    } else if (auto id = dynamic_cast<const IdentifierExpr*>(expr)) {
        int local_slot = resolve_local(id->name);
        if (local_slot != -1) {
            emit_opcode(OpCode::OpGetLocal);
            emit_byte(static_cast<uint8_t>(local_slot));
        } else {
            size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(id->name));
            emit_opcode(OpCode::OpGetGlobal);
            emit_byte(static_cast<uint8_t>(name_idx));
        }
    } else if (auto assign = dynamic_cast<const AssignExpr*>(expr)) {
        if (assign->pattern) {
            throw std::runtime_error("Destructuring assignment is not supported by the bytecode compiler");
        }
        switch (assign->op) {
            case JsTokenType::Equal:
            case JsTokenType::PlusEqual:
            case JsTokenType::MinusEqual:
            case JsTokenType::StarEqual:
            case JsTokenType::SlashEqual:
            case JsTokenType::PercentEqual:
                break;
            default:
                throw std::runtime_error("This assignment operator is not supported by the bytecode compiler");
        }
        // For `x op= v`: load x, evaluate v, apply the operator, then store.
        // The Set* opcodes leave the stored value on the stack, so this yields
        // the value of the expression as required.
        if (assign->op != JsTokenType::Equal) {
            if (auto id_target = dynamic_cast<const IdentifierExpr*>(assign->target.get())) {
                int local_slot = resolve_local(id_target->name);
                if (local_slot != -1) {
                    emit_opcode(OpCode::OpGetLocal);
                    emit_byte(static_cast<uint8_t>(local_slot));
                } else {
                    size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(id_target->name));
                    emit_opcode(OpCode::OpGetGlobal);
                    emit_byte(static_cast<uint8_t>(name_idx));
                }
            } else if (auto mem_target = dynamic_cast<const MemberExpr*>(assign->target.get())) {
                compile_expr(mem_target->object.get());
                size_t prop_idx = m_function->chunk.add_constant(JsValue::make_string(mem_target->property));
                emit_opcode(OpCode::OpGetProperty);
                emit_byte(static_cast<uint8_t>(prop_idx));
            } else if (auto idx_target = dynamic_cast<const IndexExpr*>(assign->target.get())) {
                compile_expr(idx_target->object.get());
                compile_expr(idx_target->index.get());
                emit_opcode(OpCode::OpGetIndex);
            }
            compile_expr(assign->value.get());
            emit_opcode(compound_opcode(assign->op));
        } else {
            compile_expr(assign->value.get());
        }

        if (auto id_target = dynamic_cast<const IdentifierExpr*>(assign->target.get())) {
            int local_slot = resolve_local(id_target->name);
            if (local_slot != -1) {
                emit_opcode(OpCode::OpSetLocal);
                emit_byte(static_cast<uint8_t>(local_slot));
            } else {
                size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(id_target->name));
                emit_opcode(OpCode::OpSetGlobal);
                emit_byte(static_cast<uint8_t>(name_idx));
            }
        } else if (auto mem_target = dynamic_cast<const MemberExpr*>(assign->target.get())) {
            compile_expr(mem_target->object.get());
            size_t prop_idx = m_function->chunk.add_constant(JsValue::make_string(mem_target->property));
            emit_opcode(OpCode::OpSetProperty);
            emit_byte(static_cast<uint8_t>(prop_idx));
        } else if (auto idx_target = dynamic_cast<const IndexExpr*>(assign->target.get())) {
            compile_expr(idx_target->object.get());
            compile_expr(idx_target->index.get());
            emit_opcode(OpCode::OpSetIndex);
        }
    } else if (auto conditional = dynamic_cast<const ConditionalExpr*>(expr)) {
        // `test ? consequent : alternate` (ECMA-262 13.14). The value of the
        // chosen branch is the value of the expression, so neither branch is
        // popped: `OpJumpIfFalse` leaves the test on the stack, and the
        // OpPop under it discards that test on both paths before a branch
        // pushes its own value.
        compile_expr(conditional->test.get());
        size_t alternate_jump = emit_jump(OpCode::OpJumpIfFalse);
        emit_opcode(OpCode::OpPop);
        compile_expr(conditional->consequent.get());
        size_t end_jump = emit_jump(OpCode::OpJump);
        patch_jump(alternate_jump);
        emit_opcode(OpCode::OpPop);
        compile_expr(conditional->alternate.get());
        patch_jump(end_jump);
    } else if (auto update = dynamic_cast<const UpdateExpr*>(expr)) {
        // `++i` / `i++` (ECMA-262 13.4). The lvalue expression is evaluated a
        // single time; only the resulting value is duplicated, so `a[f()]++`
        // cannot call f() twice.
        //
        //   prefix  : load; +1; store            -> yields new value
        //   postfix : load; DUP; +1; store; POP  -> yields old value
        auto load_target = [&]() {
            if (auto id = dynamic_cast<const IdentifierExpr*>(update->target.get())) {
                int slot = resolve_local(id->name);
                if (slot != -1) {
                    emit_opcode(OpCode::OpGetLocal);
                    emit_byte(static_cast<uint8_t>(slot));
                } else {
                    size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(id->name));
                    emit_opcode(OpCode::OpGetGlobal);
                    emit_byte(static_cast<uint8_t>(name_idx));
                }
            } else if (auto mem = dynamic_cast<const MemberExpr*>(update->target.get())) {
                compile_expr(mem->object.get());
                size_t prop_idx = m_function->chunk.add_constant(JsValue::make_string(mem->property));
                emit_opcode(OpCode::OpGetProperty);
                emit_byte(static_cast<uint8_t>(prop_idx));
            } else if (auto idx = dynamic_cast<const IndexExpr*>(update->target.get())) {
                compile_expr(idx->object.get());
                compile_expr(idx->index.get());
                emit_opcode(OpCode::OpGetIndex);
            }
        };

        load_target();                                  // [old]
        if (!update->prefix) emit_opcode(OpCode::OpDup); // [old, old]
        emit_constant(JsValue::make_number(1));
        emit_opcode(update->op == JsTokenType::PlusPlus ? OpCode::OpAdd : OpCode::OpSubtract);

        // Re-evaluate the target for the store. For identifiers this is exact;
        // for member/index expressions the object expression is re-evaluated,
        // which is only observable if it has side effects.
        if (auto id = dynamic_cast<const IdentifierExpr*>(update->target.get())) {
            int slot = resolve_local(id->name);
            if (slot != -1) {
                emit_opcode(OpCode::OpSetLocal);
                emit_byte(static_cast<uint8_t>(slot));
            } else {
                size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(id->name));
                emit_opcode(OpCode::OpSetGlobal);
                emit_byte(static_cast<uint8_t>(name_idx));
            }
        } else if (auto mem = dynamic_cast<const MemberExpr*>(update->target.get())) {
            compile_expr(mem->object.get());
            size_t prop_idx = m_function->chunk.add_constant(JsValue::make_string(mem->property));
            emit_opcode(OpCode::OpSetProperty);
            emit_byte(static_cast<uint8_t>(prop_idx));
        } else if (auto idx = dynamic_cast<const IndexExpr*>(update->target.get())) {
            compile_expr(idx->object.get());
            compile_expr(idx->index.get());
            emit_opcode(OpCode::OpSetIndex);
        }
        // Postfix must yield the pre-change value; discard the new one.
        if (!update->prefix) emit_opcode(OpCode::OpPop);
    } else if (auto unary = dynamic_cast<const UnaryExpr*>(expr)) {
        // ECMA-262 §13.5.3: `typeof` never throws, not even for an unresolvable
        // reference. The VM answers an undeclared global with `undefined`
        // (OpGetGlobal), which is the type such an operand has, so the
        // operand is compiled the ordinary way and no reference token is
        // needed here. `void` is still refused: nobody has asked for it.
        if (unary->op == JsTokenType::Typeof) {
            compile_expr(unary->operand.get());
            emit_opcode(OpCode::OpTypeOf);
        } else if (unary->op == JsTokenType::Minus || unary->op == JsTokenType::Bang) {
            compile_expr(unary->operand.get());
            if (unary->op == JsTokenType::Minus) {
                emit_opcode(OpCode::OpNegate);
            } else {
                emit_opcode(OpCode::OpNot);
            }
        } else {
            throw std::runtime_error("This unary operator is not supported by the bytecode compiler");
        }
    } else if (auto binary = dynamic_cast<const BinaryExpr*>(expr)) {
        if (binary->op == JsTokenType::Power || binary->op == JsTokenType::Nullish) {
            throw std::runtime_error("Exponentiation/nullish coalescing compiled via AST interpreter");
        }
        // Short-circuit logic
        if (binary->op == JsTokenType::PipePipe) {
            compile_expr(binary->left.get());
            size_t else_jump = emit_jump(OpCode::OpJumpIfFalse);
            size_t end_jump = emit_jump(OpCode::OpJump);
            patch_jump(else_jump);
            emit_opcode(OpCode::OpPop);
            compile_expr(binary->right.get());
            patch_jump(end_jump);
            return;
        }
        if (binary->op == JsTokenType::AmpAmp) {
            compile_expr(binary->left.get());
            size_t end_jump = emit_jump(OpCode::OpJumpIfFalse);
            emit_opcode(OpCode::OpPop);
            compile_expr(binary->right.get());
            patch_jump(end_jump);
            return;
        }

        compile_expr(binary->left.get());
        compile_expr(binary->right.get());

        switch (binary->op) {
            case JsTokenType::Plus:         emit_opcode(OpCode::OpAdd); break;
            case JsTokenType::Minus:        emit_opcode(OpCode::OpSubtract); break;
            case JsTokenType::Star:         emit_opcode(OpCode::OpMultiply); break;
            case JsTokenType::Slash:        emit_opcode(OpCode::OpDivide); break;
            case JsTokenType::Percent:      emit_opcode(OpCode::OpModulo); break;
            case JsTokenType::EqualEqual:   emit_opcode(OpCode::OpEqual); break;
            case JsTokenType::NotEqual:     emit_opcode(OpCode::OpNotEqual); break;
            case JsTokenType::StrictEqual:  emit_opcode(OpCode::OpStrictEqual); break;
            case JsTokenType::StrictNotEqual: emit_opcode(OpCode::OpStrictNotEqual); break;
            case JsTokenType::Greater:      emit_opcode(OpCode::OpGreater); break;
            case JsTokenType::GreaterEqual: emit_opcode(OpCode::OpGreaterEqual); break;
            case JsTokenType::Less:         emit_opcode(OpCode::OpLess); break;
            case JsTokenType::LessEqual:    emit_opcode(OpCode::OpLessEqual); break;
            default:
                throw std::runtime_error("This binary operator is not supported by the bytecode compiler");
        }
    } else if (auto member = dynamic_cast<const MemberExpr*>(expr)) {
        if (member->optional) throw std::runtime_error("Optional chaining is not supported by the bytecode compiler");
        compile_expr(member->object.get());
        size_t prop_idx = m_function->chunk.add_constant(JsValue::make_string(member->property));
        emit_opcode(OpCode::OpGetProperty);
        emit_byte(static_cast<uint8_t>(prop_idx));
    } else if (auto idx_expr = dynamic_cast<const IndexExpr*>(expr)) {
        compile_expr(idx_expr->object.get());
        compile_expr(idx_expr->index.get());
        emit_opcode(OpCode::OpGetIndex);
    } else if (auto arr = dynamic_cast<const ArrayLiteralExpr*>(expr)) {
        for (const auto& elem : arr->elements) {
            if (!elem || elem->kind == NodeKind::Spread) {
                throw std::runtime_error("Array holes and spread are not supported by the bytecode compiler");
            }
            compile_expr(elem.get());
        }
        emit_opcode(OpCode::OpNewArray);
        emit_byte(static_cast<uint8_t>(arr->elements.size()));
    } else if (auto obj_lit = dynamic_cast<const ObjectLiteralExpr*>(expr)) {
        const bool has_spread = std::any_of(obj_lit->properties.begin(), obj_lit->properties.end(),
            [](const ObjectProperty& p) { return p.kind == ObjectProperty::Kind::Spread; });
        if (has_spread) {
            // ECMA-262 13.2.5: the object is built in source order, with each
            // spread evaluated at its position. One empty object is pushed and
            // every property defines onto it, which is why a spread may call a
            // function and see the keys written so far. Only plain `key: value`
            // pairs and spreads are supported here; computed keys, methods,
            // accessors and `__proto__` are refused as in the simple form.
            // Spreading null or undefined copies nothing (§8.6.9 via
            // CopyDataProperties), which is why it needs no branch of its own.
            emit_opcode(OpCode::OpNewObject);
            emit_byte(0);
            for (const auto& prop : obj_lit->properties) {
                if (prop.kind == ObjectProperty::Kind::Spread) {
                    compile_expr(prop.value.get());
                    emit_opcode(OpCode::OpObjectSpread);
                    continue;
                }
                if (prop.kind != ObjectProperty::Kind::Init || prop.computed_key ||
                    prop.is_method) {
                    throw std::runtime_error("This object literal form is not supported by the bytecode compiler");
                }
                size_t key_idx = m_function->chunk.add_constant(JsValue::make_string(prop.key));
                emit_opcode(OpCode::OpConstant);
                emit_byte(static_cast<uint8_t>(key_idx));
                compile_expr(prop.value.get());
                emit_opcode(OpCode::OpObjectDefine);
            }
        } else {
            if (!obj_lit->is_simple()) {
                throw std::runtime_error("This object literal form is not supported by the bytecode compiler");
            }
            for (const auto& prop : obj_lit->properties) {
                size_t key_idx = m_function->chunk.add_constant(JsValue::make_string(prop.key));
                emit_opcode(OpCode::OpConstant);
                emit_byte(static_cast<uint8_t>(key_idx));
                compile_expr(prop.value.get());
            }
            emit_opcode(OpCode::OpNewObject);
            emit_byte(static_cast<uint8_t>(obj_lit->properties.size()));
        }
    } else if (auto call = dynamic_cast<const CallExpr*>(expr)) {
        if (call->optional) throw std::runtime_error("Optional calls are not supported by the bytecode compiler");
        compile_expr(call->callee.get());
        for (const auto& arg : call->args) {
            if (arg->kind == NodeKind::Spread) {
                throw std::runtime_error("Spread arguments are not supported by the bytecode compiler");
            }
            compile_expr(arg.get());
        }
        emit_opcode(OpCode::OpCall);
        emit_byte(static_cast<uint8_t>(call->args.size()));
    } else if (auto new_expr = dynamic_cast<const NewExpr*>(expr)) {
        compile_expr(new_expr->callee.get());
        for (const auto& arg : new_expr->args) {
            compile_expr(arg.get());
        }
        emit_opcode(OpCode::OpCall);
        emit_byte(static_cast<uint8_t>(new_expr->args.size()));
    } else if (dynamic_cast<const YieldExpr*>(expr)) {
        throw std::runtime_error("Yield compiled via AST interpreter");
    } else if (auto fn_expr = dynamic_cast<const FunctionExpr*>(expr)) {
        const FunctionNode& node = *fn_expr->fn;
        require_simple_function(node);
        const std::vector<std::string> params = node.simple_param_names();
        auto sub_fn = std::make_shared<BytecodeFunction>(node.name.empty() ? "__anon__" : node.name, static_cast<int>(params.size()));
        BytecodeCompiler sub_compiler(sub_fn);

        for (const auto& p : params) {
            sub_compiler.add_local(p);
        }

        sub_compiler.compile(node.body.get());

        JsValue fn_obj;
        fn_obj.type = JsType::Function;
        fn_obj.set_function_params(params);
        fn_obj.set_function_body(node.body);
        fn_obj.set_function_bytecode(sub_fn);
        m_function->sub_functions.push_back(sub_fn);
        fn_obj.set_property("__fn_ptr__", JsValue::make_number(reinterpret_cast<uintptr_t>(sub_fn.get())));
        fn_obj.set_property("__fn_name__", JsValue::make_string(node.name));

        size_t const_idx = m_function->chunk.add_constant(fn_obj);
        emit_opcode(OpCode::OpConstant);
        emit_byte(static_cast<uint8_t>(const_idx));
    } else if (auto tle = dynamic_cast<const TemplateLiteralExpr*>(expr)) {
        size_t empty_idx = m_function->chunk.add_constant(JsValue::make_string(""));
        emit_opcode(OpCode::OpConstant);
        emit_byte(static_cast<uint8_t>(empty_idx));
        for (const auto& part : tle->parts) {
            compile_expr(part.get());
            emit_opcode(OpCode::OpAdd);
        }
    } else if (auto tte = dynamic_cast<const TaggedTemplateExpr*>(expr)) {
        // ECMA-262 13.2.8: the tag runs first, then the substitutions are
        // evaluated left to right, and the tag is called with the template
        // object as its first argument. The template object itself is built
        // fresh on every evaluation, exactly as the tree-walker does — neither
        // path caches per call site — and `raw` is defined hidden, as
        // eval_tagged_template does. (Neither path freezes the arrays today;
        // that predates this phase and stays as it is.)
        //
        // Stack: the tag, then the strings array with `raw` hidden, then the
        // substitution values — the shape OpCall reads backwards.
        compile_expr(tte->tag.get());
        for (const auto& s : tte->cooked) {
            size_t idx = m_function->chunk.add_constant(JsValue::make_string(s));
            emit_opcode(OpCode::OpConstant);
            emit_byte(static_cast<uint8_t>(idx));
        }
        emit_opcode(OpCode::OpNewArray);
        emit_byte(static_cast<uint8_t>(tte->cooked.size()));
        for (const auto& s : tte->raw) {
            size_t idx = m_function->chunk.add_constant(JsValue::make_string(s));
            emit_opcode(OpCode::OpConstant);
            emit_byte(static_cast<uint8_t>(idx));
        }
        emit_opcode(OpCode::OpNewArray);
        emit_byte(static_cast<uint8_t>(tte->raw.size()));
        emit_opcode(OpCode::OpDefineTemplateRaw);
        for (const auto& e : tte->expressions) {
            compile_expr(e.get());
        }
        emit_opcode(OpCode::OpCall);
        emit_byte(static_cast<uint8_t>(tte->expressions.size() + 1));
    } else {
        throw std::runtime_error("Unsupported expression in bytecode compiler");
    }
}

} // namespace litebird
