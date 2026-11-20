# Finding 128 — a `delete` that was never refused

Phase 123. The architecture and the measurements are in
[`docs/phases/123-a-strict-delete-refuses.md`](../phases/123-a-strict-delete-refuses.md).

## 1. What was wrong

`delete` was written as if `[[Delete]]` were the whole operator:

```cpp
if (operand->kind == NodeKind::Identifier) return JsValue::make_boolean(false);
...
return JsValue::make_boolean(base.delete_property(key));
```

Three things follow from that shape:

- **The mode was never read.** The language's `DeletePropertyOrThrow` makes a
  refused delete a `TypeError` in strict code and the value `false` in sloppy
  code; this engine gave `false` in both, so `'use strict'; … delete o.x` on a
  non-configurable property went on quietly.
- **One property was never refused at all.** An array's `length` is computed
  from `array_elements`, not stored, so `[[Delete]]` found it in neither the
  element branch nor the property map and answered `true`: `delete a.length`
  said it had deleted the length and the length was still there.
- **An identifier was answered without being asked about.** `delete
  notDeclared` is `true` (there is nothing to refuse) and `delete temp` after
  `globalThis.temp = 1` is `true` and removes it, while `delete v` for a `var`
  is `false`; this engine answered `false` to all three, and `delete this` —
  not a reference at all — as well. In strict code `delete name` is not a
  question at all: it is an early `SyntaxError`.

## 2. What changed

- `JsValue::delete_property` answers `false` for an array's `length`.
- The evaluator throws the `TypeError` in strict code, with V8's words
  (`Cannot delete property 'x' of #<Object>`, `of [object Array]`, …), and
  leaves sloppy code reading `false`; `Reflect.deleteProperty` still answers
  `false` in both.
- `delete name` in sloppy code asks: a declarative binding is `false`, a
  configurable global property is deleted and `true`, an unresolvable name is
  `true`, and `this` is `true`.
- The parser refuses `delete name` in strict code with the language's early
  error, and the evaluator refuses the same thing when a direct eval inherits
  its caller's strictness after the text was parsed.

## 3. Verification

- `make test` → **282 / 282** (281 on `main`), the new suite being
  `a_strict_delete_refuses_what_it_cannot_remove`, nine checks whose expected
  text Node.js 22.22.3 printed first.
- Every corpus unchanged: js-conformance 1142/1158, DOM 678/678, tree
  construction 1973/1973, URLs 1188/1188, WPT 9567/9824 with the same 257
  listed failures. No JS-corpus case asks for a strict delete, which is why no
  score moves; `make metrics` and `check_docs.py` are clean.
- The probes of both tables were run in Node.js and in this engine, and print
  the same lines.

## 4. What is still open

- An index of an array defined through a descriptor still keeps no attributes
  of its own, so a strictly-deleted element does not throw — the row "A data
  property on an index of an array has no attributes of its own".
- `arguments`' prototype is `Array.prototype` here, so a refused delete of one
  prints `#<Array>` where V8 prints `#<Object>`. That is its own divergence,
  and the message follows it.
- `make test262` was not re-run for this phase; the figures the ledger carries
  are the ones its own date records.
