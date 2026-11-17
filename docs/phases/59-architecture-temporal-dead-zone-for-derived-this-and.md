# Phase 59 Architecture: Temporal Dead Zone for Derived `this` and Lexical Bindings (Finding 65)

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 59 Architecture: Temporal Dead Zone for Derived `this` and Lexical Bindings (Finding 65)` section.
> The text below is the section as it stood, unchanged.


### 1. Why TDZ checks were missing

Prior to Phase 59, `JsEnvironment::Binding` tracked only `is_const`,
`is_readonly`, and `is_lexical` (`include/litebird/js/JsInterpreter.hpp`).
Entering a block, function, or script only hoisted `var` and `function`
declarations; `let`, `const`, and `class` declarations added bindings only when
execution reached their declaration statement. As a result:

- Reading or `typeof`-inspecting a variable before its `let`/`const`/`class`
  declaration either escaped to an outer binding or global object, or returned
  `"undefined"` for undeclared identifiers.
- Assigning to a lexical name before its declaration fell through to outer
  scopes or created an implicit global in sloppy mode.
- Derived constructors (`invoke_function`) defined `"this"` as initialized on
  entry while separately setting `env->this_binding = ThisBinding::Unbound`, so
  `this.x = 1`, `() => this`, `super.m`, `super[key]`, and `return this` before
  `super(...)` read or mutated the pre-allocated object instead of throwing a
  `ReferenceError`.

### 2. Uninitialized binding state in `JsEnvironment`

- `JsEnvironment::Binding` gains `bool is_initialized{true}` (occupying existing
  trailing struct padding with no size growth), and `AssignResult` gains
  `Uninitialized`.
- `declare_lexical(name, is_const)` creates or resets an own binding to
  `undefined` with `is_lexical = true` and `is_initialized = false`.
- `initialize_lexical(name, value, is_const)` stores `value` and sets
  `is_initialized = true`.
- `mark_uninitialized(name)` sets `is_initialized = false` on an existing own
  binding (used for derived constructor `"this"` on entry).
- `try_assign(name, value)` checks `!b->is_initialized` **before** `b->is_const`
  so assigning to a `const` (or `let`) in its TDZ throws `ReferenceError` while
  re-assigning an initialized `const` still throws `TypeError`.

### 3. Parser & evaluator TDZ scopes

- **Parser (`include/litebird/js/JsParser.hpp`, `src/js/JsParser.cpp`):**
  `BlockStmt` (also used for `Program` and function bodies) and `SwitchStmt`
  collect `std::vector<LexicalDecl> lexical_decls` for every `let`, `const`,
  and `class` declaration directly inside them.
- **Evaluator (`src/js/JsEval.cpp`):**
  - `lookup_identifier` throws `ReferenceError` when `!binding->is_initialized`
    (special-casing `"this"` to match derived-constructor error wording);
    `assign_identifier` throws via `AssignResult::Uninitialized`; `eval_unary`
    for `typeof` evaluates identifiers through `lookup_identifier` when `env->lookup(name)` finds a binding so `typeof z` in TDZ throws `ReferenceError`
    while `typeof neverDeclared` stays `"undefined"`.
  - Pre-declaration (`declare_lexicals`) runs on entry to `run_program`,
    `invoke_function` (for both body lexicals and non-simple parameter targets
    in `bind_parameters`, with derived `"this"` marked uninitialized on entry and
    re-initialized in `eval_super_call` once the parent returns), `exec_block`,
    `execute` (`BlockStmt`), `exec_switch` (discriminant evaluated outside `scope`;
    `case` tests and bodies inside), `exec_for` (lexical `init` declarations),
    `exec_for_each` (`head_env` for iterable evaluation + per-iteration
    `iter_env`), and `exec_try` (destructured `catch` parameter names before
    binding, then handler block lexicals).
  - `copy_scope` preserves `is_initialized` and `is_lexical` flags across
    per-iteration `for (let ...)` scopes, and `exec_for` / `exec_for_each` keep
    newly allocated lexical loop environments rooted in `EnvScope`.
  - `define_class` installs an uninitialized `const` self-binding inside
    `class_env` while evaluating `super_expr` and computed property keys, then
    initializes `cls->name` before running static field initializers and
    `static {}` blocks; `ClassDecl` initializes the outer mutable lexical binding
    via `initialize_lexical(..., /*is_const=*/false)`.
  - `SuperMember` evaluation (member read, `eval_call`, and `resolve_reference`
    for writes) calls `lookup_identifier("this", env)` before evaluating a
    computed property key so `super[sideEffect()]` before `super()` throws
    without running `sideEffect()`.

### 4. Verification & remaining boundaries

- `make test`: **201/201** unit suites passed at the time (208/208 after the
  phases merged since), in both Release and ASan+UBSan
  (`build-asan/test_suite`), including three new Phase 59 suites
  (`js_derived_this_temporal_dead_zone`,
  `js_lexical_temporal_dead_zone_blocks_and_classes`,
  `js_temporal_dead_zone_in_control_flow_and_patterns`).
- `make js-conformance`: **1003/1017** Release (**1002/1017** ASan+UBSan;
  `construct.dat` **49/50**, `lexical.dat` **41/41**, everyday **90/90**),
  verified **1017/1017** under Node.js (`--reference "node -e"`). Only `eval`
  inside a function remains open in `construct.dat`.
- `make test262` (against pre-change `main` binary): **12,359 / 15,686**
  default 21 dirs (**+43 pass**, **0 regressions**); **8,195 / 10,622** across
  13 TDZ/scope dirs (`let`, `const`, `class`, `super`, `this`, `switch`, `for`,
  `for-of`, `for-in`, `block-scope`, `typeof`, `try`; **+49 pass**, **0
  regressions**, **0** failures remaining in `language/block-scope/` and **0**
  TDZ failures in `language/statements/{let,const}/`).
- Release & ASan+UBSan regression checks: `dom-conformance` **669/669**,
  `url-conformance` **1188/1188**, `conformance` **1792/1792**, `wpt`
  **9538/9744** (`0` unlisted), `verify-promise-iterators` **34/34**, `make bench`
  and `make diff-strings` (`50,509/50,580`), `fuzz_parser` (400) &
  `fuzz_pipeline` (400) **0** findings, targeted + 150 random `asan-deep` runs
  **0** failures.
- **Still open / boundaries:**
  - Across separate `<script>` elements, an earlier script referencing a `let`
    declared in a later `<script>` sees `"x is not defined"` (still
    `ReferenceError`), and redeclaring the same lexical name in a second
    `<script>` overwrites `m_global_env` rather than throwing a `SyntaxError`.
  - Direct `eval` inside a function (separate `AGENTS.md` row) and `var`
    inside a block shadowing an outer block `let`/`const` early SyntaxError
    remain open.

---
