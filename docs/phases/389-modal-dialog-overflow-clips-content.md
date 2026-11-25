# Phase 389 — modal dialogs clip overflow without losing their chrome

## What was wrong

HTML §Rendering 15.3.3 gives `dialog:modal` `overflow: auto`. Litebird's UA
stylesheet omitted that declaration because the dialog row still cited a paint
ordering bug: clipping started before the dialog's own border and background.
`paint_atom` has since been corrected to emit box chrome first, then push an
overflow clip at the padding box. The stale omission meant an open modal dialog
still computed to `overflow: visible`, so oversized contents could paint past
the dialog and over its backdrop.

The omission applied only to the modal UA rule; a non-modal dialog opened with
`show()` must not acquire this clipping declaration.

## What changed

Added `overflow: auto` to the existing `dialog:modal` rule in
`src/css/StyleResolver.cpp`. No painter change was needed: the existing
`paint_atom` path keeps the dialog's border and background outside the clip,
and wraps its contents in a clip whose rectangle is `padding_box()`.

The engine still does not draw scrollbars or implement scroll offsets. This
change supplies the standard's clipping behavior; it does not claim those
separate scrolling features.

## How it was verified

`tests/suites/389-modal-dialog-overflow-clips-content.cpp` fails against the
previous UA rule at the assertion that the modal dialog computes to
`Overflow::Auto`. With the rule in place it verifies all of the following in
the built layout and rendered pixels:

- only the modal dialog computes to `overflow: auto`; an ordinary dialog stays
  `overflow: visible`;
- the display list clips at the modal dialog's padding box and balances the
  clip command;
- the red border and blue background remain visible, the green child paints
  inside the clip, and pixels beyond the right and bottom edges show the dimmed
  backdrop rather than the overflowing child.

The suite and broader checks were rerun after merging `main` at
`8603d64e`:

- `make test`: **471/471** engine suites and **114/114** cross-origin checks;
- `make wpt`: **9570/9824**, with 254 listed failures and 0 unlisted failures;
- `make conformance`: **1973/1973**;
- `make js-conformance`: **1153/1158** (99.6%); its fixed everyday probe was
  **90/90**;
- `make dom-conformance`: **678/678**;
- `make url-conformance`: **1188/1188**;
- `make xml-conformance`: **1718/1718**;
- `make diff-trees`: four batches of **1500** pages each, 0 corrected-tree
  differences;
- `make diff-xml`: **4000** documents, 3958 agree with both references and 0
  Litebird differences (13 reference disagreements); **3000/3000** XML
  round-trip trees returned unchanged;
- `make fuzz`: parser, layout/render, XML, media, color and Brotli mutations
  completed with **0 findings**;
- `tools/fuzz/deep_pages.py --binary build/litebird --quiet --random 300
  --jobs 1`: **1332** runs over 465 pages, 0 failed.

The focused suite also passed under AddressSanitizer:

```sh
ASAN_OPTIONS=detect_leaks=0 LITEBIRD_SUITE_SHARD=1/3 \
  LITEBIRD_SUITE_CUTS=modal_dialog_overflow_auto_clips_contents_but_not_its_chrome,canvas_shadows_are_painted_under_their_shapes_and_off_by_default \
  ./build-asan/test_suite
```

The full `make asan ASAN_UNIT_STRETCHES=balanced` run exceeded the tool's
30-minute limit while running `asan-unit`; it did not finish.

## What is still open

Scrollbars and actual scrolling remain unimplemented. The `<dialog>` row also
still records author styling of `::backdrop` and painting/intrinsic sizing of
the native `<select>` as separate gaps.
