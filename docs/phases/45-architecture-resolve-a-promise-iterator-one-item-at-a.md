# Phase 45 Architecture: Resolve a Promise Iterator One Item at a Time

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 45 Architecture: Resolve a Promise Iterator One Item at a Time` section.
> The text below is the section as it stood, unchanged.


### 1. The hole

The shared `combine` path for `Promise.all`, `allSettled` and `any`, and the
separate `race` path, called `js_iterate` to the end while subscribing to the
engine's `Promise::to_promise(item)`. That conversion does not invoke the
static `Promise.resolve`, so a throwing override could not stop an infinite
iterator. The same issue hid errors in a returned promise's `then` getter.
`tests/js-conformance/async.dat` includes an iterator whose second `next()`
throws and whose first value makes `resolve` throw: the baseline answered
"advanced" rather than "stop", or ran until a timeout.

### 2. Where the iterator boundary is now

`combinator_resolve` reads the receiver's `resolve` once before getting the
iterator. In every visitor call, the returned method resolves **that** item;
`subscribe_combinator_item` gets `then` while the visitor is still on the
stack. A native promise with the built-in `then` uses `Promise::subscribe` to
keep captured values rooted; for a custom `then`, it calls the method with
two once-only, non-constructible native callbacks. A root-only handler on the
pending combinator keeps JS values captured by those callbacks alive even if
a custom thenable retains them and script calls `gc()`. The iterator helper
already invokes `return()` when its visitor throws; the surrounding
`js_capture_throw` turns the exception into the result promise's rejection.
The `remaining` sentinel starts at one so a synchronous custom `then` does
not finish `all` or `allSettled` before iteration ends. No other iterator
clients and no other files' Promise calls changed.

### 3. Reproducible checks

- `make verify-promise-iterators`: `34/34`, no crashes/timeouts; the baseline
  passes `30/34` and does not stop the unbounded cases. `python3
  tools/conformance/run_js_conformance.py --reference 'node -e' --filter
  async --quiet` prints `34/34`. The full JS scoreboard is `925/939`
  after Phase 44 (`911/925` before it).
- `make test`: `163/163` on the original base, `167/167` after the frames
  and table-layout PRs, and `170/170` after Phase 44's Unicode identifiers.
  The new suite checks the `return()` order for all
  four methods, a single `resolve` getter before `Symbol.iterator`, and a
  throwing `then` getter on a still-live iterator.
- `python3 tools/conformance/run_test262.py --binary
  before=build/litebird-promise-before --binary after=build/litebird
  --timeout 3 --jobs 2 built-ins/Promise/all built-ins/Promise/allSettled
  built-ins/Promise/any built-ins/Promise/race`: 390 tests, 210 -> 293 pass;
  84 gained and **one lost** (section 4). First run `python3
  verify/build_promise_baseline.py` to relink this tree against the old
  `src/js/Promise.cpp` from `118d946` (it prints
  `baseline binary: build/litebird-promise-before`).
- `python3 tools/conformance/compare_with_jsdom.py --html '<div
  id="log"></div>' verify/promise-iterator-page.js`: `4 lines, 4 the same` on the
  fixed binary; `0 the same` on the baseline. The checked-in script prints
  a DOM update for each of the four methods. Four lines are compared, not a
  single success marker.

The complete first release run, before merging newer main: `make test conformance js-conformance
verify-promise-iterators dom-conformance url-conformance xml-conformance wpt
diff-trees diff-xml diff-strings deep` — 163 suites; html5lib 1781/1792;
JS 911/925, DOM 640/640, URL 1188/1188, XML 1702/1702, WPT
4658/5192 (0 unlisted); diff-trees 6000 pages with 0 differences from
corrected parse5, diff-xml 4000 documents with 0 DIFFER, diff-strings at
its 95-difference ceiling, deep 1332 runs/465 pages at depth 200000 and
8 MB stack with 0 failed (slowest 20.7 s). `make asan` — 163 suites,
JS 910/925 (one fewer is the old deep-recursion limit), DOM 640/640,
URL 1188/1188, XML 1702/1702, WPT 4658/5192; 1000 differential XML
documents 0 DIFFER and 1000 round trips; fuzz 400/parser + 400/pipeline +
600/XML mutations 0 findings; 1800 media fuzz inputs clean; deep 882 runs/
315 pages at depth 10000 on a 2 MB stack, 0 failed (slowest 3.1 s).
The chain then ran `make fuzz` and `make tsan` in that order, both passing.

