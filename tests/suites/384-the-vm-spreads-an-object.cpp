// Phase 384, finding 384: the bytecode VM spreads an object.
//
// Phase 259 built the differential oracle between the tree-walker and the
// bytecode VM; the `REFUSES` list of `verify_bytecode_agreement.py` holds
// what remains, and `var o = {a: 1, ...{b: 2}}; String(o.b)` was the object
// literal form on it. The compiler required `is_simple()` — plain keys only —
// and refused a spread outright.
//
// The fix is two opcodes: `OpObjectDefine` defines one `key: value` pair onto
// the object under the operands, and `OpObjectSpread` copies a source's own
// enumerable keys onto that same object via **the tree-walker's own
// `copy_data_properties`** (ECMA-262 §7.3.24) — one implementation, two
// consumers, as with `typeof` in Phase 273 and `switch` in Phase 291. The
// object is built in source order, so a spread is evaluated at its position,
// and spreading null or undefined copies nothing, exactly as §13.2.5
// requires. Computed keys, methods, accessors and `__proto__` are refused
// as before; the plain-pairs fast path is unchanged.
TEST(the_bytecode_vm_spreads_an_object) {
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

    // ---- the original refusal ------------------------------------------------
    agree("var o = {a: 1, ...{b: 2}}; String(o.b)", "2");

    // ---- null and undefined spread to nothing; so does a number --------------
    agree("var o = {a: 1, ...undefined, ...null, ...7}; String(o.a) + '/' + String(Object.keys(o).length)",
          "1/1");

    // ---- order: an own key written after the spread wins, and vice versa -----
    agree("var o = {...{a: 1, b: 2, c: 3}, b: 9}; String(o.b) + '/' + String(o.a) + '/' + String(o.c)",
          "9/1/3");
    agree("var o = {a: 1, ...{a: 2, b: 2}, c: 3, ...{c: 4}}; String(o.a) + '/' + String(o.c)",
          "2/4");

    // ---- spreading non-plain objects: indices and positions ------------------
    agree("var o = {...[7, 8]}; String(o[0]) + '/' + String(o[1]) + '/' + String(Object.keys(o).join(','))",
          "7/8/0,1");
    agree("var o = {...'abc'}; String(o[0]) + '/' + String(o[1]) + '/' + String(o[2])",
          "a/b/c");

    // ---- each spread is evaluated at its position -----------------------------
    agree("var d = ''; function mark(x) { d += x; return {p: x}; } var o = {...mark('a'), ...mark('b')}; String(d) + '/' + String(o.p)",
          "ab/b");

    // ---- spread composes with nesting and ordinary keys -----------------------
    agree("var o = {a: {...{b: {...{c: 5}}}}}; String(o.a.b.c)", "5");
    agree("var src = {x: 1}; var p = {...src, s: 9}; String(p.x) + '/' + String(p.s)", "1/9");

    std::printf("      [bytecode object spread] source order, own enumerable keys, null/undefined copy nothing\n");
    std::fflush(stdout);
}
