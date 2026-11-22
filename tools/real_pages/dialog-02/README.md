# Dialog focus · where the focus goes when a dialog opens and closes

Phase 178 ran the dialog focusing steps from `show()` and `showModal()`, and made `close()` of a modal dialog give the focus back to the element that had it. This fixture asks the engine where `document.activeElement` is after each step, with real clicks through the shell's hit path rather than script calls to the page's own handlers.

| Step | Expectation |
| :--- | :--- |
| load | `BODY` — nothing has the focus |
| click `#first-open` (`showModal()`) | `#first-a`, the first focusable descendant |
| click `#first-b` | `#first-b` — a click inside moves the focus |
| `show()` on the open modal dialog | no throw, focus still `#first-b` |
| click `#first-cancel` | dialog closed, focus back on `#first-open` |
| `close()` again | nothing changes |
| click `#auto-open` | `#auto-focused`, the `autofocus` descendant, not `#auto-b` |
| click `#auto-cancel` | focus back on `#auto-open` |
| click `#empty-open` | `#empty` itself, at `tabIndex` `-1` |
| `close()` | focus back on `#empty-open` |
| click `#nonmodal-open` (`show()`) | `#nonmodal-a` — `show()` focuses too |
| `show()` on the open ordinary dialog | `InvalidStateError` |
| click `#nonmodal-open` again | dialog closed, `BODY` — an ordinary close restores nothing |
| click `#gone-open`, remove `#gone-wrap`, click `#doomed-cancel` | `BODY` — a previous focus that left the document is not restored |

## Reproduce

```sh
make -j2 build/litebird
tools/real_pages/dialog-02/check.sh   # 32/32 dialog focus checks passed
```

The fixture re-lays out before each click, because a page that changed since the last click moved its boxes and hit testing reads the layout.

## What this does not show

The top layer is not painted and there is no `::backdrop`; a real click on a button behind the modal dialog still reaches it, and `Tab` still walks the whole document. The shell does not deliver `Escape`, so the `cancel` event is untested. None of these are claims this fixture supports.
