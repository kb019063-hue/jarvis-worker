# Finding 513 — Thai and Lao OpenType `SARA AM` and Legacy PUA Shaping

- **Severity**: Medium — OpenType Thai and Lao shaping (`_hb_ot_shaper_thai` in `hb-ot-shaper-thai.cc` and GDEF-less/GPOS-less mark zeroing in `hb-ot-shape.cc`)
- **Status**: Resolved in Phase 513 (`src/render/FontRenderer.cpp`)
- **Affected Files**: `src/render/FontRenderer.cpp`, `tools/shaping/thai_lao_diagnostic.py`, `tools/shaping/README.md`, `tests/suites/513-thai-lao-shaping.cpp`

## Problem

Differential testing against HarfBuzz 12.1.0 (`tools/shaping/thai_lao_diagnostic.py`) exposed three missing `_hb_ot_shaper_thai` shaping stages in `src/render/FontRenderer.cpp`:

1. **No `preprocess_text_thai` (`SARA AM` `U+0E33` / `U+0EB3` decomposition and `NIKHAHIT` reordering)**:
   - `U+0E33` (`THAI CHARACTER SARA AM`) and `U+0EB3` (`LAO VOWEL SIGN AM`) carry Unicode compatibility (`<compat>`) decompositions rather than canonical decompositions, so general OpenType normalization left them unexpanded.
   - On `DejaVuSans.ttf`, `DejaVuSans-Bold.ttf`, `DejaVuSansMono.ttf`, and `DejaVuSansMono-Bold.ttf`, `18/20` default Lao probes (`22/24` with `--extended`) mismatched HarfBuzz (`2/20` and `2/24` matched — only the two negative controls without `U+0EB3`). On `ink.py` (`16`, `32`, `64` px), `51/60` (`63/72` `--extended`) `DejaVuSans.ttf` pairs and `54/60` `DejaVuSansMono.ttf` / `DejaVuSansMono-Bold.ttf` pairs rendered different pixels (up to `163` changed pixels on `DejaVuSans.ttf` and `573` on `DejaVuSansMono.ttf` at `64 px` on `ນ້ຳ ຄຳ`), because `U+0EB3` painted after the tone mark instead of decomposing to `<U+0ECD, tone, U+0EB2>` and anchoring `U+0ECD` + tone onto the consonant base.
2. **No legacy Thai PUA fallback shaping (`do_thai_pua_shaping`)**:
   - Thai fonts lacking an explicit `'thai'` `GSUB` script record did not run HarfBuzz's `T0..T3` above-state and `B0..B2` below-state machines (`SD`, `SDL`, `SL`, `RD` over Windows `U+F700..U+F71A` and Macintosh `U+F884..U+F89E` PUA slots), failing `16/16` probes (`0/16` match, `0/48` `ink.py` pairs) on both synthetic Windows and Macintosh Thai PUA faces.
3. **No GDEF-less `Mn` mark synthesis or GPOS-less offset compensation on `kTagThai` / `kTagLao`**:
   - When a Thai/Lao face lacked `GDEF` `GlyphClassDef`, `ZERO_WIDTH_MARKS_BY_GDEF_LATE` left non-zero `hmtx` mark widths unzeroed; when no `GPOS` table existed, zeroing Thai/Lao mark widths did not subtract `x_advance` / `y_advance` from `x_offset` / `y_offset`.

## Root Cause & Fix

1. **`preprocess_text_thai` before `_hb_ot_shape_normalize`**:
   - Decomposed `U+0E33` / `U+0EB3` (`(u & ~0x0080u) == 0x0E33u`) into `NIKHAHIT` (`U+0E4D` / `U+0ECD`) + `SARA AA` (`U+0E32` / `U+0EB2`) per `thai`/`lao ` run and rotated `NIKHAHIT` backwards across contiguous preceding `is_thai_lao_above_base_mark` slots (`(cp & ~0x0080u)` in `0x0E31`, `0x0E34..0x0E37`, `0x0E3B`, `0x0E47..0x0E4E`) before Round 1 normalization.
2. **`do_thai_pua_shaping` with `PreSlot` `prop_cp` retention**:
   - Implemented `has_thai_gsub_script()` (inspecting both `m_gsub_scripts` and raw `GSUB` `ScriptList` records so even an empty `'thai'` script entry suppresses PUA fallback while `'DFLT'`-only `GSUB` still runs it) and HarfBuzz's exact `kThaiAboveState[4][3]` / `kThaiBelowState[3][3]` tables (`Windows` `U+F700..U+F71A`, `Macintosh` `U+F884..U+F89E`), retaining each slot's pre-PUA `prop_cp` through Rounds 1–3 so PUA-remapped marks still sort by their original `modified_combining_class`.
3. **Thai/Lao GDEF-less `kSynthesizedMarkBit` and GPOS-less `adjust_offsets`**:
   - Synthesized `kSynthesizedMarkBit` on `kTagThai` / `kTagLao` non-default-ignorable `Mn` marks (including `U+0ECE` `LAO YAMAKKAN`) when `!m_has_gdef_glyph_class`, preserved/cleared across `GSUB`, and compensated `x_offset -= x_advance; y_offset -= y_advance` when zeroing Thai/Lao marks on faces without `GPOS`.

## Verification

- **HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) & FreeType 2.13.2 (`ink.py`) (`tools/shaping/thai_lao_diagnostic.py`)**:
  - `DejaVuSans.ttf` (`abdc775b...`): **2/20 -> 20/20** default (`9/60 -> 60/60` `ink.py`), **2/24 -> 24/24** `--extended` (`9/72 -> 72/72` `ink.py`).
  - `DejaVuSans-Bold.ttf` (`0d977336...`): **2/20 -> 20/20** default (`54/60 -> 60/60` `ink.py`), **2/24 -> 24/24** `--extended` (`57/72 -> 72/72` `ink.py`).
  - `DejaVuSansMono.ttf` (`0f5db4f1...`) & `DejaVuSansMono-Bold.ttf` (`2964f6da...`): **2/20 -> 20/20** default (`6/60 -> 60/60` `ink.py`).
  - `--synthetic-thai-pua` & `--synthetic-thai-mac-pua`: **0/16 -> 16/16** (`0/48 -> 48/48` `ink.py`), `0` changed pixels across 16/32/64 px.
- **Engine Test Suite**: `tests/suites/513-thai-lao-shaping.cpp` (failed on unmodified `main` at line 61) + full engine test suite pass under both standard `-O2` and `-fsanitize=address,undefined`.
