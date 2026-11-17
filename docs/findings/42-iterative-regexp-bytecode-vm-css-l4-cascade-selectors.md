# 42. ✅ Iterative RegExp bytecode VM, CSS L4 cascade/selectors/units, CSSOM `el.style` shorthands, and HTML5 date/time inputs

> Moved out of `AUDIT.md` — written there as a `## 42. ✅ Iterative RegExp bytecode VM, CSS L4 cascade/selectors/units, CSSOM `el.style` shorthands, and HTML5 date/time inputs` section.
> The text below is the section as it stood, unchanged.


**What was wrong.**
1. **Repeated-group regex recursion (finding 26):** `BuiltinsRegExp.cpp` used
   recursive CPS `Matcher` lambdas and recursive AST node `~Node()` destructors,
   failing with `RangeError` past ~12,000–21,800 group iterations and risking
   native stack overflow on deeply nested pattern ASTs.
2. **Modern CSS cascade, selectors, units, and deep-input bounds:** `CssParser`,
   `StyleSheet`, and `StyleResolver` lacked child/sibling combinators (`>`,
   `+`, `~`), selector lists (`:is()`, `:where()`, multi-argument `:not()`,
   `:host`), `@layer` statement/block precedence, `@supports` feature checks,
   Media Queries Level 4 ranges (`400px <= width <= 1000px`) and `not`/`and`/
   `or`, math functions (`min()`, `max()`, `clamp()`), dynamic viewport units
   (`dvh`, `dvw`, `vmax`), percentage `border-radius`, and `:root` font-size
   inheritance (`rem`). In addition, `parse_stylesheet_blocks` and
   `match_selector_impl` recursed on nested `{}` blocks and long selector
   chains, and unpruned `AllDescendant` matching took quadratic time when a
   page had a descendant selector whose length matched a 200,000-element chain.
3. **Inline `el.style` CSSOM gap (finding 31 / `inline-style.js`):**
   `DomStyleBindings.cpp` stored shorthands (`flex`, `overflow`, `inset`, `gap`,
   `border-radius`, `background`, `transition`, `text-decoration`, `list-style`,
   `outline`, `margin`, `padding`, `border`, `font`) unexpanded, omitted
   canonical `cssText`/`length`/`item()`/`removeProperty()`/`setProperty(..., 'important')`
   and `--*` custom-property normalization, and ignored vendor aliases
   (`-webkit-*`, `-moz-*`, `-ms-*`, `cssFloat`).
4. **HTML5 date/time `<input>` behavior (finding 31 / `dates-and-svg.js`):**
   `date`, `time`, `month`, `week`, and `datetime-local` inputs behaved like
   plain text inputs without calendar/leap-year/ISO-week sanitization,
   `valueAsDate`, `valueAsNumber`, `stepUp()`/`stepDown()`, or
   `min`/`max`/`step` constraint validation.

**The fixes.**
- **RegExp VM (`src/js/BuiltinsRegExp.cpp`):** Replaced recursive CPS matchers
  with a bytecode compiler (`InstOp` opcode stream covering literals, character
  classes, anchors, word boundaries, quantifiers, lazy/greedy captures, named
  backreferences, lookahead/lookbehind, and assertions) and an iterative heap
  backtracking VM (`exec_vm`) bounded by `kMaxMatchSteps = 30,000,000`. AST
  teardown uses an iterative free-worklist (`~RegexNode()`), and parser nesting
  past `kMaxCompilerDepth = 1,000` throws `SyntaxError` without touching the
  stack. Lookbehind continues to match right-to-left via sub-VMs.
- **CSS Parser, Cascade & Selectors (`StyleSheet.hpp`, `CssParser.*`,
  `StyleResolver.*`):**
  - Converted `parse_stylesheet_blocks` to an explicit heap frame stack
    preserving `@layer`/`@media`/`@supports` state and iterative
    rule-text emission (`append_qualified_rule_text` with `reserve`), keeping
    depth-200,000 nested `{}` stylesheets under ~10 MB RSS and `< 0.3 s`.
  - Added `CombinatorKind` (`None`, `Child`, `AdjacentSibling`, `GeneralSibling`),
    `:is()` / `:where()` / selector-list `:not()` / `:host`, `@layer` ordering
    (unlayered normal beats layered normal; earlier layered `!important` beats
    later layered `!important`), `@supports` condition evaluation, MQ L4 range
    syntax + `not`/`and`/`or`, `min()`/`max()`/`clamp()`, `dvh`/`dvw`/`vmax`,
    `border_radius_percent`, and iterative ancestor inheritance in
    `resolve_style` (while keeping `line_height_number` and
    `effective_line_height()`).
  - Replaced recursive `match_selector_impl` with an explicit heap frame stack
    and added `Selector::finalize_metadata()` (`min_ancestors`,
    `cached_specificity`, `any_dynamic_specificity`) plus `StyleResolver`'s
    per-tree `m_depth_cache` and `m_desc_prefix_cache` (`n > 32` `AllDescendant`
    DP memoization), turning depth-200,000 `descendant selector as long as
    the nest` from a 60s timeout into `0.0–0.1 s` (`5–14 MB`).
