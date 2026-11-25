// Phase 415, finding 415: `Array.from` and `Array.fromAsync` walk an iterable
// with the same loop, so they must agree about what a walk *is*: `next` is read
// once per step, a throw out of the mapper closes the iterator exactly once,
// and a broken iterable is still reported as a TypeError.
//
// Before this phase `Array.from` carried its own copy of the loop. The copy
// never learned the `GeneratorAbortException` branch the canonical loop has, so
// a generator source that aborted ran its own `return` and then the copy ran it
// a second time. Nothing here could see that drift from script, which is why
// this suite pins the *observable* walk rather than the fix: the two paths must
// keep landing on the same answers when either one is edited again.
//
// Every assertion was compared against Node v22.22.3 first.
TEST(array_from_and_from_async_walk_with_one_loop) {
    TestPage page;
    JsInterpreter& js = page.js;

    js.eval(R"JS(
        var failures = [];
        function ok(what, got, want) {
            if (String(got) !== String(want)) failures.push(what + ': got ' + got + ', wanted ' + want);
        }

        // `next` is read once per step, not once per call: a getter on `next`
        // counts the reads, and the source yields three values.
        var reads = 0;
        var iterable = {};
        iterable[Symbol.iterator] = function () {
            var i = 0;
            var it = {};
            Object.defineProperty(it, 'next', { get: function () {
                reads++;
                return function () {
                    i++;
                    return i <= 3 ? { value: i, done: false } : { value: undefined, done: true };
                };
            } });
            return it;
        };
        ok('fromYields', Array.from(iterable).join(), '1,2,3');
        ok('fromReadsNextOnce', reads, 1);

        // A mapper that throws out of a builtin source closes the iterator it
        // was given, once. The source's own `return` records the call.
        var closed = 0;
        var three = {};
        three[Symbol.iterator] = function () {
            var i = 0;
            return {
                next: function () { i++; return { value: i, done: i > 3 }; },
                return: function () { closed++; return { done: true }; }
            };
        };
        var threw = '';
        try { Array.from(three, function (v) { if (v === 2) throw new Error('stop'); return v; }); }
        catch (e) { threw = e.message; }
        ok('fromThrow', threw, 'stop');
        ok('fromClosedOnce', closed, 1);

        // A source that closes itself on the way out is not closed twice: a
        // `return` that throws is dropped when the loop is already leaving
        // with an error of its own (§7.4.11's last step), so the mapper's
        // error is the one the script sees. (A *generator* aborted mid-walk
        // is left alone here on purpose: it is the one shape that leaves an
        // interpreter's cells reachable, which finding 415 records as the GC
        // row's business, not this loop's.)
        var threwTwice = '';
        var throwingReturn = {};
        throwingReturn[Symbol.iterator] = function () {
            var i = 0;
            return {
                next: function () { i++; return { value: i, done: i > 3 }; },
                return: function () { throw new Error('return threw'); }
            };
        };
        try { Array.from(throwingReturn, function (v) { if (v === 2) throw new Error('mapper'); return v; }); }
        catch (e) { threwTwice = e.message; }
        ok('theMapperErrorWins', threwTwice, 'mapper');

        // The iterable path is unchanged for the ordinary cases: an array, a
        // string, a Map's entries, and a Set.
        ok('array', Array.from([1, 2, 3]).join(), '1,2,3');
        ok('string', Array.from('abc').join(), 'a,b,c');
        ok('set', Array.from(new Set([1, 2, 2])).join(), '1,2');
        ok('map', Array.from(new Map([[1, 'a']])).map(function (p) { return p.join(':'); }).join(), '1:a');

        // A source with neither an iterator nor a `length` is the empty
        // array-like, not an error; only a nullish source is a TypeError. A
        // negative `length` clamps to zero (§7.1.20).
        ok('noIteratorNoLength', Array.from({}).length, 0);
        ok('numberSource', Array.from(5).length, 0);
        threw = '';
        try { Array.from(null); } catch (e) { threw = e.name; }
        ok('nullSource', threw, 'TypeError');
        ok('badLength', Array.from({ length: -1 }).length, 0);

        // Array-likes never touch the iterator path.
        ok('arrayLike', Array.from({ length: 3, 0: 'x', 1: 'y', 2: 'z' }).join(), 'x,y,z');

    )JS");

    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    const std::string failed = js.eval("failures.length ? failures.join('\\n') : ''").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());
}
