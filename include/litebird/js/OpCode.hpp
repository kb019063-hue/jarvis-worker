#pragma once

#include <cstdint>
#include <string_view>

namespace litebird {

enum class OpCode : uint8_t {
    OpConstant,
    OpNull,
    OpUndefined,
    OpTrue,
    OpFalse,
    OpPop,

    // Variables & Scope
    OpGetLocal,
    OpSetLocal,
    OpGetGlobal,
    OpSetGlobal,

    // Object properties
    OpGetProperty,
    OpSetProperty,

    // Arithmetic
    OpAdd,
    OpSubtract,
    OpMultiply,
    OpDivide,
    OpModulo,

    // Logic & Comparison
    OpEqual,
    OpNotEqual,
    OpGreater,
    OpGreaterEqual,
    OpLess,
    OpLessEqual,
    OpNot,
    OpNegate,

    // Control Flow
    OpJump,
    OpJumpIfFalse,
    OpLoop,

    // Functions
    OpCall,
    OpReturn
};

inline std::string_view opcode_name(OpCode op) {
    switch (op) {
        case OpCode::OpConstant:     return "OP_CONSTANT";
        case OpCode::OpNull:         return "OP_NULL";
        case OpCode::OpUndefined:    return "OP_UNDEFINED";
        case OpCode::OpTrue:         return "OP_TRUE";
        case OpCode::OpFalse:        return "OP_FALSE";
        case OpCode::OpPop:          return "OP_POP";
        case OpCode::OpGetLocal:     return "OP_GET_LOCAL";
        case OpCode::OpSetLocal:     return "OP_SET_LOCAL";
        case OpCode::OpGetGlobal:    return "OP_GET_GLOBAL";
        case OpCode::OpSetGlobal:    return "OP_SET_GLOBAL";
        case OpCode::OpGetProperty:  return "OP_GET_PROPERTY";
        case OpCode::OpSetProperty:  return "OP_SET_PROPERTY";
        case OpCode::OpAdd:          return "OP_ADD";
        case OpCode::OpSubtract:     return "OP_SUBTRACT";
        case OpCode::OpMultiply:     return "OP_MULTIPLY";
        case OpCode::OpDivide:       return "OP_DIVIDE";
        case OpCode::OpModulo:       return "OP_MODULO";
        case OpCode::OpEqual:        return "OP_EQUAL";
        case OpCode::OpNotEqual:     return "OP_NOT_EQUAL";
        case OpCode::OpGreater:      return "OP_GREATER";
        case OpCode::OpGreaterEqual: return "OP_GREATER_EQUAL";
        case OpCode::OpLess:         return "OP_LESS";
        case OpCode::OpLessEqual:    return "OP_LESS_EQUAL";
        case OpCode::OpNot:          return "OP_NOT";
        case OpCode::OpNegate:       return "OP_NEGATE";
        case OpCode::OpJump:         return "OP_JUMP";
        case OpCode::OpJumpIfFalse:  return "OP_JUMP_IF_FALSE";
        case OpCode::OpLoop:         return "OP_LOOP";
        case OpCode::OpCall:         return "OP_CALL";
        case OpCode::OpReturn:       return "OP_RETURN";
    }
    return "UNKNOWN_OPCODE";
}

} // namespace litebird
