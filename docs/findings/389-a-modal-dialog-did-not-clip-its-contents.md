# Finding 389 — modal dialog contents were not clipped

**Severity:** rendering correctness.

HTML §Rendering 15.3.3 gives a modal dialog `overflow: auto`. The UA rule in
`src/css/StyleResolver.cpp` did not include it, so the modal's computed overflow
remained visible and a child taller or wider than the dialog could paint over
the surrounding page and backdrop.

The explanation for leaving the declaration out had gone stale. It referred to
a painter that clipped before emitting the box's own border and background.
`paint_atom` now emits chrome first and pushes the overflow clip at the
padding-box edge, so the dialog's chrome is not clipped away. `box_clips()`
already treats `Overflow::Auto` as a clip; the missing piece was the modal
UA-style declaration.

## Repair and regression

`dialog:modal` now has `overflow: auto`; ordinary, non-modal dialogs retain the
initial visible overflow. The new suite
`tests/suites/389-modal-dialog-overflow-clips-content.cpp` checks the computed
styles, the exact padding-box clip, and the visible result: border and
background pixels survive, an overflowing child is visible inside the dialog,
and the area beyond its right and bottom edges shows the dimmed backdrop.

Before the fix, the focused suite failed because the modal style was
`Overflow::Visible`. After it, on the branch rebased onto `main` at
`8603d64e`:

- `make test` passed **471/471** engine suites and **114/114** cross-origin
  checks;
- `make wpt` reported **9570/9824**, with 254 listed failures and 0 unlisted
  failures;
- `make conformance`, `make js-conformance`, `make dom-conformance`,
  `make url-conformance`, and `make xml-conformance` reported
  **1973/1973**, **1153/1158**, **678/678**, **1188/1188**, and **1718/1718**,
  respectively;

## What remains

The engine still paints no scrollbars and has no scroll offsets. Those are not
implemented by adding the clipping declaration. The `<dialog>` row's
`::backdrop` styling and native `<select>` gaps are independent.
