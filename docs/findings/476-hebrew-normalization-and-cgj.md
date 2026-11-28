# Finding 476 — Hebrew Normalization, Cantillation/Meteg Ordering, Presentation-Form Fallback, and CGJ

- **Severity**: Medium — OpenType Hebrew (`hebr`) normalization, mark ordering, fallback synthesis, and `U+034F` `CGJ` / default-ignorable shaping conformance
- **Status**: Resolved in Phase 476 (`src/render/FontRenderer.cpp`)
- **Affected Files**: `src/render/FontRenderer.cpp`, `tools/shaping/hebrew_diagnostic.py`, `tools/shaping/README.md`, `tests/test_suite.cpp`, `tests/suites/266-latin-mark-order.cpp`, `tests/suites/279-leading-marks.cpp`, `tests/suites/467-rtl-mark-order-and-origins.cpp`, `tests/suites/476-hebrew-normalization-and-cgj.cpp`

## Problem

After Phase 200 (`docs/phases/200-hebrew-mark-order.md`, finding 204) sorted contiguous fixed-position Hebrew points (`CCC 10..25`), differential testing against HarfBuzz 12.1.0 (`9/24` raw `match`, `16/24` `same-placement` on `tools/shaping/hebrew_diagnostic.py --extended` with `DejaVuSans.ttf`) exposed five remaining defects in `src/render/FontRenderer.cpp`:

1. **No canonical decomposition of Hebrew presentation forms (`U+FB1D`, `U+FB1F`, `U+FB2A..U+FB4E`)**:
   - Pre-`GSUB` decomposition in `TrueTypeFont::shape_glyphs` gated starter expansion on `kTagLatn` and `kTagArab` only. On `kTagHebr`, missing presentation forms mapped to `.notdef`, and mark-followed presentation forms (such as `U+FB30 ALEF WITH MAPIQ + U+05B8 QAMATS` or `U+FB2C SHIN WITH DAGESH AND SHIN DOT + U+05B8`) stayed as single presentation-form glyphs lacking `MarkToBase` base anchors in `DejaVuSans.ttf`, leaving following marks unpositioned (`x_offset = 0, y_offset = 0` instead of `(79, 0)` / `(-54, -420)`).

2. **Broken mark ordering across `U+FB1E`, cantillation (`U+0591..U+05AF`), and non-Hebrew combining marks**:
   - The Hebrew mark pass only accepted `CCC 10..25`, omitting `U+FB1E JUDEO-SPANISH VARIKA` (`CCC 26`) and terminating mark spans at cantillation marks (`CCC 220`, `222`, `228`, `230`, `232`), `U+05C4`/`U+05C5` (`CCC 230`/`220`), or general combining marks (`U+0301`, `U+0323`), so fixed-position points appearing after an accent or general combining mark failed to sort before it (`\u05e9\u0301\u05b8\u0323\u05c1\u05bc`).

3. **Missing `reorder_marks_hebrew` `meteg` swap**:
   - In `[patah/qamats (mod 20/21), sheva/hiriq (mod 22/23), meteg/CCC 220 (mod 25/220)]`, HarfBuzz swaps the second vowel and `meteg` (`hb-ot-shaper-hebrew.cc:154-182`) so `meteg` stands visually between the two below-base vowels (`לִַֽ`). `FontRenderer` left `meteg` after `sheva`/`hiriq`.

4. **Missing `gpos_tag = 'hebr'`, `compose_hebrew` fallback, and `hebr` late `GDEF` mark zeroing**:
   - `find_gpos_script(kTagHebr)` fell back to `DFLT` `GPOS` when `hebr` was absent, whereas HarfBuzz sets `gpos_tag = HB_TAG('h','e','b','r')` (`disable_gpos = true` unless `chosen_script[1] == 'hebr'`) and synthesizes `U+FB1D`, `U+FB1F`, and `U+FB2A..U+FB4E` via `compose_hebrew` whenever `!has_gpos_mark`. In addition, Stage 4 only zeroed `GDEF` class-3 mark advances for `kTagArab`, omitting `kTagHebr` (`ZERO_WIDTH_MARKS_BY_GDEF_LATE`).

