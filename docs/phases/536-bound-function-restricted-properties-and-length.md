# Phase 536: Bound functions enforce `caller`/`arguments` and correct `length`/`name`

## What was wrong

Two `Function.prototype.bind` defects, both visible in test262 (§9.4.1.3 / Annex B):

**1. `caller` and `arguments` were writable on bound functions.** Every function inherits `Function.prototype`'s poison-pill accessors for `caller`/`arguments` — a configurable getter/setter pair whose setter throws `TypeError` (`'caller', 'callee', and 'arguments' properties may not be accessed on strict mode functions…`). A bound function is a native function without a heap cell (`loose_properties`), so `JsValue::set_property` took the side-table path and never consulted the prototype chain. `b.caller = 1` created an own data property instead of throwing:

```
var f = function(){}; var b = f.bind(null);
b.caller = 1; // litebird: no throw, b.hasOwnProperty('caller') true
              // Node: TypeError
```

The own-property path also broke `BoundFunction_restricted-properties` (15.3.4.5-20-3/21-3).

**2. `length` and `name` were computed with `ToNumber`/`ToString`.** The implementation did `js_to_number(target.length) - boundArgs` and `js_to_string(target.name)` unconditionally. Per spec: `length` is only taken when `Target` has an *own* `length` and that value's type is Number; otherwise `L = 0`. When it is a number, `ToInteger` truncates (3.66 → 3, -0.77 → 0), `Infinity` stays `Infinity`, `-Infinity` becomes `0`, `NaN/±0` become `0`. `name` is only prepended with `"bound "` when it is a string; otherwise it is `""` → `"bound "`. The previous code threw on `Symbol` values and kept a fractional length:

```
Object.defineProperty(fn,'length',{value:3.66}); fn.bind().length // litebird 3.66, spec 3
Object.defineProperty(fn,'length',{value:Symbol('1')}); foo.bind(null,1).length // litebird TypeError, spec 0
Object.defineProperty(fn,'name',{value:Symbol('s')}); fn.bind().name // litebird TypeError, spec "bound "
```

## What changed

* `src/js/JsValue.cpp` `JsValue::set_property`: the `loose_properties` (native-function) path now walks the prototype chain exactly like the `property_cell` path — inherited accessor with setter calls it, accessor without setter refuses, non-writable data refuses, otherwise shadows. For native functions the chain starts at `prototype_object()` (`Function.prototype`).

* `src/js/BuiltinsObject.cpp` `Function.prototype.bind`: length follows `HasOwnProperty` + `Type(targetLen) is Number` + `ToInteger` + `max(0, i - argCount)` with `Infinity/-Infinity/NaN` handling; name uses `targetNameVal.type == String ? str : ""`.

## Verification

* `build/litebird --run-js` on bound-function samples above now matches Node v22.
* `tools/conformance/run_test262.py built-ins/Function/prototype/bind`: **97/100** (was 91/100); the 3 remaining failures are `get-fn-realm` harness limitations (`$262` missing), not logic.
* Full test262: **15249/15686 pass (97.2%) fail 437** (was 15240/15686 fail 446; +9 net, 3 of which were the bind failures fixed here).
* `make conformance` **1973/1973**, `make dom-conformance` **678/678**, `tools/conformance/run_js_conformance.py` **1154/1158**, `build/test_suite` **561/561** — all unchanged.
* New suite `tests/suites/536-bound-function-restricted-properties-and-length.cpp` — 20 assertions covering poison-pill get/set on bound and ordinary functions, `hasOwnProperty` after refused write, strict-mode throws, and `length`/`name` edge cases (own vs inherited, number vs Symbol/string, ToInteger truncation, Infinity).
