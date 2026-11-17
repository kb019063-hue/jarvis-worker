# 51. ✅ Promise combinators resolved values only after consuming the iterable

> Moved out of `AUDIT.md` — written there as a `## 51. ✅ Promise combinators resolved values only after consuming the iterable` section.
> The text below is the section as it stood, unchanged.


**The hole.** `Promise.all`, `any`, `race` and `allSettled` handed every value
straight to `Promise::to_promise` inside `js_iterate`. That bypassed the
constructor's overridable `resolve`; an iterator with no end was consumed
forever before its first value could throw. A returned promise's `then`
getter/call could not close the iterator either. Repro: for each method, use
an iterator that yields 7, then throws on its next step, and a
`Promise.resolve` override that throws `Error('stop')`. Node calls `return()`
and rejects with "stop"; the old engine did not call `resolve`, reached the
second step, and rejected with "advanced". See the four new cases in
`tests/js-conformance/async.dat`.

**The fix.** In `src/js/Promise.cpp`, read `resolve` once before the iterator
and invoke it on each item before advancing; get `then` on the returned
promise and attach the reaction within that same visitor. `js_iterate`
already closes a non-array iterator if a visitor throws, so the result
promise receives that exception after `return()`. A native promise keeps its
cheap `Promise::subscribe` path (including explicit GC roots). An overridden
`then` is called synchronously with two once-only, non-constructible
callbacks; pending values retained through those callbacks are registered
as roots on the combinator, not left opaque to the collector. The four
methods share the same step and close behavior. No `JsOps.cpp` change is
included: PR #50 claims it.

**Verification.** `make verify-promise-iterators` prints `34/34` async cases
with 0 crashes and timeouts and a 3-second limit (old binary: 30/34, two
timeouts with that limit); the reference run
`python3 tools/conformance/run_js_conformance.py --reference 'node -e'
--filter async --quiet` prints `34/34`. The full corpus prints `911/925` (the
same 14 pre-existing failures); CI's JavaScript floor rises 907 -> 911.
`make test` passes 163 suites on the original base, then 167 after merging
main (one suite from this change). A page not in the corpus through
`tools/conformance/compare_with_jsdom.py` prints `4 lines, 4 the same` on
the fixed binary versus `4 lines, 0 the same` before. The page has an
actual DOM text update from each of the four methods, not just a computed
marker. Run `python3 verify/build_promise_baseline.py` first to build the
baseline binary (it prints `baseline binary: build/litebird-promise-before`).
On the four combinators' 390 pinned test262 tests:
`python3 tools/conformance/run_test262.py --binary
before=build/litebird-promise-before --binary after=build/litebird
--timeout 3 --jobs 2 built-ins/Promise/all built-ins/Promise/allSettled
built-ins/Promise/any built-ins/Promise/race` prints 210 -> 293 pass, 84 new
passes and one false former pass lost (below). A custom `then` retained its
callbacks through `gc()` and later delivered 9 in an additional manual probe.

The first full branch run, before merging later main (`make test conformance js-conformance
verify-promise-iterators dom-conformance url-conformance xml-conformance wpt
diff-trees diff-xml diff-strings deep asan fuzz tsan`) keeps the existing
scoreboards: 163 suites, html5lib 1781/1792, JS 911/925, DOM 640/640,
URL 1188/1188, XML 1702/1702, WPT 4658/5192 (0 unlisted); 6000 random
trees and 4000 XML documents compared, 0 differences, and 1332 deep runs
across 465 pages at depth 200000, 0 failed (slowest 20.7 s). `make asan`
passes 163 suites, JS 910/925 (one fewer for deep recursion), DOM 640/640,
URL 1188/1188, XML 1702/1702, WPT 4658/5192; 400 parser, 400 pipeline,
600 XML and 1800 media fuzz cases with 0 findings; 882 deep runs across 315
pages at depth 10000, 0 failed (slowest 3.1 s). The last release `fuzz`
and `tsan` steps passed after the sanitizer run.

After merging `main` at `4b803d7` (the frames and table-placement PRs),
the release chain passed again: 167 suites, JS 911/925, WPT 5421/5463
(87 files, 42 listed, 0 unlisted), html5lib 1781/1792, DOM 640/640,
URL 1188/1188, XML 1702/1702; 6000 random trees and 4000 XML docs
0 differences; 1332 deep runs 0 failed (slowest 20.2 s). On that merged tree `make asan` passed 167 suites, JS 910/925, DOM
640/640, URL 1188/1188, XML 1702/1702, WPT 5421/5463 (0 unlisted),
1000 XML differential docs with 0 DIFFER and 1000 round trips, 400/parser
+ 400/pipeline + 600/XML mutations with 0 findings and 1800 media fuzz
cases clean, 882 deep runs 0 failed (slowest 2.4 s). `make fuzz` passed
400/400/600 mutations with 0 findings; `make tsan` passed both stress
harnesses under ThreadSanitizer and ASan. No ASan/UBSan/TSan report.

After merging `main` at `020e0d5` (Phase 44's Unicode identifiers), a
**third full chain** passed (`build/promise-third-chain.log`): 170 suites,
JS 925/939, html5lib 1781/1792, DOM 640/640, URL 1188/1188, XML
1702/1702, WPT 5421/5463 (0 unlisted), diff-trees 6000 pages and
diff-xml 4000 docs 0 differing, deep 1332 runs 0 failed (slowest 16.6 s).
ASan: 170 suites, JS 924/939, DOM/URL/XML/WPT unchanged, 1000 differential
XML docs 0 DIFFER and 1000 round trips, 400/400/600 mutations 0 findings,
1800 media fuzz clean, 882 deep runs 0 failed (slowest 2.7 s). The standalone
`make fuzz` and `make tsan` passed. The normal CI JS floor is now **925**;
the ASan floor was raised from 919 to **923** after the 924/939 sanitizer
score was repeated with `--min-pass 923` (0 crashes/timeouts). No floor
was lowered.

**Still open.** `js_iterate`'s array fast path ignores an array's overridden
`Symbol.iterator`: with `a=[1]` and an iterator yielding 9, `Promise.all(a)`
returns `[1]` here and `[9]` in Node. The new task-table row names the
shared helper, currently claimed by PR #50. A custom constructor's `resolve`
is now invoked, but the result is still a base Promise: the pre-existing
subclass row owns that change. Thus test262
`built-ins/Promise/any/species-get-error.js` loses a former pass: on this
engine an unhandled rejection is reported by a base Promise where the test's
custom constructor discards it. This is disclosed, not silenced or presented
as a conformance gain. Array holes and the general collector audit are also
outside this change.

(Finding 69, Phase 63: the array fast path now asks. `Promise.all(a)` above
resolves to `[9]`, and a `resolve` that throws closes the iterator the array
handed out.)

---
