// Phase 259, finding 263: the bytecode VM answers what the tree-walker answers.
TEST(bytecode_agrees_with_the_tree_walker) {
    using namespace litebird;
    // ROADMAP row 7 keeps the tree-walker as a differential oracle for the
    // bytecode VM, so the two have to answer the same script the same way.
    // Nothing checked that: `grep -rn eval_bytecode tests/ tools/` was empty,
    // and the compiler went on refusing `var` for as long as it liked.
    //
    // Every case here runs twice on the same document -- once through
    // `eval` and once through `eval_bytecode` -- and the answers must be
    // equal. A case that only one path can run is a failure, not a skip:
    // that is how `var` was hiding.
    //
    // `tools/conformance/verify_bytecode_agreement.py` runs the same idea over
    // a larger corpus and is shown to fail on a deliberately broken VM; this
    // suite keeps the property inside `make test`.
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
            // A fresh interpreter for the second path: whatever the first left
            // in the global object must not be what makes them agree.
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

    // ---- `var` ------------------------------------------------------------
    // ECMA-262 14.3.2. The compiler refused every `var`, so no ordinary script
    // could compile at all.
    agree("var a = 1; String(a)", "1");
    agree("var a = 1, b = 2; String(a + b)", "3");
    agree("var x = 1; x += 4; String(x)", "5");
    agree("var s = 'x'; s += 'y'; String(s)", "xy");
    // A `var` is a binding of the enclosing *function*, not of the block it is
    // written in: `{ var b = 2; }` leaves `b` readable after the block. This
    // is the case a first attempt got wrong (it answered 1, not 3) because it
    // gave the name a stack slot that leaving the block popped.
    agree("var a = 1; { var b = 2; } String(a + b)", "3");
    // ... and inside a function it is that function's, not the script's.
    agree("function f() { var t = 7; return t; } String(f())", "7");

    // ---- the ternary ------------------------------------------------------
    // ECMA-262 13.14. Refused as "Unsupported expression in bytecode compiler".
    agree("true ? 'y' : 'n'", "y");
    agree("false ? 'y' : 'n'", "n");
    agree("var q = 5; String(q > 3 ? 'big' : 'small')", "big");

    // ---- `do`/`while` -----------------------------------------------------
    // ECMA-262 14.6.2. Refused as an unsupported statement. The body runs
    // before the test, so it runs once even when the test is false.
    agree("var e = 0; do { e = e + 1; } while (e < 2); String(e)", "2");
    agree("var k = 0; do { k = k + 1; } while (false); String(k)", "1");

    // ---- `break` and `continue` -------------------------------------------
    // ECMA-262 13.8 and 13.9, both refused before. Each loop kind puts the
    // jump in a different place: `continue` in a `while` goes *backwards* to
    // the test, and in a `for` or a `do`/`while` it goes *forwards*, to the
    // update or the test. Using one direction for all three is how `continue`
    // in a `for` first hung the VM -- `for (var i = 0; i < 3; i++) {
    // continue; }` never terminated.
    agree("var m = 0; while (true) { m++; if (m > 3) break; } String(m)", "4");
    agree("var t = 0; for (var i = 0; i < 10; i++) { if (i > 4) break; t += i; } String(t)", "10");
    agree("var z = 0; do { z = z + 1; if (z > 2) break; } while (z < 10); String(z)", "3");
    agree("var u = 0; for (var i = 0; i < 5; i++) { if (i == 2) continue; u += i; } String(u)", "8");
    // The whole body is `continue`, so only the update advances the loop; this
    // is the case that hung, and it is here because a hang is not a wrong
    // answer that a comparison would catch.
    agree("var v = 0; for (var i = 0; i < 4; i++) { continue; v += 100; } String(v)", "0");
    agree("var w = 0; var j = 0; while (j < 5) { j++; if (j == 3) continue; w += j; } String(w)", "12");
    agree("var y = 0; var q = 0; do { q++; if (q == 2) continue; y += q; } while (q < 4); String(y)", "8");
    // An inner loop's `continue` must not leave the outer loop.
    agree("String((function(){ var r = 0; for (var i = 0; i < 3; i++) "
          "{ for (var j = 0; j < 3; j++) { if (j == 1) continue; r++; } } return r; })())", "6");

    // ---- loops that carry a result ----------------------------------------
    agree("var s = ''; for (var i = 0; i < 3; i++) { s += i; } String(s)", "012");
    agree("var t = 0; for (var a = 0; a < 3; a++) { for (var b = 0; b < 3; b++) { t++; } } String(t)", "9");
    agree("function h(n) { var acc = 1; for (var i = 2; i <= n; i++) acc *= n; return acc; } String(h(5))", "625");
    agree("var arr = [1,2,3]; var n = 0; for (var i = 0; i < arr.length; i++) { n = n + arr[i]; } String(n)", "6");
    agree("let s2 = 0; for (let z = 0; z < 4; z++) { s2 += z; } String(s2)", "6");

    // ---- the expressions around them, which were already shared -----------
    agree("var o = {x: 1, y: 2}; String(o.x + o.y)", "3");
    agree("var arr = []; arr.push(7); String(arr.length)", "1");
    agree("String(1 && 2)", "2");
    agree("String(0 || 5)", "5");
}
