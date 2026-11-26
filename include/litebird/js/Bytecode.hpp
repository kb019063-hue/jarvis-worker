#pragma once

#include "litebird/js/OpCode.hpp"
#include "litebird/js/JsValue.hpp"
#include <vector>
#include <string>
#include <memory>
#include <iostream>

namespace litebird {

class Chunk {
public:
    std::vector<uint8_t> code;
    std::vector<JsValue> constants;
    std::vector<int> lines;

    size_t write(uint8_t byte, int line = 0);
    size_t write_opcode(OpCode op, int line = 0);
    size_t add_constant(JsValue value);

    void disassemble(std::ostream& os, const std::string& name) const;
    size_t disassemble_instruction(std::ostream& os, size_t offset) const;
};

struct BytecodeFunction {
    std::string name;
    int arity{0};
    Chunk chunk;
    // Script var declarations exist before any statement runs.
    std::vector<std::string> global_vars;
    std::vector<std::shared_ptr<BytecodeFunction>> sub_functions;

    BytecodeFunction(std::string n = "", int a = 0)
        : name(std::move(n)), arity(a) {}
};

} // namespace litebird
