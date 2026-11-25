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

        // ---- deleting one answers false ---------------------------------
        // The old answer was `true` with the property still there.
        ok('delete Object answers false', delete globalThis.Object, false);
        ok('Object survives', typeof Object, 'function');
        ok('delete Error answers false', delete globalThis.Error, false);
        ok('Error survives', typeof Error, 'function');
        ok('still own after a refused delete',
           Object.prototype.hasOwnProperty.call(globalThis, 'Error'), true);

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
