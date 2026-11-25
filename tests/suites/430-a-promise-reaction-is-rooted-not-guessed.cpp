// Phase 430, finding 430: a pending promise with reactions owns its state
// through the pending-promise registry, and a settled promise is not kept
// alive just because a resolver is.
TEST(a_promise_reaction_is_rooted_not_guessed) {
    TestPage page;
    JsInterpreter& js = page.js;

    // A settled promise whose resolvers are still held by script should not
    // keep the object it was settled with: `rs` owns the resolver functions,
    // and after the settle nobody can observe the result except through the
    // promise object, which this loop drops. The primitive-valued promises
    // keep the same baseline, so the difference is the result object's cell.
    const JsValue residue = js.eval(R"JS(
        (function () {
            function cells(n, object) {
                var resolvers = [];
                for (var i = 0; i < n; i++) {
                    var p = new Promise(function (resolve, reject) {
                        resolve(object ? { n: i } : i);
                        resolvers.push(resolve);
                    });
                    p = null;
                }
                gc();
                gc();
                return gc().alive_cells;
            }
            var small_object = cells(20, true);
            var large_object = cells(60, true);
            var small_primitive = cells(20, false);
            var large_primitive = cells(60, false);
            return (large_object - small_object) - (large_primitive - small_primitive);
        })()
    )JS");
    assert(residue.to_number() == 0);

    // A promise may be reachable only from native code while it is pending, so
    // the reaction registered on it still has to run after the script reference
    // is dropped and a collection happens before the resolver is called.
    js.eval(R"JS(
        var kept_resolve, kept_ran = 'no';
        (function () {
            var p = new Promise(function (resolve, reject) { kept_resolve = resolve; });
            p.then(function (value) { kept_ran = value; });
            p = null;
            gc();
            gc();
        })();
    )JS");
    js.eval("kept_resolve('yes');");
    EventLoop::default_loop().run_microtasks();
    assert(js.eval("kept_ran").to_string() == "yes");

    // The ordinary path is unchanged: a reachable, settled promise still sees
    // its result through a later collection.
    js.eval(R"JS(
        var kept_resolver, kept_promise, kept_value;
        (function () {
            kept_promise = new Promise(function (resolve, reject) {
                kept_resolver = resolve;
                resolve({ fine: 42 });
            });
            gc();
            gc();
        })();
        kept_promise.then(function (value) { kept_value = value.fine; });
    )JS");
    EventLoop::default_loop().run_microtasks();
    assert(js.eval("kept_value").to_number() == 42);
}
