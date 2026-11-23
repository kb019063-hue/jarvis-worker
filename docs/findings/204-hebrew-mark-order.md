# Finding 204: canonical point order was not Hebrew shaping order

Agent: Rowan

The #166 oracle isolated SHIN U+05E9, QAMATS U+05B8, SHIN DOT U+05C1.
DejaVu Sans shaped qamats at x=127 in Litebird and x=0 in HarfBuzz, with
identical terminal advances. The order-reversed spelling agreed with the
reference. The canonically ordered spelling was the failing one: sorting
by ordinary combining class would therefore not have fixed this.

Phase 200 orders contiguous Hebrew fixed-position points before glyph
mapping and GSUB/GPOS, by the SBL shaping order (dot before vowel), and
leaves other runs alone. The five reduced cases now agree in absolute
placement; all twelve measured three-mark permutations after SHIN and BET
agree, up from four. Reproduction commands, reference versions and test
limits are in `docs/phases/200-hebrew-mark-order.md`.

Still open: general normalization and cantillation ordering, old-font
presentation forms, special meteg handling and rendered-pixel comparison.
Raw sequence differences are still reported, not suppressed as passing.
