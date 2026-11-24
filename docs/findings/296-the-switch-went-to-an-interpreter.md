# Finding 296 — the switch went to an interpreter

Phase 291, agent Arena.

## What was wrong

`tools/conformance/verify_bytecode_agreement.py` kept the bytecode VM honest
by listing what it refused. The one statement entry left on the list was
`switch`:

```
Switch compiled via AST interpreter
```

That message is not a compiler at all — it is `compile_stmt` throwing away
the statement so `eval_bytecode` can refuse the script. A page whose script
dispatched with a switch could not run in the VM for no reason connected to
what the script actually did, and no suite ran the VM against the tree-walker
on a switch, so nothing noticed.

The refusal list is what the row is for: it shrinks by decision, and §2 of
`docs/phases/259-the-vm-nothing-called.md` explains why the refusal is kept
rather than smuggled around.

## What the fix holds

- The discriminant is evaluated exactly once and lives on the stack for the
  whole statement (`OpDup` for each comparison, popped on every path).
- Matching is `strict_equals`, the tree-walker's own comparison
  (`src/js/VM.cpp` `OpStrictEqual`), evaluated in source order and **not**
  evaluated past the first match — verified with a side-effect probe
  (`side()` → `1/1`).
- Bodies fall through to the end; a plain `break` leaves the switch. A switch
  pushes a named-but-ordinary `Loop` context flagged `is_switch`, which is
  what makes `break` work, and `continue` skips those contexts to reach the
  enclosing loop (ECMA-262 14.13.4.1) — without the flag, `continue` in a
  switch would have compiled as a jump to nowhere.
- `default` is matched wherever it sits in the clause order, and falls into
  the clauses after it like any other clause; a matched clause before it
  also falls into it.
- One block scope wraps the whole switch, so a `let` in one clause is visible
  in later clauses, and clause locals pop at the end of the switch.
- `AGREES` grew 33 → 43, `REFUSES` shrank 6 → 5; the moved case is kept in
  `AGREES` with a note and the row for the VM says "43 agree, 5 refuse".
- A suite (`tests/suites/291-the-vm-switches-on-a-discriminant.cpp`) runs each
  case through both paths and asserts the tree-walker's answer.

## Still open

The five refused cases (destructuring, `try`/`catch`, `for…of`,
object-literal spread, tagged templates); the VM being the engine at all
(closures, `this`, `arguments`, exceptions, iterator protocol) is Phase
259's larger item; block-level function hoisting remains the compiler's
body-positioned treatment, the same as in an ordinary block, and no faster
anything was measured or claimed.
