# Finding 146 — four early-error families evaluated source the parser should refuse, and `FlagScope` into `m_functions.back()` is unsafe across nested parses

**Resolved by Phase 141** (`docs/phases/141-early-syntax-errors-outside-class-bodies.md`).

## 1. What was found

1. **Four early-error parser families ran `$DONOTEVALUATE()` instead of throwing at parse time.**
   Following Phase 128's class-body rules, 136 early-error test262 failures outside class
   bodies — plus parameter/rest/directive-prologue/`for-in`/`for-of` cases across the
   wider suite — shared four structural gaps in `JsParser`:
   - `parse_object_literal()` had no `CoverInitializedName` (`{a = 1}`) or duplicate
     `__proto__` tracking that could be cleared only if converted to a destructuring
     target, and getters/setters lacked parameter-count/modifier checks.
   - `parse_statement_list()` treated any `"use strict"` followed by a newline as a directive
     even when escaped (`"use\u0020strict"`), parenthesized (`("use strict")`), or followed
     by a continuing binary/member operator (`"use strict"\n+ 1`), while parameter lists had
     no duplicate, strict-binding, lexical-redeclaration, or formal-parameter `yield`/`await`
     early errors.
   - Assignment/update operators and destructuring targets accepted strict `eval`/`arguments`,
     `this`, parenthesized literals/assignments (`([a]) = [1]`), non-terminal rest commas
     (`[...a,] = x`), and invalid object-rest expressions (`({...a + b} = x)`).
   - Non-method functions and top-level scripts rejected `super()` while still permitting
     `super.x`, and `parse_class()` still allowed `static constructor` fields.
2. **Binding `FlagScope` (`bool&`) to `m_functions.back().in_params` caused a heap-use-after-free.**
   During ASan+UBSan verification, `test_js_derived_this_temporal_dead_zone` crashed in
   `~FlagScope` (`src/js/JsParser.cpp:285`). While parsing default parameter expressions
   containing an arrow function (or nested function), pushing onto
   `std::vector<FunctionContext> m_functions` reallocated its backing buffer, invalidating
   the raw `bool&` held by `FlagScope`.

## 2. Why it matters

- In ECMAScript, early `SyntaxError`s must reject malformed programs **before** evaluation
  starts (`eval` / script entry), regardless of whether a failing branch is ever reached at
  runtime. Across the full non-module test262 corpus (`47,967` tests), fixing these rules
  and deferred cover checks lifted pass counts from `33,183` to `33,886` (**+703**, **0**
  regressions).
- A raw `bool&` into any `std::vector` stack whose elements can be pushed by recursive
  sub-expressions is an ASan use-after-free bug that only manifests when `m_functions` hits
  capacity growth during parameter parsing (`InParamsScope` stores `size_t index` into
  `m_functions`, matching `parse_class`'s `enclosing` index convention).

## 3. What was done

Phase 141 implemented all four early-error families in `JsParser.hpp`/`JsParser.cpp`, added
`JsParser::InParamsScope` (indexed lookup replacing `FlagScope` at both `parse_params` and
single-param `parse_arrow_function`), and verified every case in Node.js v22.22.3 inside
`parser_rejects_early_syntax_errors_outside_class_bodies` (`tests/test_suite.cpp`).

## 4. What is still open

- **`class/dstr` (144 test262 cases):** destructuring parameter edge cases not covered by
  rest/cover syntax alone.
- **Direct/indirect `eval` strictness and `new.target` propagation (`src/js/JsEval.cpp:526`):**
  left for an owner of `JsEval.cpp` (`statements/function/13.0-8-s.js`, `13.1-2-s.js`,
  `13.1-4-s.js`, and `tests/js-conformance/eval.dat:94`).
