# 230. Font-face rules disappeared from the CSSOM

## What was wrong

`@font-face` was accepted by the renderer's CSS parser but skipped by the
separate CSSOM parser. Script therefore saw no `CSSFontFaceRule` and could not
read its descriptors or insert one, and later rules appeared at the wrong
indices.

## What changed

Phase 226 retains the blocks as type-5 CSS rules with stable live style
objects, supports insertion and deletion, and preserves them inside media
rules. It also keys rule wrappers by sheet and rule identity, not a number
that another sheet can reuse. This is script-facing only and does not connect
CSSOM edits to font loading.

## Evidence

`python3 tools/conformance/verify_css_font_face.py` compares the same page
script to pinned Chromium. The original 11 probes matched 1 before and 11
after; the expanded set matches 25/25. `make test` runs a separate engine
suite including an ordinary-style cascade control. Browser setup is recorded
in [Phase 226](../phases/226-font-face-rules-in-the-cssom.md).

## Still open

The full descriptor grammar and escaping are not implemented, and changes to
a CSSOM rule do not feed the rendering/font-loading pipeline. Detached-rule
parent links and the CSSOM's flattening of some grouping rules remain separate
limitations; the phase write-up gives the boundaries.
