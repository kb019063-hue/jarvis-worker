# Phase 384 — the object literal spreads an object

Task row: **The bytecode VM has a differential oracle since Phase 259** — the
row's remaining list of refusals, taken one at a time
(`tools/conformance/verify_bytecode_agreement.py`). Finding 384 (this pull
request's own number; phase and finding are the pull request since 24
November).

## 1. What was wrong

`var o = {a: 1, ...{b: 2}}; String(o.b)` sat in REFUSES with the note
"object literal form". The compiler's object literal required
`is_simple()` — every property a plain `key: value` pair — and refused a
spread outright, so any script building an object with a spread could not
compile at all.

The one place the obvious fix can lie was identified before writing it:
`{...source}` is CopyDataProperties (ECMA-262 §7.3.24) — the source's own
enumerable string and symbol keys in `own_keys` order, read through a live
enumerability check, written via CreateDataProperty. Answering that with a
hand-rolled loop in the VM would be a second CopyDataProperties to drift
against the interpreter's — exactly what Phases 273/291 avoided for `typeof`
and `switch`.

## 2. What changed

- **The tree-walker's `copy_data_properties` now has two consumers.** The
  definition stays in `src/js/JsEval.cpp` but moved out of the file's
  anonymous namespace into `namespace litebird`, so the VM links it; the VM
  forward-declares it locally rather than declaring it in `JsOps.hpp`, which
  another open pull request (#366) holds. The VM answers spread with the
  same §7.3.24 the tree-walker ships — own keys in order, live enumerability
  re-checked per key, `CreateDataPropertyOrThrow` semantics — and nothing is
  duplicated.
- **The VM grew two opcodes** (`include/litebird/js/OpCode.hpp`,
  `OP_OBJECT_DEFINE`, `OP_OBJECT_SPREAD`). The object literal with any
  spread in it is built **in source order**: one empty object is pushed, and
  every property defines onto it — `OpObjectDefine` defines one `key: value`
  pair (via `define_property`, matching the tree-walker's literal
  properties, not the simple path's `set_property`), and `OpObjectSpread`
  copies a source's own enumerable properties (or nothing, for null and
  undefined — §8.6.9 via §7.3.24). The source runs at its position, which is
  why `{a: f(), ...g(), b: h()}` evaluates f, g, h in order on both paths.
- **The simple path is unchanged**: literals with no spread compile exactly
  as before (`set_property`, one `OpNewObject`), so no existing answer moved.
- **Computed keys, methods, accessors and `__proto__` are still refused**,
  as they already were; only spread is added.
- **The agreement corpus moved**: `{a: 1, ...{b: 2}}` moved from REFUSES to
  AGREES, and eight more spread scripts were added there (null/undefined/
  number spreads copy nothing; later spread overwrites; earlier spread loses
  to a later own key; array indices; string positions; evaluation position;
  nesting; key order next to an own key). **AGREES 43 → 52, REFUSES 5 → 4.**
- **A suite that holds it**, `tests/suites/384-the-vm-spreads-an-object.cpp`
  (`the_bytecode_vm_spreads_an_object`): every case runs through `eval` and
  through `eval_bytecode` on a fresh interpreter each, and the asserted
  value is the tree-walker's answer.

## 3. How it was verified

**`make verify-bytecode`** — the run that shows the refusal list shrinking:

```
agreement: 52 case(s) both paths must answer alike
  52/52 agree
  4/4 still refused, 0 no longer refused

verify_bytecode_agreement: 0 failure(s)
```

(the `--dump` output reads `ast=… vm=…` per case; all nine spread scripts
answer identically on both paths, including the side-effect ordering probe
`mark('a'), mark('b')` → `ab/b`.)

**`make -j2 all`** — build complete (the one `error_kind_name` unused-function
warning is pre-existing, in `src/js/JsOps.cpp`, and was not touched).
**`make test`** — **all 442 engine test suites pass**, including
`the_bytecode_vm_spreads_an_object`.
**`make verify-bytecode`** — as above.
`check_suites.py` / `check_numbers.py` / `check_docs.py` / `check_rows.py` —
0 failures (pre-existing notes only; README.md's suite count is left to the
landing session, per AGENTS.md).

`verify-bytecode` is a local gate, not a CI job — unchanged from Phase 259;
the same property is held inside `make test` by the suite above it.

## 4. What is still open

- **The four remaining REFUSES cases**, each still named in
  `tools/conformance/verify_bytecode_agreement.py`: destructuring,
  `try`/`catch`, `for…of` and tagged templates. The count is the point: it
  shrinks by decision, never silently.
- **Computed keys, methods, accessors, `get`/`set` and `__proto__` in any
  object literal form** remain refused/unmodelled — for spread-containing
  literals the same way they were for all literals before; the `__proto__`
  semantics the tree-walker has (sets the prototype) are not in the simple
  path either, where `set_property` reaches the `Object.prototype.__proto__`
  accessor instead. That asymmetry predates this phase and is not made
  better or worse by it; the spread path deliberately uses `define_property`,
  which matches the tree-walker for ordinary keys.
- **Making the VM the engine** is Phase 259's larger open item and is
  unchanged: no closures, `this`, `arguments`, exceptions or iterator
  protocol. Nothing in this phase makes anything faster, and nothing was
  measured for speed.
