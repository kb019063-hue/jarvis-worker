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
    void compile_expr(const Expr* expr);

    void begin_scope();
    void end_scope();

    void add_local(const std::string& name);
    int resolve_local(const std::string& name);

    size_t emit_byte(uint8_t byte);
    size_t emit_opcode(OpCode op);
    size_t emit_constant(JsValue value);
    size_t emit_jump(OpCode op);
    void patch_jump(size_t offset);
    void emit_loop(size_t loop_start);

    std::shared_ptr<BytecodeFunction> m_function;
    std::vector<Local> m_locals;
    int m_scope_depth{0};
};

} // namespace litebird
