# Finding 86 — Direct eval still crossed the wrong semantic boundaries

**Severity:** medium — the common direct-eval path worked after Phase 65, but
optional calls could expose locals, invalid `super()` source ran far enough to
have effects, declarations vanished with lexical scopes, and function-expression
self names were mutable.

**Found by:** a test262 comparison after Phase 65. The affected cases were
`eval-optional-call.js`, `super-call-method.js`, `scope-head-var-none.js` for
`for-in`/`for-of`, the `switch` and catch scope cases, and named function,
generator, async-function, and async-generator reassignment-through-eval
cases.

## 1. What was wrong

The implementation decided directness from an identifier called `eval` alone,
so the optional-call AST shape was lost. It parsed all direct-eval source with
no inherited `super` grammar context, then deferred invalid `super()` calls to
the evaluator. It also treated the current lexical environment as a direct
eval's variable environment, and represented a named function expression's
inner name as a normal writable binding.

## 2. What changed

Phase 80 distinguishes optional calls before installing an `EvalFrame`. It
captures direct-eval `super` permissions in the parsed call expression, passes
them to the eval source parser, and rejects a `super()` that lacks a derived
constructor context before executing source. A derived constructor and its
arrows keep that syntax permission; object methods retain only `super` property
access.

Function activation scopes are explicitly variable environments, so sloppy
direct-eval `var` and function declarations walk past block, loop, catch, and
switch environments to the enclosing function. Named function-expression
scopes use a readonly binding, which supplies the language's sloppy-ignore and
strict-throw behavior for ordinary assignment and direct eval alike.

## 3. How it was verified

```bash
make -j2 test
# All 235 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

python3 tools/conformance/run_test262.py --timeout 30 \
  language/eval-code/direct/super-call-method.js \
  language/expressions/optional-chaining/eval-optional-call.js \
  language/statements/for-in/scope-head-var-none.js \
  language/statements/for-of/scope-head-var-none.js \
  language/statements/switch/scope-var-none-case.js \
  language/statements/try/scope-catch-param-var-none.js \
  language/expressions/function/named-no-strict-reassign-fn-name-in-body-in-eval.js \
  language/expressions/function/named-strict-error-reassign-fn-name-in-body-in-eval.js \
  language/expressions/generators/named-no-strict-reassign-fn-name-in-body-in-eval.js \
  language/expressions/generators/named-strict-error-reassign-fn-name-in-body-in-eval.js \
  language/expressions/async-function/named-reassign-fn-name-in-body-in-eval.js \
  language/expressions/async-function/named-strict-error-reassign-fn-name-in-body-in-eval.js \
  language/expressions/async-generator/named-no-strict-reassign-fn-name-in-body-in-eval.js \
  language/expressions/async-generator/named-strict-error-reassign-fn-name-in-body-in-eval.js
# test262 at 2e58f87d8350: 14 tests run, 0 skipped (modules)
# litebird 14 pass (100.0%)
```

The native Phase 80 test also proves that the invalid object-method eval does
not execute source text before its `SyntaxError`, while direct eval in a
derived constructor still calls `super()` successfully.

## 4. What is still open

The task does not attempt every remaining EvalDeclarationInstantiation conflict
or completion-value rule, and it does not take ownership of the shared
`tests/js-conformance/` corpus. Those are distinct follow-ups; the four
observable regressions in the queue row are closed.
