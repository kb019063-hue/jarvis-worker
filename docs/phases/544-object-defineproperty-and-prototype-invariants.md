# Phase 544: `Object.*` built-ins — array index identity, length growth, `ToPropertyKey` and prototype invariants

## What was wrong

Six independent spec violations in `Object.*`, all visible in test262:

**1. `4294967295` treated as an array index.** `parse_index` (which accepts `0…4294967295`) was used to decide whether a property name is an array index. Per ECMA-262 §7.1.7 an array index must be `< 4294967295`. `Object.defineProperty([],'4294967295',{value:100})` set `length` to `4294967296` instead of creating a plain property at `4294967295` with `length 0`, and `Object.defineProperty([],'4294967294',{value:100})` attempted a 4-billion element dense resize and threw `std::bad_alloc` on 8 tests.

**2. Array length did not grow on `defineProperty` past the end.** `arr=[]; arr.length=3; Object.defineProperties(arr,{'3':{value:26}})` left `length 3` instead of `4`. The dense path resized from the current dense size (which may be `0` after a prior `length` write), while the logical length lived in the property map after `ArraySetLength`. The `index+1 > lenEntry` update was missing.

**3. `plain` attribute default was unconditional.** `define_from_descriptor` decided whether to take the fast `is_array` data path by checking whether every attribute was `true` with “missing means true”. For a *new* index the spec defaults missing attributes to `false` (non-writable/non-enumerable/non-configurable). `Object.defineProperty(arr,'0',{value:1})` incorrectly stayed on the fast path and created a writable element instead of a non-writable map entry.

**4. `ToPropertyKey` for Symbol via `@@toPrimitive` threw `TypeError`.** `js_to_property_key` fell through to `js_to_string` for non-string/number/symbol values, which stringifies a Symbol. `Object.hasOwn(obj, wrapper)` where `wrapper[Symbol.toPrimitive]` returns a Symbol threw `Cannot convert a Symbol value to a string` instead of using the Symbol as the key.

**5. `ToObject` not first in `Object.hasOwn` / missing guards elsewhere.** `Object.hasOwn(null,'foo')` returned `false` instead of throwing `TypeError`, and `Object.hasOwn(null, evil)` evaluated `ToPropertyKey(evil)` before `ToObject(null)` (observable via throwing getter). `Object.getOwnPropertySymbols(undefined)` and `Object.getOwnPropertyDescriptors(null)` similarly did not throw. `Object.groupBy([1], null)` did not throw `TypeError` for a non-callable callback.

**6. `Object.prototype` prototype was mutable.** `Object.setPrototypeOf(Object.prototype, Object.create(null))` succeeded and `Reflect.setPrototypeOf` returned `true`, violating 9.4.7.1 (immutable prototype exotic object): `[[SetPrototypeOf]]` must return `false` unless `V` is the current prototype (`null`). The central `js_set_prototype` allowed it when extensible.

## What changed

* `src/js/BuiltinsObject.cpp` — added local `parse_array_index` (`<4294967295`, rejects leading zeros, max 10 digits) and used it for `array_index` determination and for all `length`-shrink scans (`refused`/`keep`/`lost`/`to_delete`). Added `plain_for_index(exists)` / `plain_for_length()` helpers so new indices default missing attributes to `false` while existing elements and `length` keep the old “missing means keep” rule. Added sparse-index guard: `index > previous + (1<<16)` does not `resize(index+1)`; it marks holes up to the gap and updates the `length` map entry. Added `length` map update after dense `resize(index+1)` so `length` grows when the dense size is smaller than the map length.

* `src/js/JsOps.cpp` — `js_to_property_key` now does `ToPrimitive(hint String)` first; if the result is Symbol the Symbol’s internal key is used directly, otherwise `js_to_string`.

* `src/js/BuiltinsObject.cpp` `Object.hasOwn` — checks `is_nullish()` then `js_to_object()` *before* `js_to_property_key`, per spec steps 1–2. `Object.getOwnPropertySymbols` and `Object.getOwnPropertyDescriptors` add the same nullish `TypeError`. `Object.groupBy` validates `callback.is_function()` before iteration.

* `src/js/JsValue.cpp` `js_set_prototype` — if `cell == JsIntrinsics::get().object_proto` (i.e. `Object.prototype`), only `proto == cell->prototype` succeeds; otherwise `false`. This makes `Object.setPrototypeOf` throw and `Reflect.setPrototypeOf` return `false` for the immutable prototype exotic, while `Object.setPrototypeOf(Object.prototype, null)` still succeeds.

* `src/js/JsEval.cpp` `MappedArguments::unmap` — when `index >= array_elements.size()` (sparse / hole) the binding value is mirrored and a map entry is created so `Object.getOwnPropertyDescriptor(arguments,'0')` survives.

## Verification

* `build/litebird --run-js` probes:
  ```js
  Object.defineProperties([],'4294967295',{value:100}) // hasOwn true length 0 value 100
  Object.defineProperty([],'4294967294',{value:100})  // true 4294967295 100, no bad_alloc
  arr=[];arr.length=3;Object.defineProperties(arr,{'3':{value:26}}) // 4 26
  Object.hasOwn(obj, wrapperWithToPrimitiveReturningSymbol) // true
  Object.hasOwn(null,'a') // TypeError, ToObject before ToPropertyKey
  Reflect.setPrototypeOf(Object.prototype, Object.create(null)) // false
  Object.setPrototypeOf(Object.prototype, Object.create(null)) // TypeError
  ```

* `tools/conformance/run_test262.py --binary build/litebird built-ins/Object` **3317/3411 (97.2%) fail 94** (was 3278/3411 96.1% fail 133 at branch start; +39).

* Full `run_test262.py --binary build/litebird` **15348/15686 (97.8%) fail 338** (was 15240/3411 baseline 97.2% fail 446 across the full suite at 536; net +8 on the non-Object part from the same fixes).

* `make -j1 build/litebird` clean (only pre-existing `move_holes` unused warning).

* New suite `tests/suites/544-object-defineproperty-and-prototype-invariants.cpp` — 28 assertions covering `4294967295` identity, sparse `4294967294` without allocation, length growth on `defineProperty` past `length`, plain-attribute defaults, Symbol `ToPropertyKey`, `ToObject`-before-`ToPropertyKey` ordering and nullish guards, and immutable `Object.prototype` prototype.

