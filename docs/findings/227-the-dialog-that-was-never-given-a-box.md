# 227. The dialog that was never given a box

**Phase 223.** A `<dialog>` opened with `showModal()` was already *modal* in this
engine: the document records the modal dialog, everything outside it is
unreachable, `<select>` inside it does not focus, `Cancel` closes it, and
`returnValue` comes back right. holdout-01 H3's gates have passed for two
re-runs. What was missing was the part a reader sees.

## What the oracle shows

`tools/real_pages/holdout-01/results-rerun06/oracle-frames/H3-ref-open.png` is
Chromium's open frame, 800×600. Measured off it:

| | Chromium (measured) | Litebird, before | Litebird, after |
|---|---|---|---|
| dialog border box | `x 253–546, y 245–355` | `x 0–800, y 0–56` | `x 342.8–457.2, y 242–358` |
| border | 3px solid black | 0px | 3px solid black |
| padding | 16px (= 1em) | 0px | 16px |
| background | white | transparent | white |
| position | out of flow, centred | in the flow, top-left | fixed, centred both axes |
| page behind it | `(229,229,229)` | `(229,229,229)` | `(229,229,229)` |

The backdrop column is the interesting one: **it was already right.** The
painter had a top layer with a `(0,0,0,26)` backdrop since the modal path was
built — that is black at 10% over white, exactly the 229 the oracle has. The
row's "no `::backdrop` as a distinct layer" was true of the *look* and not of
the implementation: with no box to sit behind, a grey page reads as a grey page.

## The causes, none of them in one place

1. **The UA sheet had no dialog rules.** `StyleResolver::default_user_agent_stylesheet`
   begins with `table { … }` and never mentions `dialog`. The open/closed
   display default was (and is) hard-coded in the cascade, so a closed dialog
   correctly generated no box while an open one generated a naked one.
2. **`border: solid` computed to no border at all.** `expand_shorthand` consumed
   the `solid` token and emitted nothing — no `border-width` at all, so the
   border was 0px wide. CSS 2.1 §8.5.1: a style with no width is `medium`, 3px.
   This was not a dialog bug; it is how *every* `border: solid` on the web
   behaved, and the HTML rendering standard writes the dialog's border exactly
   that way.
3. **`inset` was parsed and thrown away.** `expand_shorthand` returned `true`
   for `inset` from a branch commented "no renderer support yet", which made the
   caller discard the shorthand without emitting the four longhands. `top` and
   `left` worked; `inset: 0` did nothing.
4. **Out-of-flow boxes had no auto-margin centring.** `resolve_out_of_flow_positions`
   left `margin: auto` at zero, so a box with insets on both sides sat at the
   top-left. CSS 2.1 §10.3.7/§10.6.4 solve those margins against the leftover
   space, which is what centres a dialog — and it has to happen *after*
   `layout()`, because before it the content width is still zero and the box is
   centred as if it were nothing wide.

The paint path has a further defect, which this phase did **not** fix and does
not depend on: `paint_atom` pushes `PushClipCommand` at the padding box *before*
`emit_chrome`, so a box that establishes an overflow clip paints its own border
and background away (CSS 2.1 §11.1.1 clips contents, not chrome). It surfaced
while finding out why the first version of the UA rule painted an unframed white
rectangle, and it is why the `overflow: auto` the standard puts on
`dialog:modal` is left out of this rule: this engine draws no scrollbars, so the
clip buys nothing and would only hide the dialog's own border. `#273` was
holding `src/render/PaintCommand.cpp`, and a fix there belongs to that row.

## What the fix does and does not reach

The UA rule is the standard's own, from HTML §Rendering 15.3.3:

```css
dialog:not([open]) { display: none; }
dialog {
  position: absolute; left: 0; right: 0;
  width: fit-content; height: fit-content;
  margin: auto; border: solid; padding: 1em;
  background-color: Canvas; color: CanvasText;
}
dialog:modal { position: fixed; top: 0; bottom: 0;
               max-width: calc(100% - 6px - 2em);
               max-height: calc(100% - 6px - 2em); }
```

The standard's `inset-inline-start/end` are written `left`/`right` because the
engine has no logical properties and no writing mode but the horizontal one,
and `Canvas`/`CanvasText` are written as the colours they resolve to. `:modal`
is a new pseudo-class, answered from the document's modal-dialog pointer the way
`:target` and `:focus` are answered from live state; before it, `modal` was in
the "never matches" list, and in the older string-matching path a name with no
case fell through to *true*.

`width: fit-content` and `height: fit-content` are new keywords. There was no
intrinsic sizing beyond the min-content measure floats use, so a dialog shrank
to its widest unbreakable word: **114.4px against Chromium's 265px.** The box is
now the right *shape* — border, padding, background, centred, over a backdrop —
and materially narrower than the oracle, because a real `fit-content` needs a
max-content measure the engine does not have, and the native `<select>` inside
the dialog still paints no intrinsic box. Both are separate rows of work; this
one claims the chrome and the placement, not the intrinsic width.

Also out of reach here: `::backdrop` is not addressable from author CSS (the
colour lives in the painter, `PaintCommand.cpp`), `dialog::backdrop` parsing
still lands on a pseudo-element that matches no element, the native `<select>`
still renders nothing, and `dialog:modal` does not carry the standard's
`overflow: auto` because the engine cannot draw a scrollbar for it.

## The measurement

`TEST(a_modal_dialog_gets_the_ua_box_and_centring_it_is_due)`, placed beside the
`::before`/`::after` suite it shares its oracle with. It asserts all three states
in one tree: closed (no box), open but not modal (`show()`: 3px border, 16px
padding, fit-content narrower than an auto width, white background), and modal
(fixed, both margins auto, top and bottom gaps equal to within 1.5px, left and
right gaps likewise).

Run on the tree this landed on: 363/363 engine suites, render reftests 35/41
with the same 6 recorded failures, DOM 678/678, JS 1148/1158, tree construction
1973/1973, everyday probes 90/90 and 68/68.

## The general lesson

Three of the four causes were shorthands and defaults that had been accepted and
dropped: a style token consumed by `border`, a whole property swallowed by an
early `return true`, a margin that was legal to write and never solved. The
dialog was not missing a feature. It was sitting on four places where a
declaration could go in and nothing come out, and no test had put a declaration
in and measured what came out the other side.
