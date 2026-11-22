## Phase 186 — A modal dialog keeps the page behind it out of reach

Phase 178 gave a dialog the focus. The keyboard half of modality was in place and the pointer half was not: with `showModal()` open, a real click on a button behind the dialog ran that button, `Tab` walked the whole page, and `otherButton.focus()` moved the focus out of a dialog the user was in. Phase 147's write-up named it as "outside-input isolation"; this is that.

A modal dialog now records itself on its document — `Document::set_modal_dialog()`, a weak pointer, so a dialog that is removed from the document stops being modal with nobody having to say so — and `Document::is_inert(node)` answers whether a node is outside it. Three readers use it:

* `BrowserShell::element_at()` returns the modal dialog for a point outside its subtree. The backdrop's event target is the dialog itself, so what is behind it is not under the pointer as far as the page can tell. This is also what keeps a stale keyboard ring honest: `activate_focused()` refuses an inert element, because the ring's list is rebuilt with the layout and can lag a dialog that opened in between.
* `BrowserShell::scan_interactive_elements()` leaves inert elements out of the Tab order, so the ring cannot move out of the dialog either.
* `dom::focus_element()` returns without doing anything when the target is inert, so a script's `focus()` cannot pull the focus out from under a modal dialog. Blurring — `focus_element` with no element — is still allowed.

`close()` clears the document's record along with the element's flag. An ordinary `show()` never sets either, so an ordinary dialog isolates nothing: the page is still behind it, clickable and tabbable, which the fixture checks as a control.

`make -j2 build/litebird && tools/real_pages/dialog-03/check.sh`: **22/22 modal isolation checks passed**. The same probe on the engine at this branch's parent prints **15/22**. Every click in it is the shell's hit path: a page button is clicked before the dialog opens, while it is open (at the point where it is laid out, which is still behind the dialog), while an ordinary dialog is open, and after the modal one closes. A script's `element.click()` behind the modal still runs, which is the difference between input and a removed element.

`tools/real_pages/dialog-01/check.sh` and `dialog-02/check.sh` still print 28/28 and 32/32, `make -j2 test` 335/335 suites and 114 cross-origin checks with no failures.

**What is still open.** The top layer is still not painted: the modal dialog is drawn where it sits in the document, so there is no backdrop and no `::backdrop`, and the dialog does not cover the page. `Escape` is still not delivered by the shell, so the `cancel` event cannot be tested. `form method="dialog"` submission is still missing. The frozen holdout was not replayed in this session, so no score movement is claimed.
