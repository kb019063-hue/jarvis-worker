# 65. ✅ Derived constructor `this` before `super()` and `let`/`const`/`class` TDZ (Phase 59)

> Moved out of `AUDIT.md` — written there as a `## 65. ✅ Derived constructor `this` before `super()` and `let`/`const`/`class` TDZ (Phase 59)` section.
> The text below is the section as it stood, unchanged.


**The hole.** Phase 43 introduced `JsEnvironment::this_binding` for derived constructors and noted (`ARCHITECTURE.md`, Phase 43, §8) that the engine still had no uninitialized binding state:

- In a derived constructor, `this.x = 1; super();`, `() => this`, `super.m()`, `super[expr]`, and `return this` before `super(...)` read/wrote the pre-allocated instance or evaluated `expr` instead of throwing a `ReferenceError`.
- In blocks, functions, loops, `switch` bodies, `catch` clauses, parameter defaults, and top-level scripts, `let`, `const`, and `class` bindings did not exist until execution reached their declaration statement, so `typeof z` returned `"undefined"`, a block `let` failed to shadow outer bindings above its line, `let x = x` read the outer `x` or `undefined`, and `c = 2; const c = 1` succeeded in sloppy mode by creating an implicit global.

**The fix.** Implemented across `include/litebird/js/{JsInterpreter,JsParser}.hpp` and `src/js/{JsParser,JsEval}.cpp`:

- Added `Binding::is_initialized` (using existing struct tail padding, zero `sizeof(Binding)` growth), `AssignResult::Uninitialized`, and `declare_lexical` / `initialize_lexical` / `mark_uninitialized` on `JsEnvironment`, checking `!is_initialized` before `is_const` in `try_assign` so TDZ writes to `const` throw `ReferenceError` rather than `TypeError`.
- Marked derived constructor `"this"` uninitialized on entry until `eval_super_call` binds it, and updated `SuperMember` evaluation (read, call, and `resolve_reference` writes) to check `"this"` before evaluating computed property keys.
- Collected `lexical_decls` in `JsParser` for `BlockStmt` (including `Program` and function bodies) and `SwitchStmt`, and pre-declared uninitialized lexical bindings on entry to scripts, functions, async/generator frames, non-simple parameter lists, blocks, `switch`, `for`/`for-of`/`for-in` heads (rooted in `EnvScope`), `try/catch` destructuring params/blocks, and `class` `extends`/computed-key environments (before initializing static fields and `static {}` blocks).

**Verification.**

- `make test`: **201/201** unit suites passed at the time in both Release and ASan+UBSan (`build-asan/test_suite`; 208/208 after the phases merged since), including `js_derived_this_temporal_dead_zone`, `js_lexical_temporal_dead_zone_blocks_and_classes`, and `js_temporal_dead_zone_in_control_flow_and_patterns`.
- `make js-conformance`: **1003/1017** Release (**1002/1017** ASan+UBSan; `construct.dat` **49/50**, `lexical.dat` **41/41**, everyday **90/90**); Node `--reference "node -e"` **1017/1017**.
- `make test262` (`before=/tmp/litebird-before` vs `now=build/litebird`): **12,359 / 15,686** in the default 21 dirs (**+43 pass**, **0 regressions**) and **8,195 / 10,622** across 13 TDZ/scope dirs (**+49 pass**, **0 regressions**; **0** failures remaining in `language/block-scope/` and **0** TDZ failures in `language/statements/{let,const}/`).
- Release & ASan+UBSan regression sweeps: `dom-conformance` **669/669**, `url-conformance` **1188/1188**, `conformance` **1792/1792**, `wpt` **9538/9744** (`0` unlisted), `verify-promise-iterators` **34/34**, `make bench`, `make diff-strings` (`50,509/50,580`), `fuzz_parser` (400) & `fuzz_pipeline` (400) **0** findings, and `deep_pages` (targeted + 150 random ASan) **0** failures.

**Still open.**
- Separate `<script>` tags evaluate `run_program` per script, so referencing a `let` declared in a later `<script>` throws `"x is not defined"` (`ReferenceError`) rather than `"Cannot access 'x' before initialization"`, and redeclaring the same lexical name across two `<script>` tags overwrites `m_global_env` instead of raising a `SyntaxError`.
- Direct `eval` scope (tracked separately in `AGENTS.md`) and parse-time early `SyntaxError` for `{ let x; { var x; } }` remain open.

---
