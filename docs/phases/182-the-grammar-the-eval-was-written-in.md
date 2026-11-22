# Phase 182 — the grammar the eval was written in

*Finding 186.* A direct `eval` is not a script that happens to be called from
somewhere: §19.2.1.2 PerformEval hands it the strictness of the code that
called it, and the caller's grammar decides what its text is allowed to say.
This engine parsed every direct eval as if it had been written on its own, at
the top level, in sloppy code. Two of the six things a caller lends it were
already passed through — `super.x` and `super()` — through `EvalFrame`'s
`allow_super_property` and `allow_super_call`. The other four were not.

Measured before the change, against Node v22.22.3:

```
"use strict"; eval("var static;")            engine: undefined     correct: SyntaxError
class C { x = eval("arguments") }            engine: ReferenceError   correct: SyntaxError
class C { static { eval("arguments") } }     engine: undefined     correct: SyntaxError
let x; eval("var x")                         engine: ok            correct: SyntaxError
eval("break;") from inside a loop            engine: ok            correct: SyntaxError
```

## 1. Strictness is inherited, not re-derived

`run_program` already knew that a direct eval keeps the caller's strictness —
its `StrictScope` deliberately leaves the caller's environment flag alone —
but the *parser* had already run by then. `JsParserContext` carried no
strictness field, so `parse_program` always started with `strict = false` and
only a `'use strict'` in the eval's own text could change that.

The consequence is one sentence: every early error that only strict code has
to observe was skipped for eval text whose caller was strict. `var static`,
`var yield`, `var eval`, `var arguments`, `arguments = 1`, `delete x` — each of
them a `SyntaxError` in strict code and legal in sloppy code, and each decided
by `is_strict()` while parsing.

`JsParserContext::strict` now comes from the call site. `CallExpr` records it
(`direct_eval_from_strict_code`, beside the two `super` flags it already
recorded), `EvalFrame` carries it, and `parse_program` seeds the program's
`FunctionContext` with it. The directive prologue can still set the flag — it
is a `bool` that only goes true — so `eval('"use strict"; …')` from sloppy code
behaves exactly as it did.

## 2. The ban on `arguments` is lent, not re-declared

A class field initialiser and a static block are early-error contexts for
`arguments` (§15.7.1): `class C { x = arguments }` is a `SyntaxError` in every
engine, and this one already refused it, through
`FunctionContext::forbid_arguments`. Text handed to `eval` from there is the
caller's code for this purpose — `class C { x = eval("arguments") }` is a
`SyntaxError` before any of the eval's text runs, so a side effect before the
`arguments` must *not* have happened:

```
var executed = false;
class C { x = eval("executed = true; arguments;") }
try { new C() } catch (e) {}     // e is SyntaxError, and executed is still false
```

`forbid_arguments` now travels with the call the same way strictness does.

Two boundaries took a second attempt each, and both are in the tree:

* **An arrow does not lift it.** An arrow declares no `arguments` of its own,
  so it inherits the ban into its body: `eval("() => arguments")` is refused.
  The first version propagated the flag into the arrow's `FunctionContext`
  and then did not consult it inside the body — the ban was recorded and never
  checked, and half the fixtures still returned `ReferenceError`.
* **An ordinary function does lift it, from its parameters onward.** A
  `function` declares an `arguments` of its own, so `eval("function g(){ return
  arguments.length } g(7,8)")` is legal and answers `2`. The ban has to be
  cleared *before* `parse_params`, not after: a parameter default is ordinary
  code of that function, which is what
  `language/statements/class/static-init-arguments-functions.js` asserts with
  `(function({test262 = fnParam = arguments}) { … })('function')`. Clearing it
  after the parameters cost two test262 cases and the first run caught it.

## 3. `EvalDeclarationInstantiation` step 6.b

