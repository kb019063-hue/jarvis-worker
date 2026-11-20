# Finding 129 — three of `Reflect`'s thirteen functions were never installed

Phase 124. The architecture and the measurements are in
[`docs/phases/124-reflect-describes-and-answers-extensibility.md`](../phases/124-reflect-describes-and-answers-extensibility.md).

## 1. What was wrong

`install_object_builtins` made the `Reflect` object and defined ten methods on
it: `ownKeys`, `has`, `get`, `set`, `deleteProperty`, `setPrototypeOf`,
`getPrototypeOf`, `defineProperty`, `apply`, `construct`. The standard has
thirteen. The three that were missing were the three that describe or change an
object's structure:

```js
typeof Reflect.getOwnPropertyDescriptor   // "undefined"
typeof Reflect.isExtensible               // "undefined"
typeof Reflect.preventExtensions          // "undefined"
Reflect.isExtensible({})                  // TypeError: isExtensible is not a function
```

This is worse than a wrong answer, because a missing property is
indistinguishable from a typo. A page that used `Reflect` to feature-detect —
the reason `Reflect` exists — was told the feature was absent, and fell back to
whatever it does when it thinks it has an older engine, on an engine that was
only missing the spelling.

## 2. What changed

Three methods, in `src/js/BuiltinsObject.cpp`, and nothing else in the engine.

| function | what it does | what it refuses |
| :--- | :--- | :--- |
| `Reflect.getOwnPropertyDescriptor(t, k)` | `descriptor_for(t, ToPropertyKey(k))` — the same descriptor `Object.getOwnPropertyDescriptor` builds | a `t` that is not an object: `TypeError` |
| `Reflect.isExtensible(t)` | the cell's `extensible` bit | a `t` that is not an object: `TypeError`; a cross-origin `Window`/`Location`: `false` |
| `Reflect.preventExtensions(t)` | clears the bit, answers `true` | a `t` that is not an object: `TypeError`; a cross-origin `Window`/`Location`: `false`, unchanged |

The distinction from the `Object` methods is `RequireInternalSlot`: `Object`
takes `ToObject` of its argument, so `Object.isExtensible(1)` is `false` and
`Object.getOwnPropertyDescriptor('ab', '1')` describes a character. `Reflect`
does not, so all three of those are `TypeError`s. This is the one place a
shared implementation would have been a bug, and it is the reason the three
are not one-line aliases.

## 3. Verification

- `make test` → **275 / 275**; the new suite
  `reflect_describes_and_answers_extensibility` has 26 checks, each first
  printed by Node.js v22.22.3.
- `run_test262.py built-ins/Reflect` → **113 / 153**, from **95 / 153**.
- The 7 tests still failing in the three directories are all one of two
  engine-wide gaps, and neither is a `Reflect` bug: `not-a-constructor.js`
  (`js_make_native` does not set `JsObject::not_constructor`, so
  `Reflect.construct(function () {}, [], Reflect.has)` succeeds where the
  language throws — the "What a function is, in small" row) and
  `return-abrupt-from-result.js` / `return-boolean-from-proxy-object.js`
  (`Proxy` does not exist — the typed-arrays/`Proxy` row).

## 4. Still open

- The four `__defineGetter__` relatives from the same `AGENTS.md` row are
  still `undefined` on `Object.prototype`. They are the other half of the row
  and were not in this pull request's files.
- Two rows this one added while writing its test, both found by probing Node
  for the expectations:
  - `Object.isFrozen`/`Object.isSealed` answer `false` for an object that is
    not extensible and has no properties, where the language has `true`.
  - A `String` object's own indices are not described by
    `Object.getOwnPropertyDescriptor` (`descriptor_for` describes the
    primitive at that type test and returns nothing for the boxed object),
    though `Reflect.getOwnPropertyDescriptor` on the primitive is now a
    `TypeError`, which hides it from the new path.
