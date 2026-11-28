# Phase 513 — Thai and Lao OpenType `SARA AM` and Legacy PUA Shaping

Resolves [Finding 513](../findings/513-thai-lao-shaping.md) under the OpenType shaping conformance track (`_hb_ot_shaper_thai`).

## Summary

Phase 501 generalized `_hb_ot_shape_normalize` (`modified_combining_class` `103 -> 3` for Thai `U+0E38`/`U+0E39`, Lao mark sorting `118 < 122`, and `GDEF`-backed `ZERO_WIDTH_MARKS_BY_GDEF_LATE` on `kTagThai`/`kTagLao`), but `TrueTypeFont::shape_glyphs` still lacked HarfBuzz's dedicated Thai/Lao shaper (`hb-ot-shaper-thai.cc`) and `hb-ot-shape.cc`'s GDEF-less/GPOS-less mark handling:

1. **Missing `preprocess_text_thai` (`SARA AM` `U+0E33` / `U+0EB3` decomposition and above-mark reordering)**:
   - Because `U+0E33` (`THAI CHARACTER SARA AM`) and `U+0EB3` (`LAO VOWEL SIGN AM`) carry Unicode `<compat>` decompositions rather than canonical decompositions, `_hb_ot_shape_normalize` leaves them untouched; `hb-ot-shaper-thai.cc` decomposes them before normalization into `NIKHAHIT` (`U+0E4D` / `U+0ECD`) + `SARA AA` (`U+0E32` / `U+0EB2`) and moves the `SARA AM`-derived `NIKHAHIT` backwards across contiguous preceding above-base marks (`(u & ~0x0080)` in `0x0E31`, `0x0E34..0x0E37`, `0x0E3B`, `0x0E47..0x0E4E`).
   - Without `preprocess_text_thai`, everyday Thai and Lao words such as `ນ້ຳ` (`<0x0E99, 0x0EC9, 0x0EB3>`, "water") and `ກຳ` (`<0x0E81, 0x0EB3>`) emitted the unshaped compatibility glyph `U+0EB3` *after* the tone mark, failing `GPOS` `MarkToBase` / `MarkToMark` anchoring and colliding visually (`51/60` `DejaVuSans.ttf` and `54/60` `DejaVuSansMono.ttf` `ink.py` case-size pairs differed, up to `573` changed pixels at `64 px`).
2. **Missing legacy Thai PUA fallback shaping (`do_thai_pua_shaping`)**:
   - Legacy Thai TrueType fonts without an explicit `'thai'` `GSUB` script encode shifted mark variants (`SD`, `SL`, `SDL`) and descenderless consonant bases (`RD` for `U+0E0D` `YO YING` and `U+0E10` `THO THAN`) in Windows (`U+F700..U+F71A`) or Macintosh (`U+F884..U+F89E`) Private Use Area slots.
3. **Missing GDEF-less `Mn` mark synthesis and GPOS-less offset compensation on `kTagThai` / `kTagLao`**:
   - Faces without `GDEF` `GlyphClassDef` (including legacy Thai PUA faces and standalone webfonts) left non-zero `hmtx` mark advances unzeroed, and faces without a `GPOS` table omitted HarfBuzz's forward-direction `x_offset -= x_advance; y_offset -= y_advance` mark compensation (`adjust_mark_positioning_when_zeroing`).

Phase 513 implements all three pieces internal to `src/render/FontRenderer.cpp`.

## Design & Implementation

### 1. `preprocess_text_thai` and `PreSlot` Property Retention (`src/render/FontRenderer.cpp`)

- Before Round 1 of `_hb_ot_shape_normalize`, `shape_glyphs` builds `std::vector<PreSlot>` (`{cp, prop_cp, script}`) per itemized script run.
- On `kTagThai` and `kTagLao` runs, any codepoint matching `((u & ~0x0080u) == 0x0E33u)` (`U+0E33` or `U+0EB3`) expands into `nikhahit = u - 0x0E33u + 0x0E4Du` (`U+0E4D` or `U+0ECD`) followed by `sara_aa = u - 1u` (`U+0E32` or `U+0EB2`), then rotates `nikhahit` backwards across all contiguous preceding slots in the run whose `prop_cp` satisfies `is_thai_lao_above_base_mark` (`(cp & ~0x0080u)` in `0x0E31`, `0x0E34..0x0E37`, `0x0E3B`, `0x0E47..0x0E4E`).
- Because `preprocess_text_thai` executes *before* `_hb_ot_shape_normalize` mark sorting:
  - Non-above-base marks such as Lao semivowel `U+0EBC` (`ຫຼ້ຳ`) or below vowel `U+0EB8` stop `nikhahit` during preprocessing;
  - In `<0x0E81, 0x0EC9, 0x0EB8, 0x0EB3>`, `0x0EB8` stops `0x0ECD` from crossing `0x0EC9`, after which Round 2 sorts `0x0EB8` (`CCC 118`) before `0x0EC9` (`CCC 122`), yielding `<0x0E81, 0x0EB8, 0x0EC9, 0x0ECD, 0x0EB2>`, matching HarfBuzz 12.1.0 exactly.
- Literal `U+0E4D` / `U+0ECD` in the input does not trigger `SARA AM` rotation (`<0x0E81, 0x0EC9, 0x0ECD, 0x0EB2>` stays `<0x0E81, 0x0EC9, 0x0ECD, 0x0EB2>`).

### 2. Legacy Thai PUA State Machine (`do_thai_pua_shaping`)

