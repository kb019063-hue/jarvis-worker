# Phase 99 — `GDEF` and `GPOS` kerning and mark positioning

Phases 91 and 93 taught `TrueTypeFont` to read legacy `kern` format-0 pair tables
and `GSUB` `liga`/`rlig` ligatures, and left OpenType `GDEF` and `GPOS` unread.
Modern fonts (`DejaVuSansMono` and many web fonts that ship kerning only in
`GPOS`) store kerning in `GPOS` `PairPos`, carry zeroing adjustments for
monospaced combining marks in `GPOS` `SinglePos`, and carry diacritic
attachment coordinates in `GPOS` `MarkToBase`, `MarkToLig`, and `MarkToMark`.
Without `GDEF` and `GPOS`:

- `TrueTypeFont` reported no kerning on fonts whose kerning lives exclusively in
  `GPOS`,
- combining marks used raw `hmtx` advances (such as `1233` units per mark in
  `DejaVu Sans Mono`, adding a full character cell per diacritic) and were placed
  at unanchored baseline coordinates regardless of base height or mark stacking,
- `ShapedGlyph` carried only `advance` with no `(x_offset, y_offset)` pen offsets,
  so neither `FontRenderer::draw_text` nor `CanvasRenderingContext2D::layout_text`
  could lift or stack glyphs relative to the pen.

## What changed

1. **OpenType layout helpers and `GDEF` parsing (`include/litebird/render/FontRenderer.hpp`, `src/render/FontRenderer.cpp`)**
   - Added bounds-checked `OtfCoverage` (formats 1 and 2, binary search when sorted),
     `OtfClassDef` (formats 1 and 2, binary search when sorted), `GposValue`
     (`x_placement`, `y_placement`, `x_advance`, `y_advance`), and `GposAnchor`.
   - Added `TrueTypeFont::load_gdef()` supporting `GDEF` versions 1.0, 1.2, and 1.3:
     `GlyphClassDef` (`1` base, `2` ligature, `3` mark, `4` component),
     `MarkAttachClassDef`, and `MarkGlyphSetsDef` (format 1) for `LookupFlag`
     filtering (`IgnoreBaseGlyphs`, `IgnoreLigatures`, `IgnoreMarks`,
     `MarkAttachmentType`, `UseMarkFilteringSet`).

2. **`GSUB` mark skipping (`load_ligatures` & `TrueTypeFont::shape_glyphs`)**
   - `Ligature` now records whether its `GSUB` lookup carried `IgnoreMarks`
     (`0x0008`), preserving skipped combining marks in the shaped glyph stream
     with their `(lig_id, lig_comp)` component assignment so ligature marks
     (`"f\u0301i"` vs `"fi\u0301"`) attach to the right ligature component.

3. **OpenType `GPOS` parsing and shaping (`TrueTypeFont::load_gpos`, `shape_glyphs`, `kerning_units_for_glyphs`)**
   - Collects `kern`, `mark`, and `mkmk` lookups for `latn` (falling back to
     `DFLT`) across `Type 1` (`SingleAdjustment` formats 1 and 2), `Type 2`
     (`PairAdjustment` format 1 with binary-searched pairs and format 2 with
     zero-copy class matrix reads), `Type 4` (`MarkToBase`), `Type 5`
     (`MarkToLigature`), `Type 6` (`MarkToMark`), and `Type 9`
     (`ExtensionPositioning`, rejecting nested/cyclic Type 9 wrappers).
   - `TrueTypeFont::shape_glyphs(codepoints, kerning)` runs `GSUB` ligatures,
     then `SinglePos` + `PairPos` (with `font-kerning: none` suppressing `kern`
     lookups while keeping `mark`/`mkmk` and `SinglePos` active, and advancing 2
     glyphs when `ValueRecord2` is non-zero; falling back to legacy `kern` when
     `!m_has_gpos_kern`), then `MarkToBase` (not crossing ligatures) and
     `MarkToLigature`, then `MarkToMark` (applied after base attachment even when
     `mkmk` has a lower `LookupIndex`, as in DejaVu Sans/Serif), and resolves
     attachment chains left-to-right into `(x_advance, y_advance, x_offset,
     y_offset)`.
   - `has_kerning()` and `kerning_units_for_glyphs()` now consult `GPOS` `kern`
     first (before legacy `kern` fallback), and `has_gpos()` /
     `has_mark_positioning()` expose GPOS presence cleanly.

4. **`FontRenderer` and `Canvas2D` integration (`src/render/FontRenderer.cpp`, `src/render/Canvas2D.cpp`)**
   - `ShapedGlyph` gained `x_offset` and `y_offset` in scaled CSS pixels, and
     `FontRenderer::shape_text(..., bool kerning = true)` delegates directly to
     `TrueTypeFont::shape_glyphs(run, kerning)`, including horizontal synthetic
     oblique skew (`kSyntheticObliqueSkew * y_offset`) for raised marks when an
     upright face is slanted.
   - `FontRenderer::measure_text` and `draw_text` and
     `CanvasRenderingContext2D::layout_text` use one shaped pass, apply `x_offset`
     and `-lround(y_offset)` (Y-down screen pixels) at glyph placement, and
     `Canvas2D` expands its offscreen text buffer vertically above/below baseline
     for raised/lowered marks so stacked diacritics do not clip.

## Verified

- Release `make all test`: **266 / 266** unit suites + **114 / 114**
  cross-origin checks passing, including five new Phase 99 suites:
  - `gpos_pair_and_single_positioning_without_kern_table` (`SinglePos`, `PairPos`
    fmt1/2, `ValueFormat2`, `ExtensionPos` Type 9, `IgnoreMarks`, `font-kerning:
    none`, and host `DejaVu Sans Mono` verification)
  - `gpos_mark_to_base_and_mark_to_mark_anchoring` (`MarkToBase` + `MarkToMark`
    with inverted lookup indices, pixel `min_y` bounds, and host `DejaVu Sans`
    verification)
  - `gpos_mark_to_ligature_and_lookup_flag_filtering` (`GSUB` `IgnoreMarks`
    component assignment for `MarkToLigature`, `UseMarkFilteringSet`)
  - `canvas2d_and_html_paint_gpos_marks_and_kerning` (`Canvas2D` `measureText`
    + unclipped `fillText`, `BitmapRenderer` `font-kerning: none`, and synthetic
    oblique skew)
  - `gpos_rejects_malformed_and_cyclic_tables_safely` (truncated buffers,
    overflowing class matrices and coverage ranges, cyclic `Type 9` wrappers)
- Existing conformance suites (Release): `make conformance` (`1972/1973`),
  `make js-conformance` (`1137/1156`), `make dom-conformance` (`678/678`),
  `make url-conformance` (`1188/1188`), `make wpt` (`9567/9824`, `0` unlisted).
- Sanitizer (`-fsanitize=address,undefined`): `build-asan/test_suite` (**266 /
  266** suites, **0** sanitizer findings), `build-asan/cross_origin_test` (**114 /
  114**), and `tools/fuzz/fuzz_pipeline.py --binary build-asan/litebird` (**400 /
  400** runs, **0** findings).

## Still open

- Contextual positioning (`GPOS` `Type 7` / `Type 8` chaining contextual),
  `CursiveAttachment` (`Type 3`), and complex-script shaping (Arabic joining,
  Indic reordering) plus UAX #9 bidirectional reordering remain open in Tier 1.
- Extended `kern` table formats (`format 2`, Apple `kern` v1 / `morx`) and
  `Device`/`VariationIndex` delta refinement on `GPOS` anchors/values are not
  parsed yet.
- `ctx.fontKerning` on Canvas 2D remains open.
