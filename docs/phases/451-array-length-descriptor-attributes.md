# Phase 451: Array length descriptor attributes respect integrity levels

## Goal

Finish the open Phase 428 work: make array `length` descriptor attributes follow the property's fixed `enumerable` and `configurable` bits and one-way `writable` bit, and ensure adding an index to a sealed or frozen array is rejected.

## Reproduction and scope

Compared the engine with Node using fresh, read-only, frozen, and sealed arrays. The engine accepted `enumerable: true` for `length`, silently accepted attempts to restore `writable: true`, and failed to throw when `Object.defineProperty` tried to add an index to a sealed array. Node throws `TypeError` for each illegal `length` descriptor and for an added index on a sealed or frozen array. A sealed array's `length` remains writable and may grow; that behavior is retained.

## Changes

- Added the `array_length` descriptor-validation path in `src/js/BuiltinsObject.cpp`. It treats `length` as non-configurable and non-enumerable, preserves its writable state, and stores a non-writable descriptor in the property map so descriptor reads observe the attribute.
- Routed additions to non-extensible arrays away from the dense-element shortcut so the existing non-extensible-property refusal is applied; existing sealed-array length growth remains allowed.
- Added `tests/suites/451-array-length-descriptor-attributes.cpp`, covering `Reflect.defineProperty` false results, `Object.defineProperty` TypeErrors, descriptor attributes after refusal, sealed/frozen index additions, sealed length growth, and an ordinary non-extensible object control.
- Recorded the closure of the Phase 428 open issue in `AGENTS.md`.

## Verification

- `make -j2 build/test_suite && ./build/test_suite`: all 500 engine test suites pass, including the Phase 451 regression.
- `python3 tools/conformance/run_test262.py built-ins/Object`: 3,218 / 3,411 pass (193 existing failures; this run was not a before/after comparison).
- `make js-conformance`: 1,152 / 1,158; the six reported failures are the existing Proxy, iterator, BigInt, map, and inherited read-only assignment cases.
- `make dom-conformance`: 678 / 678; `make url-conformance`: 1,188 / 1,188; `make conformance`: 1,973 / 1,973.
- `make test` reaches the separate cross-origin Window/Location and MessageChannel checks, which report 1 failure in 114 checks (`hasOwnProperty('document')`, got `ok:false`). The 500 engine suites pass before that separate failure.
- `make wpt`: 9,570 / 9,824 subtests; the reported failures are the existing Grid, XML serializer, and parent-global cases. `make diff-trees`: all four runs pass with 0 differing trees. `make xml-conformance`: 1,718 / 1,718.
- `python3 tools/ci/check_docs.py`, `make check-numbers`, `python3 tools/next_ids.py --self-test`, and `git diff --check` pass. `python3 tools/ci/check_suites.py` is blocked by the already tracked `tests/suites/tmp-unforgeable.cpp`, which has a non-numbered filename and contains two `TEST` declarations; that unrelated file was not changed.
- `make deep` reports 64 failures across its 1,332 deep-page runs (mostly DOM tree-chain expectations); this change does not touch those paths. `make diff-xml` cannot run because `lxml` is not installed. `make fuzz` cannot run before an ASan binary is built; the attempted `make asan` exceeded the 30-minute tool limit and was killed during the deep-nesting unit suite.

## Remaining scope

The Phase 428 descriptor-attribute and sealed-array-index defects are closed. Broader array index descriptor storage and other `defineProperties` ordering cases are not claimed by this change.
