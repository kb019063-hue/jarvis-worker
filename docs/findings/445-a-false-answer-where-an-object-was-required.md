# Finding 445: Four `Object.prototype` methods answered `false` where an object was required

## Summary

`Object.prototype.hasOwnProperty`, `Object.prototype.propertyIsEnumerable`, `Object.prototype.isPrototypeOf`, and `Object.prototype.valueOf` all called `self()` directly without `ToObject(this value)`. For `null` and `undefined` receivers:

- `hasOwnProperty.call(null, "foo")` returned `false` instead of throwing `TypeError`.
- `propertyIsEnumerable.call(null, "foo")` returned `false` instead of throwing `TypeError`.
- `isPrototypeOf.call(null, {})` returned `false` instead of throwing `TypeError`.
- `valueOf.call(null)` returned `null` (and `valueOf.call(1)` returned the primitive `1` instead of a `Number` object).

## Root Cause

In `src/js/BuiltinsObject.cpp`, `Object.prototype.toLocaleString` already checked `me.is_nullish()` and threw `TypeError`, but the four sibling methods beside it did not. Each of §20.1.3.2 (`hasOwnProperty`), §20.1.3.3 (`isPrototypeOf`), §20.1.3.4 (`propertyIsEnumerable`), and §20.1.3.7 (`valueOf`) requires `ToObject(this value)`.

Two step-1 ordering rules also matter:
- In `hasOwnProperty(V)`, `ToPropertyKey(V)` runs before `ToObject(this value)`.
- In `isPrototypeOf(V)`, `If Type(V) is not Object, return false` runs before `ToObject(this value)`.

## Fix

Added an `as_object` helper in `src/js/BuiltinsObject.cpp` that throws `TypeError` on `null`/`undefined` and calls `js_to_object(me)` to wrap primitives, and applied it to all four methods after their respective step-1 checks. `Object.prototype.toString` (§20.1.3.6) uses `ThisObject` and remains exempt.

## Verification

- `make -j2 build/test_suite && ./build/test_suite`: 493/493 engine test suites pass, including `tests/suites/445-object-prototype-methods-refuse-null-and-undefined.cpp`.
- `python3 tools/conformance/run_test262.py`: 15,223 / 15,686 pass (+12 fixed, 0 broken vs `e610b56`).
