# Finding 176 — Direct `eval` dropped caller strictness, `new.target`, and pre-mutation declaration checks

**Resolved by Phase 171** (`docs/phases/171-direct-eval-context-and-declaration-instantiation.md`).

## 1. What was found

1. **`eval_source()` forwarded `super` flags while dropping caller `strict`, `new.target`, and `forbid_arguments`.**
   `JsInterpreter::eval_source()` constructed `JsParserContext` with only
   `allow_super_property` and `allow_super_call`. As a result:
   - Parse-time strict early errors (`eval('function dup(a, a) {}')`,
     `eval('var public = 1;')`, `eval('eval = 1;')`) never fired when called directly from
     a strict function (`13.0-8-s.js`, `13.1-2-s.js`, `13.1-4-s.js`).
   - `new.target` was permitted everywhere instead of tracking non-arrow function /
     method / field / static-block ancestry across direct `eval`, and direct `eval` inside
     a class field initializer did not reject `arguments`.
   - Indirect `eval` fell back to `run(source)`, executing as a top-level script whose
     `let`/`const`/`class` leaked into `m_global_env`.
2. **`run_program()` ran `EvalDeclarationInstantiation` in place without pre-validation, and Annex B.3.5 exempted only `var_names`.**
   Binding `var` and `function` declarations during instantiation meant a later
   `let`/`const`/catch conflict left earlier declarations partially installed. Even after
   adding atomic pre-validation for `program.var_names` and `program.functions`, the first
   two-binary `test262` run caught one regression in `annexB`:
   `annexB/language/eval-code/direct/var-env-lower-lex-catch-non-strict.js` failed because
   the `s->is_simple_catch && s->simple_catch_name == name` exemption (ECMA-262 Annex
   B.3.5) had been applied to `program.var_names` and omitted from `program.functions`.
3. **Splitting parameter-expression scopes shifted `exec_env` past body lexical hoisting.**
   When `invoke_function()` split `outer -> param_env -> body_var_env -> body_lex_env`,
   hoisting `node->body->functions` while `exec_env` still pointed to `body_var_env`
   prevented hoisted functions from closing over `body_lex_env` variables. Restoring
   `exec_env = body_lex_env` before `hoist_functions()` fixed the closure parent chain.
4. **Raw references into `m_functions.back()` inside parser guards dangle on reallocation.**
   During implementation, holding raw references/pointers to `m_functions.back().loop_depth`
   or `.labels` inside `DepthGuard` and `LabelGuard` broke whenever a function expression
   or arrow inside a loop, switch, or labelled statement pushed onto `std::vector<FunctionContext> m_functions`
   and reallocated its storage (the same vector-reallocation failure mode Finding 146 hit
   with `FlagScope`). Storing `m_functions.size() - 1` (`size_t index`) in `DepthGuard` and
   `LabelScope` avoids any pointer across nested parses.

## 2. How it was verified

- **Differential oracle (`tools/conformance/verify_eval_context.py`):** **3 / 13** matching
  Node.js v22.22.3 on `origin/main` → **13 / 13** on Phase 171 (Release and ASan+UBSan).
- **Two-binary `test262` vs `origin/main`:** `DEFAULT_DIRECTORIES` **14,898 → 14,967 / 15,686 (+69 / -0)**,
  `language/eval-code` **163 → 251 / 343 (+88 / -0)**, full non-module `language/`
  **20,629 → 20,893 / 23,005 (+264 / -0)**, and `annexB` **673 → 673 / 1,086 (+0 / -0)**
  after extending the B.3.5 simple-catch exemption to `program.functions`.
- **Unit & conformance suites:** `make test` **339 / 339** + **114 / 114** (Release and
  ASan+UBSan under `ASAN_OPTIONS=detect_leaks=0:quarantine_size_mb=1:malloc_context_size=2`),
  `make js-conformance` **1,145 / 1,158 (98.9%, 90 / 90 everyday)**, and `make asan-deep`
  **882 / 882** runs at depth 10,000.

## 3. What remains outside this finding

- `88` pre-TC39-PR#1322 `declare-arguments` tests in `language/eval-code` (where Node.js
  v22.22.3 agrees with Litebird), 3 `'with'` cases, and `indirect/realm.js` (`$262`).
- `decl-lex-configurable-global.js` / `decl-lex-restricted-global.js`, which depend on
  built-ins living solely on `m_global_object` rather than `m_global_env`
  (`src/js/JsInterpreter.cpp:800-960`).
- Strict-mode legacy octal literal rejection (`010`) in `JsLexer`.
