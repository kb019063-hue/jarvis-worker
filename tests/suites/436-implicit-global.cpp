// A sloppy implicit global is the one kind of global that can be deleted.
//
// §9.1.1.4 makes two things non-configurable: `undefined`/`NaN`/`Infinity`
// (§18.1.1-§18.1.3) and a `var` or function a script declares at the top
// level. Everything else a name on the global object can be is configurable,
// and a sloppy assignment to an undeclared name — `zz = 3` — makes one of
// those: `{writable: true, enumerable: true, configurable: true}`. It is not
// a built-in (not `{enumerable: false}`) and not a declaration (not
// `{configurable: false}`), and it is the only kind that is both enumerable
// and deletable.
//
// Phase 431 taught the global object that distinction and then wrote it as two
// branches — `if (is_permanent) false; if (!is_builtin) return is_deletable;`
// — which is a three-way table with four kinds in it. An implicit global is
// neither a built-in nor `is_deletable` (which marks what a sloppy direct
// `eval` made), so it fell through to `false` and could not be deleted, and it
// reported `enumerable: false` besides, because it inherited `is_builtin` from
// the environment it was created in.
//
// The Phase 431 write-up named this and left the assertion out on purpose:
// the call site is in a file the open #421 owned, and a test that fails in the
// tree tests nothing. #421 landed; this is that assertion, and the case.
//
// Every expected value below is Node v22.22.3's, by evaluating the same
// expression there.
TEST(an_implicit_global_is_enumerable_and_deletable) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // All three attributes in one go: this is the whole shape of the kind, and
    // a test of any one of them can pass while the others are wrong.
    assert(run("impAll = 1; 'ok'") == "ok");
    assert(run("var d = Object.getOwnPropertyDescriptor(globalThis, 'impAll');"
               "[d.writable, d.enumerable, d.configurable].join(',')") == "true,true,true");

    // Enumerable means it shows up where a script looks: `Object.keys`, not
    // just `getOwnPropertyNames`. This used to answer `-1`.
    assert(run("Object.keys(globalThis).indexOf('impAll') >= 0") == "true");
    assert(run("Object.getOwnPropertyNames(globalThis).indexOf('impAll') >= 0") == "true");

    // And deletable means the delete goes through and the name is gone -- both
    // halves. The descriptor alone cannot show the second.
    assert(run("delete globalThis.impAll") == "true");
    assert(run("Object.prototype.hasOwnProperty.call(globalThis, 'impAll')") == "false");
    assert(run("typeof impAll") == "undefined");

    // ---- the three other kinds are unmoved by this ----------------------
    // This is the assertion that makes it a *four*-way distinction rather than
    // "make more things deletable": each of the others still refuses, and for
    // its own reason.
    assert(run("var declaredImp = 1; 'ok'") == "ok");
    assert(run("Object.getOwnPropertyDescriptor(globalThis, 'declaredImp').configurable") == "false");
    assert(run("delete globalThis.declaredImp") == "false");
    assert(run("delete globalThis.NaN") == "false");
    assert(run("Object.getOwnPropertyDescriptor(globalThis, 'Math').configurable") == "true");

    // ---- a name that comes back keeps the kind of the way it came back ---
    // `delete`, then assign again with no declaration: it is an implicit
    // global again, so it is configurable again, and it is not a `var` that
    // was merely overwritten.
    assert(run("impAgain = 1; delete globalThis.impAgain; impAgain = 2; 'ok'") == "ok");
    assert(run("Object.getOwnPropertyDescriptor(globalThis, 'impAgain').configurable") == "true");

    // ---- strict code does not make one at all ---------------------------
    // The rule is about a *sloppy* assignment; a strict one is a ReferenceError
    // and creates nothing. Checking that this phase did not quietly make
    // strict assignments legal is worth one assertion.
    assert(run("(function(){ try { (function(){ 'use strict'; impStrict = 1; })(); }"
               "catch (e) { return e.name + ',' + Object.prototype.hasOwnProperty.call(globalThis, 'impStrict'); } })()")
           == "ReferenceError,false");
}
