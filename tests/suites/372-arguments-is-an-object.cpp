// Phase 372, finding 372: `arguments` was a copy.
//
// `arguments` was built as an ordinary array of the call's values, so every
// place in it was a *second* copy of a parameter. Writing one never reached the
// other:
//
//     function f(a) { arguments[0] = 42; return a; }        // 1, should be 42
//     function g(a) { a = 99; return arguments[0]; }        // 1, should be 99
//
// and the object had no `callee` at all. §10.4.4 makes a sloppy function's
// arguments object a *mapped* arguments exotic object: for each simple
// parameter there is one binding, and the object's place and the parameter are
// the same thing seen twice. A strict function's parameters and its arguments
// are separate from the first line (§10.4.4 note), which is why `strict (a)`
// below must still answer 1.
//
// The mapping is by *name*, resolved in the call's environment on every access,
// because `bind_parameters` runs after the object is built and a parameter's
// binding is never removed. Delete removes the element for good; defining a
// descriptor either lands on the binding (and keeps the mapping) or unmaps the
// place, depending on what it says.
//
// Every expectation here was measured against Node v22.22.3 first. The script
// collects failures rather than throwing, so one broken case does not stop the
// others and the `assert` below is what fails the suite.
TEST(arguments_is_an_object) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function ok(name, actual, expected) {
            if (actual !== expected) failures.push(name + ': got ' + actual + ', want ' + expected);
        }

        // ---- the two directions of one binding --------------------------
        (function () {
            function write(a) { arguments[0] = 42; return a; }
            ok('write reaches the parameter', write(1), 42);
            function read(a) { a = 99; return arguments[0]; }
            ok('parameter write reaches the place', read(1), 99);
            // A place past the parameter count is not mapped: there is no
            // parameter to be one binding with.
            function extra(a) { arguments[1] = 5; return arguments[1]; }
            ok('unmapped place is its own', extra(1), 5);
        })();

        // Strict code is the whole point of the distinction: the mapping is
        // not made at all there.
        (function () {
            function strict(a) { 'use strict'; a = 99; return arguments[0]; }
            ok('strict does not map', strict(1), 1);
            function strict2(a) { 'use strict'; arguments[0] = 42; return a; }
            ok('strict does not map, either way', strict2(1), 1);
        })();

        // ---- callee ------------------------------------------------------
        (function () {
            function f() { return arguments.callee; }
            ok('callee is the function', f() === f, true);
            ok('callee is a function', typeof f(), 'function');
            // enumerable: false, writable and configurable: true, which is the
            // only way a script can be told which it is.
            var d = Object.getOwnPropertyDescriptor(f('x'), 'callee');
            ok('callee enumerable', d.enumerable, false);
            ok('callee writable', d.writable, true);
            ok('callee configurable', d.configurable, true);
            // Strict code has no `callee` at all, not a `callee` that is
            // undefined: nothing owns the name.
            function s() { 'use strict'; return Object.prototype.hasOwnProperty.call(arguments, 'callee'); }
            ok('strict has no callee', s(), false);
            // An arrow's arguments is its parent's, so there is nothing of its
            // own to hold a callee.
            function outer() { var f = () => typeof arguments.callee; return f(); }
            ok('arrow uses the outer arguments', outer(), 'function');
        })();

        // ---- delete ------------------------------------------------------
        // §10.4.4.2 step 13: a delete removes that one mapping and the element,
        // and later writes to the parameter must not come back through it.
        (function () {
            function f(a, b) {
                delete arguments[0];
                var r = [arguments[0], b];
                a = 5;
                r.push(arguments[0]);
                r.push('0' in arguments);
                return r;
            }
            var r = f(1, 7);
            ok('deleted place reads undefined', r[0], undefined);
            ok('the other place still answers', r[1], 7);
            ok('a later write does not reappear', r[2], undefined);
            ok('and it is not in the object', r[3], false);
        })();

        // ---- Object.keys / Reflect.ownKeys --------------------------------
        (function () {
            function f(a, b) { return Object.keys(arguments).join(); }
            ok('keys are the places', f(1, 2), '0,1');
            function g(a, b) { delete arguments[0]; return Object.keys(arguments).join(); }
            ok('a deleted place is not a key', g(1, 2), '1');
        })();

        // ---- Object.defineProperty ---------------------------------------
        (function () {
            // A value in the descriptor is written into the parameter and the
            // place stays mapped: `{value: 5}` then `a = 9` reads 9.
            function f(a) {
                Object.defineProperty(arguments, '0', { value: 5 });
                var r = [a, arguments[0]];
                a = 9;
                r.push(arguments[0]);
                return r.join();
            }
            ok('value in a descriptor reaches the parameter', f(1), '5,5,9');

            // `writable: false` is what unmaps: the place can no longer follow
            // its parameter, and it keeps the value it had.
            function g(a) {
                Object.defineProperty(arguments, '0', { writable: false });
                a = 9;
                var r = [arguments[0]];
                var d = Object.getOwnPropertyDescriptor(arguments, '0');
                r.push(d.value, d.writable, d.enumerable, d.configurable);
                return r.join();
            }
            ok('writable: false unmaps and keeps the value', g(1), '1,1,false,true,true');

            // The attributes a descriptor asks for are the place's, and the
            // place keeps following its parameter while it is mapped.
            function h(a) {
                Object.defineProperty(arguments, '0', { enumerable: false });
                a = 9;
                var d = Object.getOwnPropertyDescriptor(arguments, '0');
                return [arguments[0], d.value, d.enumerable, d.writable, Object.keys(arguments).length].join();
            }
            ok('enumerable: false is kept', h(1), '9,9,false,true,0');

            function i(a) {
                Object.defineProperty(arguments, '0', { configurable: false });
                a = 9;
                var d = Object.getOwnPropertyDescriptor(arguments, '0');
                return [arguments[0], d.configurable, d.enumerable].join();
            }
            ok('configurable: false is kept', i(1), '9,false,true');

            // An accessor descriptor always unmaps, and the accessor is what a
            // read answers -- Node says 20 while the parameter still says 1.
            function j(a) {
                Object.defineProperty(arguments, '0', { get: function () { return 20; } });
                return [arguments[0], a].join();
            }
            ok('an accessor takes the place', j(1), '20,1');
        })();

        // ---- and the plain reads still agree ------------------------------
        (function () {
            function f(a, b) {
                return [arguments.length, arguments[0], arguments[1], Array.isArray(arguments)].join();
            }
            ok('length and places', f(1, 2), '2,1,2,false');
            // An arguments object is not an array and does not inherit from
            // Array.prototype (Phase 123), but it is iterable (§10.4.4.7).
            function g(a, b) { return [...arguments].join(); }
            ok('still iterable', g(1, 2), '1,2');
        })();
    )JS");
    assert(js.eval("failures.join('; ')").to_string().empty());
}
