# Phase 273 — the type of

Task row: **The bytecode VM has a differential oracle since Phase 259** — the
row's remaining list of refusals, taken one at a time
(`tools/conformance/verify_bytecode_agreement.py`). Finding 277.

## 1. What was wrong

`String(typeof 1)` sat in REFUSES with the note "unary operator". The
compiler's unary branch accepted exactly two operators, `-` and `!`, and
refused everything else with "This unary operator is not supported by the
bytecode compiler". So a script asking what type a value is — `typeof`, the
standard's introspection primitive and the most common feature test there is —
could not compile at all.

The one place the obvious fix can lie was identified before writing it:
`typeof` must not throw on an unresolvable reference (ECMA-262 §13.5.3). A
compiler that compiles the operand ordinarily and then wraps it in a `typeof`
opcode is correct exactly when loading an unresolvable name cannot throw.
Whether the VM held that had to be read, not assumed.

## 2. What changed

- **The VM grew `OpTypeOf`** (`include/litebird/js/OpCode.hpp`, disassembly
  name `OP_TYPE_OF`). The dispatch loop answers it with
  **`js_typeof_value`** — the very function the tree-walker calls
  (`src/js/JsOps.cpp`). One answers table, two consumers: the two paths
  cannot drift for any value that reaches both.
- **The compiler now accepts `typeof`** (`src/js/BytecodeCompiler.cpp`):
  the operand is compiled the ordinary way and `OpTypeOf` is emitted after
  it. Nothing about the operand changes, and the §13.5.3 case needs no new
  machinery precisely because the VM's `OpGetGlobal` answers an undeclared
  global with `undefined` instead of throwing — which is the type an
  unresolvable reference is. That asymmetry predates this phase and is now
  load-bearing; it is recorded as its own row
  (*The bytecode VM answers an undeclared read with `undefined`…*) rather
  than left as folklore.
- **The agreement corpus moved**: `String(typeof 1)` moved from REFUSES to
  AGREES, and four more `typeof` scripts were added there (string, boolean
  and `undefined`; `null`, a symbol and `Symbol` itself; an unresolvable
  reference; `typeof` of a call). **AGREES 28 → 33, REFUSES 7 → 6.**
- **A suite that holds it**, `tests/suites/273-the-vm-says-what-type.cpp`
  (`the_bytecode_vm_says_what_type_a_value_is`): every case runs through
  `eval` and through `eval_bytecode` on a fresh page each, and the asserted
  value is the tree-walker's answer — agreement alone would not catch both
  paths being wrong together.
- `void` is still refused. So are destructuring, `switch`, `try`/`for…of`,
  object-literal spread and tagged templates.

## 3. How it was verified

**`make verify-bytecode`** — the run that shows the refusal list shrinking:

```
agreement: 33 case(s) both paths must answer alike
  33/33 agree
  6/6 still refused, 0 no longer refused

verify_bytecode_agreement: 0 failure(s)
```

(the `--dump` output reads `ast=… vm=…` per case; the five `typeof` scripts
answer identically on both paths, including `neverDeclaredAnywhere` →
`undefined`.)

**`make -j2 all`** — clean (no new warnings).
**`make test`** — **all 409 engine test suites pass** (408 before this
phase), including `the_bytecode_vm_says_what_type_a_value_is`.
**`python3 tools/ci/check_suites.py`** — ok, 35 files of their own in
`tests/suites/` (34 before this phase).

`verify-bytecode` is a local gate, not a CI job — it was not wired into
`.github/workflows/ci.yml` by Phase 259 and this phase does not change that;
the same property is held inside `make test` by the two suites above it.

## 4. What is still open

- **The six remaining REFUSES cases**, each still named in
  `tools/conformance/verify_bytecode_agreement.py`: destructuring, `switch`,
  `try`/`catch` (\"Statement\"), `for…of` (\"Statement\"), object-literal
  spread and tagged templates. The count is the point: it shrinks by
  decision, never silently.
- **Making the VM the engine** is Phase 259's larger open item and is
  unchanged: no closures, `this`, `arguments`, exceptions or iterator
  protocol. Nothing in this phase makes anything faster, and nothing was
  measured for speed.
- **The `OpGetGlobal` asymmetry** (`undefined` for an unresolvable name
  where the tree-walker throws `ReferenceError`) is now an open row in
  `AGENTS.md`. It is correct for `typeof` and wrong everywhere else; the
  day the VM is the engine, ordinary reads need a throwing twin selected at
  the call site. The row says so rather than fixing it here, which would be
  a second task smuggled into this one.
