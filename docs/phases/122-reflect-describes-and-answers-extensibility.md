# Phase 122 — `Reflect` gets the three functions it was missing

Task row: **`Reflect.getOwnPropertyDescriptor`, `Reflect.isExtensible`,
`Reflect.preventExtensions` and the four `__defineGetter__` relatives do not
exist** — the `Reflect` half only. Finding 127.

The `__defineGetter__` / `__defineSetter__` / `__lookupGetter__` /
`__lookupSetter__` half of the same row is **not** taken here and is still open.

## 1. What was wrong

`Reflect` was installed with ten of its thirteen functions. Three were absent,
and absent in the way that is worst to find: `typeof` answered `"undefined"`,
so calling one was a `TypeError` from a property that does not exist rather
than a refusal from a function that does.

| probe | Node.js v22.22.3 | this engine, before |
| :--- | :--- | :--- |
| `typeof Reflect.getOwnPropertyDescriptor` | `"function"` | `"undefined"` |
| `typeof Reflect.isExtensible` | `"function"` | `"undefined"` |
| `typeof Reflect.preventExtensions` | `"function"` | `"undefined"` |
| `Reflect.isExtensible({})` | `true` | `TypeError` |
| `Reflect.getOwnPropertyDescriptor({a: 1}, 'a').value` | `1` | `TypeError` |

The important thing about these three is not that they were missing but that
they are the **strict** half of the `Object` calls. `Object.isExtensible(1)`
answers `false`; `Reflect.isExtensible(1)` is a `TypeError`, because
`Reflect` has `RequireInternalSlot` where `Object` has `ToObject`. Writing the
three as aliases of the `Object` ones would have been wrong in exactly the case
a test is most likely to ask about.

Two of the three also have a refusal that is a *value*, not a throw: on a
cross-origin `Window` or `Location`, `[[PreventExtensions]]` answers `false`
and nothing changes (`HTML §7.2.3.4, §7.2.4.4`), which is what
`Reflect.setPrototypeOf` already did on those objects and what
`Object.preventExtensions` reports as a `TypeError`.

## 2. What changed

`src/js/BuiltinsObject.cpp` — one file, no other.

- **`Reflect.getOwnPropertyDescriptor(target, key)`** (§28.1.6): a non-object
  is a `TypeError`; otherwise the key is `ToPropertyKey`'d and described by the
  same `descriptor_for` that `Object.getOwnPropertyDescriptor` already used.
  That reuse is the point: an array's index and `length`, an accessor kept in
  the property map, a function's `prototype` and `name`, an exotic object's
  computed property and a cross-origin `Window`'s exposed keys all describe
  themselves here as they do through `Object`, because it is one descriptor.
- **`Reflect.isExtensible(target)`** (§28.1.7): a non-object is a `TypeError`;
  a cross-origin `Window` or `Location` answers `false`; otherwise the cell's
  `extensible` bit. A script function with no cell yet gets one, so the answer
  does not depend on whether something had already asked for its properties.
- **`Reflect.preventExtensions(target)`** (§28.1.8): a non-object is a
  `TypeError`; a cross-origin `Window` or `Location` answers `false` unchanged;
  otherwise the cell's `extensible` bit is cleared and the answer is `true`.
  It is idempotent, as the language is: a second call still answers `true`.
- The three are defined by one `require_reflect_target` lambda, so the
  `TypeError` reads the same in all three and there is one place to change it.

**What writing the test found, and did not fix.** Two bugs came out of probing
behaviour to write the expectations, and neither belongs to `Reflect`, so
`AGENTS.md` §3 applies: they became new rows, not fixes in passing.

- `Object.isFrozen(Object.preventExtensions({}))` is `false` here and `true` in
  the language. The object has no properties, so nothing there is writable or
  unconfigurable; but our `frozen`/`sealed` bits are only ever set by
  `freeze()`/`seal()`. A non-extensible object with no properties is both.
- `Object.getOwnPropertyDescriptor(new String("ab"), "1")` is `undefined` here.
  `descriptor_for` tests `object.type == JsType::String` before it asks for a
  cell, so it describes a *primitive* and returns nothing for the boxed object
  that `is_object_like()`. `Object.getOwnPropertyDescriptor("ab", "1")` — the
  primitive — is right, and `Reflect.getOwnPropertyDescriptor("ab", "1")` is
  now a `TypeError`, so the wrong case is only reachable through `new String`.

Neither is asserted in the new suite: an assertion would be a test that fails
on this engine and passes on Node, which is a row of the table, not a phase.

## 3. How it was verified

- `make test` → **275 / 275** (274 on `main`), the new suite being
  `reflect_describes_and_answers_extensibility`, whose 26 checks were each
  printed by Node.js v22.22.3 first and then required of the engine.
- `python3 tools/conformance/run_test262.py built-ins/Reflect` →
  **113 / 153 (73.9%)**, from **95 / 153 (62.1%)** on `main`; 18 tests fixed,
  none lost. Most of the 18 are the `isExtensible` and
  `preventExtensions` directories, whose tests agree with the engine's is on
  every ordinary object.
- `make js-conformance` → **1142 / 1158 (98.6%)**, unchanged; `make
  dom-conformance` → **678 / 678**; `make url-conformance` → **1188 / 1188**;
  `make wpt` → **9567 / 9824, 0 unlisted**; `make test`'s cross-origin suite →
  **114 checks, 0 failures**, which is where the cross-origin `Window`
  refusals are actually asserted (the two new methods do not add checks there
  because an earlier phase already pins the two `Object` ones).

## 4. What is still open

- **The four `__defineGetter__` relatives** are still `undefined` on
  `Object.prototype`, and are the other half of the row this phase came from.
- **`Reflect.getOwnPropertyDescriptor` and `Reflect.isExtensible` are still
  not asserted against a cross-origin `Window` in
  `tests/cross_origin_windows.cpp`.** They take the same refusal branch as the
  `Object` methods that are asserted there, so it is covered by construction,
  not by a check of its own.
- **Two new rows** in `AGENTS.md`: the `Object.isFrozen`/`isSealed` answer for
  a non-extensible object with no properties, and the `String` object's own
  index properties.
