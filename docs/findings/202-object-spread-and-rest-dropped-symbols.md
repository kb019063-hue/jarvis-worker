# Finding 202 — object spread and rest destructuring dropped symbol keys and called setters

## What was wrong

Both `JsInterpreter::eval_object_literal` (`ObjectProperty::Kind::Spread`) and
`JsInterpreter::bind_pattern` (`Pattern::Kind::Object`, `element.rest`) in
`src/js/JsEval.cpp` walked `source.own_keys()` and copied entries via
`set_property(key, source.get_property(key))`.

Because `JsValue::own_keys()` omits `"@@*"` symbol keys by design, both object
spread and object rest destructuring silently dropped every own enumerable
symbol property:

```
var s = Symbol('k');
var o = { a: 1, [s]: 2 };
                              node    litebird (before)
({...o})[s]                      2         undefined
(({a, ...r}) => r[s])(o)         2         undefined
```

Writing with `set_property` also violated ECMA-262 §7.3.24 `CopyDataProperties`
step 6.c.ii (`CreateDataPropertyOrThrow`): spreading onto a literal with an
earlier own setter (`{ set own(v) { ... }, ...{ own: 1 } }`) or a prototype
setter invoked the setter instead of defining an own data property, and
snapshotting only initially-enumerable string keys missed live deletions and
enumerability flips triggered by earlier getters during the copy.

## What changed

`src/js/JsEval.cpp` now routes both object spread and object rest destructuring
through a shared `copy_data_properties(target, source, excluded)` helper
implementing ECMA-262 §7.3.24:

- Snapshots `[[OwnPropertyKeys]]` (all own string keys from `own_keys(true)` in
  index-then-creation order, followed by own symbol keys recognized by
  `js_symbol_from_key` in creation order), preserving the cross-origin
  `WindowProxy` / `Location` fast path.
- Skips excluded keys (including destructured computed symbol keys already
  recorded in `used` via `js_to_property_key`).
- Checks live own enumerability per key (`is_own_enumerable_for_copy`) before
  calling `source.get_property(key)`.
- Installs each copied property with `target.define_property(key, value, PropertyMap::Default)`
  so setters on the target or its prototype chain are never invoked and prior
  accessors are cleanly replaced by own data properties.

## Verification

- `python3 tools/conformance/verify_symbol_copy_paths.py`: **7/18 → 18/18**
  matching Node.js v22.22.3 (all 11 new spread/rest cases fail on the prior
  binary and match Node on the new binary).
- test262 two-binary (`before` vs `now`):
  - Default suite (15,686 tests): **14,969 → 14,978 (+9, 0 lost)**.
  - All 17 spread/destructuring directories (14,388 tests): **14,076 → 14,094
    (+18, 0 lost)**; the only 6 remaining `spread` / `obj-rest` / `object-rest`
    failures in test262 all require the unimplemented `Proxy` constructor.
- `tests/test_suite.cpp`:
  `test_object_spread_and_rest_copy_symbols_and_follow_copy_data_properties`
  added; **347/347** unit suites pass.
- `make js-conformance`: **1146/1158**; `make dom-conformance`: **678/678**.

## What this does not cover

- `Proxy`-specific `CopyDataProperties` trap ordering tests (6 test262 cases)
  await `Proxy` support in the engine.
- Plain-string property key `"__proto__"` remains masked by `is_hidden_key` in
  `src/js/JsValue.cpp`, tracked separately in the engine-slots row.
