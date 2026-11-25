// Phase 402, finding 402: bytecode disassembly is terminal-facing text.
TEST(bytecode_disassembly_filters_terminal_controls_without_changing_values) {
    using namespace litebird;
    const std::string bad = "A\xc4\x9b\x1b]0;OWN\x07\r\xc2\x9bZ";
    const std::string clean = "A\xc4\x9b\xef\xbf\xbd]0;OWN\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbdZ";
    Chunk chunk;
    const auto index = chunk.add_constant(JsValue::make_string(bad));
    for (const auto op : {OpCode::OpConstant, OpCode::OpGetGlobal, OpCode::OpSetGlobal,
                          OpCode::OpGetProperty, OpCode::OpSetProperty}) {
        chunk.write_opcode(op, 1);
        chunk.write(static_cast<uint8_t>(index), 1);
    }
    std::ostringstream output;
    chunk.disassemble(output, bad);
    const auto text = output.str();
    assert(text.starts_with("== Disassembly for " + clean + " ==\n"));
    size_t found = 0;
    for (size_t at = 0; (at = text.find("'" + clean + "'", at)) != std::string::npos; ++at) ++found;
    assert(found == 5);
    assert(text.find('\x1b') == std::string::npos);
    assert(chunk.constants[index].to_string() == bad);

    // The public name is a byte string, not necessarily valid UTF-8.
    std::ostringstream malformed_name;
    chunk.disassemble(malformed_name, std::string("ok\t\n\x9b\xc0\x9b", 7));
    assert(malformed_name.str().starts_with(
        "== Disassembly for ok\t\n\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd ==\n"));

    // The independently callable instruction printer has the same boundary.
    std::ostringstream instruction;
    assert(chunk.disassemble_instruction(instruction, 0) == 2);
    assert(instruction.str().find("'" + clean + "'") != std::string::npos);

    // JavaScript's lone surrogate is rendered as one replacement scalar.
    Chunk surrogate;
    surrogate.add_constant(JsValue::make_string("\xed\xa0\x80"));
    surrogate.write_opcode(OpCode::OpConstant, 1);
    surrogate.write(0, 1);
    std::ostringstream surrogate_output;
    surrogate.disassemble(surrogate_output, "ordinary");
    assert(surrogate_output.str().find("'\xef\xbf\xbd'") != std::string::npos);
    assert(surrogate.constants[0].to_string() == "\xed\xa0\x80");

    // The executable bytecode retains the original string after disassembly.
    JsInterpreter js(nullptr);
    auto fn = js.compile_to_bytecode("'A\\x1b]0;OWN\\x07\\r\\u009bZ'");
    std::ostringstream compiled_output;
    fn->chunk.disassemble(compiled_output, "script");
    VM vm;
    assert(vm.interpret(fn).to_string() == "A\x1b]0;OWN\x07\r\xc2\x9bZ");
}
