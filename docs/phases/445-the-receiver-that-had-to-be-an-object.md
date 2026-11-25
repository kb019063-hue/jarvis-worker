# Phase 445: The receiver that had to be an object

## Goal

Enforce `ToObject(this value)` on `Object.prototype.hasOwnProperty`, `Object.prototype.isPrototypeOf`, `Object.prototype.propertyIsEnumerable`, and `Object.prototype.valueOf` in `src/js/BuiltinsObject.cpp`.

## Changes

- Added `as_object` in `src/js/BuiltinsObject.cpp` to throw `TypeError` on `null`/`undefined` and wrap primitives via `js_to_object`.
- Preserved step-1 ordering in `hasOwnProperty` (`ToPropertyKey` before `ToObject`) and `isPrototypeOf` (non-object argument returns `false` before `ToObject`).
- Added regression suite `tests/suites/445-object-prototype-methods-refuse-null-and-undefined.cpp`.
- Added `docs/findings/445-a-false-answer-where-an-object-was-required.md`.
