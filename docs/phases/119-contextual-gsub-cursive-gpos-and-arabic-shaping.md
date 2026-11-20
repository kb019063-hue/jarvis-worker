# Phase 119 — Contextual `GSUB`, `CursiveAttachment` `GPOS`, and Arabic/Multi-Script Shaping

Phase 99 added `GDEF` glyph classes and `GPOS` positioning (`SinglePos`, `PairPos`,
`MarkToBase`, `MarkToLig`, and `MarkToMark`), while Phase 93 read `GSUB` `Type 4`
ligatures only under `latn`/`DFLT`. Complex scripts and contextual OpenType rules
remained unhandled:

- Arabic letters painted in isolated nominal forms without contextual joining
  (`isol`/`init`/`medi`/`fina`), `rlig` Lam–Alef ligatures, or `U+200C` (ZWNJ) /
  `U+200D` (ZWJ) join-control behavior (and join controls could paint `.notdef`
  boxes or break fallback face runs),
- `GSUB` ignored `Type 1` (`SingleSubst`), `Type 2` (`MultipleSubst`), `Type 3`
  (`AlternateSubst`), `Type 5` (`ContextSubst`), and `Type 6` (`ChainContextSubst`)
  (e.g., `DejaVuSans.ttf`'s `ccmp` lookup `L3 -> L33` replacing `'i'` with
  dotless `'ı'` before combining marks),
- `GPOS` lacked `Type 3` (`CursiveAttachment` LTR and RTL entry/exit chaining),
  `Type 7` (`ContextPos`), and `Type 8` (`ChainContextPos`),
- RTL runs were neither visually reordered by cluster nor kept unified across
  internal spaces and fallback fonts, and `Canvas2D` could clip negative
  horizontal cursive overhangs (`x_offset < 0` or wide shifted glyphs).

## What changed

1. **Multi-script itemization and Unicode Arabic joining (`src/render/FontRenderer.cpp`, `include/litebird/render/FontRenderer.hpp`)**
   - Added `itemize_codepoint_scripts()` classifying `latn`, `arab`, `hebr`,
     `syrc`, `thaa`, `nko `, `cyrl`, `grek`, `thai`, `lao `, `dev2`, `Inherited`,
     and `Common`: attaching `Inherited` marks and join controls (`U+200C`/`U+200D`)
     to the preceding strong script, merging non-digit `Common` spans (spaces,
     punctuation) bounded by matching scripts so multi-word RTL runs stay intact,
     and keeping embedded ASCII and Arabic-Indic/Extended digits left-to-right.
   - Added `classify_arabic_joining()` (`R`/`L`/`D`/`C`/`T`/`U`) and
     `compute_arabic_feature_masks()` emitting `isol`/`init`/`medi`/`fina` feature
     masks per glyph, respecting transparent marks, `U+0640`/`U+200D` dual/causing
     joining, and `U+200C` join breaking.
   - Tagged `U+200C`/`U+200D` with `(1u << 31)` so they participate in `GSUB`
     contextual/ligature checks (blocking unwanted ligatures like Lam–ZWNJ–Alef)
     and are stripped before `GPOS` measurement/positioning.

2. **Full `GSUB` (`Type 1..7`) and script-aware `GPOS` (`Type 1..9`) (`include/litebird/render/FontRenderer.hpp`, `src/render/FontRenderer.cpp`)**
   - Extended `TrueTypeFont::load_ligatures()` (preserving `m_ligature_sets`
     compatibility) to build per-script `GsubScriptPlan`s (`reqFeatureIndex` +
     `ccmp`, `locl`, `rlig`, `liga`, `calt`, `clig`, `rclt`, `isol`, `init`,
     `medi`, `fina`), parsing `SingleSubst` (fmt 1/2), `MultipleSubst` (fmt 1),
     `AlternateSubst` (fmt 1), `LigatureSubst` (fmt 1), `ContextSubst` (`Type 5`
     fmt 1/2/3), `ChainContextSubst` (`Type 6` fmt 1/2/3), and `ExtensionSubst`
     (`Type 7`, rejecting self-nested wrappers).
   - Extended `TrueTypeFont::load_gpos()` to build per-script `GposScriptPlan`s
     (`reqFeatureIndex` + `kern`, `curs`, `mark`, `mkmk`, `calt`, `dist`, `abvm`,
     `blwm`), parsing `CursiveAttachment` (`Type 3` EntryExit anchors), `ContextPos`
     (`Type 7`), and `ChainContextPos` (`Type 8`) alongside existing `Type 1/2/4/5/6/9`.
   - Rewrote `TrueTypeFont::shape_glyphs(codepoints, kerning, visual_rtl = false)`:
     - Bounds nested contextual recursion to `depth < 16` and glyph expansions to
       `max(orig * 8 + 64, 4096)`.
     - Executes `GSUB`, strips join controls, runs `GPOS` in three stages
       (`Single`/`Pair`/`Cursive`/`Contextual` -> `MarkToBase`/`MarkToLig` ->
       `MarkToMark`, plus legacy `kern` when `!script_has_kern`), resolves
       memoized `cursive_chain` Y attachments (handling both LTR and `RightToLeft`
       `LookupFlag & 1` chains cleanly without cycle hangs) and `mark_base` anchor
       chains, then optionally reorders RTL base+marks clusters visually
       left-to-right (keeping orphan marks standalone rather than absorbing neighbors).
   - Scoped `kerning_units_for_glyphs()` to `latn`/`DFLT` and exposed `has_gsub()`,
     `has_cursive_positioning()`, and `has_contextual_shaping()`.

