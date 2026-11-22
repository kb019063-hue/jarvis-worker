# Phase 171 — Direct `eval` context, declaration instantiation, and early syntax errors

Task row: **Early errors still open after Phases 128 and 141** (`AGENTS.md`, Open tasks).
Finding 176.

## 1. What was wrong

Phases 128 and 141 closed the parser's class-body and non-class early errors on plain
scripts, leaving five runtime and parser-context seams where `eval`, global scripts,
function parameters, loops, and classes diverged from ECMA-262:

1. **Direct `eval` dropped caller context (`eval_source`, `JsParserContext`).**
   `JsInterpreter::eval_source()` passed only `allow_super_property` and
   `allow_super_call` into `JsParserContext` — not caller `strict`, `allow_new_target`, or
   class-field `forbid_arguments`. Strict callers failed to enforce parse-time strictness
   inside direct `eval` (`language/statements/function/13.0-8-s.js`, `13.1-2-s.js`,
   `13.1-4-s.js`), direct `eval` inside a class field allowed `arguments`, and `new.target`
   was allowed everywhere at parse time instead of being restricted to functions, methods,
   arrows inheriting it, field initializers, static blocks, and direct `eval` inside them.
2. **Indirect `eval` ran as an ordinary top-level script (`run_program`).**
   `eval_source()` called `run(source)` directly for indirect `eval`, running lexical
   (`let`/`const`/`class`) declarations into `m_global_env` where subsequent code could see
   them instead of giving indirect `eval` its own global lexical environment.
3. **`EvalDeclarationInstantiation` mutated before checking and misused global property semantics.**
   `run_program()` had no atomic collision check against intervening `let`/`const`/catch
   bindings before writing `var`/function declarations (no rollback on conflict, and
   missing Annex B.3.5 exception for a simple `catch (e)` binding), did not separate
   global `var`/function descriptors between top-level scripts (`Configurable: false` on
   `m_global_object`) and sloppy `eval` (`Configurable: true`), did not mark sloppy local
   `eval` bindings deletable for `delete name`, and left `JsEnvironment::get()` unable to
   resolve bare global properties defined on `globalThis`.
4. **Function parameter-expression scopes were merged with the body, loop completion values leaked across iterations, and class static `'prototype'` / field initializers had scope leaks.**
   When formal parameters contained default initializers, `var` in the body polluted the
   scope seen by default expressions (`has_parameter_expressions` was untracked). `while`,
   `do-while`, `for`, `for-in`, and `for-of` did not reset the completion value to
   `undefined` (§14.7). Computed `static ['prototype']` class members/fields did not
   throw `TypeError`, field initializers lacked their own `strict` var-scope with
   `new.target = undefined`, and unlabelled/invalid `break`, `continue`, duplicate labels,
   and top-level `return` were accepted at parse time.

## 2. What changed

1. **Full syntactic context propagation (`include/litebird/js/JsParser.hpp`, `src/js/JsParser.cpp`, `include/litebird/js/JsInterpreter.hpp`).**
   - Extended `JsParserContext` with `strict`, `allow_new_target`, and
     `forbid_arguments`, and `CallExpr` / `JsInterpreter::EvalFrame` with
     `direct_eval_allows_new_target` and `direct_eval_forbid_arguments`.
   - Extended `FunctionContext` with `allow_new_target`, `allow_return`, `loop_depth`,
     `switch_depth`, and `labels` (`LabelInfo{name, is_iteration}`), enforcing early
     `SyntaxError`s for top-level `return`, `new.target` outside functions/fields/static
     blocks (and direct `eval` inheriting it), unlabelled or unmatched `break`/`continue`,
     `continue` targeting a non-iteration label, and duplicate nested labels.
   - Replaced raw-pointer/reference guards over `m_functions` with index-based
     `DepthGuard` and `LabelScope` guards so nested functions/arrows inside loops,
     switches, or labelled statements can reallocate `m_functions` safely without
     dangling references (`Finding 176`).
   - Added `FunctionNode::has_parameter_expressions` in `finish_params()`.
2. **`EvalDeclarationInstantiation`, indirect `eval` scope, and global bindings (`include/litebird/js/JsInterpreter.hpp`, `src/js/JsEval.cpp`).**
   - Added `bool is_eval` to `run_program(..., direct_eval, is_eval)` so indirect `eval`
     parses fresh and executes inside its own lexical layer over `m_global_env` rather than
     polluting global lexical scope.
   - Passed `frame.environment->strict_mode != 0` into `JsParserContext::strict` on direct
     `eval`.
   - Added `JsEnvironment::is_deletable`, `JsEnvironment::remove_own()`,
     `JsEnvironment::is_simple_catch`/`simple_catch_name`, and `globalThis` fallback in
     `JsEnvironment::get()`.
   - Implemented atomic pre-validation in `run_program()` before binding: direct `eval`
     walks `var`/`function` names up to the enclosing `is_var_scope` and throws
     `SyntaxError` before mutating anything if an intervening lexical binding or non-simple
     catch parameter collides (while exempting Annex B.3.5 `s->is_simple_catch && s->simple_catch_name == name`
     for both `var_names` and hoisted `functions`), and global `var`/function declarations
     reject collisions with `m_global_env` lexicals while defining own properties on
     `m_global_object` (`Writable | Enumerable` for scripts, `PropertyMap::Default` for
     sloppy `eval`). Updated `eval_unary` (`delete`) to remove `is_deletable` bindings.
