# Finding 236 — a checkbox painted its value

Phase 232.

## What was wrong

`BrowserShell::render_png_frame` painted every interactive control with the
same text-field overlay: a white rounded rectangle, a border that thickens on
focus, and `el.value()` as text at 14px. For `<input type="checkbox">` and
`<input type="radio">` with no `value=""` attribute, `FormState::value()`
answers `"on"` (`ValueMode::DefaultOn`, `src/dom/FormState.cpp:832`) — the
HTML standard's default for submission, not a label. The overlay trusted it
for display, so every checkbox and radio rendered as a text field containing
the word `on`. Holdout-01 H4-added (TodoMVC) showed the defect at pixel
scale: a 20×20 toggle box filled with `on` where Chromium shows an empty
native box.

The decision belonged in one place — the intrinsic control painting that
`appearance: auto` implies — but was split: the form state owned the value
correctly, the shell owned the painting incorrectly.

## Why it stayed hidden

The text-field overlay was added to make `<input>`/`<button>` visible in
`--gui` screenshots, where layout alone shows nothing. No suite rendered a
checkbox to PNG and checked for the text; the DOM suite exercised `value`
and `checked` separately and never asserted what the pixels held. The two
truths lived in different layers: `value() == "on"` is true and must stay
true for `entry_list`, while "the box contains `on`" is false.

## What changed

The overlay now branches on `forms::input_type` before painting. `checkbox`
and `radio` draw a bordered intrinsic box (3px radius vs circular) and, when
`forms::checked` is true, a checkmark/dot; they never read `value()`. All
other controls keep the existing text-field rendering. No `FormState` change:
the value stays `"on"` for the form.

## How to avoid it next time

Keep display value and submission value separate: a control's `value`
property is not its label. Any future control type that gains a default value
must be added to the overlay's type switch before it can be painted.
