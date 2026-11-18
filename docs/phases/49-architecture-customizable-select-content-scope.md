# Phase 49 Architecture: Customizable `<select>` Content, Scope Boundaries, and `<selectedcontent>` Cloning

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 49 Architecture: Customizable `<select>` Content, Scope Boundaries, and `<selectedcontent>` Cloning` section.
> The text below is the section as it stood, unchanged.


### 1. The gap

Litebird previously parsed `<select>` using pre-2025 HTML rules: `"select"` was
not a default scope boundary (so `has_element_in_select_scope()` scanned up to
`option`/`optgroup` instead of forwarding to `has_element_in_scope`), `Mode::InSelect`
did not call `reconstruct_formatting_elements()`, `<keygen>` and `<textarea>`
broke out of `<select>`, `stop_parsing()` left trailing open tags on the stack,
and `<selectedcontent>` cloning was absent. Those gaps caused the remaining 11
failures in the 1,792-case html5lib tree-construction corpus (`webkit02.dat`,
`tests1.dat`, `tests_innerHTML_1.dat`, `tests7.dat`).

### 2. Parser changes (`include/litebird/html/HtmlTreeBuilder.hpp`, `src/html/{HtmlTreeBuilder,HtmlTreeBuilderModes}.cpp`)

- **Default scope boundary:** `"select"` is indexed in
  `m_scope_boundary_indices[0]`, keeping `has_element_in_scope("select")` $O(1)$
  and making `has_element_in_select_scope` delegate directly to
  `has_element_in_scope`. Outer closing tags (`</font>`, `</p>`) cannot escape
  or foster-close across an open `<select>`, and `<select>`/`</select>` inside an
  open scope boundary inside `<select>` (such as `<select><div>` vs `<select><table>`)
  follows §13.2.4.2.
- **Customizable `Mode::InSelect`:** `process_character()` forwards non-NULL
  characters inside `Mode::InSelect` to `process_in_select()`, which calls
  `reconstruct_formatting_elements()` before character tokens and before
  `<option>`, `<optgroup>`, and `<hr>` start tags. Only `<input>` breaks out of
  `<select>`, while `<keygen>`, `<textarea>`, and arbitrary elements stay inside
  `<select>` via `send_to_in_body()`.
- **EOF cleanup & `<selectedcontent>` cloning:** `stop_parsing()` clears the open
  element stack via `resize_open_elements(0)` so unclosed trailing `<option>`
  elements run `pop_current_node()` cleanly. `m_saw_selectedcontent` avoids any
  bookkeeping when a page contains no `<selectedcontent>`, and `m_select_states`
  (erased when `<select>` pops) tracks the first `<selectedcontent>` and selection
  state per open `<select>`.
  - `clone_node_tree()` performs iterative deep cloning (`Element` namespaces,
    prefixes, attributes and attribute namespaces, `is_value`, `FormControlState`,
    `template_contents`, `Text`/CDATA, `Comment`, `ProcessingInstruction`,
    `DocumentFragment`, `DocumentType`) so arbitrary nesting never overflows the
    native stack.
  - Both `update_selectedcontent_on_option_pop()` and
    `sync_selectedcontent_on_pop()` ignore still-open `<option>`s
    (`open_element_position(opt) >= 0`), ignore `<option>` / `<selectedcontent>`
    nested inside another `<select>` or `<datalist>`, honor `multiple`, `size > 1`,
    `select_none`, disabled `<option>`, and disabled ancestor `<optgroup>`s up to
    the owning `<select>`, and fall back to a full `<select>` subtree rescan when
    `m_script_ran` is true (in case inline scripts mutated `<select>` during
    parsing).

### 3. Verification

- `make conformance`: **1792/1792 (100.0%)** across all 57 html5lib `.dat` files;
  CI conformance floor raised `1781 → 1792`.
- `make test`: **181/181** suites (adding
  `customizable_select_scope_and_formatting_rules` and
  `select_selectedcontent_cloning_and_edge_cases`).
- `make diff-trees`: **6,000/6,000** (`0` differ vs corrected `parse5 8.1.1`);
  with `--with-select --pages 1500`: `1,368/1,500` equal and `132` diffs (`49`
  reduced patterns, all due to Litebird retaining elements/scope boundaries inside
  `<select>` per current WHATWG HTML while `parse5 8.1.1` keeps legacy drops).

---
