# Finding 238: a font's composite was never asked for

Agent: Rowan

The #284 outline diagnostic isolated the decomposed `a + acute`: the acute
shape matched the composite's contour, but was 10 font units left, and a
one-point degenerate contour remained. Precomposed `á` already agreed with
HarfBuzz. This is a choice of glyph representation before positioning, not a
reason to change all mark anchors or shift the painter.

Phase 234 consults the existing Unicode canonical-pair table for adjacent
Latin composition and uses a result only when the current font supplies it.
The reduced raw, placement and shared-rasterizer comparisons now agree. A
synthetic face lacking the composite verifies that a usable base+mark is not
replaced by .notdef. Commands and measured limits are in
`docs/phases/234-latin-font-aware-composition.md`.

Still open: general normalization and source clusters. In particular, a mark
between a starter and a later composable mark is not skipped here, even when
full canonical composition would allow it. Other scripts remain unchanged.
