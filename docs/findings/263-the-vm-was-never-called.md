# Finding 263 — the VM was not slow, it was never called

Phase 259. Agent: Heron.

## What was believed

`ROADMAP.md` row 7, Tier 2, in the repository for many phases:

> **Finish the bytecode VM** (`VM.cpp`, `BytecodeCompiler.cpp` exist) as the
> single engine; tree-walker stays as a differential oracle.

and its aspiration in the Tier 2 note: the tree-walker is "50–100× slower than
V8 on loops and calls".

Read as written, the row is a performance task with a design already chosen.
`VM.cpp` (773 lines) and `BytecodeCompiler.cpp` (562 lines) exist, 36 opcodes
are defined and all 36 are handled in the dispatch loop. The natural next step
is to profile the VM and make it faster.

## What is true

**The compiler could not compile an ordinary script.** `compile_stmt` threw on
`var`:

```cpp
if (var_decl->kind == JsDeclKind::Var) {
    throw std::runtime_error("Function-scoped var is not supported by the bytecode compiler");
}
```

and on the ternary, `do`/`while`, `break`, `continue`, `switch` and `try`.
Of twelve ordinary statements run through both paths, **eight were refused and
four worked** — and the four that worked contained no declaration.

**And nothing called it.**

```
$ grep -rn eval_bytecode --include=*.cpp --include=*.hpp tests/ tools/
(no matches)
```

`JsInterpreter::eval` calls the tree-walker, with a comment explaining why the
VM was taken off that path: an earlier version tried the VM first and, on any
exception, re-ran the whole script in the tree-walker, so every side effect
happened twice. The VM stayed reachable through `eval_bytecode` — which no
test, no tool and no caller used.

## Why it matters

A subsystem that nothing calls cannot fail, so it does not. There was no test
whose failure would have said the compiler refused `var`; there was no diff
anyone could read to see it. Meanwhile the row said "finish the VM", and the
row's own framing — *the tree-walker stays as a differential oracle* — describes
a comparison that did not exist. **The oracle the row depends on was the part
that was missing**, and without it "finish the VM" could not be attempted
honestly: a change to the compiler had no way to be shown right or wrong.

The general shape is worth naming, because this engine audits this kind of
thing elsewhere:

> An untested subsystem in a plan that assumes it is nearly done does not stay
> nearly done. It drifts, and its drift is invisible for exactly as long as no
> one measures it.

The same day's work on `AUDIT.md` row after row is about claims that were
written down and never checked; this is the same failure with a performance
label on it.

## What was done about it

Not "finish the VM" — that is a larger piece of work and the wrong first step.
What went in is the prerequisite:

1. `tools/conformance/bytecode_probe` and `verify_bytecode_agreement.py`
   (`make verify-bytecode`): 28 scripts both paths must answer alike, 7 the
   compiler still refuses, each refusal named. **Shown to fail** — with `+`
   compiling to `OpSubtract`, 11 of 28 disagreement lines appeared — before it
   was trusted.
2. The compiler learned `var` (with the block-nesting rule that makes it
   different from `let`), the ternary, `do`/`while`, `break` and `continue`.
   **28/28 agree** where 6 were checked before.
3. A suite, `bytecode_agrees_with_the_tree_walker`, so the property is held by
   `make test` and not only by a tool someone must remember to run.

## Three defects found while doing it, all silent

None of these would have raised an error; two were infinite loops.

1. **A block-nested `var` gave the wrong answer, not an error.** Recording it
   in a stack slot at the function's depth looks right — the slot is not popped
   with the block because `end_scope` only pops locals recorded at the depth it
   leaves — but the slot is a *position on the VM stack*, and leaving the block
   `OpPop`s it. `var a = 1; { var b = 2; } a + b` answered **1** where
   JavaScript answers **3**.
2. **`break` patched before the back edge pointed at the back edge.**
   `while (true) { m++; if (m > 3) break; }` never terminated.
3. **A forward operand was written two bytes too far along.** `emit_jump`
   answers the offset of the operand, not of the opcode; writing at `at + 1`
   put the high byte into the next instruction and wrapped a jump distance to
   **65297**. Found by reading the disassembly, not by a test — which is the
   argument for the disassembler existing.

## What the row should say now

Not "finish the VM" as though it were nearly finished. The honest row is:

> The VM compiles 28 ordinary scripts that agree with the tree-walker and
> refuses 7 kinds of statement outright. It is **not** the engine; making it
> one needs closures, `this`, `arguments`, exceptions and the iterator
> protocols. The oracle that the row depends on now exists
> (`make verify-bytecode`), which is what makes the rest attemptable.

And the performance aspiration should not be copied forward unmeasured: Phase
204 measured the tree-walker at **3–9× V8 on a loop, an array and a
recursion**, and 18–27× on a property write and a call. "50–100×" was never
true for loops in this engine.
