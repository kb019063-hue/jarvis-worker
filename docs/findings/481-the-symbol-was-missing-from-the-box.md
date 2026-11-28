# Finding 481: the symbol was missing from the box

A `<use>` pointing to a `<symbol>` drew the symbol but `getBBox()` answered
zeros. Phase 481 walks the instance and maps its box through the symbol's
viewBox and preserveAspectRatio; the
[phase](../phases/481-a-symbol-instance-has-a-box.md) records Chromium
comparisons and the mutation test. Referenced SVG viewports and external
references remain open, as do precise transformed curve boxes, text and stroke.
