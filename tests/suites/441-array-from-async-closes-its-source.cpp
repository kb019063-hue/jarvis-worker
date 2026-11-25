#include "litebird/js/JsOps.hpp"

// Phase 441, finding 441: `Array.fromAsync` closes the iterator it was given.
//
// Phase 415 measured the gap and filed it: the async walk was a third copy of
// the iterator loop, it read `next` at every step instead of once, and it never
// called an iterator's `return` — not on a mapper that throws, which is the one
// place the spec is most explicit about.
//
// §23.1.3.1 puts IfAbruptCloseAsyncIterator (§7.4.16 → §7.4.15) at exactly
// three places, and nowhere else:
//
//   - the mapper's *call* throws (step 6.i.i);
//   - the mapper's *result* rejects (step 6.i.iii);
//   - CreateDataPropertyOrThrow fails (step 6.i.iv).
//
// A `next` that throws, an `await` that rejects, a step that is not an object,
// and a `done`/`value` read that throws are all `?` steps: the algorithm returns
// with its own error and leaves the iterator alone. §7.4.15 then decides who
// wins: a throw completion the caller already has beats anything the close does
// (step 5), an abrupt close beats a normal completion (steps 3 and 6), and a
// `return` that answers a non-object once awaited is a TypeError (step 7).
//
// **Node v22.22.3 is not the oracle for several edge cases.** It closes on a
// rejected async `next`, and V8's async-from-sync wrapper closes the sync source
// when `next()` throws or a `done`/`value` read throws; the spec's `?` steps do
// not. A rejected Promise returned as the sync iterator's *result object* is
// also left unhandled by V8 and terminates Node before a comparison can finish.
// Those cases are asserted against the algorithm and are identified below; the
// ordinary behavior is compared with Node line for line.
TEST(array_from_async_closes_its_source) {
    TestPage page;
    JsInterpreter& js = page.js;

    js.eval(R"JS(
        var failures = [];
        function ok(what, got, want) {
            if (String(got) !== String(want)) failures.push(what + ': got ' + got + ', wanted ' + want);
        }
        // A sync iterable whose next and return are the two functions given.
        function syncSrc(nextImpl, retImpl, counts) {
            var o = {};
            o[Symbol.iterator] = function () {
                var it = {};
                Object.defineProperty(it, 'next', { get: function () {
                    if (counts) counts.reads++;
                    return nextImpl;
                } });
                if (retImpl) it['return'] = retImpl;
                return it;
            };
            return o;
        }
        // An async iterable that yields 1,2,3 and counts the reads of `next`
        // and the calls to `return`.
        function asyncSrc(opts) {
            var o = {};
            o[Symbol.asyncIterator] = function () {
                var i = 0;
                var it = {};
                Object.defineProperty(it, 'next', { get: function () {
                    opts.reads++;
                    return function () {
                        i++;
                        if (opts.nextRejects) return Promise.reject(new Error('nope'));
                        return Promise.resolve(i <= 3 ? { value: i, done: false }
                                                      : { value: undefined, done: true });
                    };
                } });
                if (opts.withReturn !== false) {
                    it['return'] = function () { opts.closed++; return Promise.resolve({ done: true }); };
                }
                return it;
            };
            return o;
        }

        // ---- an async iterator: the mapper throws, which closes (6.i.i) ----
        var a = { reads: 0, closed: 0 };
        Array.fromAsync(asyncSrc(a), function (v) { if (v === 2) throw new Error('m'); return v; })
            .then(function () { failures.push('asyncMapper: resolved'); },
                  function (e) {
                      ok('asyncMapperError', e.message, 'm');
                      ok('asyncMapperClosedOnce', a.closed, 1);
                      // GetIteratorFromMethod reads `next` once and keeps it.
                      ok('asyncMapperReadsNextOnce', a.reads, 1);
                  });

        // ---- a walk that completes closes nothing, and still reads once ----
        var c = { reads: 0, closed: 0 };
        Array.fromAsync(asyncSrc(c))
            .then(function (r) {
                ok('asyncValues', r.join(), '1,2,3');
                ok('asyncCompleteNotClosed', c.closed, 0);
                ok('asyncCompleteReadsNextOnce', c.reads, 1);
            });

        // A rejected `next` is a `?` step, not an IfAbruptClose site. This is
        // deliberately spec-asserted: Node 22/V8 closes here instead.
        var b = { reads: 0, closed: 0, nextRejects: true };
        Array.fromAsync(asyncSrc(b))
            .then(function () { failures.push('asyncNextRejects: resolved'); },
                  function (e) {
                      ok('asyncNextRejectError', e.message, 'nope');
                      ok('asyncNextRejectDoesNotClose', b.closed, 0);
                      ok('asyncNextRejectReadsNextOnce', b.reads, 1);
                  });

        // A throwing done getter is another `?` step and leaves the async
        // iterator alone.
        var aStep = { closed: 0 };
        var badDone = {};
        Object.defineProperty(badDone, 'done', { get: function () { throw new Error('adone'); } });
        var badDoneSource = {};
        badDoneSource[Symbol.asyncIterator] = function () {
            return { next: function () { return Promise.resolve(badDone); },
                     'return': function () { aStep.closed++; return Promise.resolve({ done: true }); } };
        };
        Array.fromAsync(badDoneSource)
            .then(function () { failures.push('asyncDoneGetter: resolved'); },
                  function (e) {
                      ok('asyncDoneGetterError', e.message, 'adone');
                      ok('asyncDoneGetterDoesNotClose', aStep.closed, 0);
                  });

        // ---- a sync iterator: the mapper throws, which closes (6.i.i) ----
        var s = { closed: 0, reads: 0 }, i = 0;
        Array.fromAsync(syncSrc(function () { i++; return { value: i, done: i > 3 }; },
                               function () { s.closed++; return { done: true }; }, s),
                        function (v) { if (v === 2) throw new Error('sm'); return v; })
            .then(function () { failures.push('syncMapper: resolved'); },
                  function (e) {
                      ok('syncMapperError', e.message, 'sm');
                      ok('syncMapperClosedOnce', s.closed, 1);
                      ok('syncMapperReadsNextOnce', s.reads, 1);
                  });

        // ---- a sync iterator's value is awaited before the mapper sees it ----
        Array.fromAsync([Promise.resolve(7), 8], function (v) {
            ok('syncMapperSeesSettledValue', typeof v, 'number');
            return v;
        }).then(function (r) { ok('syncValuesAwaited', r.join(), '7,8'); });

        // Async-from-sync closes the underlying sync iterator if a yielded
        // value rejects; this happens inside its `next` promise, before the
        // Array.fromAsync source-step rejection (which itself does not close).
        var sv = { closed: 0 };
        Array.fromAsync(syncSrc(function () {
                            return { value: Promise.reject(new Error('sourceValueReject')), done: false };
                        },
                        function () { sv.closed++; return { done: true }; }))
            .then(function () { failures.push('syncValueReject: resolved'); },
                  function (e) {
                      ok('syncValueRejectError', e.message, 'sourceValueReject');
                      ok('syncValueRejectClosesOnce', sv.closed, 1);
                  });

        // ---- a mapper's result is awaited too ----
        Array.fromAsync([1, 2], function (x) { return Promise.resolve(x * 3); })
            .then(function (r) { ok('mapperResultAwaited', r.join(), '3,6'); });

        // ---- a mapper with no `return` to call still reports its own error ----
        var i7 = 0;
        Array.fromAsync(syncSrc(function () { i7++; return { value: i7, done: false }; }),
                        function () { throw new Error('n7'); })
            .then(function () { failures.push('noReturn: resolved'); },
                  function (e) { ok('noReturnKeepsMapperError', e.message, 'n7'); });

        // ---- the mapper's result REJECTS: that closes too (6.i.iii) ----
        var s13 = { closed: 0 }, i13 = 0;
        Array.fromAsync(syncSrc(function () { i13++; return { value: i13, done: i13 > 3 }; },
                               function () { s13.closed++; return { done: true }; }),
                        function (v) { if (v === 2) return Promise.reject(new Error('rejm')); return v; })
            .then(function () { failures.push('rejectedMapperResult: resolved'); },
                  function (e) {
                      ok('rejectedMapperResultError', e.message, 'rejm');
                      ok('rejectedMapperResultClosed', s13.closed, 1);
                  });

        // CreateDataPropertyOrThrow is another close site, even without a
        // mapper. A constructible receiver can return a non-extensible object.
        function RefusesDefinition() { return Object.preventExtensions({}); }
        var s14 = { closed: 0 };
        Array.fromAsync.call(RefusesDefinition,
            syncSrc(function () { return { value: 1, done: false }; },
                    function () { s14.closed++; return { done: true }; }))
            .then(function () { failures.push('createFailure: resolved'); },
                  function (e) {
                      ok('createFailureTypeError', e.name, 'TypeError');
                      ok('createFailureClosesOnce', s14.closed, 1);
                  });
    )JS");

    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    std::string failed = js.eval("failures.length ? failures.join('\\n') : ''").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());

    // ---- §7.4.15's own decisions, and the steps that must NOT close ----
    //
    // These are the cases Node is not an oracle for (see the head comment), so
    // each was read off the algorithm instead: the `?` steps of §23.1.3.1 step
    // 5.g do not close, and the completion the caller carries decides (step 5).
    js.eval(R"JS(
        var failures2 = [];
        function ok2(what, got, want) {
            if (String(got) !== String(want)) failures2.push(what + ': got ' + got + ', wanted ' + want);
        }
        function syncSrc2(nextImpl, retImpl) {
            var o = {};
            o[Symbol.iterator] = function () {
                var it = { next: nextImpl };
                if (retImpl !== undefined) it['return'] = retImpl;
                return it;
            };
            return o;
        }

        // `next` throws: a `?` step in the algorithm, so no close, and the
        // iterator's own error is the rejection.
        var n1 = { closed: 0 };
        Array.fromAsync(syncSrc2(function () { throw new Error('sn'); },
                                 function () { n1.closed++; return { done: true }; }))
            .then(function () { failures2.push('nextThrows: resolved'); },
                  function (e) {
                      ok2('nextThrowsError', e.message, 'sn');
                      ok2('nextThrowsDoesNotClose', n1.closed, 0);
                  });

        // A throwing `done` getter is a `?` step in the async-from-sync
        // wrapper and does not close the source.
        var n2d = { closed: 0 };
        Array.fromAsync(syncSrc2(function () {
                            var st = { value: 1 };
                            Object.defineProperty(st, 'done', { get: function () { throw new Error('sd'); } });
                            return st;
                        },
                        function () { n2d.closed++; return { done: true }; }))
            .then(function () { failures2.push('doneThrows: resolved'); },
                  function (e) {
                      ok2('doneThrowsError', e.message, 'sd');
                      ok2('doneThrowsDoesNotClose', n2d.closed, 0);
                  });

        // The step's `value` is read through a getter that throws: also a `?`
        // step, and it does not close either.
        var n2 = { closed: 0 };
        Array.fromAsync(syncSrc2(function () {
                            var st = { done: false };
                            Object.defineProperty(st, 'value', { get: function () { throw new Error('sv'); } });
                            return st;
                        },
                        function () { n2.closed++; return { done: true }; }))
            .then(function () { failures2.push('valueThrows: resolved'); },
                  function (e) {
                      ok2('valueThrowsError', e.message, 'sv');
                      ok2('valueThrowsDoesNotClose', n2.closed, 0);
                  });

        // A non-object sync step rejects through the async-from-sync wrapper;
        // the Array.fromAsync source-step `?` still must not close it.
        var n2o = { closed: 0 };
        Array.fromAsync(syncSrc2(function () { return 5; },
                                 function () { n2o.closed++; return { done: true }; }))
            .then(function () { failures2.push('nonObjectStep: resolved'); },
                  function (e) {
                      ok2('nonObjectStepTypeError', e.name, 'TypeError');
                      ok2('nonObjectStepDoesNotClose', n2o.closed, 0);
                  });

        // The mapper threw AND the close's `return` rejects: the mapper's error
        // is the one the caller carries, so step 5 keeps it and the close's own
        // rejection never surfaces.
        var n3 = { closed: 0 }, i3 = 0;
        Array.fromAsync(syncSrc2(function () { i3++; return { value: i3, done: i3 > 3 }; },
                                 function () {
                                     n3.closed++;
                                     return { done: true, value: Promise.reject(new Error('retrej')) };
                                 }),
                        function (v) { if (v === 2) throw new Error('m3'); return v; })
            .then(function () { failures2.push('closeRejects: resolved'); },
                  function (e) {
                      ok2('closeRejectsKeepsMapperError', e.message, 'm3');
                      ok2('closeRejectsClosedOnce', n3.closed, 1);
                  });

        // A sync `return` that answers something that is not an object makes
        // the async-from-sync wrapper reject with TypeError. The caller already
        // has a mapper throw, so AsyncIteratorClose step 5 preserves that throw.
        var n4 = {}, i4 = 0;
        Array.fromAsync(syncSrc2(function () { i4++; return { value: i4, done: i4 > 3 }; },
                                 function () { return 5; }),
                        function (v) { if (v === 2) throw new Error('m4'); return v; })
            .then(function () { failures2.push('nonObjectReturn: resolved'); },
                  function (e) { ok2('nonObjectReturnKeepsMapperError', e.message, 'm4'); });

        // A `return` that is not callable: GetMethod's refusal is an abrupt
        // step 3, and again the mapper's own error wins (step 5).
        var n5 = {}, i5 = 0;
        Array.fromAsync(syncSrc2(function () { i5++; return { value: i5, done: i5 > 3 }; }, 42),
                        function (v) { if (v === 2) throw new Error('m5'); return v; })
            .then(function () { failures2.push('nonCallableReturn: resolved'); },
                  function (e) { ok2('nonCallableReturnKeepsMapperError', e.message, 'm5'); });

        // The completion kind is not inferred from its value: `throw
        // undefined` still wins over a return value that rejects.
        var n6 = { closed: 0 }, i6 = 0;
        Array.fromAsync(syncSrc2(function () { i6++; return { value: i6, done: i6 > 3 }; },
                                 function () {
                                     n6.closed++;
                                     return { done: true, value: Promise.reject(new Error('closeRejects')) };
                                 }),
                        function (v) { if (v === 2) throw undefined; return v; })
            .then(function () { failures2.push('throwUndefined: resolved'); },
                  function (e) {
                      ok2('throwUndefinedPreserved', e === undefined, true);
                      ok2('throwUndefinedClosesOnce', n6.closed, 1);
                  });
    )JS");

    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    failed = js.eval("failures2.length ? failures2.join('\\n') : ''").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());

    // AsyncIteratorClose with a normal completion must surface a `return`
    // answer that is not an object (step 7); the fromAsync walk above exercises
    // the throw-completion path, where step 5 correctly keeps the caller's
    // error instead.
    bool saw_close_type_error = false;
    try {
        const JsValue iterator = js.eval("({ 'return': function () { return 5; } })");
        close_async_iterator(iterator, JsValue::make_number(17), false,
                             [](const JsValue& value) { return value; });
    } catch (const JsThrow& thrown) {
        saw_close_type_error = thrown.value.get_property("name").to_string() == "TypeError";
    }
    assert(saw_close_type_error);
}
