// Phase 397, finding 397: the bytecode VM calls a tagged template.
//
// Phase 259 built the differential oracle between the tree-walker and the
// bytecode VM; the `REFUSES` list of `verify_bytecode_agreement.py` holds
// what remains, and `String(f`x`)` was the expression-level entry on it. The
// compiler refused it with "Unsupported expression in bytecode compiler", so
// any script using a tagged template could not compile.
//
// The fix builds the template object in bytecode, in the same order the
// tree-walker's `eval_tagged_template` does: the tag evaluates first, the
// substitutions left to right after it, and the call receives the strings
// array (with `raw` defined hidden, as in the tree-walker) followed by the
// substitutions. The template object is built fresh on every evaluation,
// like the tree-walker's — neither path caches per call site, and neither
// freezes the arrays; both predate this phase and stay as they are.
TEST(the_bytecode_vm_calls_a_tagged_template) {
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
    agree("function f() { return 1; } String(f`x`)", "1");

    // ---- the tag receives cooked and raw strings ------------------------------
    agree("var f = function(s) { return s[0] + '/' + s.raw[0]; }; String(f`abc`)", "abc/abc");

    // ---- substitutions evaluate left to right ----------------------------------
    agree("var f = function(s, x, y) { return s[0] + x + s[1] + y + s[2]; }; String(f`a${40 + 2}b${6 * 7}c`)",
          "a42b42c");

    // ---- an escape: raw keeps it, cooked does not -----------------------------
    agree("var f = function(s) { return s.raw[0] + '/' + s[0]; }; String(f`\\u0041`)",
          "\\u0041/A");

    // ---- the tag runs before the substitutions ----------------------------------
    agree("var d = ''; var f = function(s) { d += 't'; return s[0].length; }; function sub() { d += 's'; return 'y'; } var r = f`q${sub()}w`; String(d) + '/' + String(r)",
          "st/1");

    // ---- the raw property is not enumerable ------------------------------------
    agree("var f = function(s) { return Object.keys(s).join(','); }; String(f`ab${1}cd`)",
          "0,1");

    // ---- a member-expression tag is bound to its receiver -----------------------
    agree("var o = {f: function(s) { return s[0] + s.raw[0]; }}; String(o.f`xy`)",
          "xyxy");

    std::printf("      [bytecode tagged template] tag first, substitutions in order, raw hidden and intact\n");
    std::fflush(stdout);
}
