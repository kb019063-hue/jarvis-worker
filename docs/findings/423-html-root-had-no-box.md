# Finding 423 — Html Root Had No Box

**Phase:** [`docs/phases/423-html-root-layout-box.md`](../phases/423-html-root-layout-box.md)

## What was wrong

`<html>` was not a layout box. `LayoutTree` built from `body`, and `LayoutBox::layout_block()` used `!parent` as body root. `html` margins were applied only as a viewport offset in `place_document_body(root_margin)`, so `html` borders/padding did nothing and `html(30)+body(8)` collapsed via max instead of adding.

## Root cause

No html box existed to own dimensions, so html geometry never entered block layout or margin collapse. The `blocks_collapse` predicate had no html special case, and UA left body opaque white.

## Fix and proof

Html now owns a real block box with barrier semantics; body is its child. UA body white removed. `html_root_layout_box_exists_and_offsets_body` verifies html at 30,30 and target at 38,38 for html 30+body 8, plus border/padding, transparent default, and canvas propagation. All 41/41 body-origin and 66/67 reftests preserved.
