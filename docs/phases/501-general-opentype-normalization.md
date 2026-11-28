# Phase 501 — General OpenType Normalization and Alternate Partial Decomposition

Resolves [Finding 501](../findings/501-general-opentype-normalization.md) under the shaping conformance track (`AGENTS.md`: *"Still open: alternate partial decomposition, default-ignorable barriers (`U+034F`), non-Latin/non-Arabic/non-Hebrew normalization, and source clusters."*).

## Summary

Prior phases (`234`, `240`, `266`, `270`, `279`, `292`, `442`, `476`) implemented covered canonical decomposition, mark ordering, and starter recomposition on `latn`, `arab`, and `hebr` runs only, using leaf-only expansion (`kCanonDecomp` full leaf sequences). Five gaps remained between `TrueTypeFont::shape_glyphs` and HarfBuzz 12.1.0 (`hb-ot-shape-normalize.cc`, `hb-unicode.cc`, `hb-ot-shaper-thai.cc`):

1. **No normalization on non-Latin/non-Arabic/non-Hebrew scripts (`grek`, `cyrl`, `kana`, `thai`, `lao `, `armn`, `geor`, `syrc`, `thaa`, `nko `)**: Greek polytonic (`\u03b1\u0313\u0301 -> ἄ`, `\u03b1\u0345\u0313 -> ᾀ`, `\u1f04\u0345 -> ᾄ`), Cyrillic (`\u0435\u0308 -> ё`, `\u0456\u0308 -> ї`), Kana (`\u304b\u3099 -> が`, missing `\u30d1 -> ハ + \u309a`), Thai (`\u0e01\u0e3a\u0e38`), and Lao (`\u0e81\u0ec8\u0eb8`) neither decomposed, reordered marks, nor composed starters. On `DejaVuSerif.ttf` (which lacks `U+0345` `COMBINING GREEK YPOGEGRAMMENI`), `\u03b1\u0345\u0313` and `\u1f04\u0345` emitted `.notdef` (`gid 0`) instead of composing to covered `U+1F80` (`ᾀ`) and `U+1F84` (`ᾄ`).
2. **Decomposable combining marks in multi-character clusters (`U+0340`, `U+0341`, `U+0343`, `U+0344`)**: `shape_glyphs` only decomposed codepoints followed by another mark (`before_mark`) or missing from `cmap`, leaving covered final/intermediate decomposable marks unexpanded (`"a\u0341"`, `" \u0341"`, `"\u0341\u0323"`, `"\u03b9\u0344"`, `"\u03b1\u0343\u0341"`).
3. **No 1-step binary / alternate partial canonical decomposition**: leaf-only `kCanonDecomp` expansion failed whenever any leaf mark was absent from the face, even when an intermediate 1-step canonical `(a, b)` or singleton (`U+212B -> U+00C5`, `U+1F71 -> U+03AC`, `U+1FD3 -> U+0390`, `U+01FB -> U+00E5 + U+0301`, `U+1F84 -> U+1F04 + U+0345`) was covered.
4. **Uncomposed `Common` starters**: Round 3 gated `starter_script` on `classify_codepoint_script(cp) in {latn, arab, hebr}`, rejecting `Common` starters (`'=\u0338' -> '≠'`, `'<\u0338' -> '≮'`, `'\u2190\u0338' -> '↚'`).
5. **Modified combining classes, script classification, and Thai/Lao late `GDEF` mark zeroing**: `modified_combining_class` lacked HarfBuzz's Thai (`CCC 103 -> 3`), Telugu (`84 -> 4`, `91 -> 5`), Tibetan (`130 -> 132`, `132 -> 131`, `0x0F39 -> 127`, `0x0FC6 -> 254`), and Tai Tham (`0x1A60 -> 254`) mappings; `classify_codepoint_script` omitted `U+3099..U+309A` (`Inherited`), `U+0485..U+0486` (`Inherited`), `U+0951..U+0954` (`Inherited`), `kTagKana`/`kTagArmn`/`kTagGeor`, IPA/Phonetic Latin (`U+0250..U+02AF`, `U+1D00..U+1DBF`, `U+212A..U+212B`), and misclassified `U+FEFF` as `arab`; and Stage 4 omitted `kTagThai`/`kTagLao` from `ZERO_WIDTH_MARKS_BY_GDEF_LATE` (`DejaVuSansMono.ttf` `1233`-unit Lao mark advances).

