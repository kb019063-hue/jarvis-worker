# Finding 553: `Object.defineProperties` mutated the target during descriptor conversion

## Summary

`Object.defineProperties(target, descriptors)` must first collect and convert each enumerable own descriptor value, then apply the resulting descriptors. The engine instead converted and defined one entry at a time. A getter in a later descriptor could therefore observe a property that should not exist yet, and if that getter threw, an earlier property remained on the target.

Node v22.22.3 confirms both observations: the second descriptor sees no first property, and a conversion error leaves the target unchanged.

## Root cause

`define_from_descriptor` performed both `ToPropertyDescriptor` and the target's definition. The `Object.defineProperties` loop called it for each key before reading and converting the next descriptor, interleaving the two phases of §20.1.2.3.1.

## Fix

The descriptor-field reads, Boolean conversions, and descriptor validation now produce a `PropertyDescriptorRecord`. `Object.defineProperties` (and `Object.create`'s property population) stores one such record for every enumerable own key before it calls the definition path for any of them. The direct `Object.defineProperty` and `Reflect.defineProperty` paths continue to convert their single descriptor before applying it.

A failure during the conversion pass leaves the target unchanged. A failure while applying a later, already-converted descriptor may leave earlier definitions in place; that is the specified sequential application behavior, not an atomic rollback guarantee.

## Regression coverage

`tests/suites/553-define-properties-converts-before-defining.cpp` checks that the second descriptor's `enumerable` getter observes no first property, then verifies both definitions after successful conversion. It also throws from a later descriptor getter and checks that the target has no properties afterward. The suite failed on the pre-fix tree and passes with the change.

## Verification

- `make test`: all engine test suites pass.
- `make js-conformance`: passes.
- `python3 tools/conformance/run_test262.py built-ins/Object/defineProperties`: 0 gained and 0 lost cases in that directory.
- Node v22.22.3 agrees with the regression's ordering and no-partial-mutation observations.
- `python3 tools/ci/check_docs.py`, `python3 tools/ci/check_suites.py`, and `make check-numbers` report 0 failures; `git diff --check` is clean.

## Remaining scope

Other descriptor failures remain: array `length` combinations and dense-index attributes are separate; this change does not alter the specified partial effects of a later definition refusal.
