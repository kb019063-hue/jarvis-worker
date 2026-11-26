// Phase 440, finding 440: the bytecode VM's undeclared read throws.
//
// ECMA-262 §6.2.5.5: GetValue on an unresolvable Reference is a thrown
// `ReferenceError`. `OpGetGlobal` pushed `undefined` instead, so on the VM
// `missingName` answered `undefined` and `'p' + missingName` answered
// `'pundefined'` -- a wrong answer where the tree-walker, and Node, raise an
// error. The one place the quiet answer is right is the operand of `typeof`,
// which §13.5.3 forbids from throwing, and then only for a *bare*
// unresolvable identifier: `typeof (missingName + 1)` and
// `typeof missingName.x` both throw.
//
// Every expected answer below is Node v22.22.3's.
TEST(an_undeclared_read_throws_in_the_vm) {
    using namespace litebird;

    // The VM's answer, or "ReferenceError" if it threw one. `js_throw_error`
    // carries the error's own text in the exception, so the name is the run
    // before the first colon.
    auto vm_answer = [](const std::string& source) -> std::string {
        try {
            JsInterpreter js(nullptr);
            VM vm;
            return vm.interpret(js.compile_to_bytecode(source)).to_string();
        } catch (const JsThrow& e) {
            const std::string what = e.what();
            const std::string::size_type colon = what.find(':');
            return colon == std::string::npos ? what : what.substr(0, colon);
        }
    };

    // The tree-walker does not throw out of `eval`: it reports the uncaught
    // error on stderr and answers `undefined`. So its answer is the pair
    // (value, did it report a ReferenceError), and the diagnostic is captured
    // rather than printed.
    auto tree_answer = [](const std::string& source, bool& reported) -> std::string {
        std::ostringstream diagnostic;
        auto* previous = std::cerr.rdbuf(diagnostic.rdbuf());
        std::string value;
        try {
            JsInterpreter js(nullptr);
            value = js.eval(source).to_string();
        } catch (const std::exception& e) {
            value = e.what();
        }
        std::cerr.rdbuf(previous);
        reported = diagnostic.str().find("ReferenceError") != std::string::npos;
        return value;
    };

    // An ordinary read of a name that is not there is a ReferenceError, and it
    // stays one inside a larger expression -- which is where the old answer
    // was not an error but a corrupted result (`'p' + missingName` answered
    // `'pundefined'`).
    assert(vm_answer("missingName;") == "ReferenceError");
    assert(vm_answer("'p' + missingName;") == "ReferenceError");
    assert(vm_answer("missingName + 1;") == "ReferenceError");
    // A read that is the target of a compound assignment, and of an update, is
    // the same read.
    assert(vm_answer("let y = 1; y += missingName;") == "ReferenceError");
    assert(vm_answer("missingName += 1;") == "ReferenceError");
    assert(vm_answer("missingName++;") == "ReferenceError");
    assert(vm_answer("++missingName;") == "ReferenceError");

    // `typeof` is the exception, but only for a bare identifier. The value is
    // the *string* "undefined", not the value undefined.
    assert(vm_answer("typeof missingName;") == "undefined");
    // Nested inside a larger operand it is an ordinary read again, and throws.
    assert(vm_answer("typeof (missingName + 1);") == "ReferenceError");
    // `typeof missingName.x` throws too: `missingName` is read before the
    // property access can be quiet about anything.
    assert(vm_answer("typeof missingName.x;") == "ReferenceError");

    // A name that *is* declared still reads, on both paths. This is the half
    // that a fix throwing on every global would break.
    assert(vm_answer("let q = 5; q;") == "5");
    assert(vm_answer("var g = 7; g;") == "7");
    assert(vm_answer("var g = 7; g += 2; g;") == "9");
    assert(vm_answer("var g = 7; g++; g;") == "8");
    assert(vm_answer("missingName = 3; missingName;") == "3");
    // Script var declarations exist before control reaches the declaration.
    assert(vm_answer("pre; var pre = 1;") == "undefined");
    assert(vm_answer("if (false) { var skipped = 1; } skipped;") == "undefined");
    assert(vm_answer("for (var empty of []) {} empty;") == "undefined");
    assert(vm_answer("var keep=7; for (var keep of []) {} keep;") == "7");
    assert(vm_answer("try { missingName; } catch (e) { e.name; }") == "ReferenceError");
    {
        JsInterpreter js(nullptr);
        VM vm;
        auto result = vm.interpret(js.compile_to_bytecode("typeof missingName;"));
        assert(result.type == JsType::String);
        assert(result.to_string() == "undefined");
        vm.set_global("already", JsValue::make_number(9));
        assert(vm.interpret(js.compile_to_bytecode("var already; already;")).to_string() == "9");
        vm.set_global("presentButUndefined", JsValue::make_undefined());
        assert(vm.interpret(js.compile_to_bytecode("presentButUndefined;")).is_undefined());
        try {
            vm.interpret(js.compile_to_bytecode("missingName;"));
            assert(false && "expected a JavaScript ReferenceError");
        } catch (const JsThrow& error) {
            assert(js_describe_thrown(error.value) == "ReferenceError: missingName is not defined");
        }
        // The constant operand belongs to the checked opcode, not another
        // instruction. A dump must advance over it and show the next opcode.
        auto fn = js.compile_to_bytecode("missingName;");
        std::ostringstream dump;
        fn->chunk.disassemble(dump, "checked read");
        assert(dump.str().find("OP_GET_GLOBAL_CHECKED 'missingName'") != std::string::npos);
        assert(dump.str().find("OP_RETURN") != std::string::npos);
    }

    // And the two paths agree, case by case. For a case that throws, the
    // tree-walker reports the error and answers `undefined`, so agreement is
    // "the VM threw, and the tree-walker reported"; for one that does not, it
    // is the value both answer.
    const char* shared[] = {
        "missingName;",
        "'p' + missingName;",
        "missingName + 1;",
        "typeof missingName;",
        "typeof (missingName + 1);",
        "let q = 5; q;",
        "var g = 7; g;",
    };
    for (const char* source : shared) {
        const std::string from_vm = vm_answer(source);
        bool tree_reported = false;
        const std::string from_tree = tree_answer(source, tree_reported);
        if (from_vm == "ReferenceError") {
            assert(tree_reported);
            assert(from_tree == "undefined");
        } else {
            assert(!tree_reported);
            assert(from_vm == from_tree);
        }
    }
}
