// Phase 412, finding 412: Array.fromAsync (§23.1.3.1), the method the row
// "A script can read and write the engine's own slots" named as absent.
//
// The method's defining property is that it *awaits*: every value the source
// produces, and the mapper's result for each. A built-in has no frame to park,
// so the wait is the blocking one `await` uses off the interpreter — the event
// loop is run until the promise settles.
//
// Every assertion was compared against Node v22.22.3 first.
TEST(array_from_async_awaits_every_value) {
    TestPage page;
    JsInterpreter& js = page.js;

    js.eval(R"JS(
        var out = [];
        var failures = [];
        function ok(what, got, want) {
            if (String(got) !== String(want)) failures.push(what + ': got ' + got + ', wanted ' + want);
        }
        function done(what, got, want) {
            return function (v) { ok(what, got(v), want); };
        }

        // An ordinary array, in order.
        Array.fromAsync([1, 2, 3]).then(done('array', function (a) { return a.join(); }, '1,2,3'));
        // An empty one is an empty array, not a rejection.
        Array.fromAsync([]).then(done('empty', function (a) { return a.length; }, '0'));
        // The mapper runs on each value, and its result is awaited too.
        Array.fromAsync([1, 2], function (x) { return x * 2; })
            .then(done('mapped', function (a) { return a.join(); }, '2,4'));
        Array.fromAsync([1, 2], function (x) { return Promise.resolve(x * 3); })
            .then(done('mappedPromise', function (a) { return a.join(); }, '3,6'));
        // A promise in the source is awaited, not stored.
        Array.fromAsync([Promise.resolve(1), 2])
            .then(done('promises', function (a) { return a.join(); }, '1,2'));
        // A string is iterable.
        Array.fromAsync('ab').then(done('string', function (a) { return a.join(); }, 'a,b'));
        // An array-like source: `length` once, then each place, awaited.
        Array.fromAsync({ length: 2, 0: 'a', 1: 'b' })
            .then(done('arrayLike', function (a) { return a.join(); }, 'a,b'));
        Array.fromAsync({ length: 2, 0: Promise.resolve('p'), 1: 'q' })
            .then(done('arrayLikePromises', function (a) { return a.join(); }, 'p,q'));

        // The async iterator is asked for *before* the sync one (§23.1.3.1).
        var asyncIterable = {};
        asyncIterable[Symbol.asyncIterator] = function () {
            var i = 0;
            return { next: function () {
                i++;
                return Promise.resolve(i <= 3 ? { value: i * 10, done: false }
                                              : { value: undefined, done: true });
            } };
        };
        Array.fromAsync(asyncIterable)
            .then(done('asyncIterable', function (a) { return a.join(); }, '10,20,30'));

        // A rejected value makes the whole call reject, with that reason.
        Array.fromAsync([Promise.reject(new Error('no'))])
            .then(function () { failures.push('rejected: resolved'); },
                  function (e) { ok('rejected', e.message, 'no'); });
        // A mapper that throws rejects too, rather than throwing out of the call.
        Array.fromAsync([1], function () { throw new Error('mthrow'); })
            .then(function () { failures.push('mapperThrows: resolved'); },
                  function (e) { ok('mapperThrows', e.message, 'mthrow'); });
        // A mapper that is not a function rejects with a TypeError.
        Array.fromAsync([1], 5)
            .then(function () { failures.push('badMapper: resolved'); },
                  function (e) { ok('badMapper', e.name, 'TypeError'); });
        // A source that is neither iterable nor array-like rejects.
        Array.fromAsync(null)
            .then(function () { failures.push('nullSource: resolved'); },
                  function (e) { ok('nullSource', e.name, 'TypeError'); });

        // The mapper gets the value and the index.
        var seen = [];
        Array.fromAsync([7, 8], function (v, i) { seen.push(i); return v; })
            .then(function () { ok('mapArgs', seen.join(), '0,1'); });

        // `this` is the constructor that makes the result, and a receiver that
        // is not a constructor makes a plain array.
        var C = function (n) { this.n = n; this.length = 0; };
        Array.fromAsync.call(C, [1, 2])
            .then(function (r) { ok('receiver', (r instanceof C) + '/' + r.n, 'true/undefined'); });
        Array.fromAsync.call({}, [1, 2])
            .then(done('nonCtorReceiver', function (r) { return Array.isArray(r) + '/' + r.join(); }, 'true/1,2'));

        // What it answers is a promise, not an array.
        ok('answersAPromise', typeof Array.fromAsync([]).then, 'function');
        ok('length', Array.fromAsync.length, 1);
        ok('name', Array.fromAsync.name, 'fromAsync');
    )JS");

    // Nothing here depends on how long the timers took: the loop runs until
    // there is no work left.
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    const std::string failed = js.eval("failures.length ? failures.join('\\n') : ''").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());
    assert(js.eval("out.length").to_string() == "0");
}
