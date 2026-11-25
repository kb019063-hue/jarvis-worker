// Phase 425, finding 425: a top-level var's global-object write gets a fast path.
TEST(global_var_fast_path_preserves_global_object_semantics) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function check(name, actual, expected) {
            if (actual !== expected) failures.push(name + ': got ' + actual + ', want ' + expected);
        }

        // A script-level `var` is an own data property of the global object.
        // Both assignment and postfix update must write the value the
        // identifier and the object property read.
        var globalCounter = 0;
        var globalSum = 0;
        for (var i = 0; i < 20; i++) globalSum = globalSum + i;
        check('global loop result', globalSum, 190);
        check('global var is an own property',
              Object.prototype.hasOwnProperty.call(globalThis, 'globalSum'), true);
        check('global var remains visible on window', window.globalSum, 190);
        globalThis.globalCounter = 40;
        check('identifier sees object assignment', globalCounter, 40);
        var previous = globalCounter++;
        check('postfix result', previous, 40);
        check('postfix writes the global property', window.globalCounter, 41);

        // Primitive coercion is side-effect free and can use the update path;
        // an object conversion can call user code and must use the Reference
        // path, exactly once.
        var stringCounter = '8';
        var oldStringCounter = stringCounter++;
        check('string conversion result', oldStringCounter, 8);
        check('string update writes a number', stringCounter, 9);
        var coercions = 0;
        var coercibleGlobal = { valueOf: function () { coercions++; return 4; } };
        var oldCoercible = coercibleGlobal++;
        check('object conversion result', oldCoercible, 4);
        check('object conversion runs once', coercions, 1);
        check('object update result', coercibleGlobal, 5);
        var changingGlobal = {
            valueOf: function () {
                Object.defineProperty(globalThis, 'changingGlobal', { writable: false });
                return 6;
            }
        };
        var oldChangingGlobal = changingGlobal++;
        check('object coercion returns its number', oldChangingGlobal, 6);
        check('coercion observes a changed descriptor', typeof changingGlobal, 'object');
        check('coercion made the global readonly',
              Object.getOwnPropertyDescriptor(globalThis, 'changingGlobal').writable, false);

        // A global accessor is not a writable data property: its setter still
        // runs when an identifier is updated.
        var accessorValue = 10;
        Object.defineProperty(globalThis, 'accessorCounter', {
            configurable: true,
            get: function () { return accessorValue; },
            set: function (value) { accessorValue = value; }
        });
        var oldAccessor = accessorCounter++;
        check('accessor old value', oldAccessor, 10);
        check('accessor setter receives increment', accessorValue, 11);

        // Read-only properties must retain the generic path's strict-mode
        // refusal; the failed update cannot change their value.
        Object.defineProperty(globalThis, 'lockedCounter', {
            configurable: true, value: 7, writable: false
        });
        var strictMessage = '';
        (function () {
            'use strict';
            try { lockedCounter++; }
            catch (error) { strictMessage = error.message; }
        })();
        check('strict refusal text', strictMessage,
              "Cannot set property 'lockedCounter' of this object");
        check('readonly global stays unchanged', lockedCounter, 7);

        // With-environment resolution remains on the general path and picks
        // the object binding before the global property.
        var withCounter = 100;
        var withObject = { withCounter: 3 };
        with (withObject) { withCounter++; }
        check('with object wins', withObject.withCounter, 4);
        check('with leaves global alone', withCounter, 100);

        // A local binding with the same name is still the binding that wins.
        function shadowGlobal() {
            var globalCounter = 1;
            globalCounter++;
            return globalCounter;
        }
        check('local shadow', shadowGlobal(), 2);
        check('global shadow target unchanged', globalCounter, 41);

        // Object.freeze stores object-level writability separately from the
        // property's Writable flag. Both assignment and update must therefore
        // leave the fast path and honor the generic setter's refusal.
        function strictFrozenGlobalAssignment() {
            'use strict';
            try { globalCounter = 99; }
            catch (error) { return error.message; }
            return 'no error';
        }
        function strictFrozenGlobalUpdate() {
            'use strict';
            try { globalCounter++; }
            catch (error) { return error.message; }
            return 'no error';
        }
        Object.freeze(globalThis);
        globalCounter = 99;
        check('frozen global assignment is ignored', globalCounter, 41);
        check('frozen global postfix result', globalCounter++, 41);
        check('frozen global postfix does not write', globalCounter, 41);
        check('strict frozen assignment refuses', strictFrozenGlobalAssignment(),
              "Cannot set property 'globalCounter' of this object");
        check('strict frozen update refuses', strictFrozenGlobalUpdate(),
              "Cannot set property 'globalCounter' of this object");
    )JS");
    assert(js.eval("failures.join('; ')").to_string().empty());
}
