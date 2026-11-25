# Phase 397 — the template the VM never called

Task row: **The bytecode VM has a differential oracle since Phase 259** — the
row's remaining list of refusals, taken one at a time
(`tools/conformance/verify_bytecode_agreement.py`). Finding 397 (this pull
request's own number; phase and finding are the pull request since 24
November).

## 1. What was wrong

`String(f`x`)` sat in REFUSES with the note "expression". The compiler's
expression switch had no case for `TaggedTemplateExpr` and fell into
"Unsupported expression in bytecode compiler", so a script using a tagged
template could not compile at all.

## 2. What changed

- **The VM grew one opcode** (`include/litebird/js/OpCode.hpp`,
  `OP_DEFINE_TEMPLATE_RAW`): it defines the strings array's `raw` property
  with `PropertyMap::Hidden` — exactly the flags the tree-walker's
  `eval_tagged_template` uses, so `raw` is not enumerable in either path.
- **The compiler builds the template in bytecode**
  (`src/js/BytecodeCompiler.cpp`), in the same order the tree-walker
  evaluates it: the **tag first**, then the strings array (cooked pieces,
  `OpNewArray`; the raw pieces, a second `OpNewArray`;
  `OpDefineTemplateRaw`), then the substitution values left to right, then
  `OpCall` with `expressions + 1` arguments. The strings reach the tag as
  the first argument, as §13.2.8 requires.
- **The template object is built fresh on every evaluation**, like the
  tree-walker today — neither path caches per call site, so an identity
  comparison between two evaluations agrees on both paths (both answer
  "different") — and neither freezes the arrays. Both decisions predate
  this phase and stay as they are; the spec's per-call-site caching and
  frozen arrays are not smuggled in as a side effect.
- **The agreement corpus moved**: `String(f`x`)` moved from REFUSES to
  AGREES, and six more tagged-template scripts were added there (cooked and
  raw strings; order of substitutions; an escape — raw keeps `\\u0041`,
  cooked answers `A`; tag runs before substitutions; `raw` is not enumerable;
  a member-expression tag). **AGREES 52 → 59, REFUSES 4 → 3.**
- **A suite that holds it**, `tests/suites/397-the-vm-calls-a-tagged-template.cpp`
  (`the_bytecode_vm_calls_a_tagged_template`): every case runs through
  `eval` and through `eval_bytecode` on a fresh interpreter each, and the
  asserted value is the tree-walker's answer.

## 3. How it was verified

**`make verify-bytecode`** — the run that shows the refusal list shrinking:

```
agreement: 59 case(s) both paths must answer alike
  59/59 agree
  3/3 still refused, 0 no longer refused

verify_bytecode_agreement: 0 failure(s)
```

(the `--dump` output reads `ast=… vm=…` per case; all seven
tagged-template scripts answer identically on both paths, including the
escape and the evaluation-order probe `st/1`.)

**`make -j2 all`** — build complete (the one `error_kind_name`
unused-function warning is pre-existing, in `src/js/JsOps.cpp`, and was not
touched).
**`make test`** — **all 451 engine test suites pass**, including
`the_bytecode_vm_calls_a_tagged_template`.

`verify-bytecode` is a local gate, not a CI job — unchanged from Phase 259;
the same property is held inside `make test` by the suite above it.

## 4. What is still open

- **The three remaining REFUSES cases**, each still named in
  `tools/conformance/verify_bytecode_agreement.py`: destructuring,
  `try`/`catch` and `for…of`. The count is the point: it shrinks by
  decision, never silently.
- **Template caching and frozen arrays** — the spec caches one template
  object per call site and freezes the array and `raw`; neither path does.
  Fixing both is a behavior change to the tree-walker too (its answers to
  identity comparisons and to `Object.isFrozen(t)` would change), so it is
  its own phase if ever taken, not this one's.
- **Making the VM the engine** is Phase 259's larger open item and is
  unchanged: no closures, `this`, `arguments`, exceptions or iterator
  protocol. Nothing in this phase makes anything faster, and nothing was
  measured for speed.
