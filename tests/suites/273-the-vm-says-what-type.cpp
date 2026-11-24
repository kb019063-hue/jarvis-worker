// Phase 273, finding 277: the bytecode VM says what type a value is.
//
// Phase 259 built the differential oracle between the tree-walker and the
// bytecode VM and found the VM refusing every `var`. The remaining list of
// refusals is `tools/conformance/verify_bytecode_agreement.py`'s REFUSES;
// `String(typeof 1)` was the one unary operator on it. The compiler refused
// `typeof` outright ("This unary operator is not supported"), so any script
// asking a value's type could not compile at all.
//
// The fix is one opcode the VM answers with the tree-walker's own
// `js_typeof_value`, which is the point of the oracle: the two paths cannot
// drift apart for any value that reaches both. The one place `typeof` must
// not throw — an unresolvable reference, ECMA-262 §13.5.3 — needed no new
// machinery: the VM answers an undeclared global with `undefined`
// (`OpGetGlobal`), which is the type such an operand is.
TEST(the_bytecode_vm_says_what_type_a_value_is) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;

    auto both = [&](const char* source) -> std::pair<std::string, std::string> {
        std::string walked;
        std::string compiled;
        try {
            walked = js.eval(source).to_string();
        } catch (const JsThrow& thrown) {
            walked = "threw: " + thrown.value.to_string();
        } catch (const std::exception& error) {
            walked = std::string("threw: ") + error.what();
        }
        try {
            // A fresh interpreter for the second path: whatever the first
            // left in the global object must not be what makes them agree.
            TestPage other;
            compiled = other.js.eval_bytecode(source).to_string();
        } catch (const JsThrow& thrown) {
            compiled = "threw: " + thrown.value.to_string();
        } catch (const std::exception& error) {
            compiled = std::string("threw: ") + error.what();
        }
        return {walked, compiled};
    };

    auto agree = [&](const char* source, const std::string& expected) {
        auto [walked, compiled] = both(source);
        // The tree-walker is the engine a page runs on, so it is the answer
        // that is asserted; the VM is asserted to match it.
        assert(walked == expected);
        assert(compiled == expected);
    };

    // ---- each of the answers, as the engine makes them --------------------
    agree("String(typeof 1)", "number");
    agree("String(typeof 'x')", "string");
    agree("String(typeof true)", "boolean");
    agree("String(typeof undefined)", "undefined");
    agree("String(typeof null)", "object");
    agree("String(typeof (() => 1))", "function");

    // ---- the one operand shape where typeof must not throw ----------------
    // §13.5.3: an unresolvable reference is `undefined`, never a
    // ReferenceError. The VM answers an undeclared global with `undefined`,
    // which is this operand's type.
    agree("String(typeof thisNameIsDeclaredNowhereAtAll)", "undefined");

    // ---- compound typeof expressions compile now ---------------------------
    agree("String(typeof 1) + String(typeof 'x')", "numberstring");
    agree("typeof {a: 1} === 'object' && typeof [1] === 'object' ? 'both object' : 'no'", "both object");
    agree("var q = typeof 5; String(q === 'number' ? 'yes' : 'no')", "yes");
    agree("var t = typeof 's'; if (t === 'string') { t = 'matched'; } String(t)", "matched");

    // ---- typeof of a call and of a member read ----------------------------
    agree("function f() { return 1; } String(typeof f) + String(typeof f())", "functionnumber");
    agree("var o = {x: 1}; String(typeof o.x) + String(typeof o)", "numberobject");

    // ---- typeof still composes with everything the compiler already had ---
    agree("var n = 0; for (var i = 0; i < 3; i++) { if (typeof i === 'number') n += 1; } String(n)", "3");
    agree("var s = ''; if (typeof 'a' === 'string') { s = 'y'; } else { s = 'n'; } String(s)", "y");

    std::printf("      [bytecode typeof] number, string, boolean, undefined, object, function; "
                "no throw on an unresolvable reference\n");
    std::fflush(stdout);
}