- **CSSOM `el.style` (`src/js/DomStyleBindings.cpp`):** Implemented full
  shorthand expansion/recomposition, CSSOM serialization/value normalization,
  `-webkit-`/`-moz-`/`-ms-`/`cssFloat` aliases, `length`/`item()`,
  `removeProperty`, `!important` priority, and custom-property (`--*`)
  handling.
- **Date/time `<input>` (`include/litebird/dom/FormState.hpp`,
  `src/dom/FormState.cpp`, `src/js/DomFormBindings.cpp`):** Added WHATWG input
  sanitization (civil calendar leap-year checks, ISO 8601 week counts,
  `datetime-local` space-to-`T` normalization), `valueAsNumber`, `valueAsDate`,
  `stepUp()`/`stepDown()` with millisecond-accurate stepping, and
  `rangeUnderflow`/`rangeOverflow`/`stepMismatch` constraint validation plus
  spec-compliant `TypeError`/`InvalidStateError` exceptions.

**How verified (on the merged tree).** Every line was run on the merged tree
with zero edits to `everyday*.dat` and with CI floors raised (`925 -> 926`,
`640 -> 644`, ASan JS `923 -> 924`, ASan DOM `640 -> 644`):
- `make test`: `All 174 engine test suites passed successfully!` (includes four
  new suites: `regexp_vm_handles_deep_nesting_and_long_inputs`,
  `css_cascade_layers_supports_combinators_and_mq_l4`,
  `inline_style_cssom_shorthands_and_aliases`,
  `html5_date_time_inputs_validation_and_stepping`).
- `make js-conformance`: `926/940 (98.5%)`, `regexp.dat` `39/39 (100.0%)`,
  everyday probe `90/90`, `0` crashes, `0` timeouts; `--reference "node -e"`
  `940/940 (100.0%)`.
- `make dom-conformance`: `644/644 (100.0%)`, everyday probe `68/68`,
  `--reference` `644/644 (100.0%)` (new cases in `forms.dat` and `style.dat`).
- Out-of-corpus jsdom comparisons (`tools/conformance/compare_with_jsdom.py`):
  `tests/dom-conformance/open/dates-and-svg.js` matches jsdom on `6/6` date
  lines (the remaining line is SVG DOM);
  `tests/dom-conformance/open/inline-style.js` matches on `26/27` lines (the
  single difference is `font-family: "Helvetica Neue", Arial;` retaining quotes
  per CSSOM/Chromium where jsdom's `cssstyle` strips them).
- Existing scoreboards unchanged: `make conformance` `1781/1792`,
  `make diff-trees` `6000/6000`, `make url-conformance` `1188/1188`,
  `make xml-conformance` `1702/1702`, `make diff-xml` `4000` docs (`0` differ) &
  `3000/3000` round trips, `make wpt` `5421/5463` (`0` unlisted),
  `make diff-strings` `50485/50580` (`95` differ), `make bench` (42),
  `make bench-css` (20), `make bench-attributes` (6),
  `make bench-tokenizer-attributes` (7), `make bench-surrogates` (3),
  `make security-fetch` (3), `make security-worker` (17),
  `make verify-media` (35), `make fuzz-media` (1,800), `make tsan`, and
  `make deep` (`1332` runs over `465` pages at depth `200000`, `0` failed,
  slowest `26.9 s`).
- Sanitizers (ASan+UBSan, `detect_leaks=0`): `174/174` unit suites,
  JS `925/940` (probe `90/90`, `0` crashes/timeouts), DOM `644/644`,
  URL `1188/1188`, XML `1702/1702`, WPT `5421/5463`, `diff_xml` `1,000` docs &
  `1,000` round trips, `fuzz` (`400` parser + `400` pipeline + `1927`/`600` XML +
  `1800` media, `0` findings), and `make asan-deep` (`882` runs over `315` pages
  at depth `10000`, `0` failed, slowest `3.6 s`).

**What is still open.**
- RegExp: catastrophic backtracking still terminates via `kMaxMatchSteps`
  (`30,000,000`), patterns nested >`1,000` levels raise `SyntaxError`, and
  flags `u`/`v`/`d` completeness remains under Wave 2.
- CSS: `AllGeneralSibling` (`~` in selectors longer than 32 parts at extreme
  depth) does not share `m_desc_prefix_cache`'s DP memoization; `:has()`,
  `:focus-visible`, `@container`, CSS nesting, and `::part` are still open.
- DOM: `getComputedStyle(el).width` layout metrics, `sheet.cssRules`,
  `document.write`, and the SVG DOM (`className.baseVal`, `viewBox`,
  `getBBox()`) remain open.

---
