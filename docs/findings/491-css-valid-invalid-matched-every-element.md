# Finding 491: the layout cascade ignored CSS `:valid` and `:invalid`

The real-pages image gate showed **476,809 / 480,000** differing pixels for the
Base64 page, **477,445** for Binary ASCII, **466,016** for UTF-8 and **465,800**
for HTML entities. The Litebird frames had a page-wide pink background where
Chromium had white content and a gray canvas. The interactions were not the
problem: all four tools returned the expected encoded output.

The pinned Mothereff stylesheet names `.invalid, :invalid` and paints that rule
pink. `StyleResolver::matches_simple_selector` was not the DOM selector path:
its dynamic pseudo-class switch covered states such as `:hover`, `:focus`,
`:enabled` and `:checked`, but omitted `:valid` and `:invalid`. The loop treated
an unrecognized pseudo-class as satisfied, so the stylesheet's invalid-state
rule matched ordinary elements as well as controls. The separate selector path
used by `Element.matches()` and script-facing computed styles did not have this
bug. Testing only `getComputedStyle()` therefore gave a false green for the
visible paint defect.

The production-paint regression test exercises `BrowserShell::render_png_frame`
and decodes the actual PNG. It failed before the fix on the white pixel inside a
plain `div`; it passes after adding the two state checks. This is deliberately
not an exact-page test: it isolates the rule's scope without asserting that all
other page layout is correct.

After the fix, each of the four affected pages lost about 131–135 thousand
changed pixels (532,856 fewer total), while all twenty real-page interactions
and all Chromium reference checks still pass. However, the full strict result
remains **0/19 exact**. Controls and overall dimensions/spacing remain visibly
different; no tolerance or alignment was added to the oracle. See
[Phase 491](../phases/491-css-valid-invalid-match-controls.md) and the exact
counts in `tools/real_pages/rerun-08/strict-frames.json`.
