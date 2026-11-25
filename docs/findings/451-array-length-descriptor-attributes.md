# Finding 451: Array `length` attributes bypassed descriptor validation

## Summary

An array's `length` is a non-configurable, non-enumerable own data property whose writable attribute can transition from true to false but never back. Its value and writable state are represented partly by the dense array and partly by the property map. The descriptor path did not consistently bridge those representations:

- `{ enumerable: true }` was silently accepted even though `length` must remain non-enumerable.
- The special case for `{ writable: false }` stored the wrong flag, while other descriptors could bypass the length entry entirely. As a result, restoring writability on an already read-only length was silently accepted.
- The dense-array shortcut treated a new index definition like an element write. A sealed array is non-extensible, so it must reject a new index rather than return success after the dense write is ignored.

Node probes confirmed that illegal length descriptor changes and adding an index to a sealed or frozen array throw `TypeError` through `Object.defineProperty`; `Reflect.defineProperty` returns false. Sealing does not make `length` read-only: a sealed array may still grow through `length`.

## Root cause

`define_own_property` in `src/js/BuiltinsObject.cpp` had a dedicated `ArraySetLength` value/shrink path, followed by a dense-element shortcut for ordinary array properties. The `length` attribute handling was a narrow later special case, so descriptor-only requests such as `enumerable: true` were not validated as the array's own fixed property descriptor. Index definitions also reached the dense shortcut before the common non-extensible-property rejection.

## Fix

The `length` case now runs after the existing ArraySetLength value/shrink algorithm and before the dense-element shortcut. It refuses accessor descriptors and attempts to set `configurable` or `enumerable` true, prevents a false writable bit from being restored, and keeps the attribute bit in the property-map entry when necessary. Requests that do not alter fixed attributes retain the existing length value and the array's established partial-shrink behavior.

The dense shortcut now applies to a new index only when the array remains extensible; an addition on a sealed or frozen array proceeds to the existing refusal path. Existing sealed elements and writable sealed-array length behavior are not changed by this guard.

## Regression coverage

`tests/suites/451-array-length-descriptor-attributes.cpp` covers the fresh length descriptor, read-only and frozen length transitions, reflective false results and throwing forms, index additions on sealed/frozen arrays, successful sealed-array length growth, and ordinary non-extensible-object rejection.

## Verification

- `make -j2 build/test_suite && ./build/test_suite`: all 500 engine test suites pass, including the Phase 451 regression.
- `python3 tools/conformance/run_test262.py built-ins/Object`: 3,218 / 3,411 pass (193 existing failures; this run was not a before/after comparison).
- `make js-conformance`: 1,152 / 1,158; `make dom-conformance`: 678 / 678; `make url-conformance`: 1,188 / 1,188; `make conformance`: 1,973 / 1,973.
- `make test` then reports one failure in its separate cross-origin Window/Location and MessageChannel suite: `hasOwnProperty('document')` returned `ok:false` (1 failure of 114). The engine suites pass before that separate failure.
- `make wpt`: 9,570 / 9,824 subtests with the reported Grid, XML serializer, and parent-global failures; `make diff-trees` passes all four runs with no differing trees; `make xml-conformance`: 1,718 / 1,718.
- `python3 tools/ci/check_docs.py`, `make check-numbers`, `python3 tools/next_ids.py --self-test`, and `git diff --check` pass. `python3 tools/ci/check_suites.py` reports the tracked, unrelated `tests/suites/tmp-unforgeable.cpp` filename and two-suite-format violations.
- `make deep` reports 64 failures across its 1,332 deep-page runs, mostly DOM tree-chain expectations. `make diff-xml` could not run because `lxml` is not installed; `make fuzz` could not run because the ASan binary was not available. The attempted `make asan` exceeded the 30-minute tool limit and was killed during the deep-nesting unit suite.

## Remaining scope

The Phase 428 `length` attribute and non-extensible-array index defects are closed. This change does not claim to fix broader dense-index descriptor storage or other `defineProperties` ordering behavior.
