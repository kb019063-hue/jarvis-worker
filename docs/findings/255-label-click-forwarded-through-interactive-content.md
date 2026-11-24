# 255. A label forwarded clicks through interactive content without a default action

The HTML label activation behavior forwards a click to a label's associated
control unless the event originated in interactive content inside the label,
or the associated control is disabled. Association is not proximity: the
control comes from the label's `for` value or, without `for`, its first
labelable descendant.

## Reproduction

A `<label for="check">` associated with a checkbox also contained
`<button type="button">Help</button>`. A real `BrowserShell::click_at` at the
button's center dispatched to the button, then incorrectly toggled `check`.
The button has no click default action, so `has_activation_behavior` returned
false for it. `after_click` used that predicate as the label's interactive-
descendant exception and treated the button click as a click on label text.

The inverse cases were already correct: clicks on an associated label reached
the control, a disabled control did not activate, and clicking the labeled
control itself did not cause a second activation. The broken distinction was
between *interactive content* and *content with a click activation behavior*.

## Fix

`forms::is_interactive_content` in `src/dom/FormState.cpp` now identifies the
HTML interactive-content category separately. `after_click` uses it while
walking from the original event target to the label; `has_activation_behavior`
still determines which element gets click activation. Thus a type=button
blocks label forwarding without acquiring a default click action of its own.

## Verification

`tests/suites/251-label-click-activates-control.cpp` exercises real shell clicks
for an explicit `for` association targeting the right control, an implicit
descendant association, direct control activation without a double toggle,
the type=button exception, disabled controls, and an unassociated label next
to a checkbox. The suite fails before the fix at the type=button assertion.

- `make -j2 test` — **394 engine suites passed** and the cross-origin suite
  reported **114 checks, 0 failures**.
- `python3 tools/ci/check_suites.py` — passed.

## Scope correction

The frozen holdout-01 H4 TodoMVC template has a sibling `<label>{{title}}</label>`
and checkbox with no `for` attribute and no labelable descendant. It is not a
label/checkbox association, so a conforming label activation must not toggle
that checkbox. The holdout driver also refuses a click when the requested
`.toggle` hit test returns the label. That observation remains a separate
hit-target/layout symptom; it does not justify forwarding an unassociated
label to whichever control happens to be nearby.
