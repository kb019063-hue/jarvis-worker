# Finding 432: Nonzero Arabic combining-mark advances in monospaced faces

Agent: Curlew

`TrueTypeFont::shape_glyphs` left raw `hmtx` advances on `GDEF` mark glyphs
(`GlyphClassDef == 3`) when a font's `arab` `GPOS` script contained anchor
lookups (`MarkToBase`, `MarkToLigature`, `MarkToMark`) without an explicit
`SinglePos` advance-zeroing lookup. `DejaVuSansMono.ttf` assigns a `1233`-unit
cell advance to every glyph in `hmtx` and zeroes marks via `SinglePos` Lookup 7
under `latn`/`grek`/`cyrl`, whereas its `arab` script relies on the OpenType
Arabic shaper's late `GDEF` mark-zeroing rule
(`HB_OT_SHAPE_ZERO_WIDTH_MARKS_BY_GDEF_LATE`). As a result, every surviving
Arabic combining mark in `DejaVuSansMono` added a full `1233`-unit cell to the
pen and shifted subsequent visual clusters right (`18/23` different-placement on
`tools/shaping/arabic_marks_diagnostic.py`, `384/384` on `--matrix`).

Phase 432 zeroes `x_advance` and `y_advance` on `GDEF` class-3 glyphs in `arab`
runs after `GPOS` and legacy `kern` positioning and before Stage 4a/4b
attachment offset resolution. On `DejaVuSansMono.ttf` against HarfBuzz 12.1.0,
same-placement results improve **10/11 → 11/11** on `compare.py`, **5/23 →
23/23** on `arabic_marks_diagnostic.py`, and **0/384 → 358/384** on supported
`--matrix` pairs (`0/384 → 368/384` on `DejaVuSansMono-Bold.ttf`), with no
change on `DejaVuSans` or `DejaVuSerif`.

Still open: Arabic modified combining-class ordering (2 matrix cases on Sans and
Mono), unattached mark origins under Litebird's `[base, mark]` RTL cluster
order when a font omits anchors on composed `U+0622`/`U+0623`/`U+0625` (24
Mono matrix cases), `GDEF`-less fallback mark classification, non-Arabic late
mark zeroing, and raw RTL output ordering. Exact hashes, commands, and synthetic
controls are in `docs/phases/432-arabic-mark-advances.md`.
