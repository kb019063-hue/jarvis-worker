// Phase 396, finding 396: nothing checked for `new`.
//
// Fifteen built-in constructors were ordinary functions here, so calling one
// without `new` quietly made an object instead of throwing:
//
//     Map()                    -> an empty Map        (V8: TypeError)
//     new Uint8Array(4)        (fine)   Uint8Array(4) -> a typed array of 4
//     ArrayBuffer(8)           -> 8 bytes             (DataView(…) too)
//
// §10.2.1.1 and §24.1.1.1 make these *constructible and not callable*. V8's
// message is `Constructor Map requires 'new'`, and these fifteen answer it word
// for word.
//
// The half that matters more is the negative one, and it is why the guard asks
// `JsValue::current_requires_new()` and not `!JsValue::current_is_construct()`:
// every `[[Construct]]` sets `is_construct` — a plain `new`, a derived
// `class MyMap extends Map`, `Reflect.construct(Map, …)` and a `super()` — so
// testing for it refuses exactly the call form and nothing else. A guard that
// tested the absence of a `new` in the *source* would have taken
// `Reflect.construct` away, which reaches the same native through
// `[[Construct]]` with no `new` written anywhere.
//
// `Array`, `Object`, `Date`, `RegExp`, `Error`, `Function`, `Number`, `String`,
// `Boolean` and `Symbol` are callable on purpose (§20.1.1.1 and its siblings),
// and are asserted here so that a wider guard fails this suite.
TEST(constructors_need_new) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function ok(name, actual, expected) {
            if (actual !== expected) failures.push(name + ': got ' + actual + ', want ' + expected);
        }
        // The call, and the message it has to carry.
        function refuses(name) {
            try { globalThis[name](); return 'no throw'; }
            catch (e) {
                if (!(e instanceof TypeError)) return 'threw ' + e.constructor.name;
                var want = "Constructor " + name + " requires 'new'";
                return e.message === want ? 'refuses' : 'wrong words: ' + e.message;
            }
        }

        // ---- the fifteen -------------------------------------------------
        var strict_ctors = ['ArrayBuffer', 'DataView', 'Uint8Array', 'Uint8ClampedArray',
            'Int8Array', 'Uint16Array', 'Int16Array', 'Uint32Array', 'Int32Array',
            'Float32Array', 'Float64Array', 'Map', 'Set', 'WeakMap', 'WeakSet'];
        for (var i = 0; i < strict_ctors.length; i++) ok(strict_ctors[i], refuses(strict_ctors[i]), 'refuses');

        // ---- the ten that are callable on purpose ------------------------
        // A guard that refused every constructor fails here.
        var callable = ['Array', 'Object', 'Date', 'RegExp', 'Error', 'Function',
            'Number', 'String', 'Boolean', 'Symbol'];
        for (var j = 0; j < callable.length; j++) {
            var made;
            try { made = globalThis[callable[j]](); } catch (e) { made = 'threw ' + e.constructor.name; }
            ok(callable[j] + ' is still callable', made !== undefined && String(made).indexOf('threw') !== 0, true);
        }
        // The three whose answer is a primitive of a different type.
        ok('Number() is a number', typeof Number('7'), 'number');
        ok('String() is a string', typeof String(7), 'string');
        ok('Boolean() is a boolean', typeof Boolean(0), 'boolean');
        ok('Array() makes an array', Array.isArray(Array()), true);

        // ---- every legal construction still constructs -------------------
        // `new`, including the derived form and the inherited one.
        ok('new Map', new Map([[1, 2]]).get(1), 2);
        ok('new Uint8Array', new Uint8Array(4).length, 4);
        ok('new ArrayBuffer', new ArrayBuffer(8).byteLength, 8);
        ok('new DataView', new DataView(new ArrayBuffer(8)).byteLength, 8);
        class MyMap extends Map {}
        ok('derived construction', new MyMap([[1, 2]]).get(1), 2);
        ok('derived is an instance', new MyMap() instanceof MyMap, true);
        class MyBytes extends Uint8Array {}
        ok('derived typed array', new MyBytes(4).length, 4);
        ok('derived typed array is one', new MyBytes(4) instanceof MyBytes, true);

        // `Reflect.construct` writes no `new` anywhere and must still work —
        // this is the case a "was there a new in the source" guard breaks.
        ok('Reflect.construct Map', Reflect.construct(Map, [[[1, 2]]]).get(1), 2);
        ok('Reflect.construct Set', Reflect.construct(Set, [[1, 2]]).size, 2);
        ok('Reflect.construct Uint8Array', Reflect.construct(Uint8Array, [4]).length, 4);
        ok('Reflect.construct DataView',
           Reflect.construct(DataView, [new ArrayBuffer(8)]).byteLength, 8);
        // With a new.target that is another constructor, which is how a
        // subclass is built without the class syntax.
        ok('Reflect.construct with newTarget',
           Reflect.construct(Map, [], MyMap) instanceof MyMap, true);
        // And `super()`: the subclass constructor calls the parent as a
        // constructor, not as a call.
        class Through extends ArrayBuffer { constructor(n) { super(n); } }
        ok('super() reaches the constructor', new Through(8).byteLength, 8);

        // ---- the guard is per function, not per file ---------------------
        // `Map.prototype.get` and the rest are ordinary methods and are
        // untouched: a method still refuses a wrong receiver in its own words.
        var wrong_receiver;
        try { Map.prototype.get.call({}, 1); wrong_receiver = 'no throw'; }
        catch (e) { wrong_receiver = e.constructor.name; }
        ok('a method still refuses a wrong receiver', wrong_receiver, 'TypeError');
    )JS");
    assert(js.eval("failures.join('; ')").to_string().empty());
}
