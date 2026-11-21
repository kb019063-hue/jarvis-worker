# Phase 141 — Early `SyntaxError`s outside class bodies

Task row: **the remaining 136 early-error failures outside class bodies** named in
`AGENTS.md` (and Phase 128 / finding 133): 60 in `language/expressions/object`, 64
across `language/statements/function` and `language/expressions/function`, and 12 in
`language/expressions/assignment` and `language/expressions/delete`. Finding 146.

## 1. What was wrong

Phase 128 closed the regular §15.7 class-body early errors, leaving four parser families
that still evaluated source ECMAScript rejects at parse time (`$DONOTEVALUATE()` ran
instead of throwing `SyntaxError`):

| family | examples rejected by Node.js v22.22.3, previously accepted |
| :--- | :--- |
| Object literals | `({a = 1})`, `{__proto__: 1, __proto__: 2}`, `{get p(a) {}}`, `{set p() {}}`, `{set p(...a) {}}`, `{async get p() {}}`, `{*set p(v) {}}` |
| Functions, arrows, and parameters | `'use strict'; function eval() {}`, `function f(a = 1) { "use strict"; }`, `(a, a) => {}`, `function f(a) { let a; }`, `function* g(x = yield 1) {}`, `async function f(x = await 1) {}` |
| Directive prologues & strict assignments | `function f() { ("use strict"); var eval = 1; }`, `function f() { "use\u0020strict"; var eval = 1; }`, `'use strict'; eval = 1; arguments++; --eval;` |
| Targets, rest patterns, `super`, and `parse_class` edges | `this = 1`, `[this] = [1]`, `[...a,] = x`, `({...a, b} = x)`, `({...a.b} = x)`, `([a]) = [1]`, `super.x` in plain functions/global scope, `class C { static constructor; }`, `class C { get p(x) {} set q(...r) {} }` |

Running `run_test262.py` at the branch point (`e1414f6`, baseline `/tmp/litebird-main`)
over those five directories (`language/expressions/object`, `language/statements/function`,
`language/expressions/function`, `language/expressions/assignment`,
`language/expressions/delete`) passed **2,100 / 2,438 (86.1%)**, while the 21 default
directories stood at **14,131 / 15,686 (90.1%)** and `built-ins + language + annexB`
totalled **33,183 / 47,967**.

## 2. What changed

All parser changes live in `include/litebird/js/JsParser.hpp` and `src/js/JsParser.cpp`,
holding strict parity with Node.js v22.22.3 and guarded against regressions by two-binary
test262 runs:

1. **Object literal cover grammar and accessors (`parse_object_literal`, `parse_program`,
   `expr_to_pattern`).** Added `PendingCoverError` tracking (`m_pending_cover_errors`
   plus `clear_cover_error`) for shorthand initializers (`{a = 1}`) and duplicate
   non-computed `__proto__` properties (`{__proto__: 1, __proto__: 2}`), deferring failure
   until an enclosing destructuring target proves the object is not a valid assignment
   pattern (`({a = 1})` fails at `parse_program()` if unconsumed, while `({a = 1} = x)`
   clears it). Enforced getter arity (`0` params), setter arity (`1` non-rest param),
   and refused `async`/`*` modifiers on getters/setters in both `parse_object_literal` and
   `parse_class`, along with `static constructor` fields in `parse_class`.
2. **Directive prologue strict detection (`parse_statement_list`, `is_directive_prologue_terminator`).**
   Replaced naive string equality with ECMA-262 §11.2.1 directive checks: only an
   un-escaped `"use strict"` (`tok.end - tok.start == 12`, or synthetic empty span)
   followed by `;`, `}`, EOF, or a newline-separated token that cannot continue an
   expression (`+`, `.`, `[`, `(`, `in`, binary/ternary operators, template parts) activates
   strict mode and triggers the non-simple-parameter check.
3. **Strict bindings, parameter early errors, and formal-parameter `yield`/`await`
   (`validate_binding_identifier`, `validate_function_early_errors`, `InParamsScope`).**
   Enforced strict-mode `eval`/`arguments` restrictions on declaration bindings, strict
   function-expression names, parameters, assignment operators, and prefix/postfix
   `++`/`--`. Enforced non-simple, strict, arrow, and method duplicate-parameter checks,
   param vs body `LexicalDeclaration` redeclaration checks, and parse-time refusal of
   `yield`/`await` expressions in formal parameters of generators and async functions using
   an index-based `InParamsScope` guard across nested `m_functions` pushes.
