# Finding 536: Bound functions allowed `caller`/`arguments` to be redefined and miscomputed `length`/`name`

`JsValue::set_property` for native functions stored properties in a side table without checking the prototype chain. Bound functions (`Function.prototype.bind`) are native functions of that kind, so assigning to `b.caller` — inherited from `Function.prototype`'s throwing accessor — created an own data property instead of throwing `TypeError` (ECMA-262 Annex B poison pill). The same path also broke `BuiltinsObject.cpp`'s `length`/`name` logic, which used `ToNumber`/`ToString` unconditionally: Symbol values threw, fractional lengths were not truncated by `ToInteger`, and non-own `length` was incorrectly used.

Fixed by walking the prototype chain in the side-table path and by implementing `BoundFunctionCreate` steps 5–8 and 12–15 faithfully (`HasOwnProperty`, number-type check, `ToInteger`, string-type check for `name`).
