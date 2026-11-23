# Finding 221 — two of the three lines had nowhere to go

Status: **resolved**. Phase 217, `docs/phases/217-three-lines-one-drawn.md`.

## The finding

`text-decoration` has three lines and this engine drew one. `underline` was a
`DrawRectCommand` at `rect.height - 0.08em`; `line-through` and `overline` were
parsed by the resolver, carried on every `InlineFragment`, reported correctly by
`getComputedStyle` — and only ever reached a painter branch that asked
`has_underline()`.

`docs/findings/77-shared-computed-style.md` recorded it in Phase 71 as still
open. It stayed open for 146 phases, and the reason is worth keeping: the
information was present the whole time. Nothing was missing from the resolver,
the style, or the fragment. The line's *y* was the thing that had no answer, so
the two lines were not drawn rather than drawn somewhere wrong.

## Why the y was the hard part

The underline's y was a fraction of the line box — `height - 0.08em`. That is
not where a baseline is. A line box is `1.25em` tall and the text inside it is
`ascent + descent` tall, so a fraction of the box drifts against the ink as the
face and the size change. It happened to look reasonable for the default face at
16px, which is why it survived.

Three plausible answers existed:

1. parse the `post` table's `underlinePosition` — correct for the underline, and
   not implemented for any line;
2. hard-code fractions of the em — what the underline already did, and what
   made the box-relative y drift;
3. measure the face's own glyphs — the engine already rasterises them to draw.

The third was taken: `FontRenderer::decoration_lines` rasterises `'x'` and
`'H'` at the pixel size the painter will use and returns the topmost ink row of
each. `line-through` goes at the x-height and `overline` at the cap height, so
both land inside the band the text occupies, for any face, at any size, without
a table reader.

## What a reader should take from it

- **"Reported correctly" is not evidence that something is painted.** This is
  the second finding in two phases with that shape (finding 206: `outline` was
  in the CSSOM and in no `ComputedStyle` field). A property's report is built
  from the declaration text; its paint is built from whatever the painter
  happens to read.
- **A position that cannot be computed honestly gets skipped, silently.** The
  two lines were not drawn because nobody could say where they went, and
  nothing in the pipeline complained. The fix was to give the position a real
  source, not to guess a fraction.
- **A fraction of a containing box is not a font metric.** Phase 97 made the
  same point about kerning: the face's tables decide, or the ink does.

## What is still open

`text-decoration-style`, `-color`, `-thickness`, `text-underline-position` and
`-offset` are not parsed; decoration does not inherit from a block to its inline
descendants; and adjacent fragments of one line each get their own rectangle
rather than a merged one.