4. **Targets, rest elements, parenthesized patterns, and `super.x` (`is_assignable`,
   `expr_to_pattern`, `parse_binding_pattern`, `parse_primary`).**
   Refused `this` as a simple or destructuring target, trailing commas or subsequent
   elements after `[...rest]` / `{...rest}`, non-target expressions inside `{...rest}`,
   explicitly grouped (`(...)`) array/object/assignment targets in `expr_to_pattern`, and
   `super` property lookups outside methods (`!allow_super_property`).

### Bug caught by ASan while building

The first cut of `in_params` bound `FlagScope` (a raw `bool&`) directly to
`m_functions.back().in_params`. Parsing a default parameter initializer containing an
arrow function — `(a = () => 1)` — pushes a nested `FunctionContext` onto
`std::vector<FunctionContext> m_functions`, which can reallocate the vector buffer and
leave the outer `~FlagScope` writing through a dangling reference (`heap-use-after-free`
under `build-asan/test_suite` at `test_js_derived_this_temporal_dead_zone`). Replaced
`FlagScope` on `m_functions.back().in_params` with `JsParser::InParamsScope`, addressing
the context by stack index instead of a raw reference (finding 146).

## 3. How it was verified

Every probe expectation was cross-checked in **Node.js v22.22.3** (`node -e`) and added to
the new suite `parser_rejects_early_syntax_errors_outside_class_bodies` in
`tests/test_suite.cpp`.

**Two-binary test262 comparison** (`--binary before=/tmp/litebird-main --binary now=build/litebird`,
at `2e58f87d8350`):

| slice | before (`e1414f6`) | after (head) | delta |
| :--- | :--- | :--- | :--- |
| **Default 21 directories** (`run_test262.py`) | 14,131 / 15,686 (90.1%) | **14,522 / 15,686 (92.6%)** | **+391 gained, 0 lost** |
| **`language/statements`** (9,379 run) | 8,047 (85.8%) | **8,384 (89.4%)** | **+337 gained, 0 lost** |
| **`language/expressions`** (11,087 run) | 9,482 (85.5%) | **9,837 (88.7%)** | **+355 gained, 0 lost** |
| **Other 26 `language/` dirs** (2,549 run) | 2,068 (81.1%) | **2,075 (81.4%)** | **+7 gained, 0 lost** |
| **`built-ins + language + annexB`** (all 47,967) | 33,183 | **33,886** | **+703 gained, 0 lost** |

Top directory gains across the 47,967-test run: `language/statements/class` (+124),
`language/expressions/class` (+119), `language/expressions/object` (+54),
`language/statements/function` (+54), `language/statements/for-await-of` (+37),
`language/statements/variable` (+35), `language/expressions/async-generator` (+31),
`language/expressions/arrow-function` (+27), `language/expressions/function` (+26),
`language/expressions/compound-assignment` (+23), `language/statements/for-of` (+19), plus
15 other directories (`built-ins/Function` +4, `language/directive-prologue` +4,
`language/global-code` +2, etc.), with **0 regressions**.

**Repository suites & ASan+UBSan:**

- `make test` → **312 / 312** engine test suites + **114** cross-origin checks, 0 failures.
- `make conformance` (`1973/1973`), `make js-conformance` (`1144/1158`, `90/90` everyday probe),
  `make dom-conformance` (`678/678`), `make url-conformance` (`1188/1188`),
  `make xml-conformance` (`1718/1718`), `make wpt` (`9567/9824`), `make render-conformance`,
  `make diff-strings` (`50,580/50,580`), `make verify-regexp`, `make verify-promise-iterators`,
  `make verify-collection-iterators`, `make check-numbers`, `python3 tools/ci/check_docs.py`
  all pass.
- Under ASan + UBSan (`build-asan`): `test_suite` **312 / 312**, `cross_origin_test`
  **114 / 114**, `make asan-js` (**1143 / 1158**, 0 crashes, 0 timeouts), `make asan-dom`
  (**678 / 678**), `make asan-url` (**1188 / 1188**).

## 4. What is still open

- **`class/dstr` (144 tests):** parameter destructuring early/runtime interactions remain
  separate parser work.
- **Direct/indirect `eval` inheriting enclosing `strict` or `new.target`:**
  `JsEval.cpp:526` (`eval_source()`) does not propagate the caller's strict flag
  (`statements/function/13.0-8-s.js`, `13.1-2-s.js`, `13.1-4-s.js`), and `eval.dat:94`
  relies on `eval('new.target === F')` inside a function without `direct_eval` propagating
  enclosing non-arrow state, so top-level `new.target` remains unguarded here.
