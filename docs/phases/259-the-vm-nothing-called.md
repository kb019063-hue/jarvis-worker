# Phase 259 — the VM nothing called, and what calling it found

Task: `ROADMAP.md`, Tier 2, row 7 — *"**Finish the bytecode VM**
(`VM.cpp`, `BytecodeCompiler.cpp` exist) as the single engine; tree-walker stays
as a differential oracle."* Finding 263. Agent: Heron.

Phase 204 took the first half of this row: it measured what the tree-walker
costs (`make bench-js-core`) and showed that the row's "50–100× on loops and
calls" was right about calls and wrong about loops. This phase takes the second
half, and it did not go where the row expected.

## 1. The VM is not slow. It cannot run the language.

The row reads as a performance task: finish the VM and the engine gets faster.
The first thing to look up is what the VM can compile, and the answer stops the
performance question before it starts.

```
$ ./build/litebird --run-js 'var x = 5; x * 2' --dump-bytecode
Cannot compile to bytecode: Function-scoped var is not supported by the bytecode compiler
```

`BytecodeCompiler::compile_stmt` refused `var` outright. Every ordinary script
declares one, so **no ordinary script compiled at all**. Measured on the tree
this phase started from, with a probe that runs the same source through both
paths:

| script | tree-walker | bytecode VM |
| :--- | ---: | :--- |
| `var x = 5; x * 2` | 10 | refused |
| `var i = 0; for (var j = 0; j < 5; j++) i += j; i` | 10 | refused |
| `var o = {a: 1}; o.a = 2; o.a` | 2 | refused |
| `var n = 0; n++; n` | 1 | refused |
| `var k = 0; while (k < 2) k++; k` | 2 | refused |
| `true ? 'y' : 'n'` | y | refused |
| `1 + 2` | 3 | **3** |
| `'a' + 'b'` | ab | **ab** |
| `[1,2,3].length` | 3 | **3** |
| `1 < 2` | true | **true** |

Four of twelve agreed, and the four were the ones with no declaration in them.
Before any of this was worth writing down, one more lookup settled why nobody
had noticed:

```
$ grep -rn eval_bytecode tests/ tools/
(nothing)
```

**No test, no checker, no caller.** `JsInterpreter::eval` runs the tree-walker
unconditionally — the comment above it records that the VM was removed from that
path when a failing VM run was re-executed from the top in the tree-walker and
did everything twice. So the VM was reachable only through `eval_bytecode`,
which nothing called. A subsystem nothing calls cannot fail, and so it did not:
the refusal could have stayed for another hundred phases.

That is the finding: **the VM was not unoptimised, it was untested.** "Finish
the VM" could not begin with making it faster, because there was no way to tell
whether a change to it had made it better or wrong.

## 2. So: an oracle first, and shown to fail before it is trusted

`tools/conformance/bytecode_probe` (`make` target `verify-bytecode`) evaluates
one script twice — `eval` then `eval_bytecode` — on the same document, and
prints what each answered. `tools/conformance/verify_bytecode_agreement.py`
drives it over a corpus and requires the two answers to be equal.

Two lists, and the split is the point:

- **`agrees`** — cases both paths must answer alike. A failure is a defect in
  one of them.
- **`refuses`** — cases the *compiler* still declines, each with the message it
  declines with. Not passes: the work remaining, counted so the list shrinks.

A case that starts agreeing while in `refuses` **fails the run**, the way the
pixel oracle prints `FIXED` — so moving a case across is a decision someone
makes after reading it, not something that happens quietly.

The harness was shown to fail before it was believed. With `+` made to compile
to `OpSubtract`, it reported:

```
FAIL  1 + 2
FAIL  'a' + 'b'
FAIL  String(1 < 2) + String(2 <= 2) + String(1 !== 1)
FAIL  function f(a) { return a + 1; } String(f(41))
... 7 more
```

and 0 failures again once the operator was put back. The `+`-to-`-` injection is
also the mistake this engine actually shipped for a while in a different file,
so it is the right one to test the harness with.

## 3. What the compiler learned to compile

### `var` — a binding of the function, not of the block

