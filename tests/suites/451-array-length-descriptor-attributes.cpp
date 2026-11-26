// Phase 451, finding 451: an array length descriptor obeys its fixed
// enumerable/configurable attributes and cannot regain writability.
TEST(array_length_descriptor_attributes_respect_integrity_levels) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function same(got, want, what) {
            if (got !== want) failures.push(what + ': ' + got + ' !== ' + want);
        }
        function throwsTypeError(f, what) {
            try { f(); } catch (e) {
                if (e && e.name === 'TypeError') return;
                failures.push(what + ': threw ' + (e && e.name));
                return;
            }
            failures.push(what + ': did not throw');
        }

        // The array length is born non-enumerable and non-configurable. Its
        // enumerable bit cannot be changed even when it is otherwise writable.
        var fresh = [];
        throwsTypeError(function () {
            Object.defineProperty(fresh, 'length', { enumerable: true });
        }, 'enumerable true on a fresh length');
        var fresh_desc = Object.getOwnPropertyDescriptor(fresh, 'length');
        same(fresh_desc.enumerable, false, 'fresh length enumerable');
        same(fresh_desc.configurable, false, 'fresh length configurable');
        same(Reflect.defineProperty(fresh, 'length', { enumerable: true }), false,
             'Reflect enumerable true on length');
        same(Reflect.defineProperty(fresh, 'length', { configurable: true }), false,
             'Reflect configurable true on length');
        throwsTypeError(function () {
            Object.defineProperty(fresh, 'length', { configurable: true });
        }, 'Object.defineProperty configurable true on length');

        // [[Writable]] can transition from true to false, never back. The
        // reflective form reports false; Object.defineProperty throws.
        var read_only = [];
        Object.defineProperty(read_only, 'length', { writable: false });
        same(Reflect.defineProperty(read_only, 'length', { writable: true }), false,
             'Reflect made read-only length writable');
        throwsTypeError(function () {
            Object.defineProperty(read_only, 'length', { writable: true });
        }, 'Object.defineProperty made read-only length writable');
        same(Object.getOwnPropertyDescriptor(read_only, 'length').writable, false,
             'read-only length after refused transition');

        // Freeze applies the same read-only rule even when the length has no
        // explicit property-map entry yet.
        var frozen = Object.freeze([]);
        same(Reflect.defineProperty(frozen, 'length', { writable: true }), false,
             'Reflect made frozen length writable');
        throwsTypeError(function () {
            Object.defineProperty(frozen, 'length', { writable: true });
        }, 'Object.defineProperty made frozen length writable');
        same(Object.getOwnPropertyDescriptor(frozen, 'length').writable, false,
             'frozen length after refused transition');

        // A sealed array still has a writable length, but cannot acquire a new
        // index. `Object.defineProperty` must throw instead of silently
        // ignoring the failed element write; Reflect reports false.
        var sealed = Object.seal([]);
        same(Reflect.defineProperty(sealed, '0', { value: 1 }), false,
             'Reflect added an index to sealed array');
        throwsTypeError(function () {
            Object.defineProperty(sealed, '0', { value: 1 });
        }, 'Object.defineProperty added an index to sealed array');
        same(sealed.length, 0, 'sealed array length after refused index');
        same(Object.prototype.hasOwnProperty.call(sealed, '0'), false,
             'sealed array acquired a new index');

        // A writable length on a sealed array remains writable and may grow;
        // a frozen array cannot grow through either length or an index.
        var sealed_grow = Object.seal([1]);
        sealed_grow.length = 3;
        same(sealed_grow.length, 3, 'sealed array length growth');
        same(Object.getOwnPropertyDescriptor(sealed_grow, 'length').writable, true,
             'sealed length writable');
        throwsTypeError(function () {
            Object.defineProperty(frozen, '0', { value: 1 });
        }, 'Object.defineProperty added an index to frozen array');

        // These restrictions are about arrays: an ordinary non-extensible
        // object already rejects a new property in the throwing form.
        var ordinary = Object.preventExtensions({});
        throwsTypeError(function () {
            Object.defineProperty(ordinary, 'x', { value: 1 });
        }, 'Object.defineProperty added a property to non-extensible object');
    )JS");

    const std::string failed = js.eval("failures.join('\\n')").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());
}
