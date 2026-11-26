# Phase 462: Reflect.set defines new properties on its receiver

## Goal

Complete the open Phase 414 `Reflect.set` case: when the target's own or inherited descriptor permits a data write, apply it to the supplied distinct receiver, including creating a new own data property when the receiver does not already have one.

## What was wrong

`Reflect.set` used `target.set_property(key, value)` for every call. `SetterReceiverScope` made an accessor setter see the fourth argument as `this`, but ordinary data-property writes still happened on the target. For example, `Reflect.set({ x: 1 }, "x", 2, receiver)` returned true while leaving the receiver untouched and changing the target. When the target had no property, the same path also added the new property to the target instead of the receiver.

The receiver's own property rules were never consulted: a writable receiver data property should be updated without changing its attributes; a receiver accessor or non-writable data property should refuse the write; and a missing receiver property should be created without running an inherited setter on the receiver. The target's descriptor is still decisive: an inherited non-writable data property or getter-only accessor refuses the write, while an inherited setter must be called with the receiver.

## Changes

- Added a receiver-aware OrdinarySetWithOwnDescriptor path in `src/js/BuiltinsObject.cpp`. It searches the target and its prototype chain, refuses non-writable data descriptors, and calls accessors with the supplied receiver. For writable data descriptors it checks only the receiver's own descriptor: writable data properties are updated in place, while missing properties use CreateDataProperty.
- `Reflect.set` now distinguishes an omitted fourth argument from an explicitly supplied `undefined`. The omitted case still uses the target as receiver; explicit `undefined` is a non-object receiver and refuses a data-property write.
- Added `JsValue::create_data_property` in `src/js/JsValue.cpp` and declared it in `include/litebird/js/JsValue.hpp`. It defines default writable, enumerable, configurable own data properties without consulting the receiver's prototype, with array index storage and length checks handled explicitly.
- Made own-property checks and descriptor lookup recognize sparse array indices stored in the property map, so receiver data descriptors beyond the dense element vector are updated or refused instead of being mistaken for missing properties.
- Preserved host-defined `[[Set]]` behavior when no ordinary descriptor is found (including out-of-bounds typed-array indices), while a valid typed-array element descriptor follows the supplied receiver. Removed the now-unused thread-local `SetterReceiverScope`; inherited setters are called directly with the actual receiver, so nested writes cannot inherit a stale receiver.
- Added `tests/suites/462-reflect-set-defines-on-receiver.cpp` with 20 assertions for own and inherited writable/non-writable descriptors, target and receiver setters, existing receiver descriptors and attributes, non-extensible objects, omitted versus explicit `undefined`, dense and sparse arrays, typed-array behavior, and symbol keys.

## Verification

- `make test`: all 508 engine suites pass, followed by 114/114 cross-origin Window/Location and MessageChannel checks.
- `make js-conformance`: 1,153/1,158 (99.6%; 0 crashes and 0 timeouts).
- `python3 tools/ci/check_suites.py`, `python3 tools/ci/check_docs.py`, `make check-numbers`, `python3 tools/ci/check_pr.py claim 462`, and `git diff --check`: pass.

## Remaining scope

`Proxy` is still absent from the engine, so Proxy `set` traps remain a separate task. This phase does not replace or generalize every host object's custom `[[Set]]` implementation; where no ordinary descriptor exists, the existing host handler remains authoritative.
