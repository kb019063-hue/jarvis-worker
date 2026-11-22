# Modal isolation · what the page behind a modal dialog still answers

Phase 186 made `showModal()` put the rest of the document out of reach: the shell's hit test reports the dialog for a point outside it, the Tab list skips what is outside, and `focus()` on an outside element does nothing. This fixture asks with real clicks and real Tab steps, not by calling the page's handlers.

| Step | Expectation |
| :--- | :--- |
| click `#behind` with nothing open | the counter goes to 1 |
| click `#open` (`showModal()`) | the dialog is open and `#inside` has the focus |
| `element_at` at the point where `#behind` is laid out | the dialog, not `#behind` |
| click that point | the counter stays at 1 |
| `document.getElementById('behind').focus()` | `document.activeElement` is still `#inside` |
| `shell.focus_next()` | the focused element is inside the dialog |
| `document.getElementById('behind').click()` in a script | the counter goes to 2 — a script's click is not input |
| click `#dismiss` | the dialog closes, the focus returns to `#open`, and the point hits `#behind` again |
| click `#behind` | the counter goes to 3 |
| click `#open2` (`show()`) then `#behind` | the counter goes to 4 — an ordinary dialog isolates nothing |

## Reproduce

```sh
make -j2 build/litebird
tools/real_pages/dialog-03/check.sh   # 22/22 modal isolation checks passed
```

The fixture re-lays out before each click, because a page that changed since the last click moved its boxes and hit testing reads the layout.

## What this does not show

The dialog is still painted where it sits: there is no top layer, no backdrop and no `::backdrop`, so "the dialog covers the page" is not something this fixture claims. `Escape` and the `cancel` event are not tested — the shell does not deliver `Escape`.
