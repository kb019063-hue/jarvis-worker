# Phase 461 — Positive tabindex comes first

Task: Tab focus ordering; finding 461.

## What was wrong

The shell collected all focusable elements in document order. A page with a
button without `tabindex`, then buttons with `tabindex=10`, `2` and `1`,
visited them in that order instead of visiting the positive values in numeric
order before the default stop. The shell already dispatched focus and blur;
the list it walked was wrong.

## What changed

The scan parses each focusable element's `tabindex` once using the same HTML
integer parser as the `tabIndex` property. A stable sort puts positive values
first, ascending, leaving equal values and the zero/default group in tree
order. The existing legacy click-target candidates are appended afterwards,
unchanged; click hit testing does not use this list. On a layout rebuild the
order is computed again, and the existing document-focus reconciliation
locates a focused element even if its index has changed.

## Verification

- `make test`: 506 engine suites and 114 cross-origin checks pass.
- `tests/suites/461-positive-tabindex-order.cpp` fails its first ring-order
  assertion on the original engine and passes with the change. It checks
  numerical ordering (1, 2, 2, 10), stable ties, zero and missing values,
  invalid and out-of-range values, an HTML integer with whitespace and a
  suffix, legacy candidates, real focus/blur events, reverse navigation and
  changing a value followed by a layout rebuild.

## What remains open

This is not the complete HTML sequential focus navigation algorithm. In
particular, negative `tabindex` values remain in this shell's ring, some
legacy click targets cannot actually receive DOM focus, and visibility,
shadow-tree focus scopes and stale order before a layout rebuild need separate
work. Shift+Tab is handled by the terminal's escape sequence and is not a
`handle_key` key. Other focus changes and pointer hit testing are unchanged.
