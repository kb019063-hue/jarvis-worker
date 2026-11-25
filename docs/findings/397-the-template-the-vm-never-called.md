# Finding 397 — the template the VM never called

Phase 397, agent Arena.

## What was wrong

`tools/conformance/verify_bytecode_agreement.py` kept the bytecode VM honest
by listing what it refused. The expression entry was the tagged template:

```
Unsupported expression in bytecode compiler
```

The compiler's expression switch had no `TaggedTemplateExpr` case, so a
script using a tagged template — every template-tag library call — could
not compile at all.

## What the fix holds

- The template object is built **in bytecode, per evaluation**, in the same
  order the tree-walker's `eval_tagged_template` runs it: tag first, then
  the substitutions left to right, then the call with the strings array as
  the first argument and the substitutions after it.
- The strings array carries `raw` as a **Hidden** property (new `OpDefineTemplateRaw`,
  one dispatch case in the VM) — the same flags as the tree-walker, and not
  visible to `Object.keys` on either path.
- The array is built fresh on each evaluation and is not frozen, on both
  paths equally — including the identity-comparison behavior that follows
  from it. Per-call-site caching and frozen arrays are the spec and are
  neither path's today; smuggling them in here would have made the two
  paths answer *differently*, which is the one thing the oracle forbids.
- `AGREES` grew 52 → 59, `REFUSES` shrank 4 → 3; the moved case is kept in
  `AGREES` with a note and the row for the VM says "59 agree, 3 refuse".
- A suite (`tests/suites/397-the-vm-calls-a-tagged-template.cpp`) runs
  each case through both paths and asserts the tree-walker's answer.

## Still open

The three refused cases (destructuring, `try`/`catch`, `for…of`); template
caching and frozen arrays (both paths, one phase if ever taken); the VM
being the engine at all (Phase 259's larger item). No faster anything was
measured or claimed.