3. **UAX #9 bidi coordination, `FontRenderer`, and `Canvas2D` integration (`include/litebird/layout/LayoutBox.hpp`, `src/layout/LayoutBox.cpp`, `include/litebird/render/PaintCommand.hpp`, `src/render/PaintCommand.cpp`, `src/render/BitmapRenderer.cpp`, `src/render/FontRenderer.cpp`, `src/render/Canvas2D.cpp`)**
   - Coordinated Phase 109's `apply_bidi_to_line()` with OpenType shaping:
     retained `U+200C` (ZWNJ) / `U+200D` (ZWJ) inside runs (since X9 removes `BN`
     only for bidi level resolution) and flagged odd-level visually reversed
     fragments with `is_rtl_visual` (`InlineFragment` -> `DrawTextCommand` ->
     `FontRenderer::draw_text(..., pre_reversed = true)`) so `FontRenderer`
     restores logical order for Arabic joining/ligatures/`GPOS` mark anchoring
     before reordering shaped clusters back to visual left-to-right order.
   - Updated `FontRenderer::shape_text(..., visual_rtl = true, pre_reversed = false)`
     and `draw_text()` to preserve the active `run_face` across join controls
     and same-RTL-script covered glyphs (so an Arabic run with a fallback font
     is not fractured at spaces covered by a Latin primary font), reverse
     contiguous RTL subruns across face transitions, and handle `visual_rtl` in
     the 8x8 bitmap fallback path.
   - Updated `CanvasRenderingContext2D::layout_text()` to compute shaped ink bounds
     (`min_x`, `max_x`, `max_y_up`, `max_y_down`) across `x_offset`/`y_offset`/
     `advance` and expand `layout.pad_x` (bounded at `1024.0f`), `natural_w`,
     `layout.baseline_px`, and `below_px` so cursive and mark overhangs never clip.

## Verified

- Release `make all test`: **278 / 278** unit suites + **114 / 114**
  cross-origin checks passing (including the six new Phase 119 suites plus
  HTML-Bidi vs logical `draw_text` pixel-identity for `"سلام"` using both a
  self-contained synthetic Arabic+Latin SFNT webfont `"TestArabicSans"` and
  host `DejaVu Sans` when installed):
  - `gsub_contextual_chaining_multiple_and_alternate_substitutions`
  - `arabic_joining_zwnj_zwj_lam_alef_and_mark_anchoring`
  - `gpos_cursive_attachment_ltr_rtl_and_contextual_pos`
  - `multiscript_itemization_visual_rtl_and_bidi_mixed_runs`
  - `canvas2d_and_html_paint_arabic_cursive_and_contextual_glyphs`
  - `complex_shaping_rejects_malformed_and_cyclic_tables_safely`
- Existing conformance suites (Release): `make conformance` (`1973/1973`),
  `make dom-conformance` (`678/678`), `make url-conformance` (`1188/1188`),
  `make js-conformance` (`1141/1158`), `make bidi-conformance` (`861948/861948`),
  and `make wpt` (`9567/9824`, `0` unlisted).
- Sanitizer (`-fsanitize=address,undefined`): `build-asan/test_suite` (**278 /
  278** suites, **0** sanitizer findings), `build-asan/cross_origin_test` (**114 /
  114**), and `tools/fuzz/fuzz_pipeline.py --binary build-asan/litebird` (**400 /
  400** runs, **0** findings).

## Still open

- Indic syllable clustering/reordering (`indic`/`use` engines), `LangSys`
  language-tag matching beyond `defaultLangSys`/`DFLT`, and
  `Device`/`VariationIndex` delta refinement remain open.
- CSS `direction` and `unicode-bidi` properties remain open on the layout side.
