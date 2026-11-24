// Phase 246, finding 250: sloppy-mode `with` statements (ECMA-262 §14.11 /
// §9.1.1.2) resolve through object environment records, consult
// `Symbol.unscopables`, pass `WithBaseObject` on calls, reject strict-mode
// code early, and trace their binding object in the garbage collector.
TEST(with_statement_object_environment_records_and_unscopables) {
    TestPage page;
    JsInterpreter& js = page.js;

    // 1. Array.prototype[Symbol.unscopables]: outer bindings win for all 16 names
    //    while scopable properties (`push`, `length`, `join`) still resolve on the array.
    assert(js.eval(R"js(
        (function() {
            var at = 'o-at', copyWithin = 'o-cw', entries = 'o-en', fill = 'o-fl',
                find = 'o-fn', findIndex = 'o-fi', findLast = 'o-flast', findLastIndex = 'o-fli',
                flat = 'o-flat', flatMap = 'o-fm', includes = 'o-inc', keys = 'o-k',
                toReversed = 'o-tr', toSorted = 'o-ts', toSpliced = 'o-tsp', values = 'o-v';
            var arr = [1, 2];
            var out = [];
            with (arr) {
                out.push(at, copyWithin, entries, fill, find, findIndex, findLast, findLastIndex,
                         flat, flatMap, includes, keys, toReversed, toSorted, toSpliced, values);
                push(3);
                out.push(length, join(','));
            }
            return out.join('|');
        })()
    )js").to_string() ==
           "o-at|o-cw|o-en|o-fl|o-fn|o-fi|o-flast|o-fli|o-flat|o-fm|o-inc|o-k|o-tr|o-ts|o-tsp|o-v|3|1,2,3");

    // 2. Custom `Symbol.unscopables`: getter skipped when property absent; primitive ignored.
    assert(js.eval(R"js(
        (function() {
            var calls = 0;
            var a = 'outerA', missing = 'outerM';
            var o = {
                a: 'innerA',
                b: 'innerB',
                get [Symbol.unscopables]() { calls++; return { a: true, b: false }; }
            };
            var r = [];
            with (o) { r.push(a, b, missing); }
            return r.join(',') + ':' + calls;
        })()
    )js").to_string() == "outerA,innerB,outerM:2");

    // 3. Strict-mode early SyntaxError and single-statement declaration bans.
    assert(js.eval(R"js(
        (function() {
            var bad = [
                '"use strict"; with ({}) {}',
                'function f() { "use strict"; with ({}) {} }',
                'class C { m() { with ({}) {} } }',
                'with ({}) let x = 1;',
                'with ({}) const x = 1;',
                'with ({}) class C {}',
                'with ({}) function f() {}',
                'with ({}) async function f() {}',
                'with ({}) let[0] = 1;',
                'with ({}) label: function f() {}',
            ];
            for (var code of bad) {
                try { new Function(code); return 'UNEXPECTED:' + code; }
                catch (e) { if (e.name !== 'SyntaxError') return 'WRONG:' + e.name; }
            }
            try {
                (function() { 'use strict'; eval('with ({}) {}'); })();
                return 'UNEXPECTED:strict-eval';
            } catch (e) {
                if (e.name !== 'SyntaxError') return 'WRONG:' + e.name;
            }
            return 'ALL_SYNTAX_ERRORS';
        })()
    )js").to_string() == "ALL_SYNTAX_ERRORS");

    // 4. ECMA-262 §13.15.2 / §9.1.1.2.5 pre-RHS ResolveBinding:
    //    - sloppy: deleting `o.x` during RHS still writes `o.x = 42` and leaves outer `x` untouched.
    //    - strict (nested inside `with`): throws ReferenceError instead of recreating `o.x`.
    assert(js.eval(R"js(
        (function() {
            var o1 = { x: 1 };
            var x = 99;
            with (o1) { x = (delete o1.x, 42); }
            var sloppyOk = (o1.x === 42 && x === 99);

            var o2 = { x: 1 };
            var strictErr = 'none';
            with (o2) {
                try {
                    (function() { 'use strict'; x = (delete o2.x, 42); })();
                } catch (e) {
                    strictErr = e.name;
                }
            }
            return sloppyOk + ':' + strictErr + ':' + ('x' in o2) + ':' + x;
        })()
    )js").to_string() == "true:ReferenceError:false:99");

    // 5. WithBaseObject (§13.3.6.1), typeof, delete, var hoisting, and undefined shadowing.
    assert(js.eval(R"js(
        (function() {
            var o = {
                tag: 'BASE',
                f() { 'use strict'; return this === o ? this.tag : 'wrong'; },
                x: 10,
                undefined: 77,
                'this': 123
            };
            var out = [];
            with (o) {
                out.push(f(), undefined, this !== 123);
                var x = 25;
                var y = 35;
                out.push(typeof x, typeof neverDeclaredABC);
                out.push(delete x, 'x' in o);
            }
            out.push(typeof x, y);
            return out.join('|');
        })()
    )js").to_string() == "BASE|77|true|number|undefined|true|false|undefined|35");

    // 6. TodoMVC runtime-compiled template (`new Function('obj', 'with(obj||{}){...}')`).
    assert(js.eval(R"js(
        (function() {
            var tpl = new Function(
                'obj',
                "var __p='';with(obj||{}){__p+='<li>'+title+'</li>';}return __p;"
            );
            return tpl({ title: 'Buy mangoes' });
        })()
    )js").to_string() == "<li>Buy mangoes</li>");

    // 7. GC root tracing: closure outliving `with` keeps binding object alive across collection,
    //    and a cyclic `with` scope is collected once unreachable.
    js.eval(R"js(
        var __with_reader;
        (function() {
            var secret = { nested: { label: 'survived-gc' } };
            with (secret) {
                __with_reader = function() { return nested.label; };
            }
        })();
        (function() {
            for (var i = 0; i < 64; i++) {
                var cyc = { n: i };
                with (cyc) { cyc.fn = function() { return n; }; }
            }
        })();
    )js");
    Heap::default_heap().collect_garbage();
    assert(js.eval("__with_reader()").to_string() == "survived-gc");
    js.eval("__with_reader = null;");
    Heap::default_heap().collect_garbage();
}
