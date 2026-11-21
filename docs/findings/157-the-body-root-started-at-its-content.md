# Finding 157 — the body root started at its content

**Partly resolved by Phase 152** (`docs/phases/152-body-margin-moves-the-page.md`).

## 1. What was found

The layout tree for a document skips `<html>` and starts with `<body>`. The root
content origin was set to `(0,0)`, even though layout had already accounted for
the body's margins in its width. At an 800px viewport the default body was
784px wide but its first block began at `(0,0)` instead of `(8,8)`; a 40px
margin made it 720px wide and still did not move the block. Padding and border
could push the body's own border box into negative coordinates.

## 2. What was done

After layout, place that document body from its used edges and translate its
in-flow descendants and line fragments. Normalize auto edges before width
calculation and resolve horizontal auto margins afterward. Resolve out-of-flow
positions after translation, so a fixed descendant remains viewport-relative and
an absolute descendant of a positioned body uses that body's padding edge.
Nonempty first-child top-margin chains receive a narrow collapsing correction;
this is not general margin collapse.

## 3. Evidence

`make test` passes 315 suites and 114 cross-origin checks on the tree through
#168. `make dom-conformance` is unchanged at 678/678 and `make wpt` at
9567/9824 with 257 listed, none unlisted. `make render-conformance` reports
36/38 original pages matching, 2 known, and **4/7 body-origin pages matching,
3 known**. A fresh `fe06a73` binary fails all four new passing pages. Chromium
153 matches all 29 reference pixel assertions in those seven pages; run
`node tools/conformance/verify_body_pixels.cjs` with the optional packages in
the phase write-up. One intentionally wrong reference is rejected.

## 4. What is still open

`<html>` still has no layout box, so its margin is ignored; empty and sibling
margin collapse is incomplete. The new pixel suite records both. **A visible
regression is also recorded:** before, the corner `(0,0)` of an opaque red
body happened to be red; after moving the body it is white. The underlying
missing body-background propagation to the canvas predates this fix: Chromium
paints the whole viewport red, while Litebird paints only the body's box. Do
not call this phase complete root layout or full CSS conformance.