Phase 501 implements all five pieces in `src/render/FontRenderer.cpp`.

## Design & Implementation

### 1. Exact 1-Step Binary & Alternate Partial Decomposition (`src/render/FontRenderer.cpp`)

- `decompose_one_step(cp, a, b)` recovers Unicode 17.0 1-step canonical binary `(a, b)` (`b == 0` for singletons) across all `2,081` `kCanonDecomp` entries by folding `seq[0 .. len - 2]` via `kComposePairs` (`2,052` entries) plus the `29` Unicode 17.0 `CompositionExclusions` entries (`18` singletons `U+1F71..U+1FFB`/`U+212B`, `2` Hebrew `U+FB2C`/`U+FB2D -> (U+FB49, U+05C1/U+05C2)`, and `9` Musical Symbol notes `U+1D160..U+1D164`/`U+1D1BD..U+1D1C0`), verified 100% identical (`2,081 / 2,081`) to `hb_ucd_decompose`.
- `decompose_codepoint(cp, shortest, out, depth)` mirrors `hb-ot-shape-normalize.cc:97-137`:
  1. Checks `b != 0 && glyph_index(b) == 0` -> fails immediately.
  2. Checks `has_a = (glyph_index(a) != 0)`; if `shortest && has_a`, emits `[a]` (or `[a, b]`) without recursing to leaves.
  3. Recurses on `a` with `shortest = false`, appending `b` if recursion succeeds.
  4. If leaf recursion fails (`out` rolled back to `saved`) and `has_a` is true, falls back to partial `[a]` (or `[a, b]`).

### 2. Per-Script Cluster Normalization (`Round 1`, `Round 2`, `Round 3`)

- **Round 1 (`_hb_ot_shape_normalize` cluster scan)**:
  - Runs per itemized script run for every script except deferred `kTagDev2` (Indic two-part `CCC = 0` matras require left-matra reordering not yet implemented).
  - Scans clusters `[i, end)` advancing over `is_cluster_mark(cp)` (nonzero `CCC`, `U+034F` `CGJ`, non-Brahmic `CCC == 0` combining/enclosing marks `U+0488..U+0489`, `U+07A6..U+07B0`, `U+20DD..U+20E4`, `U+A670..U+A672`, and variation selectors).
  - Variation-selector clusters bypass decomposition (`hb-ot-shape-normalize.cc:238`).
  - Simple 1-codepoint clusters keep covered standalone glyphs (`"\u0341"`, `"\u0344"`, `"\ufb2c"`) and call `decompose_codepoint(..., shortest = true)` only when `glyph_index(cp) == 0`.
  - Multi-codepoint clusters (`end - i >= 2`) call `decompose_codepoint(..., shortest = false)` on every member (base and marks alike) while preserving each run's itemized script tag across emitted codepoints.
- **Round 2 (`modified_combining_class` + script `reorder_marks`)**:
  - Unifies HarfBuzz's `_hb_modified_combining_class` table across all normalized scripts (`CCC 10..26` Hebrew, `27..33` Arabic, `84 -> 4` / `91 -> 5` Telugu, `103 -> 3` Thai, `130 -> 132` / `132 -> 131` Tibetan, `0x0F39 -> 127`, `0x0FC6`/`0x1A60 -> 254`), stably sorting contiguous nonzero-class spans within each script run and running `Arab` MCM promotion (`220 -> 25`, `230 -> 26`) and `Hebr` `reorder_marks_hebrew`.
- **Round 3 (`COMPOSED_DIACRITICS`)**:
  - Tracks `starter_script = (!cls && script != kTagDev2) ? script : 0` from the run's itemized script tag so `Common` starters (`=`, `<`, `←`), `Grek`, `Cyrl`, `Kana`, `Latn`, `Arab`, and `Hebr` (`compose_hebrew_fallback` when `!has_hebrew_gpos_mark`) compose with unblocked marks (`last_class < cls`) whenever `glyph_index(candidate) != 0`.
- **Stage 4 (`ZERO_WIDTH_MARKS_BY_GDEF_LATE`) & Script Classifier**:
  - Adds `kTagThai` and `kTagLao` alongside `kTagArab` and `kTagHebr` (`hb-ot-shaper-thai.cc:375`) while keeping `kTagLatn` unzeroed (`tests/suites/432-arabic-mark-advances.cpp`).
  - Adds `kTagArmn`, `kTagGeor`, `kTagKana`, `Inherited` ranges `U+0485..U+0486` / `U+0951..U+0954` / `U+3099..U+309A`, Latin `U+0250..U+02AF` / `U+1D00..U+1DBF` / `U+212A..U+212B`, and excludes `U+FEFF` from `kTagArab`.

