# Phase 223 — The dialog that was never given a box

**Finding:** [227. The dialog that was never given a box](findings/227-the-dialog-that-was-never-given-a-box.md)
**Row:** AGENTS.md, "`<dialog>` opened by `showModal()` gets no UA chrome"
**Claim:** #281, `Phase 223, finding 227`
**Oracle:** holdout-01 H3's open frame (`tools/real_pages/holdout-01/results-rerun06/oracle-frames/H3-ref-open.png`)

## What was asked

The row names four things the open state lacks: a UA box for `dialog[open]`
(border/padding), centring or top-layer placement, a `::backdrop` layer, and a
closed-`<select>` rendering path. It says the UA stylesheet "has no form-control
or dialog-box rules at all", and that once landed "the open frame is the oracle".

## What was measured first

A default `<dialog open>` with no author CSS, before any change:

```
display=block  border=0px  padding=0px  margin=0px
rect: x=8 y=8 w=784 h=56            <- in the flow, flush top-left
```

And the oracle's open frame, read pixel by pixel rather than eyeballed:
border box `x 253–546, y 245–355`, border `3px` solid black, interior white,
page behind `(229,229,229)`.

That last number is the one that changed the shape of the work. `229` over the
document's white is black at **10%** — and `PaintCommand.cpp` already drew
`DrawBackdropCommand{Color(0, 0, 0, 26)}` for a modal dialog, with the top layer
built around it. The backdrop was not missing. Nothing was visible behind it
because the dialog itself had no box.

## What was done

1. **UA rules** in `StyleResolver::default_user_agent_stylesheet`, written as the
   HTML §Rendering 15.3.3 rule rather than as an approximation of it:

   ```css
   dialog { position: absolute; left: 0; right: 0;
            width: fit-content; height: fit-content; margin: auto;
            border: solid; padding: 1em;
            background-color: #ffffff; color: #222222; }
   dialog:modal { position: fixed; overflow: auto; top: 0; bottom: 0;
                  max-width: calc(100% - 6px - 2em);
                  max-height: calc(100% - 6px - 2em); }
   ```

2. **`:modal`** as a real pseudo-class (`Pseudo::Modal`), answered from
   `Document::modal_dialog()` — the same live-document shape as `:target` and
   `:focus`. It was in the never-matches list before, and in the older
   string-matching pseudo-class path an unhandled name fell through to `true`.

3. **`width: fit-content` / `height: fit-content`** as keywords with a
   shrink-to-fit size at both layout entry points (`layout_block` and
   `layout_inline_formatting_context`), placed before the min/max clamps so
   those still win.

4. **Out-of-flow auto margins**, solved after `layout()` per CSS 2.1 §10.3.7 and
   §10.6.4, so `inset: 0` plus `margin: auto` centres instead of sitting at the
   top-left.

5. **Three shorthand bugs** found by measuring why (1) did not take effect:
   `border: solid` emitted no width at all (now `medium`, 3px, per CSS 2.1
   §8.5.1); the `inset` shorthand was accepted by a branch whose body was
   "no renderer support yet" and dropped; and the out-of-flow pass read the
   content width before `layout()` had produced one.

6. **Clip order in `paint_atom`**: `PushClipCommand` was emitted *before* the
   box's own border and background and at the padding box, so any box that
   clipped painted its own chrome away. CSS 2.1 §11.1.1 clips contents, not
   chrome. This was invisible until the dialog UA rule gave the top-layer dialog
   `overflow: auto`.

## Measured result

| | before | after | Chromium |
|---|---|---|---|
| border | 0px | 3px solid black | 3px solid black |
| padding | 0px | 16px | 16px |
| background | transparent | white | white |
| placement | in flow, top-left | fixed, centred both axes | centred |
| backdrop behind | `229,229,229` | `229,229,229` | `229,229,229` |
| width | 784px | 114.4px | 265px |

Suites on this tree: **363/363** engine suites (362 before, plus the new one),
render reftests 35/41 with the same 6 recorded failures, DOM 678/678, JS
1148/1158, tree construction 1973/1973, everyday probes 90/90 and 68/68,
`make bench-floats` 0.134s for 10k rows.

## What this does not claim

- **Intrinsic width.** The dialog shrinks to its widest unbreakable word (114.4px)
  rather than to its content's natural width (265px), because the engine has no
  max-content measure and the native `<select>` paints no intrinsic box. The
  chrome and the placement are the claim; the width is not.
- **`::backdrop` from author CSS.** The colour is still the painter's, not a
  style rule; `dialog::backdrop` parses to a pseudo-element that matches nothing.
- **The native `<select>`.** Still renders nothing, here or elsewhere.
- **`overflow: auto` scrollbars** on a dialog that overflows.

## Files

`src/css/StyleResolver.cpp` (UA sheet, `inset` shorthand, `border` style-with-no-width,
`fit-content`, the string-path `modal` case), `include/litebird/css/StyleResolver.hpp`,
`src/dom/Selector.cpp` + `include/litebird/dom/Selector.hpp` (`Pseudo::Modal`),
`src/layout/LayoutBox.cpp` (fit-content sizing, out-of-flow auto margins),
`src/render/PaintCommand.cpp` (clip after chrome), `tests/test_suite.cpp`,
`AGENTS.md`, `AUDIT.md`.
