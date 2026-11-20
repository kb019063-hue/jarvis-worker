# Finding 124 — Unjoined Arabic glyphs, ignored contextual `GSUB`/`GPOS`, and fallback/RTL run splitting

**Severity:** medium — Arabic words painted in disconnected isolated forms,
contextual `ccmp`/`calt`/`rlig` rules (including dotless `'ı'` before marks and
Lam–Alef ligation) did not run, `CursiveAttachment` (`GPOS Type 3`) was unread,
and fallback face selection + `Canvas2D` buffer padding split or clipped RTL
and cursive runs.

**Found while:** auditing OpenType `GSUB`/`GPOS` coverage across `DejaVuSans.ttf`
(`arab` joining/ligature features, `latn` `ccmp` `L3 -> L33`, `nko ` `reqFeatureIndex`)
and tracing `TrueTypeFont::shape_glyphs`, `FontRenderer::shape_text`, and
`CanvasRenderingContext2D::layout_text`.

## What was wrong

1. **No Unicode joining or script itemization (`GSUB` read `Type 4` for `latn`/`DFLT` only):**
   Arabic letters (`U+0600..06FF`, Supplement, Extended-A), Syriac, and N'Ko had
   no joining analysis (`isol`/`init`/`medi`/`fina`), `U+200C` (ZWNJ) /
   `U+200D` (ZWJ) emitted `.notdef` boxes or failed to gate ligation/joining,
   and `GSUB` `Type 1/2/3/5/6` lookups (such as `DejaVuSans.ttf`'s `ccmp`
   chaining rule converting `'i'` before combining accents to dotless `'ı'`)
   were skipped.
2. **No `CursiveAttachment` (`GPOS Type 3`) or contextual positioning (`GPOS Type 7/8`):**
   Entry/exit cursive Y chains (both LTR and `RightToLeft` `LookupFlag & 1`)
   and contextual `SinglePos`/`PairPos` adjustments were ignored.
3. **Fallback face run splitting and RTL cluster hazards:**
   `FontRenderer::shape_text` broke an Arabic run back to a Latin primary face at
   spaces/punctuation covered by the primary face (preventing whole-phrase Arabic
   RTL shaping), and `visual_rtl` reversal needed explicit protection for orphan
   marks and embedded digits (`0..9`, `U+0660..0669`, `U+06F0..06F9`) so digits
   remained left-to-right inside RTL text.
4. **Horizontal cursive overhang clipping in `Canvas2D`:**
   `CanvasRenderingContext2D::layout_text` expanded its offscreen raster canvas
   vertically for raised/lowered diacritics (`max_y_up`/`max_y_down`) but did
   not pad horizontally for negative `x_offset` cursive/positional shifts.

## What changed

- Added `itemize_codepoint_scripts()`, `classify_arabic_joining()`, and
  `compute_arabic_feature_masks()` (`src/render/FontRenderer.cpp`), preserving
  join controls during `GSUB` and stripping `(1u << 31)` join controls prior to
  `GPOS`.
- Extended `TrueTypeFont::load_ligatures()` and `load_gpos()` to parse multi-script
  `GSUB` (`Type 1..7`) and `GPOS` (`Type 1..9`, including `CursiveAttachment`,
  `ContextPos`, `ChainContextPos`) with `depth < 16`, glyph cap
  `max(orig * 8 + 64, 4096)`, and cycle-safe memoized DFS for `cursive_chain`.
- Updated `TrueTypeFont::shape_glyphs`, `FontRenderer::shape_text` / `draw_text`,
  and `CanvasRenderingContext2D::layout_text` (`src/render/Canvas2D.cpp`) for
  coherent RTL fallback runs, cluster-safe `visual_rtl` ordering, and bounded
  (`1024.0f`) horizontal overhang padding, and coordinated Phase 109's
  `apply_bidi_to_line()` (`LayoutBox.{hpp,cpp}`, `PaintCommand.{hpp,cpp}`,
  `BitmapRenderer.cpp`) so `U+200C`/`U+200D` join controls survive X9 removal
  and `is_rtl_visual` fragments shape logically before cluster reordering.

## Verified

- Release `make all test`: **279 / 279** suites passing (including six new Phase
  119 contextual/cursive/Arabic/malformed-table suites + HTML-Bidi `"سلام"`
  pixel-identity oracle across synthetic `"TestArabicSans"` and host `DejaVu Sans`) + **114 / 114** cross-origin checks +
  `make bidi-conformance` (**861,948 / 861,948**).
- ASan+UBSan: `build-asan/test_suite` (**279 / 279** suites, **0** sanitizer
  findings), `build-asan/cross_origin_test` (**114 / 114**), and
  `tools/fuzz/fuzz_pipeline.py --binary build-asan/litebird` (**400** runs,
  **0** findings).
