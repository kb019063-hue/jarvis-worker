// Phase 291, finding 296: the bytecode VM switches on a discriminant.
//
// Phase 259 built the differential oracle between the tree-walker and the
// bytecode VM; the remaining list of refusals is `REFUSES` in
// `tools/conformance/verify_bytecode_agreement.py`, and `switch` was the one
// statement on it. The compiler refused it outright, so any script with a
// switch could not compile at all.
//
// The fix compiles the switch the way ECMA-262 14.13.4 runs it: the
// discriminant is evaluated exactly once and kept on the stack; each clause
// test is a strict equality with it, evaluated in source order until the
// first match (a test after the match is never evaluated, and may not be
// free of side effects); the matched body then runs from that clause to the
// end (fall-through) unless a plain `break` leaves the switch. A switch is
// not a loop, so `continue` inside one reaches the enclosing loop. One
// block scope surrounds the whole switch.
TEST(the_bytecode_vm_switches_on_a_discriminant) {
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

    // ---- the discriminant is evaluated once --------------------------------
    agree("var d = 0; function side() { d += 1; return 2; } var r = 0; "
          "switch (side()) { case 1: r = 1; break; case 2: r = d; } String(r) + '/' + String(d)",
          "1/1");

    // ---- matching is a strict equality, in source order ---------------------
    agree("var r = 0; switch ('1') { case 1: r = 10; break; case '1': r = 11; break; } String(r)",
          "11");
    agree("var r = 'x'; switch (2) { case 1: r = 'one'; case 2: r = 'two'; case 3: r = 'three'; } String(r)",
          "three");
    agree("var r = 0; switch (9) { case 1: r = 1; default: r = 3; case 2: r = 2; } String(r)",
          "2");
    // A matched clause falls into a default that sits after it.
    agree("var r = 0; switch (1) { case 1: r = 7; default: r = 5; } String(r)",
          "5");
    agree("var r = 'none'; switch (9) { case 1: r = 'one'; case 2: r = 'two'; } String(r)",
          "none");

    // ---- fall-through chains to the end unless a break leaves --------------
    agree("var r = ''; switch ('a') { case 'a': r += 'A'; case 'b': r += 'B'; case 'c': r += 'C'; } String(r)",
          "ABC");
    agree("var r = 0; switch (1) { case 1: r += 1; case 2: r += 2; case 3: break; case 4: r += 4; } String(r)",
          "3");

    // ---- a `let` in a clause lives to the end of the switch scope -----------
    agree("String((function() { var r = ''; switch (1) { case 1: let s = 'kept'; case 2: r = s; } return r; })())",
          "kept");

    // ---- a plain `continue` in a switch reaches the enclosing loop ---------
    agree("var n = 0; for (var i = 0; i < 4; i++) { switch (i) { case 1: continue; default: n += i; } } String(n)",
          "5");
    agree("var w = 0; var j = 0; while (j < 3) { j++; switch (j) { case 2: continue; } w += j; } String(w)",
          "4");
    // A `break` in a switch inside a loop ends the switch, not the loop.
    agree("var u = 0; for (var i = 0; i < 3; i++) { switch (i) { case 1: break; } u += 1; } String(u)",
          "3");

    // ---- a nested loop in a clause: break is the loop's, continue is the loop's
    agree("var t = 0; switch (0) { case 0: for (var i = 0; i < 3; i++) { if (i == 1) continue; if (i == 2) break; t += i; } } String(t)",
          "0");

    // ---- functions compile in clauses the way they do in blocks ------------
    agree("String((function() { function f(x) { switch (x) { case 'a': return '1'; case 'b': return '2'; default: return '3'; } } return f('b') + f('z'); })())",
          "23");

    std::printf("      [bytecode switch] discriminant once, source-order strict match, fall-through, break, continue to the enclosing loop\n");
    std::fflush(stdout);
}
