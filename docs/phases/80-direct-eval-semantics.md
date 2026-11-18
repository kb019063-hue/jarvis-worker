# Phase 80 — Direct eval follows its syntax and variable environment

## 1. What was wrong

Phase 65 made a plain `eval(...)` see the caller, but four details still used
the wrong boundary:

- `eval?.(...)` was recognized by its identifier callee and therefore ran as a
direct eval, although an optional call is an indirect eval and must run as
global code.
- Source passed to a direct eval was parsed as an ordinary program. Thus
  `eval('super()')` in an object-literal method reached `eval_super_call()` and
  failed at run time, after preceding eval text could already have run. It must
  be a parse-time `SyntaxError`; a derived constructor must still be allowed to
  inherit its valid `super()` capability.
- A sloppy direct eval used the lexical environment at the call site as its
  `var` target. A `for` iteration, `catch`, `switch`, or block could retain the
  declaration until that lexical scope ended instead of putting it in the
  enclosing function.
- A named function expression installed its inner name as an ordinary writable
  binding. `var r = function F() { F = 1; }` changed `F` in sloppy code instead
  of ignoring the write, and direct eval inherited the same mistake.

## 2. What changed

`CallExpr` now records the two grammar permissions a syntactic direct eval may
inherit: whether `super.property` is valid and whether `super(...)` is valid.
`JsParser` carries those permissions through arrows, class methods,
object-literal methods, fields, and static blocks. `EvalFrame` transports them
only when the run-time callee is the realm's real `eval`; the parser then uses a
`JsParserContext` for the source text. In that context an object method may
still parse `super.m()`, but `super()` fails before the eval program runs; a
derived constructor (and an arrow lexically inside it) retains the permission.

Optional calls are excluded from the direct-eval decision explicitly. They do
not get an eval frame and therefore use the existing global `run(source)` path.
The parser also does not pre-hoist the caller's `arguments`, `new.target`, or
`super` just because an optional eval spelling appears.

Each function invocation now marks its environment as a variable environment.
For a sloppy direct eval, `run_program()` walks from the current lexical
call-site environment to that nearest variable environment before hoisting
`var` and sloppy function declarations. Strict eval source remains isolated in
its fresh eval layer as before.

Finally, the private scope made for a named function expression installs its
name with `define_readonly()`. The established identifier-assignment path
therefore ignores a write in sloppy code and throws `TypeError` in strict code,
with the same result when the write originates in direct-eval text.

## 3. How it was verified

The native regression suite deliberately owns the new coverage: the JavaScript
corpus is unchanged because open PR #85 owns `tests/js-conformance/`.
`js_direct_eval_follows_syntax_and_variable_environment_rules` checks all four
boundaries in one Node-recorded script: optional eval visibility, a no-side-
effects parse-time `super()` failure plus a valid derived-constructor call,
`for...of`/`for...in`/`catch`/block `var` targets, and sloppy/strict named
function-expression assignments with and without eval.

```bash
make -j2 test
# All 235 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

make js-conformance
# eval.dat 38/38
# JS conformance, full corpus: 1128/1152 (97.9%)  [29 files, 0 crashes, 0 timeouts]
```

The directly affected standard cases were also run against the repository's
pinned test262 checkout: the optional-call and object-method-super cases,
`scope-head-var-none` for `for-in` and `for-of`, the `switch` and catch scope
cases, and all eight function/generator/async-function/async-generator named
function-expression eval cases (sloppy and strict): **14/14 pass**.

## 4. What is still open

This is a repair to direct eval, not a complete implementation of every eval
instantiation rule. In particular, the remaining Phase 65 work around eval in
class field initializers/static blocks and completion-value details is separate.
The broader JavaScript corpus remains owned by its existing pull request; this
phase adds no corpus rows or expectations.
