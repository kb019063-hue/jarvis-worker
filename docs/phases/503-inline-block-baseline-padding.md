# Phase 503 — an inline-block's baseline follows its top padding

## What was wrong

An inline-block in a text line contributes the baseline of its last in-flow
line box; if it has no in-flow line box, CSS 2.1 §10.8.1 puts its baseline at
the bottom margin edge. `layout_inline_formatting_context()` instead chose the
line height from the tallest fragment and vertically centered every baseline
fragment in it. When the inline-block's own `padding-top` made it taller, that
centered position did not use the inline-block's moved content baseline. In a
20px line with a 20px inline-block content line and 10px top padding, the
surrounding text's baseline sat 5px above the inline-block's text baseline.

## What changed

- Derive a text fragment's baseline from the same font-face metrics used by
  `FontRenderer::draw_text()`.
- For an inline-block with line boxes, use its last line box's baseline,
  including its top margin, border and padding. For one without line boxes, use
  its bottom margin edge.
- Compute the line's baseline ascent and descent before placement. Baseline
  fragments are then positioned against that final baseline, so padding that
  grows the line also moves the surrounding text baseline instead of retaining
  the old centered sink. `vertical-align: top`, `bottom`, `middle`, and the
  other non-baseline values keep their existing placement path.
- Add `tests/suites/503-inline-block-baseline-padding.cpp`, which checks that
  text before a 10px-top-padded inline-block and text inside it share a
  baseline, and that the resulting line is 30px tall.
- Close the specific open inline-block baseline item in `AGENTS.md`; the
  all-inline-block tabs' vertical-stacking sizing defect remains separate.

## Verification

- `make test`: all 536 engine suites pass, followed by the cross-origin checks.
- `make render-conformance`: 66/67 reftests pass (the sole known failure is
  the AVIF image decoder) and 41/41 body-origin cases pass; the baseline fix
  introduced no reference-pixel failure.
- `make -j2 all` builds the engine.

## Still open

The separately recorded all-inline-block tabs vertical-stacking sizing defect
is not part of this baseline fix. Other inline-formatting work, including full
CSS vertical-align behavior beyond this baseline case, remains outside scope.
