# Phase 526: An inherited read-only property stops an assignment

## What was wrong

`JsValue::set_property` in `src/js/JsValue.cpp` only consulted the prototype chain for accessor properties. When a prototype held a non-writable data property, the engine created an own data property on the receiver instead of refusing the write.

```js
var p = {};
Object.defineProperty(p, 'ro', {value: 1});
var o = Object.create(p);
o.ro = 2; // should leave o without own property, o.ro still 1
```

`o.hasOwnProperty('ro')` was `true` and `o.ro` was `2`. In strict code the same site should throw `TypeError`; the engine answered `'assigned'`. The same applied to `class A {} Object.defineProperty(A,'ro',{value:1,writable:false}); class B extends A {} B.ro = 2`.

The prototype loop checked `if (!proto->has_accessors) continue;` — any prototype without an accessor was skipped entirely, so its non-writable data property was never seen.

## What changed

`src/js/JsValue.cpp` `JsValue::set_property` now checks every prototype for an own property with that name:

- accessor with setter → call it with the receiver
- accessor without setter → refuse (return false)
- data property with `writable: false` → refuse
- data property with `writable: true` → allow shadowing (break)

The `has_accessors` guard is removed. `writable` is taken from `PropertyMap::Writable`; array and string exotic paths already handled their own checks and are unchanged.

## Verification

* `tests/suites/526-inherited-readonly-assignment.cpp` — 22 assertions covering plain objects, `Object.create` chains two levels deep, `Object.defineProperty` with `writable:false/true`, accessor with/without setter, setter invocation on receiver, `hasOwnProperty` after refusal, strict-mode `TypeError`, class inheritance (`class B extends A`), and shadowing of writable prototype.
* `build/litebird --run-js` on the same scripts matches Node v22.22.3.
* `make js-conformance` **1154/1158** (was 1153/1158): `prototypes.dat` 75/76 → **76/76**, the previously failing "an inherited property that cannot be written stops an assignment" case now passes. Remaining 4 failures are expected missing features (Proxy, BigInt, `@@iterator` string key).
* `make test` **559/559** engine suites, **114/114** cross-origin checks, no new failures.

The fix is one loop in `src/js/JsValue.cpp`; no parser, GC, DOM, layout or network code changes.
