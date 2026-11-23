# Phase 198 — object spread and rest copy symbols via `CopyDataProperties`

Task: the remaining `src/js/JsEval.cpp` gap of the `Object.assign` symbol-copy
family row in `AGENTS.md` — object spread (`{...source}`) and object rest
destructuring (`{...rest} = source`). Finding 202.

## 1. What was wrong

After `Object.assign` (Phase 167), `Reflect.ownKeys` (Phase 175), and
`Object.getOwnPropertyDescriptors` (Phase 181), every reflection and copy path
in `BuiltinsObject.cpp` saw symbol keys, while the two evaluator paths defined
by ECMA-262 §7.3.24 `CopyDataProperties` still did not:

- `JsInterpreter::eval_object_literal` (`ObjectProperty::Kind::Spread`) called
  `source.own_keys()` and wrote with `object.set_property(key, ...)`.
- `JsInterpreter::bind_pattern` (`Pattern::Kind::Object`, `element.rest`)
  called `value.own_keys()` and wrote with `rest.set_property(key, ...)`.

That produced three distinct spec divergences:

1. **Dropped symbol keys.** `JsValue::own_keys()` intentionally omits `"@@*"`
   keys so `Object.keys`, `for...in`, and `JSON.stringify` stay string-only.
   Both `{ ...src }` and `const { ...rest } = src` therefore dropped every own
   enumerable symbol property (`Object.getOwnPropertySymbols({...src}).length`
   was `0`).
2. **`[[Set]]` instead of `CreateDataPropertyOrThrow`.** Writing with
   `set_property` invoked earlier own setters in the same object literal
   (`{ set x(v) { ... }, ...{ x: 1 } }`) and setters on the prototype chain
   (`{ __proto__: protoWithSetter, ...{ x: 1 } }`, or a setter installed on
   `Object.prototype`), where §7.3.24 step 6.c.ii requires
   `CreateDataPropertyOrThrow(target, nextKey, propValue)` — defining a fresh
   writable, enumerable, configurable own data property without triggering any
   setter.
3. **No live enumerability check.** Snapshotting only enumerable string keys
   up front missed getters that delete a later key, flip a later key's
   `enumerable` bit off, or reveal a previously non-enumerable key during the
   copy (§7.3.24 step 6.c.i).

## 2. What changed

Added `copy_data_properties(JsValue& target, const JsValue& source, const std::vector<std::string>* excluded)`
and `is_own_enumerable_for_copy(const JsValue& source, const std::string& key)`
in `src/js/JsEval.cpp`, shared by both `eval_object_literal` (`Spread`) and
`bind_pattern` (`Pattern::Kind::Object`, `element.rest`):

- Returns immediately on `source.is_nullish()` (rest destructuring already
  throws a `TypeError` on nullish targets before entering the pattern loop).
- Preserves the cross-origin `WindowProxy` / `Location` fast path through
  `litebird::dom::cross_origin_own_keys(source, false, cross_keys)`, still
  respecting `excluded` and writing via `define_property`.
- Snapshots `[[OwnPropertyKeys]]` in exact spec order:
  `source.own_keys(/*include_non_enumerable=*/true)` (array/string indices
  ascending, then own string keys in creation order) followed by own symbol
  keys from `property_cell()->properties` (or `loose_properties()`) recognized
  by `js_symbol_from_key`, in creation order.
- For each snapshotted key not in `excluded`:
  1. Checks live own enumerability via `is_own_enumerable_for_copy` (covering
     primitive strings, boxed `String` objects, arrays and holes, exotic
     objects via `JsExotic::computed_property`, ordinary `PropertyMap` entries,
     and loose function properties).
  2. Reads `source.get_property(key)` and installs the result with
     `target.define_property(key, std::move(prop_val), PropertyMap::Default)`,
     which clears any prior accessor flags/setter and creates an own data
     property without invoking setters on `target` or its prototype chain.
- Because `bind_pattern` already records destructured computed keys via
  `js_to_property_key` in `used`, destructuring a symbol property
  (`const { [s1]: v, ...rest } = src`) automatically excludes `s1` while
  copying every other enumerable symbol key.

## 3. Verification

- `tools/conformance/verify_symbol_copy_paths.py` (extended from 7 to 18
  Node.js v22.22.3 comparison cases covering spread, declaration/assignment/
  parameter rest, computed symbol exclusion, non-enumerable symbols, getter
  order, same-description symbols, `CreateDataProperty` vs setters, live
  enumerability, strings, sparse arrays, TypedArrays, functions, and nullish):
  - Before: **7/18 match Node** (all 11 new spread/rest cases failed)
  - After: **18/18 match Node**
- test262 two-binary (`before` vs `after`):
  - Default suite (15,686 tests): **14,969 → 14,978 pass (+9, 0 lost)**
  - All 17 spread & destructuring directories (14,388 tests):
    **14,076 → 14,094 pass (+18, 0 lost)**, with **0 non-`Proxy` failures**
    left across every `spread`, `obj-rest`, and `object-rest` test in test262.
- `tests/test_suite.cpp`: added
  `test_object_spread_and_rest_copy_symbols_and_follow_copy_data_properties` —
  **345/345 engine test suites pass**.
- `make js-conformance`: **1146/1158** (90/90 everyday, 0 crashes).
- `make dom-conformance`: **678/678** (68/68 everyday, 0 crashes).
