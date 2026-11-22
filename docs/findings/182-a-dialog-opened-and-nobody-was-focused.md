## 182. A dialog opened and nobody was focused

`showModal()` set the `open` attribute and stopped there. The element that opened the dialog kept the focus, and everything inside the dialog was unreachable from the keyboard. `close()` did not touch the focus either: on the MDN holdout the dialog's Cancel button kept it after the dialog had been hidden, so `document.activeElement` was a `display: none` button.

The dialog focusing steps now run when a dialog opens, and `close()` of a modal dialog gives the focus back to the element that had it when it opened. A dialog with nothing focusable inside takes the focus itself, at `tabindex="-1"`. A previous focus that is no longer connected is dropped, and the document is unfocused instead of leaving it on a hidden control. `tools/real_pages/dialog-02/check.sh` prints **32/32 dialog focus checks passed**.

Only the focus is resolved. The modal dialog is still painted inline, with no backdrop; input outside it is still not isolated, by pointer or by `Tab`; and the `cancel` event from `Escape` and the `form method="dialog"` submit path are still missing. The frozen holdout score is unchanged: nothing here was replayed against the pinned pages in this session.
