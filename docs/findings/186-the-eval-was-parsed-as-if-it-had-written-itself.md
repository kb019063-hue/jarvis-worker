# Finding 186 — the eval was parsed as if it had written itself

A direct `eval` and an indirect one differ in six things, and the standard says
so in one place: §19.2.1.2 PerformEval's `strictCaller` and §19.2.1.3
EvalDeclarationInstantiation's `varEnv`. This engine had passed two of the six
through since `EvalFrame` was introduced — a direct eval written in a method
may say `super.x`, one written in a derived constructor may say `super()` —
and it passed neither strictness nor the caller's ban on `arguments`. The
parser was given no way to know it was parsing eval text at all, beyond
`direct_eval`, which only relaxed errors and never tightened them.

The cost was not local. `let x; eval('var x')` is a `SyntaxError` under
§19.2.1.3, and the engine answered `undefined`: an eval could walk into a scope
and replace a lexical binding the caller was still using. That is a wrong
answer, not a missing feature, and it had been shipping.

## Why it was invisible

`language/eval-code` is not one of the 21 directories `make test262` runs, so
the whole family sat at 47.5% while the published number was 95%. The three
defects below were each found by running it directly:

```
python3 tools/conformance/run_test262.py language/eval-code     # 163 / 343
```

The default directory list is a claim about what a change can move, and it
said nothing this change could move. Adding `language/eval-code` to it is the
kind of decision that belongs in a separate change, because it moves the
published denominator; it is not done here, and the suite is named in the
phase document instead so the next reader finds it.

## The mistake that recurred

Twice, the fix was to propagate a flag and the bug was to propagate it *one
context too far*, in opposite directions:

* `forbid_arguments` needed to reach arrows — an arrow has no `arguments` of
  its own — and the first version pushed it into the arrow's `FunctionContext`
  and then never consulted it inside the body, because the identifier check
  reads `m_functions.back()` and the arrow's body parses under a *block*, not
  a new function context.
* It needed to stop at an ordinary function's parameters, and the first
  version cleared it *after* `parse_params`, which made
  `(function({test262 = fnParam = arguments}) {})()` a `SyntaxError` in a
  static block. Two test262 cases caught it in the class directory.

Both are the same lesson: this parser's "current context" is a stack of
`FunctionContext`s that *blocks* do not push, so any per-context flag has to be
checked against which of the stack's entries a given piece of syntax runs
under, not against whether a flag was set somewhere above.

## The rule that was measured and declined

`language/eval-code/direct` has 112 remaining failures with one shape:
sloppy direct-eval text declaring `var arguments` in a function with a
non-simple parameter list must be a `SyntaxError`. That is Annex B B.3.2.1, and
V8 does not implement it — `(function (p = eval("var arguments")) {})()` answers
`undefined` in Node v22.22.3. Implementing it would make this engine reject
programs Node accepts, to pass procedurally generated cases that no other
directory exercises. The decision is recorded in the phase document and in
`AGENTS.md` rather than left as an unexplained 112.
