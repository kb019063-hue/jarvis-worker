// Phase 371, finding 371: `__proto__` is a property of `Object.prototype`.
//
// Before this phase the name was nowhere. `JsValue::get_property` synthesised
// `__proto__` for every object before it consulted a property table, and
// `JsValue::set_property` did the same on the way in, so `({}).__proto__` and
// `o.__proto__ = p` both worked. Every reflection API disagreed: there was no
// descriptor to report, `hasOwnProperty` said false, `in` said false, and
// `Object.getOwnPropertyNames(Object.prototype)` left the name out.
//
// The accessor is now a real entry on `Object.prototype` (Annex B.2.2.1),
// enumerable: false, configurable: true, with a getter and a setter. These
// tests hold both halves of the change: that the property is now *findable*,
// and that nothing about what `__proto__` *does* moved while it was made
// findable. The second half matters more -- `__proto__` is an ordinary hidden
// slot accessor for every script that has ever used it, and a property that is
// merely described correctly while assigning to it stopped changing the
// prototype would be a worse engine than one that cannot describe it.
//
// Every expectation here was measured against Node v22.22.3 first. The
// script collects failures rather than throwing, so one broken case does not
// stop the others and the `assert` below is what fails the suite.
TEST(proto_is_a_property_of_object_prototype) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function ok(name, actual, expected) {
            if (actual !== expected) failures.push(name + ': got ' + actual + ', want ' + expected);
        }

        // ---- it is a property, and it is an accessor --------------------
        var d = Object.getOwnPropertyDescriptor(Object.prototype, '__proto__');
        ok('descriptor exists', d !== undefined, true);
        if (d !== undefined) {
            ok('getter is a function', typeof d.get, 'function');
            ok('setter is a function', typeof d.set, 'function');
            ok('enumerable', d.enumerable, false);
            ok('configurable', d.configurable, true);
            ok('no value field', 'value' in d, false);
        }

        // The three ways a script asks whether a property is there, which all
        // disagreed with the descriptor before this phase.
        ok('hasOwnProperty', Object.prototype.hasOwnProperty('__proto__'), true);
        ok('in operator', '__proto__' in Object.prototype, true);
        ok('getOwnPropertyNames',
           Object.getOwnPropertyNames(Object.prototype).indexOf('__proto__') >= 0, true);

        // It is own to Object.prototype and inherited, not own, everywhere else.
        ok('not own to {}', ({}).hasOwnProperty('__proto__'), false);
        ok('inherited by {}', '__proto__' in {}, true);

        // ---- and it still does what it did ------------------------------
        // Reading it is the prototype, for an object with no entry of its own.
        ok('plain read', ({}).__proto__, Object.prototype);
        (function () {
            var p = { marker: 1 };
            var o = { __proto__: p };          // the object-literal form
            ok('literal prototype', o.__proto__, p);
            ok('literal inherited property', o.marker, 1);
        })();

        // Writing it changes the prototype, and the new one supplies properties.
        (function () {
            var p = { marker: 7 };
            var o = {};
            o.__proto__ = p;
            ok('after assignment', o.__proto__, p);
            ok('assigned prototype supplies', o.marker, 7);
            ok('getPrototypeOf agrees', Object.getPrototypeOf(o), p);
        })();

        // `null` is the one primitive that means something.
        (function () {
            var o = {};
            o.__proto__ = null;
            ok('null clears the prototype', Object.getPrototypeOf(o), null);
            // Node answers undefined here, not null: with the prototype gone the
            // accessor is not on the chain any more, so there is nothing to ask.
            ok('read after null', o.__proto__, undefined);
        })();

        // Any other primitive is ignored, and the prototype does not move.
        (function () {
            var primitives = [42, 'text', true, Symbol('s'), undefined];
            for (var i = 0; i < primitives.length; i++) {
                var o = {};
                o.__proto__ = primitives[i];
                ok('primitive ignored at ' + i,
                   Object.getPrototypeOf(o) === Object.prototype, true);
            }
        })();

        // A cycle is a TypeError; it is the setter's own throw.
        (function () {
            var threw = false;
            try { var o = {}; o.__proto__ = o; } catch (e) { threw = e instanceof TypeError; }
            ok('cycle throws TypeError', threw, true);
        })();

        // ---- a property a script defines is the one that is read ---------
        // This is what the phase could have broken. The synthesis runs before
        // the property table is consulted, so it has to stand aside when the
        // object -- or Object.prototype itself -- has an entry of its own.
        (function () {
            var o = {};
            Object.defineProperty(o, '__proto__', { value: 42, configurable: true });
            ok('own data property wins', o.__proto__, 42);
            ok('defining did not move the prototype',
               Object.getPrototypeOf(o) === Object.prototype, true);
        })();
        (function () {
            var q = {};
            Object.defineProperty(q, '__proto__', { get: function () { return 'mine'; } });
            ok('own accessor wins', q.__proto__, 'mine');
        })();

        // ---- and it can itself be replaced ------------------------------
        // The last thing a script can do to a configurable property, and the
        // one a synthesis that always runs would hide. Both halves: a read
        // must call the new getter, and an assignment must call the new setter
        // rather than quietly moving the prototype.
        (function () {
            var before = Object.getOwnPropertyDescriptor(Object.prototype, '__proto__');
            // Without the accessor there is nothing to replace, and the cases
            // above have already recorded that; do not throw here on top of it.
            if (before === undefined || typeof before.set !== 'function') return;
            Object.defineProperty(Object.prototype, '__proto__', {
                get: function () { return 'replaced'; },
                set: function (v) { this.__was_set__ = v; },
                configurable: true
            });
            try {
                ok('replaced getter is called', ({}).__proto__, 'replaced');
                var o = {};
                o.__proto__ = 'value';
                ok('replaced setter is called', o.__was_set__, 'value');
                ok('replaced setter did not move the prototype',
                   Object.getPrototypeOf(o) === Object.prototype, true);
            } finally {
                // Put it back: every suite after this one should see the
                // engine's own accessor.
                Object.defineProperty(Object.prototype, '__proto__', {
                    get: before.get, set: before.set, configurable: true
                });
            }
            ok('restored', ({}).__proto__, Object.prototype);
        })();
    )JS");
    assert(js.eval("failures.join('; ')").to_string().empty());
}
