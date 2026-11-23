# Finding 206 — an outline was only a report

Status: **resolved** for the solid ring. Phase 202,
`docs/phases/202-the-ring-nobody-drew.md`.

## The finding

`outline` was in the engine twice and neither copy could paint. The CSSOM had
it — `outline` in the shorthand table, `outline-width`/`outline-style`/
`outline-color`/`outline-offset` in the initial-value table — so
`getComputedStyle(el).outlineWidth` answered `5px` for
`outline: 5px solid #0f0`. The cascade had nothing: `ComputedStyle` did not
have a field for any of the four, so the layout painted no ring and the display
list held only the box's own rectangle.

The report was the reason nobody noticed. Every interface a page checks for
"is this drawn" answered yes; the only witness to the gap was the ink, and
nothing compared the two.

## What was wrong, in one sentence

A property that is only in the *reporting* table is not implemented — it is
implemented enough to lie.

## The shape of the fix

1. the four longhands become fields of `ComputedStyle` and enter `hash()`;
2. the cascade's `apply_longhand` stores the shorthand and each longhand;
3. `emit_chrome` draws one `DrawBorderCommand` ring around the border box grown
   by `outline-offset + outline-width`, after the background and the border.

The first two are what make the third possible, and the third is the only part
a page can see.

## What a reader should take from it

- The `outline` row of the initial-value table is not a test that the property
  exists. `getComputedStyle` can answer a property the box model has never
  heard of, because its answer is the declaration text plus the initial values.
  A property is implemented where it is *consumed* — by the layout or by the
  painter — and nowhere else.
- An outline is outside the border box, so the ring at offset 0 shows only the
  band beyond the edge. The coordinate that proves a fix is
  `border_box.width + 2`, not `border_box.x + 2`; the first attempt at the
  suite asserted the second and failed against a correct painter.
- `outline-style: none` keeps a width and draws nothing. That inverted pair is
  the one place where the two halves of the property must not be collapsed.

## Still open

`dashed`, `dotted`, `double`, `groove`, `ridge`, `inset` and `outset` are stored
and paint solid, exactly as the border painter does; `border-radius` is not
applied to the ring. Both are limits of `DrawBorderCommand`, not of the outline
itself.
