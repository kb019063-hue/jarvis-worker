// A global's `delete` agrees with the descriptor it advertises.
//
// §9.1.1.4 makes a `var` or function declaration a script makes at the top
// level a non-configurable property of the global object, and §18.1.1-§18.1.3
// do the same to `undefined`, `NaN` and `Infinity`. Everything §17-§26 defines
// — `Object`, `Math`, `JSON`, `RangeError`, `parseInt` — is an ordinary data
// property with `{writable: true, enumerable: false, configurable: true}`, the
// Note to §18.1 saying so in as many words.
//
// This interpreter had it the other way round. It marked every binding it had
// created before any script ran as non-configurable, and its
// `[[GetOwnProperty]]` derived `configurable` from `!is_readonly`, which
// happened to be right for `undefined` and wrong for every function. So the
// object reported `{configurable: true}` and `[[Delete]]` refused, and a
// program that believed either one was wrong about the other:
//
//     Object.getOwnPropertyDescriptor(globalThis, "Object").configurable  // true
//     delete globalThis.Object                                            // false
//
// Node v22.22.3 answers `true` to both, and the bare name is a ReferenceError
// afterwards — the binding goes with the property. That last part is the one a
// test of the boolean alone would miss.
//
// The same derivation also made a sloppy implicit global (`zz = 3`) report
// `{enumerable: false, configurable: false}` where Node says
// `{enumerable: true, configurable: true}`, because a name a running script
// created inherited the "built-in" mark from the environment it was put in.
//
// Every expected value below is Node v22.22.3's, by evaluating the same
// expression there.
TEST(a_global_is_deleted_only_if_its_descriptor_says_it_can_be) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // Every question is asked through a reference taken before anything is
    // deleted: a successful delete removes the name from this page's global
    // scope for good, so a later assertion that mentions `Object` would fail
    // with a ReferenceError of the test's own making — as an earlier draft of
    // this one did.
    assert(run("var K = Object; K.getOwnPropertyDescriptor(globalThis, 'Object').configurable") == "true");

    // ---- the descriptor and the delete say the same thing ----------------
    // These two are the whole finding, and they come first because they are
    // what fails on the engine as it was: `Object`/`Error`/`Math` keep their
    // own assertions below, so nothing here has to survive a deletion.
    assert(run("delete globalThis.Object") == "true");
    assert(run("K.prototype.hasOwnProperty.call(globalThis, 'Object')") == "false");
    // The bare name stops resolving — that is what makes this the same
    // deletion and not a bookkeeping one, and it is the half a test of the
    // boolean alone would miss.
    assert(run("typeof Object") == "undefined");
    assert(run("delete globalThis.Error") == "true");
    assert(run("typeof Error") == "undefined");

    // ---- every other library global is the same shape ---------------------
    assert(run("[K.getOwnPropertyDescriptor(globalThis,'Math').configurable,"
               "K.getOwnPropertyDescriptor(globalThis,'JSON').configurable,"
               "K.getOwnPropertyDescriptor(globalThis,'RangeError').configurable,"
               "K.getOwnPropertyDescriptor(globalThis,'parseInt').configurable].join(',')")
           == "true,true,true,true");

    // ---- the three permanent values refuse -------------------------------
    // These were right before and have to stay right: they are the one kind
    // §9.1.1.4 does make non-configurable.
    assert(run("[K.getOwnPropertyDescriptor(globalThis,'undefined').configurable,"
               "K.getOwnPropertyDescriptor(globalThis,'NaN').configurable,"
               "K.getOwnPropertyDescriptor(globalThis,'Infinity').configurable].join(',')")
           == "false,false,false");
    assert(run("delete globalThis.undefined") == "false");
    assert(run("delete globalThis.NaN") == "false");
    assert(run("K.prototype.hasOwnProperty.call(globalThis, 'NaN')") == "true");

    // ---- a script's own `var` is non-configurable, and enumerable --------
    // §9.1.1.4 from the other side. A `var` is the property the standard makes
    // non-configurable, which is exactly backwards from what this engine had;
    // and it is enumerable, where a built-in is not. The declaration has to be
    // at the top level of the *page*: a `var` inside a function is that
    // function's, not the global object's, so these are separate `eval` calls
    // rather than closures.
    assert(run("var declaredGlobal = 1; 'ok'") == "ok");
    assert(run("K.getOwnPropertyDescriptor(globalThis, 'declaredGlobal').enumerable") == "true");
    assert(run("K.getOwnPropertyDescriptor(globalThis, 'declaredGlobal').writable") == "true");
    assert(run("K.getOwnPropertyDescriptor(globalThis, 'declaredGlobal').configurable") == "false");
    assert(run("delete globalThis.declaredGlobal") == "false");

    // ---- and a sloppy implicit global is enumerable ----------------------
    // `zzImplicit = 3` with no declaration is a third kind: not a built-in and
    // not a declaration, so §9.1.1.4's rules for a `var` do not reach it. It
    // is `{writable: true, enumerable: true, configurable: true}`, and the
    // `configurable` half was **still wrong when this suite was written** —
    // the name was created through the same `define` a built-in uses, in a
    // file the open #421 owned, so the assertion that would hold it was left
    // out rather than written red: a test failing in the tree tests nothing.
    // #421 has landed; the call site now uses `define_script_global` and the
    // case is closed below.
    assert(run("zzImplicit = 3; 'ok'") == "ok");
    assert(run("K.keys(globalThis).indexOf('zzImplicit') >= 0") == "true");
    assert(run("[K.getOwnPropertyDescriptor(globalThis,'zzImplicit').writable,"
               "K.getOwnPropertyDescriptor(globalThis,'zzImplicit').enumerable,"
               "K.getOwnPropertyDescriptor(globalThis,'zzImplicit').configurable].join(',')")
           == "true,true,true");
    assert(run("delete globalThis.zzImplicit") == "true");
    assert(run("typeof zzImplicit") == "undefined");

    // ---- a `let` is not a property in either sense -----------------------
    // The fix touches what the global object reports, so this must not have
    // swept a lexical binding in with the rest.
    assert(run("let lexicalGlobal = 1; 'ok'") == "ok");
    assert(run("K.getOwnPropertyDescriptor(globalThis, 'lexicalGlobal') === undefined") == "true");

    // ---- the lists are unchanged -----------------------------------------
    // Deleting is the new behaviour; enumerating must not have moved.
    assert(run("K.keys(globalThis).indexOf('Math')") == "-1");
    assert(run("K.getOwnPropertyNames(globalThis).indexOf('Math') >= 0") == "true");
}