5. **Visible `.notdef` / unskipped `U+034F` Combining Grapheme Joiner (`CGJ`) and default-ignorables**:
   - `U+034F` (`CCC = 0`, category `Mn`) was mapped directly through `cmap` (producing glyph `0` `.notdef` on fonts without a `U+034F` cmap entry, or a visible glyph with nonzero advance on synthetic faces), blocked `MarkToBase` attachment from reaching the preceding base, and failed to trigger pre-mark starter decomposition (`\ufb2a\u034f\u05b8`, `\u00e9\u034f\u0323`).

## Root Cause & Fix

1. **`hebr` `GPOS` script guard & `has_hebrew_gpos_mark`**:
   - Added `bool has_mark = false` to `GposScriptPlan` (populated during `parse_gpos` when any active lookup has `GposLookup::Feature::Mark`) and updated `find_gpos_script` to return `nullptr` for `kTagHebr` when no explicit `hebr` script record exists.
2. **Covered canonical decomposition & `compose_hebrew_fallback`**:
   - Enabled covered recursive canonical decomposition on `kTagHebr` when `!has_glyph(cp)` or `before_mark` (`unicode_ccc(next) != 0 || next == 0x034F`), with 1-step `FB2A`/`FB2B + 05BC` fallback for `FB2C`/`FB2D`, while preserving compatibility ligature `U+FB4F`.
   - Skipped Round 3 recomposition on `kTagHebr` when `has_hebrew_gpos_mark` is true, and implemented `compose_hebrew_fallback` (`FB1D`, `FB1F`, `FB2A..FB4E`) when `!has_hebrew_gpos_mark`.
   - Enabled `ZERO_WIDTH_MARKS_BY_GDEF_LATE` for `kTagHebr` alongside `kTagArab`.
3. **Unified `hebrew_modified_class` & `reorder_marks_hebrew`**:
   - Mapped `CCC 10..26` through `{22, 15, 16, 17, 23, 18, 19, 20, 21, 14, 24, 12, 25, 13, 10, 11, 26}`, sorted entire non-zero-`CCC` spans in `kTagHebr` runs across cantillation and non-Hebrew marks, and applied HarfBuzz's `reorder_marks_hebrew` `[20/21, 22/23, 25/220]` swap.
4. **Default-ignorable hiding & `CGJ` `GSUB`/`GPOS` rules**:
   - Added `is_default_ignorable(cp)`, `kDefaultIgnorableBit`, and `kSkipIgnorableBit` on `ShapingGlyph::feature_mask`, keeping `CGJ` blocking in `GSUB` iff `i == 0`, `i + 1 == count`, or `0 < cc(i + 1) < cc(i - 1)`, skipping all default-ignorables in `GPOS` and `kern`, and replacing default-ignorables with the zero-advance `' '` glyph (or dropping them if `' '` is absent).

## Verification

- **HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) Differential Suites (`build/shaping-probe`)**:
  - `DejaVuSans.ttf` (`abdc775b...`) & `DejaVuSans-Bold.ttf` (`0d977336...`):
    - `hebrew_diagnostic.py --extended`: **9/24 -> 24/24** exact raw `match`, **16/24 -> 24/24** `same-placement` (`exit 0`).
    - `hebrew_diagnostic.py` (`5/5`) and `--permutations` (`12/12`): `100%` exact raw `match` (`exit 0`).
    - `ink.py` (`FreeType 2.13.2`, sizes `16`, `32`, `64` px) on `hebrew_diagnostic.py --extended`: **72/72** `same-ink-and-advance` (`0` changed pixels).
  - Zero regressions across `compare.py` (`13/13` Sans/Bold, `11/11` Mono/Mono-Bold, `7/7` Serif), `arabic_marks_diagnostic.py` (`23/23`, `16/16`, `486/486` Sans; `23/23`, `16/16`, `384/384` Mono), and all four Latin diagnostics (`5/5`, `8/8`, `8/8`, `8/8`), plus `make shaping-oracle-self-test`, `test_ink.py`, and `test_outlines.py`.
- **Engine Test Suite**: `tests/suites/476-hebrew-normalization-and-cgj.cpp` + all `523` engine test suites pass.
