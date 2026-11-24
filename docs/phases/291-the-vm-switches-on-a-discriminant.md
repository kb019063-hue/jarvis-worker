# Phase 291 — the switch that went to an interpreter

Task row: **The bytecode VM has a differential oracle since Phase 259** — the
row's remaining list of refusals, taken one at a time
(`tools/conformance/verify_bytecode_agreement.py`). Finding 296.

## 1. What was wrong

`switch (1) { case 1: break; }` sat in REFUSES with the note "Switch", and
the compiler refused it outright at the top of `compile_stmt`:

```
Switch compiled via AST interpreter
```

(the tree-walker runs `exec_switch` (`src/js/JsEval.cpp`); the VM was never
given the statement). A script with a switch — the language's dispatch
construct — could not compile at all.

## 2. What changed

- **The compiler now compiles `SwitchStmt`** (`src/js/BytecodeCompiler.cpp`,
  `compile_switch`), the way ECMA-262 14.13.4 runs it:
  - the discriminant is evaluated **exactly once** and kept on the stack;
  - each clause test in source order is a **strict equality** against it
    (`OpDup` / `OpStrictEqual`), evaluated **until the first match** — a test
    after the match is never evaluated, and clause tests may not be free of
    side effects;
  - the matched body runs from that clause **to the end of the switch**
    (fall-through), unless a plain `break` leaves it;
  - a switch is **not a loop**: a plain `break` jumps to the end of the
    switch, and an unlabelled `continue` inside a switch targets the
    enclosing loop (ECMA-262 14.13.4.1). `Loop` gained an `is_switch` flag to
    tell the two apart, and `continue` skips switch contexts;
  - the whole switch is **one block scope**, like the tree-walker's `switch`
    scope, so a `let` in one clause is visible to later ones;
  - a `default` clause sits wherever the source puts it: the no-match path
    jumps to it, matched fall-through runs into it, and the last-wins rule of
    the tree-walker for a duplicated default is kept.
- **The refusal itself is gone.** The explicit early throw that routed to the
  AST interpreter is deleted; `switch` compiles like every other statement.
- **The agreement corpus moved**: `switch (1) { case 1: break; }` moved from
  REFUSES to AGREES, and nine more `switch` scripts were added there (a
  middle clause, `default` first/last/duplicated, fall-through and strict
  equality, break and `continue`-to-enclosing-loop, discriminant evaluated
  once). **AGREES 33 → 43, REFUSES 6 → 5.**
- **A suite that holds it**, `tests/suites/291-the-vm-switches-on-a-discriminant.cpp`
  (`the_bytecode_vm_switches_on_a_discriminant`): every case runs through
  `eval` and through `eval_bytecode` on a fresh interpreter each, and the
  asserted value is the tree-walker's answer — agreement alone would not catch
  both paths being wrong together.
- Still refused: destructuring, `try`/`catch`, `for…of`, object-literal spread
  and tagged templates.

## 3. How it was verified

**`make verify-bytecode`** — the run that shows the refusal list shrinking:

```
agreement: 43 case(s) both paths must answer alike
  43/43 agree
  5/5 still refused, 0 no longer refused

verify_bytecode_agreement: 0 failure(s)
```

(the `--dump` output reads `ast=… vm=…` per case; all ten `switch` scripts
answer identically on both paths — including `switch (2)` taking only the
`case 2` and `case 3` bodies, `switch (1)` falling into a `default` that sits
after it and answering `5`, and the side-effect probe
`side()` → `1/1`.)

**`make -j2 all`** — clean (no new warnings; the ones printed are the
pre-existing set outside this change).
**`make test`** — **all 421 engine test suites pass**, including
`the_bytecode_vm_switches_on_a_discriminant`. (The 408-suite baseline on
`bc271edb` has moved with the merges already on `main`; `check_numbers.py`
notes README.md names a different suite count and leaves it to the landing
session, per AGENTS.md.)
**`python3 tools/ci/check_suites.py`** — ok, 45 files of their own in
`tests/suites/`.
**`python3 tools/ci/check_numbers.py`** / **`check_docs.py`** /
**`check_rows.py`** — 0 failures (only the pre-existing blank-line/row notes
in `AGENTS.md`, which this phase does not touch or `--write`).

`verify-bytecode` is a local gate, not a CI job — it was not wired into
`.github/workflows/ci.yml` by Phase 259 and this phase does not change that;
the same property is held inside `make test` by the suite above it.

## 4. What is still open

- **The five remaining REFUSES cases**, each still named in
  `tools/conformance/verify_bytecode_agreement.py`: destructuring, `try`/
  `catch`, `for…of`, object-literal spread and tagged templates. The count is
  the point: it shrinks by decision, never silently.
- **Making the VM the engine** is Phase 259's larger open item and is
  unchanged: no closures, `this`, `arguments`, exceptions or iterator
  protocol. Nothing in this phase makes anything faster, and nothing was
  measured for speed.
- **Block-level function hoisting is still not done in the compiler** — a
  function declared in a clause is body-positioned, exactly as it already is
  in an ordinary block; the tree-walker hoists it. That asymmetry predates
  this phase, is the same decision Phase 259 and 273 made, and is recorded
  here rather than half-fixed.