ECMA-262 14.3.2. The naive fix, `throw` out and treat `var` exactly like `let`,
is wrong in a way no single-line test shows: `var a = 1; { var b = 2; } a + b`
must answer 3, and a `let`-shaped `b` is popped when the block ends.

Where a `var` can live depends on what the compiler has a slot for:

- A **function body** has var slots — its parameters are the first of them.
- The **script body** has none. Its bindings are properties of the global
  object, which is what `OpSetGlobal` writes, and which is where a top-level
  `let` already went.

So a `var` goes to a local only when the enclosing function has already given
that name a slot; otherwise it goes where a top-level `var` goes. The first
attempt gave the block-nested name a stack slot with `add_local_at(name, 0)` on
the reasoning that recording it at the function's depth would survive the
block. It does not: the slot is a *position on the VM stack*, and leaving the
block pops it, so the binding was gone before anything read it — the case above
answered **1**. That attempt is recorded here and in the code, because it
looks right and is not.

Destructuring declarators are still refused, and now say so in their own words
rather than under "multiple declarators".

### The ternary, `do`/`while`, `break` and `continue`

Four refusals, each with a different reason to be careful.

- **`?:`** (13.14) is two jumps and no pops: the chosen branch's value *is* the
  expression's value.
- **`do`/`while`** (14.6.2) runs its body before its test, so it is the one loop
  that cannot be written as a `while` with the test moved to the top.
- **`break`** must jump past the loop's back edge. Patched before `emit_loop`,
  it pointed *at* the back edge, and `while (true) { ...; break; }` went round
  for ever — an infinite loop, reported by nothing.
- **`continue`** goes *backwards* to the test in a `while`, and *forwards* to
  the update or test in a `for` and a `do`/`while`. Using one direction for all
  three hung `for (var i = 0; i < 3; i++) { continue; }`.

And one bug in the patching itself, which the disassembler caught and no test
would have. `emit_jump` answers the offset of the jump's two **operand** bytes,
not of its opcode — `patch_jump` uses that convention and `patch_back_jump`
does not, because its offset is taken before the opcode is written. Writing the
forward operand at `at + 1`/`at + 2` put the high byte in the next instruction
and wrapped the distance to **65297**, a jump into nowhere. A second helper,
`patch_forward_jump(at, target)`, now exists for the case `patch_jump` cannot
do: jumping *into* code already written, rather than over a branch.

## 4. Where it landed

| | before | after |
| :--- | ---: | ---: |
| corpus cases both paths must agree | 6 | **28** |
| of those, agreeing | 6 | **28 / 28** |
| still refused by the compiler | 6 | **7** (a different, narrower list) |
| `eval_bytecode` tests anywhere in the repo | **0** | suite + `make verify-bytecode` |

The seven still refused, honestly: destructuring declarators, `switch`, `try`,
`for...of`, `typeof` (and the other unary operators), object spread, and tagged
templates. Each is a real gap with a reason — `switch` needs `break` to mean
two different things at once, `for...of` needs an iterator protocol the VM has
no opcodes for.

`make test`: **395 suites, all passing.** `make verify-bytecode`: 0 failures.

## 5. What this does *not* do

**It does not make anything faster, and that is the honest result.** The VM is
still not the path a page runs on: `eval` remains the tree-walker. Making the
VM the engine is the rest of row 7 and is a much larger change — the VM has no
closures, no `this`, no `arguments`, no exceptions and no iterator protocol,
which is why `require_simple_function` refuses so much. What this phase
establishes is the thing that made all of that unmeasurable: **a way to tell
whether the VM is right**, which is the prerequisite the row never mentioned.

The number that matters for the next phase is that 28 scripts now run on both
engines and agree, where 0 were checked before.

## 6. Checks

```
make -j2 all && make -j2 test         395 suites, all passing
make verify-bytecode                  28/28 agree, 7/7 still refused, 0 failures
python3 tools/conformance/verify_bytecode_agreement.py --self-test
                                      6 readings
make js-conformance                   1149/1158 (unchanged)
make dom-conformance                  678/678 (unchanged)
make conformance                      1973/1973 (unchanged)
make wpt                              9567/9824 (unchanged)
make url-conformance                  1188/1188 (unchanged)
```

Nothing a page does changes: the engine a page runs on was not touched.
