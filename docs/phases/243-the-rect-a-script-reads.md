# Phase 243 — the rect a script reads and the box that was painted

The AGENTS.md row (L942) said the geometry a script reads disagrees with the
painted layout, and pointed at the DOMRect path: `getBoundingClientRect`, the
cached `Element::m_bounding_box`, and the `ensure_layout` version check that
might be handing back a stale box.

**That premise was wrong, and the phase's job was to establish that rather than
to build a fix on it.** The rect path is not stale, and the rect is not
disagreeing with the painted box. Measured on the holdout page that produced the
report, every element's rect and its layout box agree to the last decimal:

| element | `getBoundingClientRect()` | painted layout box | Δy |
| --- | --- | --- | --- |
| `#tab-1` | 8.0, 498.5, 180.5 × 41.0 | 8.0, 498.5, 180.5 × 41.0 | **0** |
| `#tab-2` | 8.0, 539.5, 180.5 × 46.0 | 8.0, 539.5, 180.5 × 46.0 | **0** |
| `#tablist-1` | 8.0, 460.0, 784.0 × 22.5 | 8.0, 460.0, 784.0 × 22.5 | **0** |
| `#tabpanel-2` | 8.0, 673.5, 798.0 × 174.0 | 8.0, 673.5, 798.0 × 174.0 | **0** |

The rectangle was telling the truth about a page that was laid out wrongly. The
fault was upstream of geometry, in **which stylesheet the browser believed the
page had**.

## The defect

`getComputedStyle` reads a cascade of its own, built in
`src/js/DomStyleBindings.cpp` (`style_elements_of` → `sheet_of` →
`compute_block`), separate from the cascade the layout paints from
(`DocumentStyles::build_resolver`). The script-facing one collected `<style>`
elements and nothing else:

```cpp
if (is_html_element(node, "style")) {
    auto& element = static_cast<Element&>(node);
    ...
    part.style_elements.push_back(shared(element));
}
```

A `<link rel="stylesheet">` therefore contributed no sheet at all. And even if it
had been collected, `sheet_of` reads `element.text_content()` — which is always
empty for a `<link>`. The page's only stylesheet was invisible to script, so
every rule in it was missing from what `getComputedStyle` answered, while the
layout painted all of them.

The result is the signature the holdout recorded: an element that **matches** a
rule and reports the wrong value for it.

```
[role="tabpanel"].is-hidden { display: none }   /* the rule */
panel2.matches('[role="tabpanel"].is-hidden') -> true
getComputedStyle(panel2).display              -> block   /* before this phase */
```

The smallest statement of it needs two files and six lines:

```html
<!-- linked.html -->
<link rel="stylesheet" href="page.css">
<div id="box">box</div>
```
```css
/* page.css */
#box { display: none; height: 123px; }
```
```
getComputedStyle(#box).display -> block     (CSS says none)
getComputedStyle(#box).height  -> auto      (CSS says 123px)
```

The same rules in an inline `<style>` were answered correctly, which is what
made the bug survive so long: every test anyone had written used an inline
sheet.

## What changed

**`DocumentStyles::load_linked_stylesheet`** (`src/css/DocumentStyles.cpp`,
declared in the header) loads the sheet a `<link>` points at through the *same*
`load_css_resource`, `@import` expansion, charset handling and `StyleSheetCache`
the layout already uses. Reusing the layout's loader rather than writing a
second one is the point: two loaders are free to disagree, and disagreeing
loaders are exactly this bug.

**`DocumentStyles::is_stylesheet_rel`** is now public so both cascades ask the
same question about `rel` — `stylesheet` present, `alternate` absent.

**`sheet_of` / `sheet_text_of`** (`src/js/DomStyleBindings.cpp`) read a
`<link>`'s href instead of its (empty) text. Both the first parse and the
re-parse path use it, so a linked sheet is not wiped when the tree version
changes.

**`style_elements_of`** now collects `<link rel=stylesheet>` alongside `<style>`
elements, so the script-facing cascade sees the linked sheet. A `media`
containing `print` is still skipped, as before.

The rect path, `Element::m_bounding_box`, `set_attribute`'s
`mark_needs_layout(); bump_tree_version();` and
`LayoutTree::build`'s stale-box clearing were read closely and **left alone**.
They were correct: hiding a 100px div moves a lower element 208 → 108 and back,
with `scrollY` 0, and `tree_version` is bumped on every inline-style write.

## What this fixed on the holdout

The holdout's H2 task (APG automatic tabs, ArrowRight) now hides and shows the
panels correctly. Before: both panels computed `display: block` and the page's
tabs stacked vertically, because `[role="tablist"] { min-width: 100% }` and the
flex/`inline-block` rules in the linked sheet applied only to the painter:

```
panel-1: block -> none      (was block -> block)
panel-2: none  -> block     (was block -> block)
```

**What it did not fix, stated plainly:** the holdout's automated gate still
fails. It asserts `visible('#tabpanel-2 p')`, which requires the paragraph's
`top` to be under 600 in the 600px viewport. It reads **680.5**. That is not a
geometry defect any more — the rect agrees with the painted box to the decimal,
and always did. The paragraph is below the fold because the page above it is
~673px tall: a 3873px document whose tablist sits at y≈460, with the three tabs
laid out **vertically** (498, 539, 589) rather than in a row. Two things remain
in front of that gate, and neither is this phase's row:

1. `display: inline-block` tabs are being laid out as blocks on their own lines.
2. The tablist reports `784 × 22.5` from `getBoundingClientRect` while its three
   children stack to `181px`, so something in the flex/`min-width: 100%` path is
   not sizing the container to its content.

The phase doc says "the gate still fails" because it does. Claiming otherwise
would be the sort of overstated result this repository's rows exist to prevent.

## Verification

- `tests/suites/243-a-linked-stylesheet-reaches-the-script.cpp`: a linked sheet
  and an inline sheet holding the same rules must answer the same
  `getComputedStyle`, and the answer must match the layout (`display: none` box
  reserves no room). Reverting the fix makes it fail on
  `linked.display == "none"`; with the fix it passes.
- Whole suite: **384/384** (383 before this phase).
- Holdout H2 driven end to end: panel visibility now switches correctly;
  rect-vs-layout-box Δ is 0 on every element that has a box.
