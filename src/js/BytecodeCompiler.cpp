#include "litebird/js/BytecodeCompiler.hpp"
#include <stdexcept>

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
    if (m_locals.size() >= 256) {
        throw std::runtime_error("Too many local variables in scope (>256)");
    }
    m_locals.push_back({name, m_scope_depth});
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
    if (dynamic_cast<const SwitchStmt*>(stmt) || dynamic_cast<const BreakStmt*>(stmt)) {
        throw std::runtime_error("Switch/break compiled via AST interpreter");
    }

    if (auto expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        compile_expr(expr_stmt->expression.get());
        emit_opcode(OpCode::OpPop);
    } else if (auto var_decl = dynamic_cast<const VarDeclStmt*>(stmt)) {
        if (var_decl->kind == JsDeclKind::Var) {
            throw std::runtime_error("Function-scoped var compiled via AST interpreter");
        }
        if (var_decl->initializer) {
            compile_expr(var_decl->initializer.get());
        } else {
            emit_opcode(OpCode::OpUndefined);
        }

        if (m_scope_depth > 0) {
            add_local(var_decl->name);
        } else {
            size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(var_decl->name));
            emit_opcode(OpCode::OpSetGlobal);
            emit_byte(static_cast<uint8_t>(name_idx));
            emit_opcode(OpCode::OpPop);
        }
    } else if (dynamic_cast<const ArrayDestructuringDeclStmt*>(stmt) || dynamic_cast<const ObjectDestructuringDeclStmt*>(stmt) || dynamic_cast<const ClassDeclStmt*>(stmt)) {
        throw std::runtime_error("Falling back to AST interpreter");
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
        compile_expr(while_stmt->condition.get());

        size_t exit_jump = emit_jump(OpCode::OpJumpIfFalse);
        emit_opcode(OpCode::OpPop);

        compile_stmt(while_stmt->body.get());
        emit_loop(loop_start);

        patch_jump(exit_jump);
        emit_opcode(OpCode::OpPop);
    } else if (auto ret_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        if (ret_stmt->value) {
            compile_expr(ret_stmt->value.get());
        } else {
            emit_opcode(OpCode::OpUndefined);
        }
        emit_opcode(OpCode::OpReturn);
    } else if (dynamic_cast<const ForOfStmt*>(stmt)) {
        throw std::runtime_error("ForOf compiled via AST interpreter");
    } else if (auto fn_decl = dynamic_cast<const FunctionDeclStmt*>(stmt)) {
        if (fn_decl->is_generator) {
            throw std::runtime_error("Generator compiled via AST interpreter");
        }
        auto sub_fn = std::make_shared<BytecodeFunction>(fn_decl->name, static_cast<int>(fn_decl->params.size()));
        BytecodeCompiler sub_compiler(sub_fn);

        // Parameters are local variables in the function scope
        for (const auto& p : fn_decl->params) {
            sub_compiler.add_local(p);
        }

        sub_compiler.compile(fn_decl->body.get());

        // Create a function object holding the compiled bytecode function
        JsValue fn_obj;
        fn_obj.type = JsType::Function;
        fn_obj.set_function_params(fn_decl->params);
        fn_obj.set_function_body(fn_decl->body);
        fn_obj.set_function_bytecode(sub_fn);
        m_function->sub_functions.push_back(sub_fn);
        // Embed the compiled BytecodeFunction pointer into properties
        fn_obj.set_property("__fn_ptr__", JsValue::make_number(reinterpret_cast<uintptr_t>(sub_fn.get())));
        // Keep a strong reference in properties so it isn't freed
        fn_obj.set_property("__fn_name__", JsValue::make_string(fn_decl->name));

        size_t const_idx = m_function->chunk.add_constant(fn_obj);
        emit_opcode(OpCode::OpConstant);
        emit_byte(static_cast<uint8_t>(const_idx));

        if (m_scope_depth > 0) {
            add_local(fn_decl->name);
        } else {
            size_t name_idx = m_function->chunk.add_constant(JsValue::make_string(fn_decl->name));
            emit_opcode(OpCode::OpSetGlobal);
            emit_byte(static_cast<uint8_t>(name_idx));
            emit_opcode(OpCode::OpPop);
        }
    }
}

void BytecodeCompiler::compile_expr(const Expr* expr) {
    if (!expr) {
        emit_opcode(OpCode::OpUndefined);
        return;
    }

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
        if (unary->op == JsTokenType::Typeof) {
            throw std::runtime_error("typeof compiled via AST interpreter");
        }
        compile_expr(unary->operand.get());
        if (unary->op == JsTokenType::Minus) {
            emit_opcode(OpCode::OpNegate);
        } else if (unary->op == JsTokenType::Bang) {
            emit_opcode(OpCode::OpNot);
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
            default: break;
        }
    } else if (auto member = dynamic_cast<const MemberExpr*>(expr)) {
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
            compile_expr(elem.get());
        }
        emit_opcode(OpCode::OpNewArray);
        emit_byte(static_cast<uint8_t>(arr->elements.size()));
    } else if (auto obj_lit = dynamic_cast<const ObjectLiteralExpr*>(expr)) {
        for (const auto& [k, v] : obj_lit->entries) {
            size_t key_idx = m_function->chunk.add_constant(JsValue::make_string(k));
            emit_opcode(OpCode::OpConstant);
            emit_byte(static_cast<uint8_t>(key_idx));
            compile_expr(v.get());
        }
        emit_opcode(OpCode::OpNewObject);
        emit_byte(static_cast<uint8_t>(obj_lit->entries.size()));
    } else if (auto call = dynamic_cast<const CallExpr*>(expr)) {
        compile_expr(call->callee.get());
        for (const auto& arg : call->args) {
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
        if (fn_expr->is_generator) {
            throw std::runtime_error("Generator compiled via AST interpreter");
        }
        auto sub_fn = std::make_shared<BytecodeFunction>(fn_expr->name.empty() ? "__anon__" : fn_expr->name, static_cast<int>(fn_expr->params.size()));
        BytecodeCompiler sub_compiler(sub_fn);

        for (const auto& p : fn_expr->params) {
            sub_compiler.add_local(p);
        }

        sub_compiler.compile(fn_expr->body.get());

        JsValue fn_obj;
        fn_obj.type = JsType::Function;
        fn_obj.set_function_params(fn_expr->params);
        fn_obj.set_function_body(fn_expr->body);
        fn_obj.set_function_bytecode(sub_fn);
        m_function->sub_functions.push_back(sub_fn);
        fn_obj.set_property("__fn_ptr__", JsValue::make_number(reinterpret_cast<uintptr_t>(sub_fn.get())));
        fn_obj.set_property("__fn_name__", JsValue::make_string(fn_expr->name));

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
    } else {
        throw std::runtime_error("Unsupported expression in bytecode compiler");
    }
}

} // namespace litebird
