# Phase 251 — Label activation respects interactive descendants

**Finding 255** — [`docs/findings/255-label-click-forwarded-through-interactive-content.md`](../findings/255-label-click-forwarded-through-interactive-content.md).

## What was wrong

The shell's real pointer path already dispatched a click on a `<label>` through
the DOM activation machinery. `forms::label_control` resolved the actual
association (`for` to an ID, or otherwise the first labelable descendant), and
`after_click` forwarded the click to that control unless it was disabled.

The exception for clicks inside interactive content was too narrow. It reused
`has_activation_behavior`, which answers whether an element has a click default
action. Those are different questions: a `<button type="button">` is
interactive content but has no click default action. Clicking such a button
inside `<label for="check">` therefore looked like a click on label text and
incorrectly forwarded a second click to the checkbox.

## What changed

`forms::is_interactive_content` now classifies the HTML interactive-content
elements, including the attribute-dependent link, media, image-map, and
non-hidden-input cases. The label's activation exception uses that category
while walking from the original event target up to the label. The separate
`has_activation_behavior` check is unchanged, so interactive elements without
a default action do not become activation targets themselves.

The existing `for` and descendant associations, disabled-control refusal, and
`BrowserShell::click_at` path remain in place. The new suite tests through the
shell's real hit test and click dispatch rather than calling a control handler
directly.

## Verification

- `make -j2 test` — **394 engine test suites passed**; the cross-origin suite
  reported **114 checks, 0 failures**.
- `python3 tools/ci/check_suites.py` — passed; the new file is one `TEST` in
  its own suite file.
- The new test failed before the fix on the `button type="button"` case and
  passed after it.

## What remains open

Holdout-01 H4 is not a label-activation oracle. Its pinned TodoMVC template
creates `<input class="toggle" type="checkbox">` next to
`<label>{{title}}</label>`; the label has neither a `for` association nor a
labelable descendant. HTML gives it no control to activate. The holdout driver
reports that the requested toggle's hit test lands on the label and refuses
the target mismatch. That hit-target/layout symptom remains separate; this
phase neither invents a label association nor changes the driver.
