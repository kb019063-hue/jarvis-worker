# Finding 83 — async `yield*` used the sync iterator protocol (Phase 77)

## What was wrong

`async function* outer() { yield* inner(); }` failed when `inner()` returned
an async generator: delegation asked for `Symbol.iterator` instead of
`Symbol.asyncIterator`. Returning into a suspended generator terminated it
without forwarding the return completion to the delegate.

## What changed

Async delegation now selects the async protocol or wraps a sync iterator,
forwards next/throw/return and awaits the appropriate results. Generator
returns resume through the yield point, so finally blocks and the delegate's
return method run. Sync delegated results are passed through without eagerly
reading their value. See [Phase 77](../phases/77-async-generator-yield-star.md).

## Verification

`python3 tools/conformance/verify_async_generator.py` compares seven cases
with Node (7/7); CI runs it. The pinned test262 differential for async
generators, generators, and yield is 1210 → 1388 of 1614, with 178 newly
passing and none newly failing. `make test` passes 226 suites on the merged tree.

## Still open

Async generator requests are not queued as promise jobs; blocking await and
job ordering remain incomplete. The async-from-sync missing-throw path differs
from Node 22. See the phase write-up for scope and reproducible commands.
