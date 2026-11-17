<!-- Moved out of AUDIT.md when new write-ups began going to docs/
     (docs/README.md); AUDIT.md keeps the open findings. -->

# Finding 71 — `eval` inside a function was global code

Phase 65. The architecture is in
[`docs/phases/65-direct-eval-scope.md`](../phases/65-direct-eval-scope.md).

## 1. What was wrong

`eval` was one line: parse the argument and run it as a program against the
global environment. Every call, so every eval was the *indirect* one. A
function that wanted to read its own `var` through `eval` got a
`ReferenceError`; `eval('this')` gave the global object in sloppy code and the
global object in strict code too; `eval('super()')` in a derived constructor
was a `SyntaxError` because there was no `%fn` to read; `eval('new.target')`
was `undefined` above and below a `new`; and `eval('var x = 1')` planted `x`
on `window`.

## 2. What changed

- **`src/js/JsInterpreter.cpp`** — the `eval` global calls `eval_source`
  instead of `run`.
- **`src/js/JsEval.cpp`** — an `EvalFrame{environment, direct}` stack
  (`eval_call_stack()`), pushed in `eval_call` around the call and popped by
  RAII. `eval_call` decides directness itself: an `Identifier` callee named
  `eval` that resolves (through the caller's scope, so shadowing counts) to the
  global `eval`. `eval_source` reads the frame: indirect (or no frame) → `run`;
  direct → parse and `run_program(program, caller_env, /*direct_eval=*/true)`.
  `run_program` gained a scope and a mode: the eval's `let`/`const`/`class` and
  strict-case `function` declarations go into a fresh layer over the caller's
  environment, its sloppy `var`s and function declarations into the caller's
  environment, and its strictness is the caller's unless its own text says
  otherwise.
- **src/js/JsParser.cpp** — a call whose callee is the identifier `eval` marks
  the nearest non-arrow function with `uses_arguments`, `uses_new_target` and
  `uses_super`. Those flags are what make `invoke_function` put `arguments`,
  `%fn`, `%new.target` and `%home` into the function's environment, so the
  bindings the eval text needs exist without any lookup at run time.
- `include/litebird/js/JsInterpreter.hpp` — `EvalFrame`, `eval_call_stack()`,
  `eval_source()`, the three-parameter `run_program`.

## 3. How it is verified

`make js-conformance` **1085 / 1112 → 1124 / 1150**: a new
`tests/js-conformance/eval.dat` (38 cases, all passing, every expectation from
Node.js — `--reference "node -e"` prints 1150 / 1150) and `construct.dat`'s
recorded failure *"new.target and super() inside eval"* now passing. `make test`
**209 → 210** suites on the merged tree (`js_direct_eval_runs_in_the_callers_scope`,
its expected string produced by running the same script under Node). `make wpt` 9550 / 9744
before and after — neither gained nor lost. `make conformance` 1792 / 1792,
`make dom-conformance` 669 / 669, `make url-conformance` 1188 / 1188 all
unchanged.

## 4. What is still open

Field initializers and `static {}` blocks (`arguments` is a `SyntaxError`
there, `super`/`new.target` are separate rows), and `UpdateEmpty` for
`break`/`continue` inside eval'd text. `AGENTS.md` no longer has the row.