- `has_thai_gsub_script()` checks `m_gsub_scripts` and inspects `GSUB`'s raw `ScriptList` records so even an empty `'thai'` `GSUB` script entry suppresses PUA fallback (matching `!plan->map.found_script[0]`), whereas a `GSUB` table containing only `'DFLT'` still runs `do_thai_pua_shaping`.
- When `script == kTagThai && !has_thai_gsub_script()`, `shape_glyphs` steps HarfBuzz's exact `4 x 3` above-state (`T0..T3`, initial `NotConsonant -> T3`) and `3 x 3` below-state (`B0..B2`, initial `NotConsonant -> B2`) tables across `NC`, `AC` (`0x0E1B`, `0x0E1D`, `0x0E1F`), `RC` (`0x0E0D`, `0x0E10`), `DC` (`0x0E0E`, `0x0E0F`), and `AV` / `BV` / `T` marks:
  - `RD` updates `pre[base].cp` (`0x0E0D -> 0xF70F` / `0xF89A`, `0x0E10 -> 0xF700` / `0xF89E`) when covered in `cmap`;
  - `SD`, `SDL`, and `SL` update `pre[idx].cp` preferring Windows PUA (`U+F700..U+F71A`) and falling back to Macintosh PUA (`U+F884..U+F89E`), while keeping `pre[idx].prop_cp` set to the original Thai mark codepoint so Round 2 still orders out-of-order PUA-remapped marks (`<0x0E01, 0x0E48, 0x0E38> -> [0x0E01, 0x0E38, 0xF70A]`, `<0x0E0E, 0x0E3A, 0x0E38> -> [0x0E0E, 0xF718, 0xF71A]`).

### 3. GDEF-Less `Mn` Synthesis and GPOS-Less Offset Adjustment

- When `!m_has_gdef_glyph_class` on `kTagThai` or `kTagLao` runs (leaving `kTagArab` and `kTagLatn` untouched, preserving `tests/suites/432-arabic-mark-advances.cpp`), slots whose `prop_cp` is a non-default-ignorable `Mn` mark (Thai `0x0E31`, `0x0E34..0x0E3A`, `0x0E47..0x0E4E`; Lao `0x0EB1`, `0x0EB4..0x0EBC`, `0x0EC8..0x0ECE` including Unicode 15.0 `LAO YAMAKKAN`; plus general combining `Mn` ranges) carry `kSynthesizedMarkBit` (`1u << 29`) on `ShapingGlyph::feature_mask`, cleared on multi-glyph `MultipleSubst` expansions and non-mark `LigatureSubst` results.
- In `ZERO_WIDTH_MARKS_BY_GDEF_LATE`, Thai/Lao marks (from `GDEF` class 3 or `kSynthesizedMarkBit`) subtract `x_advance` / `y_advance` from `x_offset` / `y_offset` when `!has_gpos_table()` before zeroing `x_advance` and `y_advance`.

## Differential & Suite Verification

### HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) & `ink.py` (`FreeType 2.13.2`) Before vs After (`tools/shaping/thai_lao_diagnostic.py`)

| Face (`SHA-256` prefix) | Mode (`cases`) | Raw `match` / `same-placement` (Before -> After) | `ink.py` `16/32/64 px` `same-ink-and-advance` (Before -> After) |
|---|---|---|---|
| `DejaVuSans.ttf` (`abdc775b...`) | default (`20`) / `--extended` (`24`) | `2/20` (`2/24`) -> **`20/20` (`24/24`)** | `9/60` (`9/72`) -> **`60/60` (`72/72`)** |
| `DejaVuSans-Bold.ttf` (`0d977336...`) | default (`20`) / `--extended` (`24`) | `2/20` (`2/24`) -> **`20/20` (`24/24`)** | `54/60` (`57/72`) -> **`60/60` (`72/72`)** |
| `DejaVuSansMono.ttf` (`0f5db4f1...`) | default (`20`) | `2/20` -> **`20/20`** | `6/60` -> **`60/60`** |
| `DejaVuSansMono-Bold.ttf` (`2964f6da...`) | default (`20`) | `2/20` -> **`20/20`** | `6/60` -> **`60/60`** |
| Synthetic Thai Windows PUA | `--synthetic-thai-pua` (`16`) | `0/16` -> **`16/16`** | `0/48` -> **`48/48`** |
| Synthetic Thai Macintosh PUA | `--synthetic-thai-mac-pua` (`16`) | `0/16` -> **`16/16`** | `0/48` -> **`48/48`** |

- Zero regressions across `compare.py`, `hebrew_diagnostic.py`, `arabic_marks_diagnostic.py`, and `normalization_diagnostic.py`.
- `tests/suites/513-thai-lao-shaping.cpp` (failed on unmodified `main` at line 61) covers Lao `SARA AM` + all four tones + `U+0ECE` + `U+0EBC` + `U+0EB8` ordering interactions, Windows & Macintosh Thai PUA `SD`/`SDL`/`SL`/`RD` + post-PUA mark reordering + `GSUB(DFLT)` vs `GSUB(thai)` gating + `GPOS` offset-adjustment gating, host `DejaVu Sans` / `DejaVu Sans Mono` parity, and end-to-end HTML `BitmapRenderer` pixel equivalence (`&#x0e99;&#x0ec9;&#x0eb3;` == `&#x0e99;&#x0ecd;&#x0ec9;&#x0eb2;`).

## Remaining Limits

- Indic (`kTagDev2`) two-part matra decomposition and Indic/USE syllable reordering remain deferred.
- Algorithmic Hangul (`LV`/`LVT` <-> `L + V (+ T)`) shaping and public source-cluster indices remain separate.
