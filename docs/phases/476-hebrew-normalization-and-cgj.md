# Phase 476 — Hebrew Normalization, Cantillation/Meteg Ordering, Presentation-Form Fallback, and CGJ

Resolves [Finding 476](../findings/476-hebrew-normalization-and-cgj.md) under the shaping conformance track (`AGENTS.md`: *"Hebrew fixed-position points are ordered before shaping (Phase 200, finding 204)"*).

## Summary

Phase 200 (`docs/phases/200-hebrew-mark-order.md`) ordered contiguous spans of fixed-position Hebrew points (`CCC 10..25`), repairing the `U+05E9 U+05B8 U+05C1` (`SHIN + QAMATS + SHIN DOT`) placement mismatch. Five gaps remained between `TrueTypeFont::shape_glyphs` and HarfBuzz 12.1.0 (`hb-ot-shaper-hebrew.cc`, `hb-ot-shape-normalize.cc`, `hb-ot-shape.cc`, `hb-ot-layout-gsubgpos.hh`):

1. **Hebrew canonical decomposition & GPOS-mark-aware recomposition**: missing or mark-followed Hebrew alphabetic presentation forms (`U+FB1D`, `U+FB1F`, `U+FB2A..U+FB4E`) were never decomposed on `hebr` runs, stranding marks on unanchored presentation-form glyphs (e.g. `U+FB30 + U+05B8`, `U+FB2C + U+05B8`).
2. **Ordering across `U+FB1E`, cantillation (`U+0591..U+05AF`), and non-Hebrew combining marks**: the Hebrew mark pass broke spans at any mark outside `CCC 10..25`, failing to sort `U+FB1E` (`CCC 26`), cantillation marks (`CCC 220`, `222`, `228`, `230`, `232`), and non-Hebrew `CCC 220`/`230` marks together with fixed-position points.
3. **Special `meteg` reordering (`reorder_marks_hebrew`)**: `[patah/qamats (mod 20/21), sheva/hiriq (mod 22/23), meteg/CCC 220 (mod 25/220)]` was left in sorted order instead of swapping the last two marks so `meteg` sits between the two below vowels (e.g. `לִַֽ`).
4. **`gpos_tag = 'hebr'`, `compose_hebrew` fallback, and `ZERO_WIDTH_MARKS_BY_GDEF_LATE`**: fonts lacking an explicit `hebr` script record in `GPOS` fell back to `DFLT` `GPOS` instead of disabling `GPOS` and synthesizing `U+FB1D` / `U+FB1F` / `U+FB2A..U+FB4E` presentation forms; monospaced `GDEF` class-3 Hebrew mark advances were also not zeroed after `GPOS`.
5. **Default-ignorable hiding and `U+034F` Combining Grapheme Joiner (`CGJ`)**: `U+034F` has `CCC = 0` and general category `Mn` (`is_default_ignorable`), so it must trigger pre-mark starter decomposition, stop combining-mark reordering (e.g. Biblical Hebrew *Yerushalayim* `לַ͏ִ`), block `GSUB` across the barrier only when it actually prevented reordering (`cc(i - 1) > cc(i + 1) > 0`), be skipped by `GPOS` (`MarkToBase` / `MarkToMark` / `PairPos` / `CursivePos`) regardless of `IgnoreMarks`, and hide to the font's zero-advance `' '` glyph (alongside `U+061C` `ALM`, `U+00AD`, `U+200B..U+200F`, variation selectors, etc.).

Phase 476 implements all five pieces in `src/render/FontRenderer.cpp`.

## Design & Implementation

### 1. Hebrew Canonical Decomposition & `has_hebrew_gpos_mark` (`src/render/FontRenderer.cpp`)

- `GposScriptPlan::has_mark` records whether a `GPOS` script's active lookups include `Feature::Mark`, and `find_gpos_script(kTagHebr)` refuses `DFLT` fallback (matching HarfBuzz `gpos_tag = HB_TAG('h','e','b','r')` / `disable_gpos`).
- On `kTagHebr` runs, missing composites and mark-followed composites (`before_mark` true when followed by `unicode_ccc(next) != 0` or `next == 0x034F`) expand recursively through `unicode::canonical_decomposition` when all leaf codepoints exist in the current face. For `U+FB2C` (`SHIN WITH DAGESH AND SHIN DOT`) and `U+FB2D` (`SHIN WITH DAGESH AND SIN DOT`), if `U+05E9` lacks coverage while intermediate `U+FB2A`/`U+FB2B` and `U+05BC` are covered, a 1-step decomposition is retained. Compatibility ligature `U+FB4F` (`ALEF LAMED`, in `kCompatDecomp`, absent from `kCanonDecomp`) is never decomposed.
- Standalone covered presentation forms (`!before_mark`) stay precomposed.

