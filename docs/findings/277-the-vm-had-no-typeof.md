# Finding 277 — the VM could not say what type a value was

Phase 273, agent Arena.

## What was wrong

`tools/conformance/verify_bytecode_agreement.py` kept the bytecode VM
honest by listing what it refused. The list's only unary-operator entry was
`String(typeof 1)`, with the compiler refusing at the first `typeof` it was
shown:

```
This unary operator is not supported by the bytecode compiler
```

`typeof` is the primitive every feature test in the language is written
against; a VM that could not compile it could not run ordinary script for no
reason connected to what the script actually did.

The one way the fix could lie was known before it was written:
`typeof x` must answer `"undefined"` — must not throw — when `x` is an
unresolvable reference (ECMA-262 §13.5.3). A change that compiles the
operand ordinarily and wraps it in a `typeof` opcode is correct only when
loading an unresolvable name cannot throw. That had to be read out of the
VM, not assumed, and it held only by construction: `OpGetGlobal` answers a
missing global with `undefined` and never throws (`src/js/VM.cpp`). That
asymmetry against the tree-walker — which does throw `ReferenceError` —
predates this phase, is now load-bearing, and is recorded as its own row in
`AGENTS.md` rather than fixed here, where fixing it would be a second task.

## What the fix holds

- The VM answers `typeof` with the tree-walker's own `js_typeof_value`
  (`src/js/JsOps.cpp`): one answers table, two consumers, no way for the two
  paths to drift on any value that reaches both.
- The operand is compiled the ordinary way; no reference tokens and no
  special register were needed, and `void` remains, honestly, refused.
- `REFUSES` shrank 7 → 6 and `AGREES` grew 28 → 33; the moved case is kept
  in `AGREES` with a note saying where it came from, and the count in the
  row for the VM says "33 agree, 6 refuse" so the next agent takes the list
  from the document, not from memory.
- A suite (`tests/suites/273-the-vm-says-what-type.cpp`) runs each case
  through both paths and asserts the tree-walker's answer, which also
  catches both paths being wrong the same way.

## Still open

The six refused cases (destructuring, `switch`, `try`/`catch`, `for…of`,
object-literal spread, tagged templates); the VM being the engine at all
(closures, `this`, `arguments`, exceptions, iterator protocol) is Phase
259's larger item. The `OpGetGlobal` asymmetry is an `AGENTS.md` row.
