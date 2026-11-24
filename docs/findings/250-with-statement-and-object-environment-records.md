# Finding 250 — `with` statements now run through Object Environment Records and exercise `Symbol.unscopables`

Phase: [`docs/phases/246-with-statement.md`](../phases/246-with-statement.md)

## What was missing before Phase 246

`src/js/JsParser.cpp:569` rejected `JsTokenType::With` unconditionally with `SyntaxError: 'with' statements are not supported`, which caused TodoMVC ES5's runtime-compiled template (`todomvc-common/base.js`: `with(obj||{}){...}`) to log an uncaught `SyntaxError` on load and left Phase 233's `Array.prototype[Symbol.unscopables]` table unexercised.

## What works now

- **Sloppy-mode parsing + strict-mode early errors (`src/js/JsParser.cpp`):** `with (expr) stmt` parses in sloppy mode, rejects strict code early (top-level `"use strict"`, strict functions, `new Function`, strict direct `eval`, and class bodies/static blocks), and rejects single-statement declarations (`let x`, `const`, `class`, `function`, `async function`, `let[0]`, labelled declarations) while permitting `with ({}) let = 1;`, `var`, blocks, `;`, and labelled statements.
- **Object Environment Records (`include/litebird/js/JsInterpreter.hpp`, `src/js/JsInterpreter.cpp`, `src/js/JsEval.cpp`):**
  - `JsEnvironment::make_with_scope` carries `m_with_object` and propagates a cached `m_has_with_ancestor` bit to descendants so non-`with` code keeps all existing fast paths untouched.
  - `with_object_has_binding` implements ECMA-262 §9.1.1.2.1 (`HasProperty(O, N)` first, then `Get(O, @@unscopables)` only when present, ignoring primitives and synthetic slots `this`/`%...`/`#...`).
  - `eval_call` and `eval_tagged_template` pass `WithBaseObject()` as `this`; `eval_assign`, `exec_var_decl`, `bind_pattern`, `eval_update`, `typeof`, `delete`, `eval` completions (`UpdateEmpty(C, undefined)`), generators, `async`/`await`, and GC root/cycle tracing all handle `with` scopes.
- **Verification:** `python3 tools/conformance/verify_with_statement.py` agrees **19/19** with Node.js v22.22.3 (wired into `.github/workflows/ci.yml`), and `tests/suites/246-with-statement.cpp` verifies all 16 `Array.prototype[Symbol.unscopables]` names, strict early errors, §13.15.2 pre-RHS binding resolution, TodoMVC's template pattern, and GC survival/cycle sweeping.

## Notes

- Node.js v22.22.3 confirms `Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Int8Array.prototype), Symbol.unscopables)` is `undefined` (ES2024 does not define a separate `%TypedArray%.prototype[@@unscopables]` table).
- V8 has a long-standing divergence on sloppy `x = (delete o.x, 42)` inside `with (o)` (resolving after RHS instead of before RHS); Litebird implements ECMA-262 §13.15.2 pre-RHS `ResolveBinding` and §9.1.1.2.5 strict `SetMutableBinding`, verified in `tests/suites/246-with-statement.cpp`.
