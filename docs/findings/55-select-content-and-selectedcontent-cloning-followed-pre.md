# 55. ✅ `<select>` content and `<selectedcontent>` cloning followed pre-2025 rules (Phase 49)

> Moved out of `AUDIT.md` — written there as a `## 55. ✅ `<select>` content and `<selectedcontent>` cloning followed pre-2025 rules (Phase 49)` section.
> The text below is the section as it stood, unchanged.


**The hole.** Litebird parsed `<select>` using the pre-2025 HTML rules:
`"select"` was omitted from default scope boundaries (`has_element_in_select_scope`
walked only `option`/`optgroup`), `Mode::InSelect` character/element paths
skipped `reconstruct_formatting_elements()`, `<keygen>` and `<textarea>` broke out
of `<select>`, `stop_parsing()` cleared nothing (so an unclosed trailing
`<option>` never fired pop-time hooks), and `<selectedcontent>` cloning did not
exist. That left 11 cases failing in the html5lib tree-construction corpus
(`webkit02.dat` 43/49, `tests1.dat` 110/112, `tests_innerHTML_1.dat` 79/81,
`tests7.dat` 33/34 — `1781/1792`, 99.4%).

**The fix.** `include/litebird/html/HtmlTreeBuilder.hpp`,
`src/html/HtmlTreeBuilder.cpp`, and `src/html/HtmlTreeBuilderModes.cpp` update
the tree builder to the current WHATWG HTML `<select>` rules:
- `"select"` is indexed in `m_scope_boundary_indices[0]` as a default scope
  boundary, keeping `has_element_in_scope("select")` $O(1)$ and forwarding
  `has_element_in_select_scope` to it.
- `Mode::InSelect` reconstructs active formatting elements before character
  tokens (except `U+0000`) and before `<option>`, `<optgroup>`, and `<hr>`, and
  only breaks out of `<select>` on `<input>` (leaving `<keygen>` and `<textarea>`
  inside `<select>` via `send_to_in_body`).
- `stop_parsing()` pops all remaining open elements via
  `resize_open_elements(0)` so EOF-open `<option>` elements trigger
  `pop_current_node()`.
- Iterative DOM cloning (`clone_node_tree`) populates the nearest-ancestor
  `<select>`'s first `<selectedcontent>` (outside nested `<select>`/`<datalist>`)
  when an `<option>` closes (only once popped, ignoring disabled `<option>` /
  disabled ancestor `<optgroup>`, respecting `multiple`, `size > 1`, and
  `select_none`) or when `<selectedcontent>` itself closes after earlier
  `<option>`s, with `m_saw_selectedcontent` avoiding tree walks when absent and
  `m_script_ran` triggering a full DOM rescan when inline scripts mutate the
  `<select>`. Per-`<select>` `SelectState` is erased when `<select>` pops.

**Verification.** `make conformance` **1792/1792 (100.0%)** across all 57
html5lib `.dat` files (`webkit02.dat` 49/49, `tests1.dat` 112/112,
`tests_innerHTML_1.dat` 81/81, `tests7.dat` 34/34); CI conformance floor
raised `1781 -> 1792`. `make test` **181/181** suites (adding
`customizable_select_scope_and_formatting_rules` and
`select_selectedcontent_cloning_and_edge_cases`). `make js-conformance`
**926/940**, `make dom-conformance` **644/644**, `make url-conformance`
**1188/1188**, `make wpt` **7968/8009** across 103 files (`41` listed, `0`
unlisted), `make diff-trees` **6000/6000** (`0` differ vs corrected `parse5
8.1.1`; `--with-select --pages 1500` gives `1368/1500` equal and `132` / `49`
reduced patterns all matching current HTML `<select>` element retention / scope
boundary vs `parse5`'s legacy drops), `make verify-media` **35/35**,
`make fuzz-media` **1800/1800**, `make diff-strings` **50485/50580** (`95`
known Unicode diffs), `make deep` **1332/1332** runs at 200k depth (0 failed),
`make tsan` green, and ASan+UBSan (`build-asan`) green on all 181 unit suites,
tree-construction (`1792/1792`), DOM (`644/644`), URL (`1188/1188`), JS
(`924/940`), WPT (`7968/8009` on merged CI/Release floor), `fuzz_parser` (400),
`fuzz_pipeline` (400), `fuzz_xml` (600), `verify-media` (35), `fuzz-media`
(1800), and `asan-deep` (882 runs at 10k depth).

**Still open.** Script-driven dynamic `<option>`/`<selectedcontent>` updates
*after* parsing finishes (live DOM mutations via `el.selected = true` or
inserting/removing `<option>` children via JS) are not coupled to `<selectedcontent>`
re-cloning; only parse-time and post-parse-script-rescan `<selectedcontent>`
sync are implemented here.

---