### 2. Unified Hebrew Mark Ordering & `reorder_marks_hebrew` (`src/render/FontRenderer.cpp`)

- `hebrew_modified_class(cp)` maps `CCC 10..26` (including `U+FB1E` `JUDEO-SPANISH VARIKA`, `CCC 26 -> 26`) via HarfBuzz's `_hb_modified_combining_class` table `{22, 15, 16, 17, 23, 18, 19, 20, 21, 14, 24, 12, 25, 13, 10, 11, 26}` and leaves all other non-zero `CCC` values (`220`, `222`, `228`, `230`, `232`, etc.) unchanged.
- Contiguous non-zero-class spans in `kTagHebr` runs (leading or post-base, terminating at `CCC == 0` including `U+034F` `CGJ`) stably insertion-sort by `hebrew_modified_class`.
- Immediately after sorting each span `[start, end)`, `reorder_marks_hebrew` scans `i` from `start + 2` to `end - 1`: when `(c_{i-2}, c_{i-1}, c_i)` matches `({20, 21}, {22, 23}, {25, 220})`, it swaps `i - 1` and `i` and breaks out of the loop, matching HarfBuzz `hb-ot-shaper-hebrew.cc:154-182`.

### 3. `compose_hebrew_fallback` & Late `GDEF` Mark Zeroing (`src/render/FontRenderer.cpp`)

- When `script_tag == kTagHebr`:
  - If `has_hebrew_gpos_mark` is true, Round 3 canonical recomposition is skipped (`compose = nullptr` in HarfBuzz `compose_hebrew` when `has_gpos_mark`), keeping decomposed base + marks separate for `GPOS` anchor attachment.
  - Otherwise (`!has_hebrew_gpos_mark`), Round 3 runs both canonical pair composition (`U+FB1D`, `U+FB1F`, `U+FB2A..U+FB4E` via `unicode::canonical_compose`) and HarfBuzz's explicit `s_dagesh_forms` / `RAFE` / `SHIN_DOT` / `SIN_DOT` / `HOLAM` / `PATAH` / `QAMATS` / `HIRIQ` table when the target composite glyph is covered in the face.
- After `GPOS` / `kern` and before Stage 4a/4b attachment offset propagation, `kTagHebr` joins `kTagArab` in zeroing `x_advance` and `y_advance` on `GDEF` class-3 mark glyphs (`ZERO_WIDTH_MARKS_BY_GDEF_LATE`).

### 4. `U+034F` `CGJ` & Default-Ignorable Handling (`src/render/FontRenderer.cpp`)

- `is_default_ignorable(cp)` matches HarfBuzz `_hb_glyph_info_is_default_ignorable` (`U+00AD`, `U+034F`, `U+061C`, `U+115F..U+1160`, `U+17B4..U+17B5`, `U+180B..U+180E`, `U+200B..U+200F`, `U+202A..U+202E`, `U+2060..U+206F`, `U+3164`, `U+FE00..U+FE0F`, `U+FEFF`, `U+FFA0`, `U+FFF0..U+FFF8`, `U+1BCA0..U+1BCA3`, `U+1D173..U+1D17A`, `U+E0000..U+E0FFF`).
- After Round 2 mark ordering and before Round 3 recomposition, `CGJ` at `i` stays `GSUB`-blocking (`cgj_blocks_gsub[i] = true`) iff `i == 0`, `i + 1 == count`, or `0 < cc(i + 1) < cc(i - 1)` (matching HarfBuzz `unhide_cgj` in `hb-ot-shape-normalize.cc:425-443`).
- In `GSUB`, `kSkipIgnorableBit` skips all default-ignorables except `U+200C` (`ZWNJ`) and `GSUB`-blocking `U+034F` (`CGJ`), while non-`GDEF` fallback `is_mark_glyph` treats default-ignorables as non-marks (`hb_synthesize_glyph_classes`).
- Before `GPOS`, `kSkipIgnorableBit` is enabled on all `kDefaultIgnorableBit` glyphs so `GPOS` (`SinglePos`, `PairPos`, `CursivePos`, `MarkToBase`, `MarkToLig`, `MarkToMark`, `ContextPos`, `ChainContextPos`) and legacy `kern` skip `CGJ` and other default-ignorables cleanly.
- After `GSUB` (`hb_ot_hide_default_ignorables`) and `GPOS` (`hb_ot_zero_width_default_ignorables`), default-ignorable slots replace their glyph ID with the face's `' '` glyph (when present; otherwise drop the slot) with `x_advance = 0, y_advance = 0`.

