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
    OpDup,          // push a copy of the top of stack (postfix ++/--)

    // Variables & Scope
    OpGetLocal,
    OpSetLocal,
    OpGetGlobal,
    OpSetGlobal,

    // Object properties & indexing
    OpGetProperty,
    OpSetProperty,
    OpGetIndex,
    OpSetIndex,
    OpNewArray,
    OpNewObject,
    // The object literal with a spread, built left to right on the stack:
    // one empty object is pushed first, and every property defines onto it.
    OpObjectDefine, // define a `key: value` pair on the object below the operands
    OpObjectSpread, // Object.keys-order copy of a source's own enumerable properties
    OpDefineTemplateRaw, // the tagged template's strings.raw, hidden as the tree-walker makes it

    // Array destructuring: values come one at a time from a cursor the VM
    // holds on a stack of its own, so defaults never see the next pull and
    // `[a, ...rest]` drains exactly the remainder.
    OpIteratorInit,     // pop the source, push it as the open cursor
    OpIteratorNext,     // pull one value (undefined once exhausted)
    OpIteratorDrain,    // drain the rest into a new array and push it
    OpIteratorClose,    // the normal way out: report a broken `return`, pop it
    OpIsUndefined,      // push whether the top of stack is undefined

    // Arithmetic
    OpAdd,
    OpSubtract,
    OpMultiply,
    OpDivide,
    OpModulo,

    // Logic & Comparison
    OpEqual,
    OpNotEqual,
    OpStrictEqual,
    OpStrictNotEqual,
    OpGreater,
    OpGreaterEqual,
    OpLess,
    OpLessEqual,
    OpNot,
    OpNegate,
    OpTypeOf,

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
        case OpCode::OpDup:          return "OP_DUP";
        case OpCode::OpGetLocal:     return "OP_GET_LOCAL";
        case OpCode::OpSetLocal:     return "OP_SET_LOCAL";
        case OpCode::OpGetGlobal:    return "OP_GET_GLOBAL";
        case OpCode::OpSetGlobal:    return "OP_SET_GLOBAL";
        case OpCode::OpGetProperty:  return "OP_GET_PROPERTY";
        case OpCode::OpSetProperty:  return "OP_SET_PROPERTY";
        case OpCode::OpGetIndex:     return "OP_GET_INDEX";
        case OpCode::OpSetIndex:     return "OP_SET_INDEX";
        case OpCode::OpNewArray:     return "OP_NEW_ARRAY";
        case OpCode::OpNewObject:    return "OP_NEW_OBJECT";
    case OpCode::OpObjectDefine: return "OP_OBJECT_DEFINE";
    case OpCode::OpObjectSpread: return "OP_OBJECT_SPREAD";
    case OpCode::OpDefineTemplateRaw: return "OP_DEFINE_TEMPLATE_RAW";
    case OpCode::OpIteratorInit:     return "OP_ITERATOR_INIT";
    case OpCode::OpIteratorNext:     return "OP_ITERATOR_NEXT";
    case OpCode::OpIteratorDrain:    return "OP_ITERATOR_DRAIN";
    case OpCode::OpIteratorClose:    return "OP_ITERATOR_CLOSE";
    case OpCode::OpIsUndefined:      return "OP_IS_UNDEFINED";
        case OpCode::OpAdd:          return "OP_ADD";
        case OpCode::OpSubtract:     return "OP_SUBTRACT";
        case OpCode::OpMultiply:     return "OP_MULTIPLY";
        case OpCode::OpDivide:       return "OP_DIVIDE";
        case OpCode::OpModulo:       return "OP_MODULO";
        case OpCode::OpEqual:        return "OP_EQUAL";
        case OpCode::OpNotEqual:     return "OP_NOT_EQUAL";
        case OpCode::OpStrictEqual:  return "OP_STRICT_EQUAL";
        case OpCode::OpStrictNotEqual: return "OP_STRICT_NOT_EQUAL";
        case OpCode::OpGreater:      return "OP_GREATER";
        case OpCode::OpGreaterEqual: return "OP_GREATER_EQUAL";
        case OpCode::OpLess:         return "OP_LESS";
        case OpCode::OpLessEqual:    return "OP_LESS_EQUAL";
        case OpCode::OpNot:          return "OP_NOT";
        case OpCode::OpNegate:       return "OP_NEGATE";
        case OpCode::OpTypeOf:       return "OP_TYPE_OF";
        case OpCode::OpJump:         return "OP_JUMP";
        case OpCode::OpJumpIfFalse:  return "OP_JUMP_IF_FALSE";
        case OpCode::OpLoop:         return "OP_LOOP";
        case OpCode::OpCall:         return "OP_CALL";
        case OpCode::OpReturn:       return "OP_RETURN";
    }
    return "UNKNOWN_OPCODE";
}

} // namespace litebird
