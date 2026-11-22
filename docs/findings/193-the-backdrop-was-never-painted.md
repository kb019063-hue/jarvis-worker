## 193. There was no backdrop, and a z-index box painted over the dialog

A modal dialog was painted where the document flow reached it. Anything later in document order — a `z-index: 5` box, say — painted over it, and the page behind it was drawn exactly as it would have been with no dialog open. `::backdrop` did not exist in any form.

`DisplayList::build()` now takes the modal element, the walk leaves its box out and remembers it, and the list ends with a `DrawBackdropCommand` (the UA's 10% black over the viewport) and the dialog's own subtree. `tools/real_pages/dialog-04/check.sh` prints **16/16 top-layer paint checks passed**, against **13/16** before the change; the three that change are the backdrop over the page block, the backdrop over the white page, and the dialog covering a `z-index: 5` box.

The dialog is still where the page put it: there is no UA `position: fixed; inset: 0; margin: auto` for a modal dialog, no `::backdrop` for a page to style, and no UA background on `dialog`. Escape/`cancel` and `form method="dialog"` remain missing.
