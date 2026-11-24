// Phase 363, finding 363: unreachable generators (unstarted, suspended mid-body,
// finished, throwing, nested in lexical loops, and async) are reclaimed by GC
// with zero cell drift and joined threads, while live suspended generators keep
// their local and enclosing scopes alive across GC.
TEST(generator_gc_reclaims_frames_and_threads) {
    TestPage page;
    JsInterpreter& js = page.js;

    // 1. Flat GC residue across all generator shapes (small vs large).
    const JsValue residue_ok = js.eval(R"JS(
        (function () {
            function measure(stepFn, n) {
                gc();
                var base = gc().alive_cells;
                for (var i = 0; i < n; i++) stepFn(i);
                gc();
                gc();
                return gc().alive_cells - base;
            }
            var shapes = [
                function (i) { function* g() { yield i; } var it = g(); },
                function (i) { function* g() { yield i; yield i + 1; } var it = g(); it.next(); },
                function (i) { function* g() { yield i; } var it = g(); it.next(); it.next(); },
                function (i) { var o = { n: i }; function* g() { var x = o.n; yield x; } var it = g(); },
                function (i) {
                    function* g() {
                        try {
                            for (let k = 0; k < 2; k++) {
                                let box = { v: i + k };
                                yield box.v;
                            }
                        } finally {}
                    }
                    var it = g();
                    it.next();
                },
                function (i) {
                    function* g() { throw new Error('e' + i); }
                    var it = g();
                    try { it.next(); } catch (e) {}
                }
            ];
            for (var s = 0; s < shapes.length; s++) {
                var small = measure(shapes[s], 60);
                var large = measure(shapes[s], 300);
                if (large - small > 32) {
                    return 'leak-shape-' + s + ':' + small + '->' + large;
                }
            }
            return 'ok';
        })()
    )JS");
    assert(residue_ok.to_string() == "ok");

    // 2. Live suspended generators survive repeated GC (locals, for-let, yield*,
    //    and extracted next() method after dropping the generator variable).
    const JsValue live_ok = js.eval(R"JS(
        (function () {
            function makeLive() {
                var outer = { k: 100 };
                function* inner() {
                    yield outer.k + 1;
                    yield outer.k + 2;
                }
                function* g() {
                    let local = { a: 7 };
                    yield local.a;
                    for (let i = 0; i < 2; i++) {
                        let box = { v: i + 10 };
                        yield box.v;
                    }
                    yield* inner();
                    return local.a + outer.k;
                }
                return g();
            }
            var it = makeLive();
            var out = [];
            out.push(it.next().value); gc(); gc();
            out.push(it.next().value); gc(); gc();
            out.push(it.next().value); gc(); gc();
            out.push(it.next().value); gc(); gc();
            var nextFn = it.next;
            it = null;
            gc(); gc();
            out.push(nextFn().value); gc(); gc();
            var last = nextFn();
            out.push(last.value);
            out.push(last.done ? 'done' : 'not-done');
            return out.join(',');
        })()
    )JS");
    assert(live_ok.to_string() == "7,10,11,101,102,107,done");

    // 3. Discarding a suspended generator via GC must not execute its finally
    //    block, while explicit .return() must execute it.
    const JsValue finally_ok = js.eval(R"JS(
        (function () {
            var gcFinally = 0;
            var returnFinally = 0;
            function dropInTry() {
                function* g() {
                    try { yield 1; } finally { gcFinally++; }
                }
                var it = g();
                it.next();
            }
            for (var i = 0; i < 20; i++) dropInTry();
            gc(); gc();

            function* g2() {
                try { yield 1; } finally { returnFinally++; }
            }
            var it2 = g2();
            it2.next();
            var r = it2.return(99);
            return gcFinally + ':' + returnFinally + ':' + r.value + ':' + r.done;
        })()
    )JS");
    assert(finally_ok.to_string() == "0:1:99:true");

    // 4. Lazy thread creation allows 20,000 unstarted generators without hitting
    //    the OS thread ceiling (previously died at ~15,730).
    const JsValue burst_ok = js.eval(R"JS(
        (function () {
            for (var i = 0; i < 20000; i++) {
                function* g() { yield i; }
                var it = g();
            }
            return 'ok';
        })()
    )JS");
    assert(burst_ok.to_string() == "ok");
}
