# Phase 123 — a strict `delete` refuses what it cannot remove

Task row: **`delete` of a property that is not configurable does not throw in
strict code.** Finding 128.

## 1. What was wrong

The operator was three branches of the evaluator, and the one that mattered was
a single line:

```cpp
if (operand->kind == NodeKind::Identifier) return JsValue::make_boolean(false);
...
return JsValue::make_boolean(base.delete_property(key));
```

Measured against Node.js before anything changed:

| probe | Node.js | this engine, before |
| :--- | :--- | :--- |
| `'use strict'; var o = {}; Object.defineProperty(o, 'x', { value: 1 }); delete o.x` | `TypeError: Cannot delete property 'x' of #<Object>` | `false`, and the code went on |
| the same in sloppy code | `false` | `false` |
| `'use strict'; var a = [1]; delete a.length` | `TypeError: Cannot delete property 'length' of [object Array]` | `true`, in either kind of code, with the length still there |
| `'use strict'; delete Object.freeze({ y: 2 }).y` | `TypeError` | no throw |
| `'use strict'; new String('ab')` — `delete s.length` | `TypeError` | no throw |
| `delete notDeclared` (sloppy) | `true` | `false` |
| `globalThis.temp = 1; delete temp` (sloppy) | `true`, and the property is gone | `false`, and it stayed |
| `delete this` | `true` | `false` |
| `eval("'use strict'; var x = 1; delete x")` | `SyntaxError: Delete of an unqualified identifier in strict mode.` | `false` |

Two of the bugs were in `[[Delete]]` itself, and the rest in the operator: the
refusal was there in two of the rows (a plain object, a frozen one) but nothing
looked at the mode, and the other rows were answered without asking anybody.

## 2. What changed

- **`[[Delete]]` answers `false` for an array's `length`** (`JsValue.cpp`). It is
  computed from `array_elements` rather than stored, so the old code fell
  through both the index branch and the property map and answered `true`: a
  "yes" that removed nothing, which no script could tell from a real one. It is
  not configurable in the standard, so the answer is `false` — and only then
  can strict code make it a `TypeError`.
- **Strict code turns the refusal into a `TypeError`** (`JsEval.cpp`). Sloppy
  code reads `[[Delete]]`'s `false` as the value of the delete expression,
  which is why the mode has to be consulted at the operator and not inside
  `[[Delete]]`; `Reflect.deleteProperty` keeps answering `false` in both modes,
  as it must.
- **The message is V8's**, because that is what the corpus records:
  `Cannot delete property 'x' of …`, where the tail is the value itself for a
  function and an Error, `[object Array]`, `[object Date]`, `[object RegExp]`
  and the wrappers by tag, `[object Object]` for an object with no prototype,
  and `#<Constructor>` for everything else. The constructor's name is read off
  the prototype's own `constructor` without running a getter, so an error being
  built cannot call a script.
- **`delete name` is answered rather than always `false`.** In sloppy code a
  name that resolves to a declarative binding is not deletable (`delete v` after
  `var v` is `false`), a name the global object carries is deleted if it is
  configurable (`globalThis.temp = 1; delete temp` is `true` and the property
  goes) and one that resolves to nothing is `true`, which is what `delete
  notDeclared` means. `this` is not a reference at all and answers `true`
  (the parser keeps it as an identifier node, so it needs the one exception).
- **`delete name` in strict code is the early error it is in the language**
  (`JsParser.cpp`): `Delete of an unqualified identifier in strict mode.` The
  parser refuses it where the strictness is known — a script or function
  written with `"use strict"` — and the evaluator refuses what reaches it,
  which is a direct eval that inherits its caller's strictness only once it
  runs.

## 3. How it is verified

- `make test` → **275 / 275** suites (274 on `main`). The new one,
  `a_strict_delete_refuses_what_it_cannot_remove`, is nine checks whose expected
  strings Node.js printed first: the sloppy answer and the property that is
  still there; six strict `TypeError`s (a plain object, a frozen one, a String
  object's `length`, a Date, a Map, a class instance); `delete a.length` in both
  modes with the array unchanged afterwards; the seven message shapes, joined
  and compared as one string; strictness belonging to the deleting code (a
  strict function and a class method inside a sloppy script); everything that
  may be deleted still deleted (`delete o.x`, a hole where an element was,
  `delete nothere`, `delete this`, `delete f()`, `delete 1`); the three answers
  for an identifier (`delete declared` `false`, `delete notDeclared` `true`,
  the strict one a `SyntaxError` through `eval`); a global property that goes and
  a `var` that does not; and `Reflect.deleteProperty` answering `false` without
  throwing.
- Every corpus is unchanged: `make js-conformance` **1142 / 1158 (98.6%)**,
  `make dom-conformance` **678 / 678**, `make conformance` **1973 / 1973**,
  `make url-conformance` **1188 / 1188**, `make wpt` **9567 / 9824** with 257
  listed failures and none unlisted. No case of the JS corpus asks for a strict
  delete, so nothing in it moves; `make metrics` and `check_docs.py` are clean.
- Node.js 22.22.3 and this engine print the same seventeen lines for the probes
  of both tables above.

## 4. What is still open

- **A data property on an index of an array keeps no attributes of its own.**
  `Object.defineProperty(a, '0', { value: 1 })` makes an element that is
  configurable whichever way it was defined, so the strict delete of it does
  not throw (`a` an array): that is the row "A data property on an index of an
  array has no attributes of its own", and it is where this phase's third
  divergence lives. The suite does not assert it.
- **`arguments` prints as `#<Array>` in the message**, because its prototype is
  `Array.prototype` in this engine where the language has `Object.prototype`
  (`Object.getPrototypeOf(arguments) === Array.prototype` is `true` here and
  `false` in Node). The message follows the prototype, so it follows that bug.
- **A refused delete inside a getter** whose refusal happens during another
  error's construction is not something this phase changed: the message reads
  the prototype's own `constructor` without running a getter, so it cannot
  throw, but it can name a class the script replaced.
- **`make test262` was not re-run.** The strict-delete tests of test262 are not
  among the directories the ledger's figures cover for this phase, and the
  numbers it publishes are dated rather than re-measured here.
