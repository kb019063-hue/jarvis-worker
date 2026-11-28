# Phase 534 — OpenType Hangul Syllable, Jamo, and Tone-Mark Shaping

Resolves [Finding 534](../findings/534-hangul-shaping.md) under the OpenType shaping conformance track (`_hb_ot_shaper_hangul`).

## Summary

Through Phase 513, `TrueTypeFont::shape_glyphs` itemized neither `'hang'` (`Script=Hangul`) nor HarfBuzz's dedicated Hangul shaper (`hb-ot-shaper-hangul.cc`, `HB_OT_SHAPE_NORMALIZATION_MODE_NONE`, `HB_OT_SHAPE_ZERO_WIDTH_MARKS_NONE`):

1. **No `'hang'` script itemization (`kTagHang`)**:
   - All 14 Unicode `Script=Hangul` ranges (`U+1100..U+11FF`, `U+302E..U+302F`, `U+3131..U+318E`, `U+3200..U+321E`, `U+3260..U+327E`, `U+A960..U+A97C`, `U+AC00..U+D7A3`, `U+D7B0..U+D7C6`, `U+D7CB..U+D7FB`, `U+FFA0..U+FFBE`, `U+FFC2..U+FFC7`, `U+FFCA..U+FFCF`, `U+FFD2..U+FFD7`, `U+FFDA..U+FFDC`) fell back to `kTagCommon` -> `kTagLatn`, bypassing `'hang'` `GSUB`/`GPOS` script tables altogether.
2. **No algorithmic Hangul syllable composition/decomposition (`preprocess_text_hangul`) or per-jamo `ljmo`/`vjmo`/`tjmo` feature masks**:
   - Conjoining `<L, V>` / `<L, V, T>` sequences (`U+1100..U+1112`, `U+1161..U+1175`, `U+11A8..U+11C2`) and `<LV, T>` sequences did not compose to precomposed `U+AC00..U+D7A3` syllables when covered by `cmap`, and missing precomposed syllables, `<LV, OldT>`, or Old Hangul (`U+1100..U+11FF`, `U+A960..U+A97C`, `U+D7B0..U+D7C6`, `U+D7CB..U+D7FB`) sequences neither decomposed to `<L, V(, T)>` nor activated 1-glyph-scoped `ljmo` / `vjmo` / `tjmo` `GSUB` and `GPOS` lookups.
3. **Unconditional `calt` application on conjoining jamo**:
   - `calt` was merged into `kGsubFeatGlobal` in both `GSUB` and `GPOS`, whereas HarfBuzz 15.0.0 (`override_features_hangul` + `setup_masks_hangul`, `#4853`) disables `calt` specifically on conjoining jamo (`isL || isV || isT`) while keeping `calt` active on precomposed syllables (`U+AC00..U+D7A3`), compatibility jamo (`U+3131..U+318E`), spaces, and fillers (`U+3164`, `U+FFA0`).
4. **No Middle Korean `U+302E` / `U+302F` tone-mark reordering or `U+25CC` dotted-circle insertion**:
   - Spacing (`hmtx` advance > 0) Bangjeom tone marks (`U+302E`, `U+302F`) were not rotated to the start of a preceding valid Hangul syllable, and standalone, duplicate, or non-syllable-following tone marks did not insert `U+25CC` (`{tone, U+25CC}` when spacing, `{U+25CC, tone}` when zero-width).

Phase 534 implements all four components in `include/litebird/render/FontRenderer.hpp` and `src/render/FontRenderer.cpp`.

## Design & Implementation

### 1. `'hang'` Itemization and Scoped `ljmo`/`vjmo`/`tjmo`/`calt` Feature Masks (`FontRenderer.hpp`, `FontRenderer.cpp`)

- Added `kTagHang = ot_tag('h', 'a', 'n', 'g')` and mapped all 14 Unicode `Script=Hangul` ranges to `kTagHang` in `classify_codepoint_script`.
- Extended `GsubFeatureMask` with `kGsubFeatLjmo` (`1u << 5`), `kGsubFeatVjmo` (`1u << 6`), `kGsubFeatTjmo` (`1u << 7`), and `kGsubFeatCalt` (`1u << 8`), and added `uint32_t feature_mask` to `GposScriptPlan::Entry` so both `load_ligatures` (`GSUB`) and `load_gpos` (`GPOS`) bind `ljmo`/`vjmo`/`tjmo`/`calt` to their dedicated mask bits (OR-ing masks when multiple features share a lookup).
- Updated `apply_gsub_lookup_at` (`Ligature` and `Context` input slots) and `apply_gpos_lookup_at` (initial slot, `PairPos` second slot, and `ContextPos` input slots) to enforce `feat_mask` across all matched input positions on top-level lookups (`check_feat_mask = true`) while leaving recursive `Context` action dispatches unmasked (`check_feat_mask = false`).
- On `kTagHang` runs, per-slot masks start at `kGsubFeatGlobal | kGsubFeatCalt`, OR in `kGsubFeatLjmo` / `kGsubFeatVjmo` / `kGsubFeatTjmo` from `PreSlot::hangul_feat` (`1..3`), and clear `kGsubFeatCalt` iff `is_hangul_l(u) || is_hangul_v(u) || is_hangul_t(u)`.