After merging `main` at `4b803d7` (frames and table layout), the release
chain passed again: `make test` **167/167**; JS **911/925**; WPT **5421/5463**
(87 files, 42 listed, 0 unlisted); html5lib **1781/1792**, DOM **640/640**,
URL **1188/1188**, XML **1702/1702**, diff-trees 6000 pages 0 differing,
diff-xml 4000 docs 0 differing, diff-strings at its 95 ceiling, deep
1332 runs/465 pages at depth 200000 with 0 failures (slowest 20.2 s).
The sanitizer/fuzz/thread stages passed on this merged tree too: `make asan`
167/167 suites, JS 910/925, DOM 640/640, URL 1188/1188, XML 1702/1702,
WPT 5421/5463 (0 unlisted), 1000 XML differential docs 0 DIFFER and
1000 round trips, fuzz 400/parser + 400/pipeline + 600/XML mutations 0
findings, 1800 media fuzz inputs clean, deep 882 runs/315 pages at depth
10000 on a 2 MB stack 0 failed (slowest 2.4 s). `make fuzz` again printed
0 findings; `make tsan` passed the model/thread stress under TSan and ASan.
Both complete logs are in ignored `build/promise-chain.log` and
`build/promise-merged-chain.log` for this working session.

Finally, after merging `main` at `020e0d5` (Phase 44, Unicode identifiers),
the **third full chain passed on the combined tree** (`build/promise-third-chain.log`):
`make test` 170/170; JS 925/939, DOM 640/640, URL 1188/1188, XML 1702/1702,
html5lib 1781/1792, WPT 5421/5463 (42 listed, 0 unlisted); diff-trees 6000
pages 0 differing, diff-xml 4000 docs 0 differing, diff-strings at ceiling,
deep 1332 runs/465 pages 0 failed (slowest 16.6 s). `make asan` 170/170,
JS 924/939, DOM/URL/XML/WPT unchanged, 1000 XML differential docs 0 DIFFER
and 1000 round trips, fuzz 400/parser + 400/pipeline + 600/XML mutations 0
findings, media fuzz 1800 clean, ASan deep 882 runs/315 pages 0 failed
(slowest 2.7 s). `make fuzz` and `make tsan` also passed. The CI release JS
floor rose to 925; `JS_ASAN_FLOOR` rose from 919 to 923 only after running
`ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/run_js_conformance.py
--binary build-asan/litebird --quiet --timeout 60 --min-pass 923`, which printed
`924/939`, 0 crashes/timeouts.

### 4. What remains

`js_iterate` has a fast path for an array that walks its backing elements
without asking for its own `Symbol.iterator`. Thus an array with a custom
iterator still yields the wrong values (`[1]` here versus `[9]` in Node),
and an override with `return()` is not closed; that is a new AGENTS row for
`JsOps.cpp` (already claimed by PR #50). Promise combinators called through a
subclass or custom constructor still return a base Promise, the existing
"A built-in does not make the subclass" row. Now that we correctly invoke
`C.resolve`, test262's `Promise.any/species-get-error.js` loses a *false*
pass: its custom constructor ignores rejection, but our base Promise reports
an unhandled rejection. We do not suppress the rejection or special-case a
test; the subclass construction is separate work.

(Phase 63 took the array fast path out of the way: an array with an iterator
of its own is asked for it, and `Promise.all(a)` above resolves to `[9]`.)

---
