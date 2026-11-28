# Finding 534 — OpenType Hangul Syllable, Jamo, and Tone-Mark Shaping

- **Severity**: Medium — OpenType Hangul shaping (`_hb_ot_shaper_hangul` in `hb-ot-shaper-hangul.cc` and `NORMALIZATION_MODE_NONE` in `hb-ot-shape-normalize.cc`)
- **Status**: Resolved in Phase 534 (`include/litebird/render/FontRenderer.hpp`, `src/render/FontRenderer.cpp`)
- **Affected Files**: `include/litebird/render/FontRenderer.hpp`, `src/render/FontRenderer.cpp`, `tools/shaping/hangul_diagnostic.py`, `tools/shaping/README.md`, `tests/suites/534-hangul-shaping.cpp`

## Problem

Differential testing against HarfBuzz 15.0.0 (`tools/shaping/hangul_diagnostic.py`) exposed four missing OpenType Hangul shaping stages in `FontRenderer`:

1. **Missing `'hang'` script itemization**:
   - `classify_codepoint_script` did not classify any of Unicode's 14 `Script=Hangul` blocks (`U+1100..U+11FF`, `U+302E..U+302F`, `U+3131..U+318E`, `U+3200..U+321E`, `U+3260..U+327E`, `U+A960..U+A97C`, `U+AC00..U+D7A3`, `U+D7B0..U+D7C6`, `U+D7CB..U+D7FB`, `U+FFA0..U+FFDC`), routing Hangul text to `kTagLatn`/`DFLT` instead of `'hang'`.
2. **No algorithmic Hangul syllable composition/decomposition or `ljmo`/`vjmo`/`tjmo` feature masks**:
   - NFD conjoining jamo `<L, V>` / `<L, V, T>` and partial `<LV, T>` sequences emitted uncomposed jamo glyphs instead of covered `U+AC00..U+D7A3` syllables, while missing precomposed syllables (`U+B2E4`, `U+B2E5`) emitted `.notdef` (`4` `.notdef` failures across `hangul_diagnostic.py`) and Old Hangul syllables (`U+1140 U+119E U+11EB`, `U+A960 U+D7B0 U+D7CB`) never activated `ljmo`/`vjmo`/`tjmo` `GSUB`+`GPOS` lookups (`3/24` default matches, `54/72` `ink.py` pixel diffs up to `2,613` px at `64 px`).
3. **Unconditional `calt` execution on conjoining jamo**:
   - `calt` ran globally on every glyph in both `GSUB` and `GPOS`, firing undesired `calt` substitutions/shifts on conjoining jamo (`U+1100`) even under `'DFLT'` (`2/8` matches on `--dflt-no-dotted-circle`).
4. **No `U+302E` / `U+302F` tone-mark reordering or `U+25CC` dotted-circle insertion**:
   - Spacing tone marks stayed after the syllable instead of rotating to `start`, and standalone/repeated/non-syllable tone marks omitted `U+25CC` (`1/12` matches and `33/36` `ink.py` diffs up to `3,516` px on `--zero-width-tones`).

## Root Cause & Fix

1. **`kTagHang` itemization and scoped `ljmo`/`vjmo`/`tjmo`/`calt` masks**:
   - Mapped all 14 `Script=Hangul` ranges to `kTagHang`, added `kGsubFeatLjmo` / `kGsubFeatVjmo` / `kGsubFeatTjmo` / `kGsubFeatCalt` to `GsubFeatureMask` and `GposScriptPlan::Entry`, enforced `feat_mask` across `GSUB` (`Ligature`, `Context`) and `GPOS` (`SinglePos`, `PairPos`, `ContextPos`) input slots, and cleared `kGsubFeatCalt` specifically on `is_hangul_l(u) || is_hangul_v(u) || is_hangul_t(u)` while keeping `calt` on precomposed syllables and compatibility jamo.
2. **`preprocess_text_hangul` + `NORMALIZATION_MODE_NONE` / `ZERO_WIDTH_MARKS_NONE`**:
   - Implemented HarfBuzz's exact `preprocess_text_hangul` before Round 1 (`<L,V(,T)>` and `<LV,T>` covered composition; `<LV>`/`<LVT>`/`<covered LV, T>` covered decomposition with `1..3` `ljmo`/`vjmo`/`tjmo` tags; spacing vs zero-width `U+302E`/`U+302F` rotation and `U+25CC` insertion), plus `always_short_circuit` in Round 1 and no recomposition in Round 3 on `kTagHang`.

## Verification

- **HarfBuzz 15.0.0 (`uharfbuzz` 0.57.0) & FreeType 2.13.2 (`ink.py`) (`tools/shaping/hangul_diagnostic.py`)**:
  - Default `'hang'` hybrid face: **3/24 -> 24/24** raw `match` & `same-placement` (`9/72 -> 72/72` `ink.py` `same-ink-and-advance`, `0` changed pixels).
  - `--zero-width-tones`: **1/12 -> 12/12** raw `match` & `same-placement` (`3/36 -> 36/36` `ink.py` `same-ink-and-advance`, `0` changed pixels).
  - `--dflt-no-dotted-circle`: **2/8 -> 8/8** raw `match` & `same-placement` (`6/24 -> 24/24` `ink.py` `same-ink-and-advance`, `0` changed pixels).
- **Engine Test Suite**: `tests/suites/534-hangul-shaping.cpp` (failed on unmodified `main` at line 156) + all `562` engine test suites pass cleanly; all shaping diagnostics pass under `-fsanitize=address,undefined`.