3. **Parameter/body scope split, loop completions, and class fields (`src/js/JsEval.cpp`).**
   - `invoke_function()` splits `outer -> param_env -> body_var_env -> body_lex_env` when
     `node->has_parameter_expressions` is true, and restores `exec_env = body_lex_env`
     before hoisting body `functions` so hoisted functions bind in `body_var_env` while
     closing over `body_lex_env`.
   - `exec_while`, `exec_do_while`, `exec_for` (after `init`), and `exec_for_each` reset
     `frame.completion` to `undefined`.
   - `define_class()` throws `TypeError` for computed static `'prototype'` methods,
     getters, setters, and fields, and runs field initializers in an isolated strict var
     scope with `%new.target = undefined`.

## 3. How it was verified

All final numbers below were measured on `build/litebird` and `build-asan/litebird` after
the final engine edit (clean `origin/main` baseline built at `/tmp/main-wt/build/litebird`,
pinned `test262` at `2e58f87d8350`):

- **Unit & cross-origin suites (Release + ASan/UBSan):**
  - `make test`: **337 / 337** engine test suites passed (including two new `TestPage`
    suites `direct_eval_inherits_context_and_checks_declarations_before_mutating` and
    `parameter_scopes_loop_completions_class_fields_and_early_syntax_errors`) + **114 / 114**
    cross-origin Window/Location/MessageChannel checks.
  - `ASAN_OPTIONS=detect_leaks=0:quarantine_size_mb=1:malloc_context_size=2 ./build-asan/test_suite`:
    **337 / 337** passed; `ASAN_OPTIONS=detect_leaks=0 ./build-asan/cross_origin_test`:
    **114 / 114** passed; `make asan-deep`: **882 runs over 315 pages at depth 10000**, 0 failed.
- **Node.js differential oracle (`tools/conformance/verify_eval_context.py`):**
  - `python3 tools/conformance/verify_eval_context.py --binary build/litebird`:
    **13 / 13** match Node.js v22.22.3 (vs **3 / 13** on `origin/main`), and **13 / 13**
    under `build-asan/litebird`.
- **Recorded JS conformance (`make js-conformance`, `make asan-js`):**
  - Release: **1,145 / 1,158 (98.9%)**, **90 / 90 (100.0%)** everyday probe, 0 crashes/timeouts.
  - ASan+UBSan: **1,144 / 1,158 (98.8%)**, **90 / 90** everyday probe, 0 crashes/timeouts.
- **Two-binary `test262` vs `origin/main` (`tools/conformance/run_test262.py --timeout 5`):**
  - `DEFAULT_DIRECTORIES`: **14,898 → 14,967 / 15,686 (95.4%)**, 14 skipped, **+69 / -0**.
  - `language/eval-code`: **163 → 251 / 343 (73.2%)**, 4 skipped, **+88 / -0**
    (`direct` 125 → 195 / 284 [+70]; `indirect` 38 → 56 / 59 [+18]).
  - Full non-module `language/` (`23,005` run across misc, `statements`, `expressions`; 95 skipped):
    **20,629 → 20,893 / 23,005 (+264 / -0)** — zero regressions:
    `statements/break` 8 → 20 / 20, `statements/continue` 11 → 24 / 24,
    `statements/return` 5 → 15 / 16, `directive-prologue` 58 → 62 / 62,
    `statements/class` 4,315 → 4,343 / 4,413 (+28),
    `expressions/class` 4,051 → 4,070 / 4,102 (+19),
    `expressions/object` 1,141 → 1,152 / 1,169 (+11),
    `statements/variable` 168 → 177 / 178 (+9),
    `statements/for` 355 → 363 / 385 (+8), `statements/for-in` 91 → 99 / 119 (+8),
    `statements/for-of` 692 → 698 / 749 (+6), `global-code` 21 → 27 / 42 (+6).
  - `annexB`: **673 → 673 / 1,086 (62.0%)**, **+0 / -0**.
  - ASan+UBSan `test262` (`eval-code`, `annexB`, `break`, `continue`, `return`, `labeled`,
    `variable`, `for`, `for-in`, `for-of`, `directive-prologue`, `function-code`,
    `global-code`, 3,263 tests): **2,636 / 3,263**, 0 sanitizer findings.

## 4. What is still open

- **`language/eval-code` remaining 92 failures:** `88` are pre-TC39-PR#1322
  `declare-arguments` tests expecting a `SyntaxError` that modern ECMA-262 and Node.js
  v22.22.3 reject (Litebird matches Node.js on probed cases); 3 fail because `with`
  statements are unsupported; 1 (`indirect/realm.js`) needs `$262.createRealm`.
- **`decl-lex-configurable-global.js` and `decl-lex-restricted-global.js`:** built-in
  globals (`Array`, `Promise`, etc.) are installed on `m_global_env` in
  `src/js/JsInterpreter.cpp:800-960` rather than solely as configurable properties of
  `m_global_object`; kept out of Phase 171 to avoid touching `JsInterpreter.cpp`.
- **Legacy octal literals in strict direct `eval` (`eval('010')`):** `JsLexer` tokenizes
  legacy octal literals without consulting strict mode.
- **Whole unimplemented features outside this task:** `Proxy` (96 cases), `$262` (53), and
  `statement/return/tco.js` (tail-call optimization).
