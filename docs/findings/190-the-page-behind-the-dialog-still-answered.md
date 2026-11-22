## 190. The page behind the dialog still answered the click

A modal dialog had no effect on anything outside itself. With one open, a real click on a button behind it ran that button's handler and moved the focus to it; `Tab` stepped through the whole document; and `behind.focus()` in a script took the focus out of the dialog. The engine had a modal flag and no reader that involved input.

The document now remembers which dialog is modal, `Document::is_inert()` says whether a node is outside it, and the shell's hit test, the shell's Tab list and `focus_element()` all consult it: a point behind the dialog hits the dialog (the backdrop's target), a `Tab` step stays inside, and a script's `focus()` on an inert element does nothing. `tools/real_pages/dialog-03/check.sh` prints **22/22 modal isolation checks passed**, against **15/22** on the same probe before the change.

What is left is paint. The dialog is still drawn in document order, so nothing verifies that it covers the page, and there is no `::backdrop` at all. `Escape`/`cancel` and `form method="dialog"` are still missing. The frozen score is unchanged and was not re-measured.
