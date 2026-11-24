# Finding 247 — the rectangle was right about the wrong page

**Severity:** correctness, and a class error rather than a one-off. Two cascades
disagreed: the one the layout paints from and the one `getComputedStyle` reads.
They were built in different files by different code, so nothing kept them
honest, and the script-facing one had never handled `<link rel="stylesheet">` at
all.

**Found by:** refusing to accept the row's premise. AGENTS.md L942 said the
geometry a script reads disagrees with the painted layout, which sounds like a
stale-cache problem in the DOMRect path. The value the holdout reported
(2061.4) did not reproduce — driving the same pinned page, the same click and
the same `ArrowRight` gave `673.5`. A number that does not reproduce is a
statement about the measurement, not about the engine, so the measurement was
taken apart instead of patched around.

## What had actually gone wrong

`getComputedStyle` reads a cascade built in `src/js/DomStyleBindings.cpp`. The
layout reads a different one, built by `DocumentStyles::build_resolver`. The
first collected `<style>` elements only:

```cpp
if (is_html_element(node, "style")) { ... part.style_elements.push_back(...); }
```

so a `<link rel=stylesheet>` was never in it — and `sheet_of` read
`element.text_content()`, which for a `<link>` is the empty string. A page whose
only stylesheet was a link had **zero** sheets in the script-facing cascade and
all of them in the painted one.

That is why the holdout looked the way it did. `[role="tabpanel"].is-hidden
{ display: none }` lives in `css/tabs.css`, reached through a link. The element
matched the selector; the rule was never in the resolver; so:

```
panel2.matches('[role="tabpanel"].is-hidden')  -> true
getComputedStyle(panel2).display               -> block
```

Both panels stayed `display: block`, the tabs stacked, and the biography sat at
y≈673 in a 600px viewport. The recorded complaint — "the JS rectangle reports
its top at y≈2061 in a 600px viewport" — was a true report of a page this
browser had laid out from a rulebook it would not admit to.

## Why the geometry was innocent

The row's own diagnostics were run and rejected:

- **Not a stale box.** Hiding a 100px div moves the element below it 208 → 108,
  and restoring moves it back. `set_attribute` does call
  `mark_needs_layout(); bump_tree_version();` (`src/dom/Node.cpp:704`) and
  `LayoutTree::build` clears stale boxes (`LayoutTree.cpp:915-919`).
- **Not a `tree_version` short-circuit.** `Document::ensure_layout` checks it,
  and it is bumped on every inline-style write.
- **Not a scroll offset divide.** All four `set_scroll_offset` sites in
  `BrowserShell` and the painter's `set_scroll` come from one `m_scroll_y`.

Then the direct comparison, on the real pinned page after the click and the
arrow key. Rect against the layout box the painter used:

| element | rect | layout box | Δy |
| --- | --- | --- | --- |
| `#tab-1` | 8.0, 498.5, 180.5 × 41.0 | 8.0, 498.5, 180.5 × 41.0 | 0 |
| `#tab-2` | 8.0, 539.5, 180.5 × 46.0 | 8.0, 539.5, 180.5 × 46.0 | 0 |
| `#tablist-1` | 8.0, 460.0, 784.0 × 22.5 | 8.0, 460.0, 784.0 × 22.5 | 0 |
| `#tabpanel-2` | 8.0, 673.5, 798.0 × 174.0 | 8.0, 673.5, 798.0 × 174.0 | 0 |

Zero on every row. The rect path was never the liar; the style path was.

## The fix, and why it is in one place

`DocumentStyles::load_linked_stylesheet` loads a `<link>`'s sheet through the
same `load_css_resource`, the same `@import` expansion, the same charset
handling and the same `StyleSheetCache` the layout uses. `sheet_of` calls it
instead of reading `text_content()`, and `style_elements_of` now collects
`<link rel=stylesheet>` next to `<style>`.

The alternative — a second loader written for the script path — is how this
finding happened the first time. A loader that agrees with the other one by
construction cannot drift from it.

## What is still wrong, and is not this row

The holdout's H2 gate still fails. It asserts `visible('#tabpanel-2 p')`, i.e.
`top < 600`; the paragraph reads **680.5**. That is now a layout fact, not a
reporting one: the rect equals the painted box exactly, and the box is 80px
below the fold because everything above it — a 3873px document with its tablist
at y≈460, its three tabs stacked vertically at 498/539/589 — takes that much
room. Two separable defects are in front of that gate:

1. `display: inline-block` tabs lay out as blocks, one per line, instead of in a
   row.
2. `#tablist-1` reports `784 × 22.5` while its children stack to 181px, so the
   container is not being sized to its content on the flex/`min-width: 100%`
   path.

Either is a better claim than this row was, and both are now visible without
the linked-stylesheet bug standing in front of them. The gate is left failing
and the assertion is left in place: it is catching something real, just not what
the README imputed to it.

## Honest scope

This phase does **not** turn a holdout row green, and does not claim to. What it
turns is a page's CSS from invisible-to-script into visible-to-script, proven on
the smallest possible page (a linked sheet and an inline sheet answering the
same question the same way) and mutation-tested by reverting the fix.
