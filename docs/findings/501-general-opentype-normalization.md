# Finding 501 — General OpenType Normalization and Alternate Partial Decomposition

- **Severity**: Medium — OpenType shaping normalization (`_hb_ot_shape_normalize`) across `Grek`, `Cyrl`, `Kana`, `Thai`, `Lao`, `Common`, decomposable combining marks, and partial canonical decomposition
- **Status**: Resolved in Phase 501 (`src/render/FontRenderer.cpp`)
- **Affected Files**: `src/render/FontRenderer.cpp`, `tools/shaping/normalization_diagnostic.py`, `tools/shaping/README.md`, `tests/suites/266-latin-mark-order.cpp`, `tests/suites/279-leading-marks.cpp`, `tests/suites/501-general-opentype-normalization.cpp`

## Problem

Differential testing against HarfBuzz 12.1.0 (`tools/shaping/normalization_diagnostic.py`: `5/30` match on `DejaVuSans.ttf`, `DejaVuSans-Bold.ttf`, and `DejaVuSerif.ttf`; `4/30` match on `DejaVuSansMono.ttf`, with `8` `.notdef` failures on `DejaVuSerif.ttf` and `DejaVuSansMono.ttf`) exposed five normalization gaps in `src/render/FontRenderer.cpp`:

1. **Script gate restricted normalization to `latn`, `arab`, and `hebr`**:
   - Pre-`GSUB` decomposition, mark-span ordering, and starter recomposition ignored `grek`, `cyrl`, `kana`, `thai`, `lao `, `armn`, `geor`, `syrc`, `thaa`, and `nko `. Decomposed Greek polytonic (`\u03b1\u0313\u0301`), Cyrillic (`\u0435\u0308`), Kana (`\u304b\u3099`), and out-of-order Lao/Thai marks failed to match precomposed/canonical equivalents, and `DejaVuSerif.ttf` emitted `.notdef` (`gid 0`) on `\u03b1\u0345\u0313` and `\u1f04\u0345` because `U+0345` is absent while `U+1F80` (`ᾀ`) and `U+1F84` (`ᾄ`) are present.
2. **Covered decomposable marks (`U+0340`, `U+0341`, `U+0343`, `U+0344`) were not decomposed in multi-codepoint clusters**:
   - Round 1 only decomposed codepoints when `before_mark` or `glyph_index(cp) == 0`, leaving trailing decomposable marks unexpanded (`"a\u0341"` stayed `[a, 0341]` instead of `á`; `"\u03b9\u0344"` stayed `[ι, 0344]` instead of `ΐ`; `"\u0341\u0323"` failed to decompose `0341 -> 0301`).
3. **Leaf-only canonical expansion failed when intermediate composites were covered**:
   - `expand` walked `kCanonDecomp`'s full leaf sequence directly instead of HarfBuzz's 1-step binary `(a, b)` recursion (`hb-ot-shape-normalize.cc:97-137`). When a leaf mark (such as `U+030A`, `U+0313`, or `U+0308`) was missing from a font while an intermediate composite (`U+00E5`, `U+1F04`, `U+00C5`, `U+03AC`, `U+0390`) was covered, both standalone `shortest = true` and cluster `shortest = false` expansion failed instead of emitting the covered partial decomposition `[a]`/`[a, b]`.
4. **`Common` starters never recomposed in Round 3**:
   - Round 3 derived `starter_script` from `classify_codepoint_script(input[i])`, which returned `kTagCommon` (`0`) for `'='`, `'<'`, and `'←'`, preventing `'=\u0338' -> '≠'`, `'<\u0338' -> '≮'`, and `'\u2190\u0338' -> '↚'`.
5. **Incomplete `modified_combining_class`, script classification, and Thai/Lao `ZERO_WIDTH_MARKS_BY_GDEF_LATE`**:
   - Thai `U+0E38`/`U+0E39` (`CCC 103`) used raw `CCC 103` instead of modified `3` (sorting after `U+0E3A` `PHINTHU`, `CCC 9`); `U+3099..U+309A` (`Inherited`) and `kTagKana` were unclassified; `U+FEFF` was included in `0xFE70..0xFEFF` (`kTagArab`); and `DejaVuSansMono.ttf` left `1233`-unit `hmtx` advances on Lao marks (`kTagLao` lacked `ZERO_WIDTH_MARKS_BY_GDEF_LATE`).

## Root Cause & Fix

1. **`decompose_one_step` & `decompose_codepoint`**:
   - Reconstructed exact Unicode 17.0 1-step binary `(a, b)` across all `2,081` `kCanonDecomp` entries via `kComposePairs` prefix folding plus the `29` `CompositionExclusions` overrides (`18` singletons, `2` Hebrew `FB2C`/`FB2D`, `9` Musical Symbol notes), and implemented HarfBuzz's `shortest` short-circuit and `has_a` partial fallback on recursion failure.
2. **Per-run cluster normalization (`script != kTagDev2`)**:
   - Scanned clusters `[i, end)` per itemized script run, skipping variation-selector clusters, using `shortest = true` on missing 1-char clusters, and decomposing all members of `>= 2`-char clusters with `shortest = false` while preserving each run's script tag.
3. **Unified `modified_combining_class`, `Common` starter composition, and Thai/Lao `GDEF` mark zeroing**:
   - Added Thai/Telugu/Tibetan/Tai Tham modified combining classes, tracked `starter_script` from `input_scripts[i]` in Round 3, refined `classify_codepoint_script` (`Inherited` `0485..0486` / `0951..0954` / `3099..309A`, `kTagArmn`, `kTagGeor`, `kTagKana`, `Latn` IPA/Phonetic/`212A..212B`, `0xFE70..0xFEFE`), and added `kTagThai`/`kTagLao` to `ZERO_WIDTH_MARKS_BY_GDEF_LATE`.

## Verification

- **HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) Differential Oracle (`tools/shaping/normalization_diagnostic.py`)**:
  - `DejaVuSans.ttf` (`abdc775b...`): **5/30 -> 30/30** (`--with-lao`: **6/32 -> 32/32**) exact raw `match` & `same-placement`.
  - `DejaVuSans-Bold.ttf` (`0d977336...`): **5/30 -> 30/30** exact raw `match` & `same-placement`.
  - `DejaVuSerif.ttf` (`13e61509...`): **5/30 -> 30/30** (`8` `.notdef` unsupported -> `0`) exact raw `match` & `same-placement`.
  - `DejaVuSansMono.ttf` (`0f5db4f1...`): **4/30 -> 30/30** (`--with-lao`: **4/32 -> 32/32**, `8` `.notdef` unsupported -> `0`) exact raw `match` & `same-placement`.
  - `ink.py` (`FreeType 2.13.2`, sizes `16`, `32`, `64` px): **100%** `same-ink-and-advance` (`0` changed pixels) across all faces; zero regressions across all existing shaping diagnostics.
- **Engine Test Suite**: `tests/suites/501-general-opentype-normalization.cpp` (failed on unmodified `main` at line 53) + all `536` engine test suites pass.
