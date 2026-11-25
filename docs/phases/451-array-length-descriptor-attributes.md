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

- `make test`: all 506 engine test suites pass, followed by 114/114 cross-origin Window/Location and MessageChannel checks.
- `python3 tools/conformance/run_test262.py built-ins/Object`: 3,218 / 3,411 pass (193 failures; this run was not a before/after comparison).
- `make js-conformance`: 1,153 / 1,158; `make dom-conformance`: 678 / 678; `make url-conformance`: 1,188 / 1,188; `make conformance`: 1,973 / 1,973.
- `make wpt`: 9,570 / 9,824 subtests, with the reported Grid, XML serializer, and parent-global failures. `make xml-conformance`: 1,718 / 1,718.
- `make diff-trees`: all four runs pass with 0 differing trees. `PATH="$PWD/build/venv/bin:$PATH" make diff-xml` (with `lxml` installed in a local venv): 4,000 XML differential inputs with 0 differences and 3,000/3,000 round trips.
- `make deep`: 1,332 / 1,332 runs pass. `python3 tools/ci/check_suites.py`, `python3 tools/ci/check_docs.py`, `make check-numbers`, `python3 tools/next_ids.py --self-test`, and `git diff --check` pass.
- A local `make asan` attempt exceeded the 30-minute tool limit and was killed during its deep-nesting unit suite. The full merged-head CI run on `96c37749` passed all jobs, including sanitizer and deep-page jobs. A later full run on `c8755412` passed every other job, but the jsdom reference step reported 677/678; this did not reproduce locally with `python3 tools/conformance/run_dom_conformance.py --reference --show 10` (678/678). A further CI result is pending; the local fuzz run was not completed.

## Remaining scope

The Phase 428 descriptor-attribute and sealed-array-index defects are closed. Broader array index descriptor storage and other `defineProperties` ordering cases are not claimed by this change.
