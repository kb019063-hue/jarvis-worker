// Phase 204, finding 208: the counter the walk found is the one it writes.
TEST(a_counter_is_written_where_the_walk_found_it) {
    using namespace litebird;
    // Phase 204. `i++` no longer builds a `Reference` -- a std::string copy of
    // the name and a copy of a JsValue -- reads the binding, converts it and
    // walks the scope chain a second time to write it. One walk finds the
    // binding and the write lands on it. A fast path that finds the wrong
    // binding, or refuses in different words, would change what a script
    // means; these are the cases that decide it. Each one makes the *engine*
    // throw where it is wrong, so the suite fails on the engine's own script
    // rather than on a comparison written here.
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        function fail(what) { throw new Error(what); }

        // The counter of a loop, and the answer the loop gives.
        (function () {
            var count = 0;
            for (var i = 0; i < 1000; i++) { count = count + i; }
            if (count !== 499500) fail('loop sum ' + count);
        })();

        // Prefix and postfix, on a number, on a variable that is a string, and
        // on an object property (`a.n++` is not the fast path and must agree
        // with it).
        (function () {
            var i = 5, j = 5, s = '4', a = { n: 5 };
            var post = i++, pre = ++j, text = s++, prop = a.n++;
            if (post !== 5 || i !== 6) fail('postfix ' + post + ' ' + i);
            if (pre !== 6 || j !== 6) fail('prefix ' + pre + ' ' + j);
            if (text !== 4 || s !== 5) fail('string counter ' + text + ' ' + s);
            if (prop !== 5 || a.n !== 6) fail('property counter ' + prop + ' ' + a.n);
            if ((i--) !== 6 || i !== 5) fail('decrement ' + i);
        })();

        // A shadowed counter is the inner one, and the outer is untouched.
        (function () {
            var i = 100;
            function inner() {
                var i = 0;
                for (i = 0; i < 4; i++) {}
                return i;
            }
            if (inner() !== 4) fail('shadowed counter ' + inner());
            if (i !== 100) fail('outer counter ' + i);
        })();

        // A write an eval makes to the counter is what the next iteration
        // reads: the binding is the same one.
        (function () {
            var i = 0, seen = '';
            for (i = 0; i < 3; i++) {
                // `i = 1` while `i` is 0: the update makes it 2, the body sees
                // 1, and the write truly landed on the same binding. (Checked
                // against Node.js v22.22.3, which prints 1,2,.)
                if (i === 0) eval('i = 1;');
                seen = seen + i + ',';
            }
            if (seen !== '1,2,') fail('eval wrote the counter: ' + seen);
        })();

        // A global counter, which resolves through the global scope.
        (function () {
            window.g = 0;
            function bump() { for (var k = 0; k < 5; k++) { window.g++; } }
            bump();
            if (window.g !== 5) fail('global counter ' + window.g);
        })();

        // Every refusal keeps its own exception and its own words.
        (function () {
            function attempt(body) {
                try { return 'no throw: ' + body(); }
                catch (e) { return e.constructor.name + ': ' + e.message; }
            }
            var constant = attempt(function () { const c = 1; c++; return c; });
            if (constant !== 'TypeError: Assignment to constant variable.') fail('const: ' + constant);
            var prefix = attempt(function () { const c = 1; return ++c; });
            if (prefix !== 'TypeError: Assignment to constant variable.') fail('const prefix: ' + prefix);
            var tdz = attempt(function () { tdz2++; let tdz2 = 2; return tdz2; });
            if (tdz !== "ReferenceError: Cannot access 'tdz2' before initialization") fail('tdz: ' + tdz);
        })();

        // A `for (let ...)` head gives every iteration its own binding, and a
        // function made in the body keeps the iteration it was made in.
        (function () {
            var fns = [];
            for (let n = 0; n < 3; n++) { fns.push(function () { return n; }); }
            var total = fns[0]() + fns[1]() + fns[2]();
            if (total !== 3) fail('per-iteration bindings ' + total);
        })();

        // A name that is not declared anywhere is a ReferenceError, not a
        // crash and not a silent success.
        (function () {
            try { neverDeclaredAnywhere++; return fail('no throw on an undeclared name'); }
            catch (e) {
                if (e.constructor.name !== 'ReferenceError') fail('undeclared: ' + e.constructor.name);
            }
        })();
    )JS");
}
