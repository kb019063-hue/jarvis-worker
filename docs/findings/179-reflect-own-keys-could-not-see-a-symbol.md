# Finding 179 — `Reflect.ownKeys` could not see a symbol

## What was wrong

§28.1.11 is `[[OwnPropertyKeys]]`: every own key, the string keys in
index-then-creation order and after every one of them the symbols in creation
order, with the symbols themselves in the array rather than their descriptions.

`Reflect.ownKeys` answered with strings only:

```
var s = Symbol('k'); var o = {a: 1}; o[s] = 2;
                            node    litebird
Reflect.ownKeys(o).length     2         1
```

So the engine disagreed with itself about what an object's own keys are.
`Object.assign` on the same object copied `s` (Phase 167 added the symbol
half), and `Reflect.ownKeys` reported an object with one key where the copy
path saw two. A script reflecting over a record — the usual reason to call it —
could not find the symbol-keyed entries at all.

The comment above `assign_keys` in `BuiltinsObject.cpp` had already named this:
"The ones that copy every key (`Object.assign`, spread,
`Object.getOwnPropertyDescriptors`) have to add the symbols themselves; this is
that list." `Object.assign` was converted and the other callers were left, on
purpose, to be taken one at a time.

## What changed

`Reflect.ownKeys` takes `assign_keys` — the list that knows the order — instead
of `reflected_own_keys`, which is the strings-only answer `Object.keys` and
`for...in` want. Each key is resolved back to the symbol it came from, so the
array holds the symbols themselves and `o[keys[i]]` works. The cross-origin
Window/Location rule is consulted first, so a cross-origin window still reports
exactly its exposed keys.

`assign_keys` gained a parameter. Its symbol half filtered to enumerable keys,
which is right for `Object.assign` (§20.1.2.1 copies enumerable own properties)
and wrong for `[[OwnPropertyKeys]]`, which reports a non-enumerable key too.
This was a **regression the harness caught**: the first version of the fix
answered `Reflect.ownKeys` with length 1 for an object holding one string key
and one non-enumerable string key, where the engine before the change answered
2.

## Verification

`tools/conformance/verify_reflect_own_keys.py` runs eight cases through Node and
the engine and compares the output. Three deliberate runs:

| variant | result |
| --- | --- |
| the unfixed engine (strings only) | **1/8** — seven cases fail |
| the fix with `assign_keys(object, true)` (the enumerable-only regression) | **7/8** — one fails |
| the fix as merged | **8/8** |

The middle row is worth keeping: it is the near-miss this change actually
produced, and only the non-enumerable case distinguishes it from a correct fix.

The cases check identity, not just counts: `keys[1] === s` and `o[keys[0]]`
returning the stored value, so a fix that answered `Symbol(description)` —
passing a count-and-order test — would fail. Two symbols sharing a description
are checked to stay distinct.

Wider runs:

- test262 two-binary, `built-ins/Reflect`, 153 tests: **128 → 131 pass, 0 lost**.
- test262 two-binary, `built-ins/Object` + `built-ins/Reflect`, 3564 tests:
  **3192 → 3195 pass, 0 lost**.
- `make test`: 326/326. js-conformance: 1145/1158, unchanged.

## What this does not cover

The other copy paths the row names still drop symbol keys:

- **object spread** (`{...o}`) and **rest destructuring** (`(({...r}) => r)(o)`)
  — both measured as broken on the same commit, and both live in
  `src/js/JsEval.cpp`, which an open pull request held when this was written.
- **`Object.getOwnPropertyDescriptors`** — in the same file as this change and
  straightforward to convert next; it was left so this pull request stays one
  caller wide, as the row asks.

`Object.keys`, `for...in` and `JSON` must keep the strings-only answer. They
were not touched.
