// Phase 404, finding 404: the global object owns the globals it can find.
//
// `JsIntrinsics::add_global` keeps the library's globals in a `std::vector`
// beside the property map, and nothing keeps the two in step. Reads and writes
// worked, because the global object reaches the global environment through
// `lookup_fallback` and `assign_hook` -- and both of those take a name the
// caller already has. Nothing could ask *which* names, so nothing answered;
// `[[Delete]]` never asked whether a name was owned outside the table either.
//
// So `Object.getOwnPropertyNames(globalThis)` reported the handful of names
// that happened to be property-table entries, and `delete globalThis.Error`
// erased nothing while answering `true` -- the wrong half of a pair, since
// `true` means the property is gone.
//
// These tests hold the four things §9.1.1.4 makes true of a built-in global,
// and the three it makes true of a script's own declaration, because the fix
// could easily have made the second look like the first: a `var` is enumerable
// and a built-in is not, and a `let` is a property of the global object in
// neither sense.
//
// Every expectation here was measured against Node v22.22.3 first.
TEST(global_object_owns_the_globals_it_can_find) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function ok(name, actual, expected) {
            if (actual !== expected) failures.push(name + ': got ' + actual + ', want ' + expected);
        }
        function names() { return Object.getOwnPropertyNames(globalThis); }
        function keys() { return Object.keys(globalThis); }

        // ---- a built-in global is an own property -----------------------
        var want = ['Object', 'Function', 'Array', 'String', 'Number', 'Boolean',
                    'Error', 'TypeError', 'Math', 'JSON', 'Reflect', 'Promise',
                    'Date', 'RegExp', 'Map', 'Set', 'parseInt', 'parseFloat',
                    'isNaN', 'isFinite', 'undefined', 'NaN', 'Infinity', 'eval',
                    'globalThis'];
        var missing = [];
        for (var i = 0; i < want.length; i++) {
            if (names().indexOf(want[i]) < 0) missing.push(want[i]);
        }
        ok('every built-in is in getOwnPropertyNames', missing.join(','), '');

        // The three ways a script asks whether a property is there.
        ok('hasOwnProperty Object',
           Object.prototype.hasOwnProperty.call(globalThis, 'Object'), true);
        ok('in Object', 'Object' in globalThis, true);
        ok('descriptor is not undefined',
           Object.getOwnPropertyDescriptor(globalThis, 'Object') !== undefined, true);

        // §9.1.1.4: writable, non-enumerable, configurable.
        (function () {
            var d = Object.getOwnPropertyDescriptor(globalThis, 'Object');
            if (d === undefined) { failures.push('no descriptor for Object'); return; }
            ok('Object writable', d.writable, true);
            ok('Object enumerable', d.enumerable, false);
            ok('Object configurable', d.configurable, true);
        })();
        // `undefined`, `NaN` and `Infinity` are the non-writable and
        // non-configurable kind.
        (function () {
            var d = Object.getOwnPropertyDescriptor(globalThis, 'NaN');
            if (d === undefined) { failures.push('no descriptor for NaN'); return; }
            ok('NaN writable', d.writable, false);
            ok('NaN configurable', d.configurable, false);
        })();

        // ---- and it is not enumerable -----------------------------------
        ok('Object.keys excludes Object', keys().indexOf('Object') >= 0, false);
        ok('Object.keys excludes Error', keys().indexOf('Error') >= 0, false);
        (function () {
            var found = 0;
            for (var name in globalThis) { if (name === 'Object') found++; }
            ok('for-in skips non-enumerable', found, 0);
        })();

        // ---- deleting one answers true, and the name goes with it ---------
        // This block used to assert `false` and "Object survives" — in the
        // same file, twenty lines below the assertion that the descriptor has
        // `configurable: true`. The two could not both be right, and Node
        // v22.22.3 settles it: `delete globalThis.Object` is `true` and the
        // bare name is a ReferenceError afterwards. Corrected in Phase 425,
        // which is the phase that made `[[Delete]]` agree with the descriptor
        // it had been reporting all along. The reference is held first so the
        // rest of the test does not have to mention a name it just removed.
        var held = Object;
        ok('Error is configurable', Object.getOwnPropertyDescriptor(globalThis, 'Error').configurable, true);
        ok('delete Error answers true', delete globalThis.Error, true);
        ok('Error is gone', Object.getOwnPropertyNames(held.create(globalThis)).length >= 0, true);
        ok('Error no longer own',
           held.prototype.hasOwnProperty.call(globalThis, 'Error'), false);
        ok('delete Object answers true', delete globalThis.Object, true);
        ok('Object no longer own',
           held.prototype.hasOwnProperty.call(globalThis, 'Object'), false);
        globalThis.Error = held;         // put them back for what follows
        globalThis.Object = held;

        // ---- a script's declaration is a property of a different kind ----
        var scriptVar = 1;
        ok('var is in names', names().indexOf('scriptVar') >= 0, true);
        ok('var is enumerable', keys().indexOf('scriptVar') >= 0, true);
        ok('var is own', Object.prototype.hasOwnProperty.call(globalThis, 'scriptVar'), true);
        ok('var is the binding', globalThis.scriptVar, 1);

        // A `let` is not a property of the global object at all, and the fix
        // must not have swept it in with the `var` above.
        let scriptLet = 2;
        ok('let is not in names', names().indexOf('scriptLet') >= 0, false);
        ok('let is not in keys', keys().indexOf('scriptLet') >= 0, false);
        ok('let is not own', Object.prototype.hasOwnProperty.call(globalThis, 'scriptLet'), false);
    )JS");
    assert(js.eval("failures.join('; ')").to_string().empty());
}
