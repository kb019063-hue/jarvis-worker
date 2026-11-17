# Phase 37 Architecture: Iterative RegExp VM, Modern CSS Cascade & Units, CSSOM `el.style`, and HTML5 Date/Time Inputs

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 37 Architecture: Iterative RegExp VM, Modern CSS Cascade & Units, CSSOM `el.style`, and HTML5 Date/Time Inputs` section.
> The text below is the section as it stood, unchanged.


Phase 37 closes four cross-subsystem correctness, stack-safety, and real-web
DOM gaps documented in `AUDIT.md` (finding 42):

### 1. Iterative Bytecode RegExp VM (`src/js/BuiltinsRegExp.cpp`)
- **Bytecode ISA & Compiler:** Compiles parsed `RegexNode` ASTs into flat
  `InstOp` bytecode sequences (literals, character classes, anchors, `\b`/`\B`,
  fixed/greedy/non-greedy quantifiers, capture groups, named backreferences,
  lookahead, and right-to-left lookbehind sub-programs).
- **Iterative Teardown & Heap Backtracking:** `~RegexNode()` frees AST graphs
  via an explicit heap worklist (`std::vector`) rather than stack recursion;
  `exec_vm()` runs iteratively against a heap `backtrack_stack` guarded by
  `kMaxMatchSteps = 30,000,000`, while `kMaxCompilerDepth = 1,000` turns
  deeply nested groups into a clean `SyntaxError` without touching the stack.

### 2. Modern CSS Cascade, Selectors L4, Units & Deep-Input Pruning (`StyleSheet.hpp`, `CssParser.*`, `StyleResolver.*`)
- **Parser & AST:** Replaces recursive block parsing with an explicit heap
  frame stack inside `parse_stylesheet_blocks` (`append_qualified_rule_text`
  with capacity reservation), supports `@layer` (block and statement ordering),
  `@supports` feature queries (`and`/`or`/`not`), Media Queries Level 4 ranges
  (`400px <= width <= 1000px`) and `not`/`and`/`or`, child/sibling combinators
  (`>`, `+`, `~`), selector lists inside `:is()`, `:where()`, and `:not()`,
  and `:host`.
- **Cascade, Units & Values:** Adds cascade-layer priority (unlayered normal >
  layered normal; earlier layered `!important` > later layered `!important`),
  `min()`/`max()`/`clamp()`, `dvh`/`dvw`/`vmax`, percentage `border-radius`,
  and iterative `:root`/ancestor inheritance for `rem`/`em` while preserving
  `line_height_number` and `effective_line_height()`.
- **Bounded-Stack & Non-Quadratic Selector Matching:** `match_selector_impl`
  runs on an explicit heap frame stack; `Selector::finalize_metadata()` caches
  `min_ancestors`, `cached_specificity`, and `any_dynamic_specificity`; and
  `StyleResolver` clears per-tree `m_depth_cache` + `m_desc_prefix_cache`
  (DP prefix memoization for `AllDescendant` chains longer than 32 parts),
  reducing depth-200,000 descendant matching from timeout to `0.0–0.1 s`.

### 3. CSSOM `el.style` Shorthand Expansion & Normalization (`src/js/DomStyleBindings.cpp`)
- Expands/recomposes CSS shorthands (`flex`, `overflow`, `inset`, `gap`,
  `borderRadius`, `background`, `transition`, `textDecoration`, `listStyle`,
  `outline`, `margin`, `padding`, `border`, `font`) for inline `el.style`,
  canonicalizes `cssText`/`length`/`item()`/`removeProperty()`/`!important`/
  custom `--*` properties, and maps `-webkit-`/`-moz-`/`-ms-`/`cssFloat` aliases.

### 4. HTML5 Date/Time `<input>` Semantics (`FormState.*`, `src/js/DomFormBindings.cpp`)
- Implements WHATWG sanitization, `valueAsNumber`, `valueAsDate`,
  `stepUp()`/`stepDown()`, and `rangeUnderflow`/`rangeOverflow`/`stepMismatch`
  validity for `date`, `time`, `month`, `week`, and `datetime-local` (plus
  `TypeError`/`InvalidStateError` checks).

### 5. Verification (Merged Tree)
- **Release:** `make test` `174/174`; `make js-conformance` `926/940` (`regexp.dat`
  `39/39`, probe `90/90`, `--reference "node -e"` `940/940`);
  `make dom-conformance` `644/644` (probe `68/68`, `--reference` `644/644`);
  `dates-and-svg.js` matches `jsdom` on `6/6` date checks; `inline-style.js`
  matches on `26/27` lines; `make conformance` `1781/1792`, `make diff-trees`
  `6000/6000`, `make url-conformance` `1188/1188`, `make xml-conformance`
  `1702/1702`, `make diff-xml` `0 DIFFER` (`4000`) & `3000/3000`, `make wpt`
  `5421/5463`, `make deep` `1332/1332` (depth `200,000`, slowest `26.9 s`).
- **ASan+UBSan (`detect_leaks=0`):** `174/174` suites, JS `925/940`, DOM
  `644/644`, URL `1188/1188`, XML `1702/1702`, WPT `5421/5463`, `fuzz` `0`
  findings (`400`+`400`+`1927`/`600`+`1800`), `make asan-deep` `882/882` (slowest
  `3.6 s`), and `make tsan` clean.

---
