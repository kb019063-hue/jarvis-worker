# Phase 423 — Full <html> Root Layout Box

**Finding:** [`docs/findings/423-html-root-had-no-box.md`](../findings/423-html-root-had-no-box.md)

## Problem

`<html>` had no layout box. `LayoutTree::build()` started at `body` and treated `html` margins via a one-off `root_margin` offset in `place_document_body()`. `html` borders, padding, and `border-radius` never affected layout, and `html(30px)+body(8px)` produced `30px` not Chromium's `38px` (addition, not max). The UA sheet also forced `body { background-color: #ffffff }` with a `UserAgentSheet` transparent reset, hiding the spec default `transparent`.

## Change

1. **LayoutTree html wrapper** (`src/layout/LayoutTree.cpp`): when `html` style is not `none`/`contents`, create an `html` `LayoutBox` (block), append the previous `root_box` (usually `body`), normalize, and make it the new `root_box`. Preserve `has_document_canvas` and canvas propagation. Add `has_html_root` handling to call `prepare_document_body`/`place_document_body` on the html root (no extra `root_margin` — html's own dimensions already include it).

2. **Html barrier** (`src/layout/LayoutBox.cpp:blocks_collapse`): `is_html_root` (tag `html`) blocks parent/child margin collapse, so `html` and `body` margins add (30+8=38) per CSS, without affecting other containers. `body` still collapses normally with its children.

3. **UA cleanup** (`include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`): remove `body { background-color: #ffffff }` from `default_user_agent_stylesheet()` and the `UserAgentSheet` `body { background-color: transparent }` override (now empty passthrough). Default body background is transparent.

4. **Tests** (`tests/suites/376-css-vertical-margin-collapse.cpp`, `tests/test_suite.cpp`, `tests/suites/423-html-root-layout-box.cpp`): update 376 suite to locate `body` by tag (root is now `html`), unwrap `html` in legacy `test_suite.cpp` suites, and add `html_root_layout_box_exists_and_offsets_body` checking html exists, html 30+body 8=38, border/padding containment, transparent UA default, and canvas propagation.

## Verification

- `make test` 259 suites passing.
- `make render-conformance`: `tests/render/reftests.json` 66/67 (1 AVIF known), `tests/render/body-origin.json` 41/41.
- `sizeof(LayoutBox) < 400` unchanged.
