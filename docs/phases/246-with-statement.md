# Phase 246 — ECMA-262 `with` Statements and Object Environment Records

**Date:** 2026-11-24  
**Finding:** [`docs/findings/250-with-statement-and-object-environment-records.md`](../findings/250-with-statement-and-object-environment-records.md)  
**Oracles:** `python3 tools/conformance/verify_with_statement.py` (**19/19** match Node.js v22.22.3 line for line, CI-wired), `make test` (`tests/suites/246-with-statement.cpp`)

## Goal

Implement sloppy-mode `with (expr) stmt` (ECMA-262 §14.11 and §9.1.1.2 Object Environment Records with `[[IsWithEnvironment]] = true`), wire it to `Symbol.unscopables` (landed in Phase 233), preserve strict-mode early `SyntaxError` rejection, and remove the `SyntaxError: 'with' statements are not supported` failure hit by TodoMVC ES5's runtime-compiled template (`todomvc-common/base.js`).

## Changes

### 1. AST & Parser (`include/litebird/js/JsParser.hpp`, `src/js/JsParser.cpp`)

- Added `NodeKind::With` and `WithStmt(object, body)` (automatically destroyed iteratively by `ast_delete` via `std::unique_ptr`).
- Implemented `JsParser::parse_with_statement()`:
  - Rejects `with` with an early `SyntaxError` whenever `is_strict()` is true (top-level `"use strict"`, strict functions, `new Function('"use strict"; ...')`, strict direct `eval`, and inside class bodies/static blocks/field initializers).
  - Enforces `WithStatement : with ( Expression ) Statement` (§14.11): rejects lexical/class/function declarations directly in the single-statement body (`with ({}) let x = 1;`, `const`, `class`, `function`, `async function`, `let[0] = 1;`, labelled declarations) while allowing `with ({}) let = 1;`, `var`, blocks `{ function f() {} }`, `;`, and labelled statements.

### 2. Object Environment Record & GC Tracing (`include/litebird/js/JsInterpreter.hpp`, `src/js/JsInterpreter.cpp`)

- Extended `JsEnvironment` with `m_is_with_scope`, `m_has_with_ancestor` (propagated to child scopes so code outside `with` pays a single boolean check and zero runtime overhead), and `m_with_object`, created via `JsEnvironment::make_with_scope(parent, obj)`.
- Kept `m_is_var_scope = false` on `with` scopes so `var` hoisting and sloppy direct-`eval` `EvalDeclarationInstantiation` skip cleanly to the enclosing variable scope.
- Traced `m_with_object` in `environment_trace()` (`src/js/JsInterpreter.cpp`) and `JsEnvironment::collect_roots()`, and cleared it in `clear_bindings()` so closures outliving `with` keep their binding object alive across GC while unreachable cyclic `with` scopes sweep cleanly.

### 3. Evaluator Semantics (`src/js/JsEval.cpp`)

- **`with_object_has_binding()` (§9.1.1.2.1 `HasBinding`):** ignores `this` and internal synthetic slots (`%fn`, `%home`, `%new.target`, `#...`); calls `HasProperty(O, N)` first and returns `false` without reading `O[@@unscopables]` when absent; reads `Get(O, @@unscopables)` and ignores primitive values; returns `false` when `ToBoolean(Get(unscopables, N))` is `true`.
- **`exec_with()` (§14.11.2):** runs `js_to_object(evaluate(object))` (throwing `TypeError` on `null`/`undefined` and boxing primitives), pushes the `with` scope via `EnvScope`, resets `frame.completion = undefined` (`UpdateEmpty(C, undefined)`), and runs the body.
- **`try_lookup_identifier()` / `resolve_identifier_reference()` / `Reference`:**
  - `eval_call` & `eval_tagged_template` pass `WithBaseObject()` as `this` (§13.3.6.1 step 4.b) for unqualified identifier calls/tags resolved on a `with` object, while `(0, fn)()` and outer declarative functions keep `undefined`.
  - `eval_assign`, `exec_var_decl`, `bind_pattern` (`BindMode::Var` / `BindMode::Assign`), `eval_update`, and `for-in`/`for-of` resolve the identifier reference before RHS evaluation (§13.15.2 / §14.3.2.1); in strict mode (a nested strict function inside `with`), `write_reference` checks §9.1.1.2.5 `SetMutableBinding` (`ReferenceError` if the property was deleted from the `with` object during RHS evaluation; `TypeError` on non-writable properties).
  - `typeof` resolves once through `with` scopes (respecting `@@unscopables` and returning `"undefined"` when unbound); `delete` skips unscopable properties and calls `delete_property` on the `with` object when bound.
  - Guarded the `id->is_undefined` and `eval_update` identifier fast paths with `!env->has_with_ancestor()` so `with ({ undefined: 42 }) { undefined }` returns `42` without slowing down non-`with` loops.

## Verification

- `python3 tools/conformance/verify_with_statement.py`: **19/19** cases match Node.js v22.22.3 line for line (basic lookup/shadowing/nesting, prototype chain, primitive boxing + null/undefined `TypeError`, all 16 `Array.prototype[Symbol.unscopables]` names, custom `Symbol.unscopables`, `WithBaseObject` calls & tagged templates, `typeof`, `delete`, `var` hoisting & destructuring, compound/logical/update operators + getters/setters, `for-of`, `undefined` shadowing, strict early `SyntaxError`s, nested strict functions, closures/abrupt completion, `eval` completions & direct `eval`, generators/`async`/`await`, `try`/`catch`/`finally`, and TodoMVC's runtime-compiled template). Wired into `.github/workflows/ci.yml`.
- `make test`: all engine test suites pass including `tests/suites/246-with-statement.cpp` (adds §13.15.2 pre-RHS deletion checks and GC liveness/cycle-collection checks).
- `make js-conformance`, `check_numbers.py`, `check_docs.py`, `check_suites.py`, `check_ci_parts.py`: 0 failures.
