<!-- Moved out of ARCHITECTURE.md when new write-ups began going to docs/
     (docs/README.md); the phase table in ARCHITECTURE.md points here. -->

# Phase 65 — `eval` is not a script: it runs in the scope that called it

Task row: **`eval` inside a function is global code**. Finding 71.

## 1. What was wrong

The global `eval` was `run(source)`: parse the text, then run the `Program`
against `m_global_env`. That is *indirect* eval — the behaviour of
`(0, eval)(x)` and `window.eval(x)` — applied to every call. So:

```js
function f() { var local = 5; return eval('local + 1'); }   // ReferenceError
function g() { return eval('this'); }                        // the global object
class B extends A { constructor() { eval('super()'); } }      // SyntaxError
```

and `eval('var made = 42')` inside a function made `made` a **global**, which
is the exact opposite of what the standard says a direct eval does with a
`var`.

The `AGENTS.md` row said the fix was small: only a call written `eval(...)` is
direct, and `(0, eval)(...)` and `window.eval(...)` stay global. That is true
of the *decision* and wrong about the rest: a direct eval is not "the same
program run somewhere else", it is a `PerformEval` with a different set of
rules, and each of those rules is a piece of the engine this one did not have.

## 2. What changed, piece by piece

**Where directness is decided.** `eval_call` (the one place a call expression
becomes a call) already knows the callee expression, so it answers the
standard's question there: the callee has to be an `Identifier` *named*
`eval`, and that identifier has to resolve to this realm's `eval` — a
`var e = eval; e(x)` has an Identifier callee and is indirect. It pushes an
`EvalFrame{environment, direct}` on a thread-local stack
(`JsInterpreter::eval_call_stack()`), calls the function, and pops the frame
with an RAII guard. The `eval` global then reads the innermost frame and picks
one of two paths.

**The two paths.** `eval_source` is `run(source)` — unchanged, global — when
the frame is missing, says indirect, or has no environment. Otherwise it parses
the text and calls `run_program(program, caller_env, /*direct_eval=*/true)`.

**`run_program`, three parameters instead of one.** The one-parameter form is
now a wrapper for the global case. With a scope:

- **Strictness.** A script's `"use strict"` is the global scope's for as long
  as it runs; a direct eval's *inherits* the caller's, and only the eval's own
  text can change it. So the `StrictScope` is applied only for the global
  path, and the caller's environment keeps the flag its own code set.
- **Which declarations go where.** The eval gets a fresh environment layer
  over the caller's (`makes the caller's visible, keeps the eval's to itself`).
  Its `let`/`const`/`class`/`function` declarations go there, in
  `declare_lexicals` and `hoist_functions`, so a second `eval('hidden')` in the
  same function does not see the first one's `let hidden`, and the caller's
  code does not see it after the call returns. Its `var`s go into the
  **caller's** environment — unless the text is strict, or the caller is, in
  which case they stay in the eval's layer. (B.3.3.1 for the function
  declarations: sloppy eval text's `function inner() {}` is var-scoped like a
  `var`, so it lands in the caller's environment too.)

**The binding a function hoists because its body contains an `eval`.** This is
the part that is not a runtime concern at all. `arguments`, `new.target`,
`super` and `%fn` are engine-level bindings that `invoke_function` puts in a
function's environment *only when the function's own body mentions them* —
the parser sets `uses_arguments` / `uses_new_target` / `uses_super` on the
nearest non-arrow function as it goes. A body whose only mention is inside a
string literal mentions none of them, so `function f() { return
eval('arguments.length'); }` ran with no `arguments` in scope at all. The fix
is in the parser: a call of the identifier `eval` marks the enclosing function
with all three. It costs a function that calls `eval` an `arguments` object and
two bindings it may not need (which is what the standard's `varEnv` /
`lexEnv` do implicitly — V8 pays the same price); it makes
`eval('arguments[0]')`, `eval('new.target')` and `eval('super.m()')` work with
no runtime lookup at all.

