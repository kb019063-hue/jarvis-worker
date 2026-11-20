# Finding 105 — `GPOS` kerning, mark zeroing, lookup order, and pen offsets

**Severity:** medium — fonts with `GPOS`-only kerning had no kerning, monospaced
combining marks advanced a full cell width (`1233` units in `DejaVu Sans Mono`),
stacked diacritics in `DejaVu Sans` / `DejaVu Serif` depend on `mkmk` resolving
after `mark` despite lower `LookupIndex`, and `ShapedGlyph` had no `(x_offset,
y_offset)` to place anchored marks or `PairPos` placements.

**Found while:** auditing OpenType `GDEF`/`GPOS` tables across host DejaVu
fonts (`DejaVuSans.ttf`, `DejaVuSans-Bold.ttf`, `DejaVuSerif.ttf`,
`DejaVuSansMono.ttf`) and tracing `FontRenderer` / `Canvas2D` placement.

## What was wrong

1. **Combining-mark advance zeroing lives in `GPOS` `SinglePos` under `mark`:**
   in `DejaVu Sans Mono`, combining marks (`U+0301`, `U+0302`, `U+0323`,
   `U+0327`) carry `hmtx` advance `1233`, and `latn` `mark` pairs `MarkToBase`
   (`Lookup 6`) with `SingleAdjustment` (`Lookup 7`, `x_advance = -1233`).
   Ignoring `SinglePos` or disabling all `GPOS` on `font-kerning: none` turns
   `"a\u0301b"` from two cells into three.
2. **`mkmk` precedes `mark` in `LookupList` order in real fonts:** `DejaVu
   Sans` places `mkmk` (`Type 6`) at `Lookup 4` and `mark` (`Type 4`) at
   `Lookup 13` (`DejaVu Serif`: `0` and `1`). Evaluating `Type 4` after `Type 6`
   in a single pass either skips `mkmk` before base attachment exists or
   overwrites the mark-to-mark parent with the base glyph.
3. **`ShapedGlyph` lacked `(x_offset, y_offset)` and vertical headroom:** marks
   and `ValueFormat` `x_placement`/`y_placement` shift ink relative to the pen
   without changing pen advance, and `CanvasRenderingContext2D::layout_text`
   sized its temporary coverage canvas to `[ceil(ascent)+1, ceil(descent)+1]`,
   clipping stacked diacritics above uppercase bases.
4. **`CoverageFormat2` index upper bound:** a hostile `CoverageFormat2` range
   with a large `startCoverageIndex` could wrap when cast to `uint16_t` for
   `MarkArray` validation.

## What changed

- Added `OtfCoverage`, `OtfClassDef`, `GDEF` (`GlyphClassDef`,
  `MarkAttachClassDef`, `MarkGlyphSetsDef`), and `GPOS` (`Type 1`, `Type 2`
  fmt1/2, `Type 4`, `Type 5`, `Type 6`, `Type 9`) parsers and `TrueTypeFont::shape_glyphs`
  (`include/litebird/render/FontRenderer.hpp`, `src/render/FontRenderer.cpp`).
- Separated shaping into deterministic passes: (1) `GSUB` `liga`/`rlig` tracking
  skipped marks' `(lig_id, lig_comp)`, (2) `SinglePos` and `PairPos` (only
  `kern`-feature `PairPos` respects `font-kerning: none`; legacy `kern` runs
  only when `!m_has_gpos_kern`), (3) `MarkToBase`/`MarkToLig`, (4) `MarkToMark`,
  then (5) left-to-right `attach_parent` offset resolution accounting for
  adjusted advances between parent and mark.
- Added `(x_offset, y_offset)` to `ShapedGlyph`, propagated `kerning` through
  `FontRenderer::shape_text`, applied `kSyntheticObliqueSkew * y_offset` when
  shearing oblique runs, and expanded `Canvas2D`'s offscreen text buffer
  vertically by `max_y_up` / `max_y_down`.
- Bounded `CoverageFormat2` mark counts to `<= 0xFFFF` and rejected nested/cyclic
  `Type 9` `ExtensionPos` wrappers.

## Verified

`make all test` (**266 / 266** suites, including the five new GPOS/GDEF suites)
and ASan+UBSan `build-asan/test_suite` + `tools/fuzz/fuzz_pipeline.py` (**400**
runs, **0** findings).
