#include "litebird/js/Bytecode.hpp"
#include "litebird/common/TerminalText.hpp"
#include "litebird/js/JsOps.hpp"
#include <iomanip>

namespace litebird {

size_t Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    lines.push_back(line);
    return code.size() - 1;
}

size_t Chunk::write_opcode(OpCode op, int line) {
    return write(static_cast<uint8_t>(op), line);
}

size_t Chunk::add_constant(JsValue value) {
    // Deduplicate simple constants
    for (size_t i = 0; i < constants.size(); ++i) {
        if (constants[i].type == value.type) {
            if (value.type == JsType::Number && constants[i].number_val == value.number_val) return i;
            if (value.type == JsType::String && constants[i].string_val == value.string_val) return i;
        }
    }
    constants.push_back(std::move(value));
    return constants.size() - 1;
}

void Chunk::disassemble(std::ostream& os, const std::string& name) const {
    // This is a human-readable dump: callers and bytecode may supply text
    // containing terminal instructions. Leave the underlying values intact.
    os << "== Disassembly for " << terminal_safe(name) << " ==\n";
    size_t offset = 0;
    while (offset < code.size()) {
        offset = disassemble_instruction(os, offset);
    }
    os << "================================\n";
}

size_t Chunk::disassemble_instruction(std::ostream& os, size_t offset) const {
    os << std::setfill('0') << std::setw(4) << offset << " ";

    if (offset > 0 && lines[offset] == lines[offset - 1]) {
        os << "   | ";
    } else {
        os << std::setfill(' ') << std::setw(4) << lines[offset] << " ";
    }

    uint8_t instruction = code[offset];
    auto op = static_cast<OpCode>(instruction);
    os << std::setfill(' ') << std::left << std::setw(18) << opcode_name(op) << std::right;

    switch (op) {
        case OpCode::OpConstant:
        case OpCode::OpGetGlobal:
        case OpCode::OpSetGlobal:
        case OpCode::OpGetProperty:
        case OpCode::OpSetProperty: {
            uint8_t const_idx = code[offset + 1];
            os << " '" << terminal_safe(js_output_text(constants[const_idx].to_string()))
               << "' (idx=" << (int)const_idx << ")\n";
            return offset + 2;
        }
        case OpCode::OpGetLocal:
        case OpCode::OpSetLocal: {
            uint8_t slot = code[offset + 1];
            os << " slot=" << (int)slot << "\n";
            return offset + 2;
        }
        case OpCode::OpJump:
        case OpCode::OpTry:
        case OpCode::OpJumpIfFalse: {
            uint16_t jump = (static_cast<uint16_t>(code[offset + 1]) << 8) | code[offset + 2];
            os << " -> " << (offset + 3 + jump) << "\n";
            return offset + 3;
        }
        case OpCode::OpLoop: {
            uint16_t jump = (static_cast<uint16_t>(code[offset + 1]) << 8) | code[offset + 2];
            os << " -> " << (offset + 3 - jump) << "\n";
            return offset + 3;
        }
        case OpCode::OpCall: {
            uint8_t arg_count = code[offset + 1];
            os << " args=" << (int)arg_count << "\n";
            return offset + 2;
        }
        case OpCode::OpNewArray: {
            uint8_t count = code[offset + 1];
            os << " count=" << (int)count << "\n";
            return offset + 2;
        }
        case OpCode::OpNewObject: {
            uint8_t count = code[offset + 1];
            os << " entries=" << (int)count << "\n";
            return offset + 2;
        }
        default:
            os << "\n";
            return offset + 1;
    }
}

} // namespace litebird
