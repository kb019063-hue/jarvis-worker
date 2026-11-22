# Finding 185 — the descriptor view could not see a symbol

## What was wrong

§20.1.2.9 is `CreateDataProperty(descriptors, key, desc)` for each key of
`[[OwnPropertyKeys]]`, so a symbol-keyed property gets a symbol-keyed descriptor
entry. `Object.getOwnPropertyDescriptors` took the strings-only key list and
dropped the symbol:

```
var o = {a: 1}; o[Symbol('k')] = 2;
                              node    litebird
Reflect.ownKeys(result).length   2         1
```

This is one call away from what Phase 175 fixed in `Reflect.ownKeys`, and the
consequence is the same shape: the engine disagreed with itself. `Object.assign`
and the descriptor view were both working from `assign_keys`, but only the
descriptor view was still taking the strings-only path, so a key that a copy
would carry had no descriptor entry.

## What changed

The function takes `assign_keys(object, false)` — every own key, strings then
symbols — and each key is turned back into the symbol it came from before the
descriptor is stored, so the entry is symbol-keyed. `assign_keys` answers the
internal text a symbol is stored under; `js_to_property_key` is what turns a
symbol back into it.

The `false` matters. `assign_keys`' symbol half filters to enumerable keys by
default, which is right for `Object.assign` (§20.1.2.1 copies enumerable own
properties) and wrong here: `[[OwnPropertyKeys]]` reports a non-enumerable key
too. That is the same trap Phase 175 fell into and its harness caught.

## Verification

`tools/conformance/verify_symbol_copy_paths.py` runs seven cases through Node
and the engine and compares output. Three runs:

| variant | result |
| --- | --- |
| the unfixed engine (strings only) | **1/7** |
| `assign_keys(object, true)` — the enumerable filter | **5/7** |
| the fix as merged | **7/7** |

The middle row is the near-miss this family actually produced one phase earlier,
kept as a case so it cannot come back quietly.

The cases compare the three reflection paths to each other rather than to
separate expectations: the descriptor view's key count is checked against
`Reflect.ownKeys(o).length` and against `getOwnPropertySymbols`, and each
descriptor is compared field by field with what
`getOwnPropertyDescriptor(o, s)` returns. Two symbols sharing a description must
stay distinct entries.

Wider runs:

- test262 two-binary, `built-ins/Object`, 3411 tests: **3064 → 3066, 0 lost**.
- test262 two-binary, `built-ins/Object` + `built-ins/Reflect`, 3564 tests:
  **3195 → 3197, 0 lost**.
- `make test`: 335/335.

## What this does not cover

Still open in the same row: **object spread** (`{...o}`) and **rest
destructuring** (`(({...r}) => r)(o)`) drop symbol keys — both measured broken,
and both live in `src/js/JsEval.cpp`, which an open pull request holds. With
this change, every copy path in `BuiltinsObject.cpp` handles symbols, so what
remains is confined to the evaluator.