## Differential & Suite Verification

### HarfBuzz 12.1.0 (`uharfbuzz` 0.52.0) Before vs After (`tools/shaping/normalization_diagnostic.py`)

| Face (`SHA-256` prefix) | Suite | Raw `match` (Before -> After) | `same-placement` (Before -> After) |
|---|---|---|---|
| `DejaVuSans.ttf` (`abdc775b...`) | `default` (`30`) / `--with-lao` (`32`) | `5/30` (`6/32`) -> **`30/30` (`32/32`)** | `5/30` (`7/32`) -> **`30/30` (`32/32`)** |
| `DejaVuSans-Bold.ttf` (`0d977336...`) | `default` (`30`) | `5/30` -> **`30/30`** | `5/30` -> **`30/30`** |
| `DejaVuSerif.ttf` (`13e61509...`) | `default` (`30`, `8` formerly `.notdef`) | `5/22` (`8` unsupported) -> **`30/30`** | `5/22` (`8` unsupported) -> **`30/30`** |
| `DejaVuSansMono.ttf` (`0f5db4f1...`) | `default` (`30`) / `--with-lao` (`32`, `8` formerly `.notdef`) | `4/22` (`4/24`) -> **`30/30` (`32/32`)** | `4/22` (`4/24`) -> **`30/30` (`32/32`)** |

- `tools/shaping/ink.py` (`FreeType 2.13.2` / `freetype-py 2.5.1`) on `normalization_diagnostic.py` across `DejaVuSans.ttf`, `DejaVuSerif.ttf`, and `DejaVuSansMono.ttf`: **100%** `same-ink-and-advance` (`0` changed pixels at 16, 32, and 64 px).
- Zero regressions across all existing shaping harnesses (`compare.py`, `arabic_marks_diagnostic.py`, `hebrew_diagnostic.py`, `decomposition_diagnostic.py`, `latin_diagnostic.py`, `leading_marks_diagnostic.py`, `mark_order_diagnostic.py`, `supported_composite_diagnostic.py`).

### Portable & Host-Font Regression Tests

- `tests/suites/501-general-opentype-normalization.cpp`:
  - Synthetic SFNT testing 1-step & multi-step alternate partial canonical decomposition (`U+01FB -> U+00E5 + U+0301` with `U+030A` absent, both standalone `shortest = true` and `{U+01FB, U+0323}` `shortest = false`; `U+1F84 -> U+1F04 + U+0345` with `U+0313` absent; singleton `U+212B`, `U+1F71`, `U+1FD3` both with and without leaf marks).
  - Greek polytonic composition, equal-CCC blocking, `CGJ` barrier, and decomposable marks (`U+0341`, `U+0343`, `U+0344`) in multi-char clusters vs standalone 1-char retention; Cyrillic (`ё`, `ї`, missing `ѐ` guard, `CGJ`); Kana (`が`, missing `パ -> ハ + U+309A`); Common (`= + U+0338 -> ≠`); Thai (`U+0E38` modified CCC 3 before `U+0E3A` CCC 9); Lao (`U+0EB8` CCC 118 before `U+0EC8` CCC 122); cross-script boundary guard (`'á' + U+05B8`); host `DejaVuSans`/`DejaVuSerif` checks; and end-to-end HTML `BitmapRenderer` pixel equivalence (`&#x03b1;&#x0313;&#x0301; &#x0435;&#x0308;` == `&#x1f04; &#x0451;`).
- Updated legacy non-Latin controls in `tests/suites/266-latin-mark-order.cpp` and `tests/suites/279-leading-marks.cpp` (`0x03B1` Greek now orders; deferred `0x0915` Devanagari `dev2` added as control).

## Remaining Limits

- Indic (`kTagDev2`) two-part matra decomposition and Indic/USE syllable reordering remain deferred to the Indic shaper track.
- Algorithmic Hangul (`LV`/`LVT` <-> `L + V (+ T)`) normalization remains separate under the Hangul shaping row.
- Public source-cluster reporting (`PositionedGlyph` cluster mapping) and fallback geometric mark centering (`_hb_ot_shape_fallback_mark_position`) remain unimplemented.
