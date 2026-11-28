# Finding 512 — the border style was parsed, computed and never painted

**Severity:** silent wrong rendering. `dashed`, `dotted`, `double`, `groove`,
`ridge`, `inset` and `outset` all painted as solid borders and as a solid
outline ring. `border: 2px dashed red` itself came out solid.

**Found by:** reading `DrawBorderCommand`. It carried geometry, widths and
colour, and no style, so neither painter had any input that could express
anything but solid — and for the outline ring there was not even a style.
The cascade and the CSSOM already knew the styles (`getComputedStyle`
answered `dashed`, the declaration block held `border-style: dashed`), which
is exactly why nothing caught it: the report was right and the ink was not.

**Repro:**

```html
<style>
  .a { border: 4px dashed #f00; width: 100px; height: 40px }
  .b { outline: 6px double #0f0; outline-offset: 0; width: 40px; height: 20px; margin: 20px }
</style>
<div class="a"></div>
<div class="b"></div>
```

Both drew solid: no gaps in `.a`, one band instead of two in `.b`.

**What changed:** `DrawBorderCommand` now carries per-edge styles, the
cascade parses and stores them (`border-style` one to four values, the
`border-*-style` longhands, the outline family), both painters implement
them, and the `border` shorthand no longer drops its style component. The
full detail is in [Phase 512](../phases/512-border-styles-paint-as-their-style.md).

A second defect surfaced while measuring this one: the `border-top-style`
longhand compared four characters at offset 7 against `"top"`, which never
matches, so the top edge never applied the box expansion — `border: 4px inset
#f00` resolved the other three edges and left the top at `none`. One
character; the top border vanished and `flex_stretch_subtracts_the_padding_and_the_border`
(render-conformance) caught the painter's absence of ink for exactly that
edge.

**Still open:**

- The outline ring is square on a rounded box.
- The terminal backend draws the compound styles as glyph approximations, not
  pixel-exact band splits; `dotted` differs in detail between the two
  backends (circles in the bitmap, per-edge marks in the terminal).
- `hidden` paints like `none`. The one documented difference between them
  (collapsed-table conflict resolution) is unimplemented here — this engine
  models table borders as a single solid style anyway.
- A declared `border-width` is still reserved in layout when
  `border-style: none` — the used-value zeroing was considered and
  deliberately not taken in this phase.
- Where the horizontal and vertical edges overlap at the corners, the
  horizontal bands own the corner pixels (they are painted first); the
  compromise the solid painter always made.
