# Phase 77 — async-generator delegation and return resumption

Task: `yield*` in an async generator does not ask for `Symbol.asyncIterator`.
Finding 83.

## What was wrong

An async generator delegated using the synchronous iterator protocol. Another
async generator was not iterable, and even manually implemented async
iterators could not drive delegation correctly. A generator's `.return()`
terminated the frame instead of resuming the suspended yield with a return
completion, skipping the delegate's return protocol. Async direct `yield`
also did not adopt a rejected operand at the suspension point.

## What changed

`eval_yield` obtains the async iterator when present and otherwise wraps the
sync iterator in an async-from-sync adapter. It caches `next`, forwards sent
values and throw/return completions, validates iterator results, and awaits
async steps. The adapter awaits sync step values and handles their rejection.
Normal async `yield` awaits its operand. A return completion unwinds through
`finally`; delegated `return` can itself produce another non-done result.
For synchronous `yield*`, the raw delegate result is delivered to the caller
without reading its value first, as required by GeneratorYield.

## Verification

- `python3 tools/conformance/verify_async_generator.py`: **7/7 match Node**
  (Node v22.22.3). The CI oracle covers delegation, sync fallback, return
  through finally, caught rejected yield, raw result identity/lazy access,
  and non-callable methods and primitive sync step results.
- `make test`: **223 suites passed**, plus 114 cross-origin/channel checks.
- `python3 tools/conformance/run_js_conformance.py --quiet`: **1128/1152**;
  same merged-main build without these changes **1127/1152**. The single
  newly passing case is async `yield*` in `iteration.dat`; no new failures.
- `python3 tools/conformance/run_test262.py --binary before=/path/to/main/litebird --binary now=build/litebird language/expressions/async-generator language/statements/async-generator language/expressions/yield language/statements/generators language/expressions/generators built-ins/AsyncGeneratorPrototype built-ins/AsyncGeneratorFunction`
  on test262 revision `2e58f87d8350`, comparing binaries built from merged
  `main` at `631589c` and this change: **1210 → 1388 / 1614**, 178 newly
  passing and zero newly failing. `before` is a separately built executable,
  not a file committed to the repository.
- The same before/now command with the runner's default 21 directories:
  **12422 → 12657 / 15686**, 235 newly passing and none newly failing.
- `make wpt`: **9550/9744**, 194 failures listed, zero unlisted.
  `python3 tools/conformance/run_js_conformance.py --reference 'node -e'`
  confirms all **1152/1152** corpus expectations against Node.

## Still open

Async generators currently use blocking `eval_await` rather than an async
request queue, so a promise that can only settle after `next()` returns cannot
be awaited correctly. Promise-job timing tests still fail. Generator and async
function prototype identities, promise resolving-function constructibility,
syntax early errors and other pre-existing test262 failures remain outside
this task. The async-from-sync missing-throw path also differs from Node 22;
its observable cleanup/error semantics need independent further work. This
change does not claim complete async-generator conformance.