## Differential & Suite Verification

### HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) Before vs After

| Face (`SHA-256` prefix) | Harness | Raw `match` (Before -> After) | `same-placement` (Before -> After) |
|---|---|---|---|
| `DejaVuSans.ttf` (`abdc775b...`) | `hebrew_diagnostic.py` / `--permutations` / `--extended` | `5/5` / `12/12` / `9/24` -> **`24/24`** | `5/5` / `12/12` / `16/24` -> **`24/24`** |
| `DejaVuSans-Bold.ttf` (`0d977336...`) | `hebrew_diagnostic.py` / `--permutations` / `--extended` | `5/5` / `12/12` / `9/24` -> **`24/24`** | `5/5` / `12/12` / `16/24` -> **`24/24`** |
| `DejaVuSans.ttf` (`abdc775b...`) | `compare.py` / `arabic_marks` (`default`, `--ordering`, `--matrix`) / `latin` (`4` suites) | **`13/13`** / **`23/23`**, **`16/16`**, **`486/486`** / **`29/29`** | **`100%`** across all supported cases |
| `DejaVuSansMono.ttf` (`0f5db4f1...`) | `compare.py` / `arabic_marks` (`default`, `--ordering`, `--matrix` 384 supported) | **`11/11`** / **`23/23`**, **`16/16`**, **`384/384`** | **`100%`** across all supported cases |

- `tools/shaping/ink.py` (`FreeType 2.13.2` / `freetype-py 2.5.1`) on `hebrew_diagnostic.py --extended` (`DejaVuSans.ttf`): **72/72** `same-ink-and-advance` (`0` changed pixels at 16, 32, and 64 px).
- Stripped-`hebr`-`GPOS` `DejaVuSans.ttf` control: `DFLT` `GPOS` is disabled and `compose_hebrew` synthesizes exact HarfBuzz glyph IDs (`FB1D`, `FB1F`, `FB2A`, `FB2C`, `FB30`, `FB35`, `FB4C`).

### Portable & Host-Font Regression Tests

- `tests/suites/476-hebrew-normalization-and-cgj.cpp`:
  - Synthetic SFNT covering cantillation (`U+0591`, `U+0596`, `U+05A0`, `U+05AB`, `U+05AE`) + `U+05C4` / `U+05C5` + `U+FB1E` + non-Hebrew `U+0301` / `U+0323` sorting, `reorder_marks_hebrew` (`[patah/qamats, sheva/hiriq, meteg/CCC220]` swaps, negative controls, `start + 2` 4-mark offset, first-swap `break`), `U+FB1D` / `U+FB1F` / `U+FB2A..U+FB4E` pre-mark decomposition vs standalone retention vs `U+FB4F` compatibility guard, `gpos_tag = 'hebr'` refusing `DFLT` `GPOS` and synthesizing `compose_hebrew` presentation forms, partial-coverage fallback (`FB2C` -> `FB49 + SHIN DOT`), `ZERO_WIDTH_MARKS_BY_GDEF_LATE`, `U+034F` `CGJ` vs `U+200C` `ZWNJ` in `GSUB`/`GPOS` across Latin and Hebrew, and end-to-end `DejaVuSans.ttf` + `BitmapRenderer` HTML pixel identity (`&#xFB2C;&#x05B8;` == `&#x05E9;&#x05B8;&#x05BC;&#x05C1;` and `&#x05DC;&#x05B7;&#x034F;&#x05B4;` == `&#x05DC;&#x05B7;&#x05B4;`).
- Updated legacy expectations in `tests/test_suite.cpp`, `tests/suites/266-latin-mark-order.cpp`, `tests/suites/279-leading-marks.cpp`, and `tests/suites/467-rtl-mark-order-and-origins.cpp`.

## Remaining Limits

- When a Hebrew font lacks `hebr` `GPOS`, `compose_hebrew` synthesizes covered presentation forms and disables `DFLT` `GPOS`, but uncomposed marks without `GPOS` are not geometrically centered via HarfBuzz's `_hb_ot_shape_fallback_mark_position` glyph-extents heuristic.
- OpenType dotted-circle (`U+25CC`) insertion on defective combining-mark sequences and public source-cluster reporting remain outside `TrueTypeFont::shape_glyphs`.
