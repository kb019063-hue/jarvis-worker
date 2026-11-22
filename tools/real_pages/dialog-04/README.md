# Top layer · what a modal dialog does to the pixels around it

Phase 189 put a modal dialog in a top layer: its box is painted after everything else the document drew, in front of a UA backdrop covering the viewport. This fixture reads three fixed points out of the frame.

| Point | Where |
| :--- | :--- |
| A | `(400,60)` — inside the red page block, outside the dialog |
| B | `(80,135)` — inside the dialog, and inside a `z-index: 5` blue box behind it |
| C | `(500,400)` — the white page background, below the page block |

| State | A | B | C |
| :--- | :--- | :--- | :--- |
| nothing open | red | blue | white |
| `showModal()` | `229,0,0` (10% black over red) | green — dialog above the `z-index` box, undimmed | `229,229,229` |
| after `close()` | red | blue | white |
| `show()` | red — no backdrop | blue — the `z-index` box still wins | white |

## Reproduce

```sh
make -j2 build/litebird
tools/real_pages/dialog-04/check.sh   # 16/16 top-layer paint checks passed
```

The rows use the renderer's integer source-over: `(0*26 + 255*229 + 127) / 255` is 229, where a browser's rounding would say 230.

## What this does not show

The terminal renderer does not draw the backdrop — `TerminalRenderer.cpp` belongs to open PR #227, so the command is left unhandled there and the terminal shows no dimming.

The dialog is positioned by the fixture's own CSS — the engine has no UA `position: fixed; inset: 0; margin: auto` for a modal dialog, so an unstyled one stays where the flow put it. Pages cannot style the backdrop (`::backdrop` does not exist), and a dialog with no background of its own lets the backdrop show through it. A scrolled page with a dialog open is not tested.