**`super()` in an eval'd constructor.** Free, once `%fn` and `%home` are
hoisted: `eval_super_call` reads `%fn` from the environment and
`derived_constructor_scope()` walks out to the constructor. The `this` binding
that `super()` performs therefore lands in the constructor's own scope, which
is the scope the eval is layered over, which is why `eval('super()')` binds the
constructor's `this` and a second one throws `ReferenceError` exactly as a
written one does.

## 3. How it is verified

- `make js-conformance` → **1124 / 1150** in 29 files, up from 1085 / 1112 in
  28. The new `tests/js-conformance/eval.dat` is 38 cases, all passing: the
  caller's `var`/`let`/parameter/closure, a sloppy `var` that stays in the
  function and does not reach the global, strict text and a strict caller that
  keep theirs, `arguments` (plain and through an arrow), `new.target`,
  `super.m()`, `super()` in a derived constructor, a shadowed `eval`, indirect
  through `(0, eval)` and through an alias, a strict eval's assignment,
  completion values, nested evals, a `SyntaxError` in the text, `this` in
  sloppy and strict code, and `eval.length` / `eval.name`.
  `--reference "node -e"` re-runs all 38 under Node.js and prints
  **1150 / 1150**: every expectation is Node's, not this engine's.
- `construct.dat`'s recorded failure *"new.target and super() inside eval"* now
  passes — its first two columns were `false false` before and are `true true`
  now — so `make js-conformance` counts it among the 26 that changed.
- `make test` → **210 suites** (`209 → 210` on the merged tree, where Phase 64
  added one too), the new one being
  `js_direct_eval_runs_in_the_callers_scope`: the same ground as
  `eval.dat` in one script, whose expected string was produced by running that
  script under Node.
- `make wpt` → **9550 / 9744** subtests, 194 listed, 0 unlisted — the same
  score as before the change, measured before and after with the same binary
  recipe, so this phase neither gained nor lost a subtest of
  `dom/nodes`, `domparsing`, `encoding`, `css-flexbox` or `css-grid` (none of
  them calls `eval` outside a test harness).
- `make conformance` 1792 / 1792, `make dom-conformance` 669 / 669,
  `make url-conformance` 1188 / 1188, `make xml-conformance` 1718 / 1718,
  unchanged.
- `make asan-core` (ASan+UBSan) green: 209 suites (before the merge of Phase
  64), JS **1123 / 1150** — the one
  case below Release is `deep-nesting.dat`'s recursion-depth case, which the
  sanitizer build has always missed — DOM 669 / 669, XML 1718 / 1718, WPT
  9550 / 9744, both fuzzers 0 findings. `JS_ASAN_FLOOR` in the `Makefile` moved
  from 1083 to 1121.

## 4. What is still open

- **`eval` in a class field initializer or a `static {}` block** is not a
  case the parser's `nearest_non_arrow_function()` covers: there is no
  function there, and `arguments` is a `SyntaxError` in those positions
  anyway. `super` and `new.target` in field initializers are separate rows.
- **`eval` returning the completion value in a `switch` or a loop** is
  whatever `Frame::completion` holds; the standard's `UpdateEmpty` for
  `break`/`continue`/`return` inside eval'd text is not modelled. Not
  exercised by the corpus.
- **A function declared by sloppy eval text** lands in the caller's
  environment, which is right, but it is not *also* reachable after the
  calling function returns in the case where the eval ran at the top level of
  the program: there it goes to the global environment, which is the same
  scope, so it is — and the corpus has a case for it.
- **`%fn` is hoisted for every function that calls `eval`**, even one that only
  ever evaluates `'1 + 1'`. That is one extra binding and, when `arguments` is
  touched, one array copy per call. A profile-driven refinement would be to
  scan the string at the first eval for the three names and decide then; it is
  not worth the second code path.

The `AGENTS.md` row is gone; `ARCHITECTURE.md`, Phase 43, section 8, keeps
the pointer to the cases this phase left alone.
