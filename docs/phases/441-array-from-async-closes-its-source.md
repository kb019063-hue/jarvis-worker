# Phase 441 — Array.fromAsync closes its source at the abrupt sites

Task: finish the open item in the Array.fromAsync row, measured in Phase 415.
Finding 441.

## 1. What was wrong

Phase 412 added `Array.fromAsync`, but its iterable branches were separate hand-written loops. The async loop fetched `iterator.next` in the loop condition, so a two-value source read a `next` getter three times (once per item plus the final done step); the iterator record requires one read for the whole walk. Neither branch performed the async close when an operation named by the algorithm failed. The synchronous branch also stepped the raw iterator instead of adapting it with CreateAsyncFromSyncIterator, losing details such as awaiting the sync result's `value` and the adapter's own close-on-value-rejection behavior.

## 2. What the algorithm requires

`Array.fromAsync` (ECMA-262 §23.1.2.2; previously §23.1.3.1) obtains an Iterator Record before constructing its result and calls the recorded `[[NextMethod]]` on each step. For a synchronous iterable, it first applies CreateAsyncFromSyncIterator (§27.1.6; previously numbered §27.1.4), which returns async `next` and `return` methods and awaits each sync iterator result's `value`.

The iterator walk uses IfAbruptCloseAsyncIterator (§7.4.16) at the mapper call, the await of the mapper result, CreateDataPropertyOrThrow, and the `k ≥ 2^53 − 1` guard. Those failures call AsyncIteratorClose (§7.4.15). Source-step operations are ordinary `?` steps instead: a throwing/rejecting `next`, a non-object step, or an abrupt `done`/`value` read is returned without closing. A rejection while the async-from-sync adapter awaits a yielded sync value is handled by that adapter's own close rule.

AsyncIteratorClose reads `return`, calls it, and awaits a normal call's result. If the caller already has a throw completion, that exact completion wins over a getter/call/await error and over a non-object return result. The completion *kind* must be separate from its value: `throw undefined` is still an abrupt completion. With a normal completion, a failed close propagates and a non-object return result becomes a TypeError.

## 3. What changed

`BuiltinsArray.cpp` now builds one iterator record for either source kind and sends both through one `walk(iterator, next, made)` loop. `next` is captured exactly once before constructing the result; `k` is tracked directly rather than storing a second vector of values. The sync branch uses the existing CreateAsyncFromSyncIterator implementation shared with `yield*`, so the wrapper's `next`, `return`, and awaited values all follow the same rules.

The mapper call, mapper-result await, property definition, and safe-integer guard each have their own abrupt-close path. Property definition is guarded whether or not a mapper was supplied. Source-step failures remain outside those catches, and the array-like path is unchanged.

`close_async_iterator` in `JsOps` implements AsyncIteratorClose's completion precedence and awaits `return`. It takes an explicit completion-kind flag rather than using `undefined` as a sentinel. `js_create_async_from_sync_iterator` exposes the evaluator's existing adapter to built-ins without duplicating it.

## 4. Regression suite

`tests/suites/441-array-from-async-closes-its-source.cpp` checks:

- one `next` getter read for async and sync sources;
- exactly one close for mapper-call throws, mapper-result rejections, and a failed CreateDataPropertyOrThrow (including the no-mapper case);
- no close on a rejected async `next`, sync `next` throw, non-object sync step, or throwing `done`/`value` access;
- sync values are awaited before the mapper, and a rejected yielded sync value closes through the async-from-sync adapter;
- a failed close does not replace the mapper's error, including `throw undefined`; close results are awaited; and the normal-completion AsyncIteratorClose path rejects a non-object result with TypeError.

Ordinary iteration and mapper outcomes were compared with Node.js v22.22.3. Node is deliberately not used as the oracle for the highlighted source-step cases: V8 closes on a rejected async `next` and on some failing sync-source steps where the spec's `?` operations do not. A sync iterator whose `return` returns a rejected Promise *as the result object* also triggers an unhandled rejection in Node, so the suite tests the corresponding rejection through the result's `value` instead.

The new suite fails against the old implementation: its mapper-throw checks observe `closed = 0` instead of `1`, and its getter check observes two reads instead of one. `make test` passes with the new implementation (see the verification totals below).

## 5. Verification

- `make -j2 all`: passed.
- `make -j2 test`: **495/495 engine suites** and **114/114 cross-origin checks** after merging the latest `main`.
- `make js-conformance`: **1,153/1,158**, everyday probe **90/90**.
- `make wpt`: **9,570/9,824** subtests; 254 failures listed in the repository's expected-failures file, 0 unlisted.
- test262 before/after totals are recorded once the two-binary run finishes.

The pinned test262 commit predates `Array.fromAsync`, so it contains no tests for this method; its before/after comparison is a regression check, not a direct test of this change.

## 6. Scope

The array-like path, result construction policy, and constructor validation were not otherwise changed. This phase closes the Phase 415 finding; it does not introduce `Promise.try` or broaden the separate work on Array.from.