The third rule is not about `arguments` at all, and it is the one that is worth
the most outside the eval-code directories. §19.2.1.3 gives a direct eval's
`var`s to the caller's *variable* environment — which is what makes
`function f() { eval('var x = 1'); return x; }` answer `1` — but it refuses the
declaration outright when a name it would create is already a **lexical**
binding in a scope the caller can see:

```
function f() { let x; eval("var x") }     // SyntaxError: Identifier 'x' has already been declared
function f() { var x; eval("var x") }     // fine: a var replaces a var
function f() { { let x; } eval("var x") } // fine: the block is not in the caller's chain
```

The engine checked none of this. The check is on the eval's parsed
`var_names` against the caller's scope chain, walked from the call site up to
and including the caller's variable environment, and it is skipped when the
eval's own text is strict — step 5 gives strict eval text a variable
environment of its own, so its `var`s *shadow* the caller's `let` instead of
colliding with it. That last distinction is what four `var-env-*` fixtures
exist to test, and getting it wrong lost them.

## 4. `break`, `continue` and `return` are judged on the eval's own text

An eval's text is a Script. The caller's loop does not enclose it and the
caller's function does not contain it, so `eval("break;")` is a `SyntaxError`
however the call site is placed — including inside the very loop it cannot
break. This one is free once the parser knows it is parsing eval text: it keeps
a loop and a switch depth, which start at zero for every direct eval, and
`return` is refused while no function context encloses it.

## What moved

| Suite | Before | After |
|---|---|---|
| test262, the 21 default directories | 14,898 / 15,686 (95.0%) | **14,926 / 15,686 (95.2%)** |
| `language/eval-code` (not in the default run) | 163 / 343 (47.5%) | **196 / 343 (57.1%)** |
| `language/statements/class` | 4,315 / 4,413 | **4,321 / 4,413** |
| unit suites | 334 | **336** |
| js-conformance | 1,145 / 1,158 | 1,145 / 1,158 |
| dom-conformance | 678 / 678 | 678 / 678 |

28 gained across the default directories, 33 across `language/eval-code`, and
**nothing lost** on the final tree. `make test262` itself is 0-cost: the walk
that collects the caller's lexical names runs only when the eval text actually
declares a `var` or a function, and the frame carries no set when it does not.

## What is still open here

The 112 fixtures left in `language/eval-code/direct` are one Annex B rule, and
it is **not** implemented on purpose: B.3.2.1 makes it a `SyntaxError` for
sloppy eval text to declare a `var` named `arguments` in a function whose
parameter list is non-simple. V8 does not implement it — measured, not assumed:

```
(function (p = eval("var arguments")) {})   // Node v22.22.3: no error
```

Since the platform this engine is measured against answers the other way, and
no test outside `language/eval-code` exercises it, writing it would add a
divergence from Node to win 112 procedurally generated test262 cases. That
trade is worth recording rather than taking.

Also still open in this family, unrelated to eval's grammar: `with` is
unimplemented (3 cases), `$262` is missing (1), and the `var-env-*` global
fixtures need `delete` on a global var binding and a non-configurable global
function binding to behave as §19.2.1.3 step 16 describes.

## Files

* `include/litebird/js/JsParser.hpp` — `JsParserContext::{strict, forbid_arguments}`,
  `CallExpr::{direct_eval_from_strict_code, direct_eval_forbids_arguments}`,
  `m_loop_depth`/`m_switch_depth`.
* `include/litebird/js/JsInterpreter.hpp` — `EvalFrame::{strict, forbid_arguments}`,
  `JsEnvironment::is_var_scope`.
* `src/js/JsParser.cpp` — `parse_program` seeds strictness, `parse_params_and_body`
  carries and clears the ban, `parse_statement` and `parse_return_statement`
  enforce the completion early errors, `LatchedScope` maintains the depths.
* `src/js/JsEval.cpp` — `caller_lexical_names`, the step 6.b check in
  `eval_source`, and the frame fields in `eval_call`.
* `tests/test_suite.cpp` — `js_direct_eval_inherits_the_callers_grammar`.
