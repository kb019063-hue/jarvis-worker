# Phase 553: `Object.defineProperties` converts before defining

## Goal

Follow ECMA-262 §20.1.2.3.1: convert every enumerable own descriptor value before defining the first property on the target. A getter or invalid descriptor encountered later in the conversion pass must not leave earlier properties behind.

## Reproduction and scope

On Node v22.22.3, the `enumerable` getter of the second descriptor observes that the target does not yet own the first property. Once all descriptors have been converted, both properties are present. If conversion of the second descriptor throws, the target remains unchanged.

The old implementation iterated the descriptor keys and called `define_from_descriptor` immediately for each one. That combined `ToPropertyDescriptor` for the next descriptor with `DefinePropertyOrThrow` for the previous descriptor. The new suite failed on the pre-fix tree because the second conversion observed the first definition.

This is specifically the conversion phase. If every descriptor converts successfully but a later definition itself is refused, the earlier definitions stay applied; `Object.defineProperties` is not a transaction.

## Changes

- Extracted `ToPropertyDescriptor` into an ordered conversion step that records each present field, converts the Boolean fields, and validates accessor/data descriptors before application.
- Changed `Object.defineProperties` and `Object.create`'s second-argument path to prepare the full list of key/descriptor records before applying any of them to the target.
- Added `tests/suites/553-define-properties-converts-before-defining.cpp`, covering both the observation made by a later descriptor getter and the no-partial-mutation result when a later conversion throws.

## Verification

- `make test`: all engine test suites pass; the cross-origin Window/Location and MessageChannel suite passes.
- `make js-conformance`: passes.
- `python3 tools/conformance/run_test262.py built-ins/Object/defineProperties`: directory remains at pre-change count (0 gains, 0 losses); the new suite covers the observable order that this directory does not currently test.
- Node v22.22.3 gives the expected second-descriptor observation (`false`) and leaves the target with no `first` property after the later conversion getter throws.
- `python3 tools/ci/check_suites.py`: all standalone suite files follow the documented form; `python3 tools/ci/check_docs.py`: **0 failures**; `make check-numbers`: **0 failures**; `git diff --check`: clean.

## Remaining scope

The other open property-descriptor cases in `AGENTS.md` remain separate: array `length` descriptor combinations, attributes on dense array indices, and the remaining `defineProperties` application/refusal cases. This phase does not roll back definitions when an application step itself fails.
