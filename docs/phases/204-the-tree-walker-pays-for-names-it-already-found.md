# Phase 204 — what a loop actually costs, measured, and the counter

Task: `ROADMAP.md`, Tier 2, row 7 — *"finish the bytecode VM — the tree-walker
is 50–100× slower than V8 on loops and calls"*. This phase is the half of that
row that does not need the VM: what a *measuring* of the tree-walker says it
spends, and the hot-path cost that could be taken without changing one
observable answer. Finding 208. Agent: Heron.

## 1. First the number, because the row is a claim and not a measurement

`tools/bench/bench_js_core.py` is new (`make bench-js-core`). It runs six
scripts the way the JavaScript scoreboard runs one — a one-paragraph page, then
`--run-script` — so what is measured is the evaluator and not a page parse, and
it alternates the two binaries run after run and reports the **median** of each,
because every number here moved ±20% between runs without that.

On `3a0b600`'s engine, against Node.js v22.22.3 on the same box:

| case | litebird | node | ratio |
| :--- | ---: | ---: | ---: |
| `loop` — 1.5M iterations of `s = (s + i) % 1000003` | 351 ms | 39 ms | **9.0×** |
| `property` — 1.5M reads and writes of `o.x` | 776 ms | 42 ms | **18.4×** |
| `call` — 1M calls of `add(t, 1)` | 895 ms | 33 ms | **27.5×** |
| `recursion` — `fib(24)`, 150k calls | 97 ms | 32 ms | **3.1×** |
| `array` — 200k writes, 200k reads | 148 ms | 40 ms | **3.7×** |
| `method` — 500k `c.bump(1)` | 584 ms | 34 ms | **17.1×** |

So the row's **"50–100×" is right about calls and wrong about everything
else**: this engine is 3× V8 on recursion and array indexing and 9× on a plain
loop. What is slow is *a call* and *a property write through the method it
finds*, and the rest is a tax of about 0.2 µs per evaluated expression.

Two traps in measuring it, both of which the first version of the bench fell
into, because the next person to benchmark this engine will hit them too:

- **A top-level `var` is a property of `window` here.** Done at script top
  level the same loop is **748 ms instead of 355 ms**: two thirds of it is
  `WindowExotic::set` and the global object's `assign_hook`, not the evaluator.
  Every case is inside a function for that reason. (A global `var` is also
  written *twice* per assignment — §5.)
- **One run measures the machine.** The same binary measured "1.27× faster"
  with fastest-of-three and "0.95×" on the next run. The bench reports medians
  of interleaved runs, and every number below is the median of three such runs.

## 2. What the 0.2 µs per expression is made of

`gprof` over an `-O2 -pg` build, on a loop inside a function, an empty call in
a loop, and a method call in a loop. Nothing dominates — a dozen items between
3% and 15%. In order: `invoke_function` (a `shared_ptr` frame, a `define` per
parameter and per `var`, a lambda and an `EnvScope` per call) for every *call*;
then, for every *expression*, `evaluate`, `eval_binary`, `combine_binary`,
`lookup_identifier`, `resolve_reference`, `read_reference`, `write_reference`
and `gc_stress_point`, with `std::string` and `JsValue` copies on each. A call
costs **~2.4 µs** and an empty loop iteration **~0.18 µs**; the call is the whole
difference between them.

## 3. What changed: `i++` writes where the walk found it

`eval_update` used to build a `Reference` for `i++` — copying the name into a
`std::string` and the value into a copy of a `JsValue` — then read the binding,
convert it, and walk the scope chain **a second time** to write it back. For the
counter of a loop that is two walks, two string compares per scope, and two
value copies, per iteration, for a binding the interpreter had resolved on the
previous iteration.

Now one walk finds the binding and the write lands on it. The refusals keep the
general path's words exactly — `Assignment to constant variable.`, the TDZ
message, and a read-only binding ignored in sloppy code and thrown in strict —
and a name that is not a binding of any scope falls through to the general path,
so `a[i++]++`, a global that resolves through the global object, and an
undeclared name all behave as before.

**What it is worth** (median of three interleaved runs each, before → after):

| case | 3a0b600 | this branch | speedup |
| :--- | ---: | ---: | ---: |
| `loop` | 358–385 ms | 318–340 ms | **1.11–1.13×** |
| `property` | 789–829 ms | 738–762 ms | **1.07–1.09×** |
| `array` | 149–151 ms | 137–148 ms | 1.01–1.10× |
| `call`, `method`, `recursion` | — | — | unchanged (1.0×) |

## 4. What was tried and thrown away (this is the useful part)