### 2. `preprocess_text_hangul` Before Normalization (`FontRenderer.cpp`)

- On each `kTagHang` script slice `[rs, re)` prior to Round 1:
  - **Tone marks (`U+302E..U+302F`)**: Checks `is_zero_width_hangul_char` (`glyph_index(u) != 0 && advance == 0`). When immediately following a valid syllable span (`start < end && end == pre.size()`), appends `u` and rotates `u` to `pre[start]` if non-zero-width; otherwise inserts `U+25CC` when covered (`{u, 0x25CC}` if non-zero-width, `{0x25CC, u}` if zero-width) and resets `start = end = pre.size()`.
  - **`<L, V(, T)>` sequences**: Composes `is_combining_hangul_l(l) && is_combining_hangul_v(v) && (t == 0 || is_combining_hangul_t(t))` to `s = SBase + (l - LBase) * NCount + (v - VBase) * TCount + tindex` when `glyph_index(s) != 0` (`end = start + 1`); otherwise emits `L(ljmo)`, `V(vjmo)`, and optional `T(tjmo)` (`end = start + 2` or `3`), including Hangul fillers `U+115F` (`isL`) and `U+1160` (`isV`).
  - **Precomposed `<LV>` / `<LVT>` (`U+AC00..U+D7A3`)**: Composes `<LV, combining T>` to `LV + (T - TBase)` when covered; otherwise decomposes missing `<LV>`/`<LVT>` or `<covered LV, any T>` into `<L(ljmo), V(vjmo)(, T(tjmo))>` when component jamo are covered (consuming a trailing `isT` into `s_len = 3` only when `has_glyph(LV)` is true, matching `hb-ot-shaper-hangul.cc:291-297`).

### 3. `NORMALIZATION_MODE_NONE` and `ZERO_WIDTH_MARKS_NONE`

- Round 1 applies `always_short_circuit` (`shortest = true`, decomposing only when `glyph_index == 0`) across multi-codepoint clusters on `kTagHang`, Round 2 continues sorting non-zero `modified_combining_class` spans (ordering `U+302E` `CCC 224` before `U+0301` `CCC 230` and `U+0323` `CCC 220` before `U+0301`), Round 3 skips canonical pair recomposition on `kTagHang`, and `ZERO_WIDTH_MARKS_BY_GDEF_LATE` remains inactive on `kTagHang`.

## Differential & Suite Verification

### HarfBuzz 15.0.0 (`uharfbuzz` 0.57.0) & `ink.py` (`FreeType 2.13.2`) Before vs After (`tools/shaping/hangul_diagnostic.py`)

| Synthetic OpenType Hangul Face | Mode (`cases`) | Raw `match` / `same-placement` (Before -> After) | `ink.py` `16/32/64 px` `same-ink-and-advance` (Before -> After) |
|---|---|---|---|
| Hybrid `'hang'` (`GSUB` + `GPOS` + `GDEF`, spacing tones) | default (`24`) | `3/24` (`3` `.notdef`) -> **`24/24`** | `9/72` (max `2,613` px diff) -> **`72/72` (`0` px diff)** |
| Hybrid `'hang'` (zero-width `GDEF` class-3 tones + `MarkToBase`) | `--zero-width-tones` (`12`) | `1/12` (`2/12` placement) -> **`12/12`** | `3/36` (max `3,516` px diff) -> **`36/36` (`0` px diff)** |
| `'DFLT'`-only face without `U+25CC` | `--dflt-no-dotted-circle` (`8`) | `2/8` (`1` `.notdef`) -> **`8/8`** | `6/24` (max `1,893` px diff) -> **`24/24` (`0` px diff)** |

- Zero regressions across all existing shaping diagnostics (`compare.py`, `normalization_diagnostic.py`, `arabic_marks_diagnostic.py`, `hebrew_diagnostic.py`, `latin_diagnostic.py`, `decomposition_diagnostic.py`, `mark_order_diagnostic.py`, `leading_marks_diagnostic.py`, `supported_composite_diagnostic.py`, `thai_lao_diagnostic.py`) under `-fsanitize=address,undefined`.
- `tests/suites/534-hangul-shaping.cpp` (failed on unmodified `main` at line 156) and all `562` engine test suites pass cleanly.

## Remaining Limits

- Indic (`kTagDev2`) two-part matra decomposition and Indic/USE syllable reordering remain deferred.
- Public source-cluster indices and GPOS-less geometric bounding-box fallback mark positioning (`_hb_ot_shape_fallback_mark_position`) remain separate.
