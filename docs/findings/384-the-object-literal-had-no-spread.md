# Finding 384 — the object literal had no spread

Phase 384, agent Arena.

## What was wrong

`tools/conformance/verify_bytecode_agreement.py` kept the bytecode VM honest
by listing what it refused. The one object-literal entry was a spread:

```
This object literal form is not supported by the bytecode compiler
```

The compiler's object literal required every property to be a plain
`key: value` pair (`is_simple()`), built all of them on the stack, and only
then made the object from the whole batch. A spread cannot be batched: it is
§7.3.24 CopyDataProperties evaluated at its position, interleaved with the
other properties, including calls on both sides of it. So an object built
with a spread — ordinary modern script — could not compile at all.

## What the fix holds

- The literal with any spread is built **in source order**: one empty object
  on the stack, one opcode per property. `OpObjectDefine` defines a
  `key: value` pair onto that object (`define_property`, as the tree-walker's
  literal properties do), `OpObjectSpread` copies a source onto it
  (§7.3.24), keeping the object on the stack for what follows.
- The VM calls **the tree-walker's own `copy_data_properties`** — the §7.3.24
  that object rest destructuring already uses — so the two paths cannot
  drift on what a spread copies, in what order, or with which flag. The
  definition remains in `JsEval.cpp` (moved out of the anonymous namespace)
  and is forward-declared in the VM rather than exported through
  `JsOps.hpp`, which #366 holds; the one §7.3.24 is unchanged in behavior.
- Null and undefined spread to nothing — no opcode branch of their own,
  because §7.3.24 returns the target unchanged; `{a: 1, ...undefined}`
  answers `1` on both paths.
- Later spreads overwrite earlier ones, and an own key written after a
  spread wins over it — the corpus holds both orders on both paths.
- The simple (no-spread) path is untouched: no existing answer moved, and
  no existing suite changed.
- `AGREES` grew 43 → 52, `REFUSES` shrank 5 → 4; the moved case is kept in
  `AGREES` with a note and the row for the VM says "52 agree, 4 refuse".
- A suite (`tests/suites/384-the-vm-spreads-an-object.cpp`) runs each case
  through both paths and asserts the tree-walker's answer.

## Still open

The four refused cases (destructuring, `try`/`catch`, `for…of`, tagged
templates); computed keys, methods, accessors and `__proto__` in any object
literal (unmodelled before and after this phase); the `set_property`
vs `define_property` asymmetry in the simple path (pre-existing, not made
worse); the VM being the engine at all (Phase 259's larger item). No faster
anything was measured or claimed.