Three changes were built and measured, and one was kept.

1. **A per-scope cache of the last four bindings looked up**, read and written
   by `lookup_identifier`, `assign_identifier`, `resolve_reference` and
   `write_reference`. It was correct — the soundness rule was that only a
   binding of the scope the lookup *started from* is remembered, and within one
   scope there is never more than one binding of a name, so the slot whose name
   matches is the one a walk would find — and it passed everything. It was
   worth **nothing**: `loop` 1.04×, `property` 1.00×, `array` 1.13× against a
   baseline that moved ±20% (one run even measured the patched binary *slower*
   on `call` and `recursion`). It also cost 176 bytes of every scope, and a
   `std::string` per cached name. Reverted, and the diff of the kept change is
   smaller than the diff of the cache alone.
2. **A global version stamp** to invalidate that cache, bumped whenever a
   binding entered or left any scope. It never hit once: `invoke_function`
   defines `this`, each parameter and each `var` on entry, so a 1M-iteration
   loop bumped it **3,000,078 times** (`hits=1000000 misses=5000005`, from a
   temporary counter). A version number is the wrong shape of correctness for a
   lookup that has to hit a million times in a row.
3. **Caching the thread's stack floor** in `StackLimit.hpp` (`js_check_stack`
   recomputed it before every expression — 2% of the profile). It measured no
   faster, and the sanitizer build was refusing its deep-recursion case, so it
   was reverted rather than defended. The sanitizer refusal turned out to be
   pre-existing (a sanitizer build of `3a0b600` from a worktree refuses the
   same case, byte for byte); the caching is still unproven and is left open.

The lesson for the next phase is in (1): in this engine the *scope chain* is
not where a loop's time goes. `std::string` compares are not the cost; the
`Reference`'s copies and the second walk were. Measuring the third of these
without measuring the first two would have shipped the 176 bytes and the
complexity for 4%.

## 5. What is still open — and what to take next

1. **A global `var` is written twice.** `assign_identifier` writes the
   binding, then `set_property` on the global object finds the same binding
   again through `WindowExotic::set` and the global object's `assign_hook`
   writes it a second time. A top-level loop is **748 ms against 355 ms** for
   the same work inside a function: this is the largest single item measured in
   this phase and it is in `src/js/JsInterpreter.cpp` and
   `src/js/DomWindowBindings.cpp`.
2. **`invoke_function`'s frame** (~2.4 µs a call): a `shared_ptr` environment, a
   `define` per parameter and per `var`, a lambda `prepare`, an `EnvScope`.
   `src/js/JsEval.cpp`'s `invoke_function` and `bind_parameters`.
3. **`JsValue::set_property`'s exotic branch** compares origins and calls
   through a `std::function` for every property write on the global object.
   #193 held `src/js/JsValue.cpp` during this phase.
4. **The stack floor** (§4.3) and **`Binding`'s 80 bytes** (32 of them an
   uninterned name).
5. **The row is not closed**: `call` remains 22–27× Node and is the bytecode
   VM's; the loop cases are 8–9×. `make bench-js-core` is how the next phase
   knows what moved.

## 6. Verified

- `make test` — **347/347**, one of them new:
  `a_counter_is_written_where_the_walk_found_it` (postfix and prefix, a string
  counter, a property counter that is *not* the fast path, a shadowed counter,
  an `eval` write to the counter — checked against Node.js v22.22.3, which
  prints `1,2,` — a global counter through `window`, the words of the `const`
  and TDZ refusals, per-iteration `let` bindings, and an undeclared name).
- `make js-conformance` **1146/1158**, `everyday` **90/90**; `make
  dom-conformance` **678/678**; `make conformance` **1973/1973**; `make wpt`
  **9567/9824**; `make url-conformance` **1188/1188** — all unchanged.
- Under `-fsanitize=address,undefined`: the new suite's script (as a script
  through the CLI), the DOM corpus **678/678**, and the JS corpus **1143/1158
  with 2 timeouts** — both reproduced on a sanitizer build of `3a0b600`, so
  neither is this change: `deep-nesting.dat`'s million-node linked list takes
  2.5 s in the ordinary build and 17 s with the sanitizers (the runner's 10 s
  timeout is wall time; its floor for the sanitizer build is 1122), and
  `functions.dat`'s deep recursion is refused for stack reasons in both.
- Verified with `python3 tools/ci/check_docs.py` and
  `python3 tools/ci/check_numbers.py`: 0 failures each. The unit-suite count in
  `README.md` is left for the landing session, as `AGENTS.md` asks.
