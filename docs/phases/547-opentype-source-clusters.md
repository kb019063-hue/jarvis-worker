# Phase 547 — OpenType Shaping Public Source-Cluster Indices

Resolves [Finding 547](../findings/547-opentype-source-clusters.md) under the OpenType shaping conformance track (`HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES` in `hb-buffer.cc` / `hb-ot-shape.cc`).

## Summary

Through Phase 534 the shaping pipeline produced glyph IDs, advances and offsets that matched HarfBuzz 15.0.0 on every supported diagnostic case, but it discarded the correspondence between each output glyph and the source text it came from:

1. **No cluster field on either public result type**:
   - `TrueTypeFont::PositionedGlyph` and `FontRenderer::ShapedGlyph` carried only `glyph`, `x_advance`, `y_advance`, `x_offset` and `y_offset`. A caller could not map a ligature back to the codepoints that formed it, could not tell which glyph belonged to which combining sequence, and could not do caret placement, text selection, or hit testing on shaping output at all.
2. **No `hb_form_clusters` equivalent**:
   - HarfBuzz runs `hb_set_unicode_props` and `hb_form_clusters` **before** any preprocessing (`hangul`, `thai`) or normalization, merging every UAX #29 grapheme continuation into the cluster of the starter it continues. Litebird had no notion of a cluster, so there was nothing to merge or preserve.
3. **No cluster maintenance across the pipeline stages that rewrite the buffer**:
   - `GSUB` `Multiple` substitution (1→N expansion and 1→0 deletion), `Ligature` substitution, normalization Rounds 1–3 (decomposition, `hb_buffer_t::sort` mark reordering, recomposition), the `preprocess_text_hangul` / `preprocess_text_thai` reordering, `delete_glyph`'s neighbour merges, and the final RTL buffer reversal all move, duplicate, merge or remove slots. Each of those needs HarfBuzz's `merge_clusters_impl` rule to keep clusters monotone and to keep a merged glyph pointing at the **earliest** source position it covers.
4. **No UTF-8 source offsets in `FontRenderer::shape_text`**:
   - The public text level entry point splits on script, font fallback and Bidi runs, and it re-reverses `pre_reversed` Bidi runs, so a cluster index that is an index into one run's codepoint vector is meaningless to a caller. Only a byte offset into the original UTF-8 string is.

Phase 547 adds the fields, the grapheme tables, and the cluster maintenance across the whole pipeline in `include/litebird/render/FontRenderer.hpp` and `src/render/FontRenderer.cpp`, and extends the shaping differential in `tools/shaping/` to compare clusters too.

## Design & Implementation

### 1. Public Cluster Fields (`FontRenderer.hpp`)

- `TrueTypeFont::PositionedGlyph::cluster` — the 0-based index of the source codepoint in the vector given to `shape_glyphs`, matching `HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES`.
- `TrueTypeFont::ShapingGlyph::cluster` — the same index carried through the internal shaping buffer.
- `FontRenderer::ShapedGlyph::cluster` — the 0-based **UTF-8 byte offset** of the source text that produced the glyph, in the caller's original string, across multi-byte UTF-8, ligatures, multi-face fallback runs, visual RTL and `pre_reversed` Bidi runs.

### 2. `hb_form_clusters` and the Grapheme Continuation Tables (`FontRenderer.cpp`)

- Two sorted `CpRange` tables taken from Unicode 17.0: `kGraphemeContinuationRanges` (`331` ranges: `General_Category` `Mn`/`Mc`/`Me`, `U+1F3FB..U+1F3FF` emoji modifiers, `U+FF9E..U+FF9F` halfwidth sound marks, and `U+E0020..U+E007F` / `U+E0100..U+E01EF` tags and variation selectors) and `kExtendedPictographicRanges` (`156` ranges) for the `ZWJ` rule. `U+200C` (`ZWNJ`) is deliberately **not** a continuation, so it keeps its own cluster.
- Both tables are binary-searched (`is_grapheme_continuation`, `is_extended_pictographic`), and `shape_glyphs` initializes `cluster[i] = i` and then, **per script slice**, merges: any `Mn`/`Mc`/`Me`, emoji tag or halfwidth mark; `U+200D` together with the following `Extended_Pictographic`; and a Regional Indicator that directly follows an unclaimed Regional Indicator (pairs form one cluster, a third starts a new one). This runs before the Hangul and Thai/Lao preprocessors, exactly as `hb_ot_shape_internal` orders it.
- `merge_slot_clusters` is a faithful port of `hb_buffer_t::merge_clusters_impl`: the merged cluster is the `min` over the range, and the range grows forward across slots that share the old last cluster and backward across slots that share the old first cluster, bounded by the current script slice.

### 3. Cluster Maintenance Through Every Rewriting Stage

- **`GSUB` `Multiple` 1→0 (`delete_glyph`)**: a deleted slot whose cluster is shared with a neighbour merges the neighbours; otherwise a lower cluster propagates backward into the previous slots and a deleted leading slot merges `[0, 2)` forward.
- **`GSUB` `Multiple` 1→N**: every produced slot inherits the source slot's cluster.
- **`GSUB` `Ligature`**: components (and any skipped intervening marks) merge into the ligature's cluster before the glyphs collapse, so `f U+0300 i` produces one glyph at cluster `0`.
- **Preprocessors**: `preprocess_text_hangul` merges composed `<L,V(,T)>` / `<LV,T>` sequences, decompositions, and tone-mark rotations into the source cluster; `preprocess_text_thai` merges the `NIKHAHIT` + `SARA AA` pair and the preceding above-base mark span the `NIKHAHIT` is rotated past. A merge inside a preprocessor propagates back to `orig_clusters` so later stages see the same numbering.
- **Normalization Round 1/3**: decomposition products inherit their source slot's cluster; Round 3 recomposition merges the starter forward across everything from the starter to the recomposed slot, and propagates back to the still-unconsumed `input` slots that shared that cluster.
- **Normalization Round 2**: the mark reordering is now HarfBuzz's stable insertion sort rather than a `std::stable_sort`, so the exact `[q, p + 1)` span that `hb_buffer_t::sort` merges is known and merged; the Arabic MCM promotion likewise merges the swapped pair.
- **RTL**: `reverse_rtl` moves the whole derived cluster block, so a reversed run keeps each base attached to its marks and reports the base's cluster first, matching HarfBuzz's `hb_buffer_reverse`.

### 4. `FontRenderer::shape_text` Byte Offsets

- The UTF-8 decoder now records each codepoint's byte offset, reverses that vector together with the codepoints for `pre_reversed` Bidi runs, carries the offsets along script/face/Bidi run splitting, and maps `PositionedGlyph::cluster` (a run-local codepoint index) to the caller's byte offset on the way out. The synthesized fallback path (a run whose face lacks the text) also records the byte offset rather than an invented value.

### 5. Differential Tooling (`tools/shaping/`)

- `probe.cpp` emits six-field rows `[glyph, x_advance, y_advance, x_offset, y_offset, cluster]`.
- `compare.py` compares all six fields, keeps `placement_signature` on the first five (so placement-only equivalence is still reported separately), records `cluster` in the report's `fields`, and its self-test now mutates all six columns plus a cluster-only drift case, asserting that cluster drift fails raw comparison while leaving placement equal.
- `ink.py`, `outlines.py` and `test_ink.py` accept both five- and six-field rows and validate that a cluster is `>= 0` and within the diagnostic limit, so a widened probe cannot smuggle a negative offset into the raster comparison.
- New `cluster_diagnostic.py`: `20` `DejaVuSans.ttf` multi-script cases (Latin ligatures with intervening/trailing marks and joiners, combining sequences, Greek polytonic, Cyrillic, Hebrew and Arabic RTL, `ZWNJ` vs `ZWJ`, MCM reordering, Lao `SARA AM`) plus `--synthetic`, `16` cases against a synthetic SFNT whose `GSUB` exercises `Multiple` expansion/deletion, `Alternate`, ligature across marks, emoji `ZWJ`/Regional Indicator/tag sequences, halfwidth katakana sound marks and Hangul tone/jamo handling.

## Differential & Suite Verification

### HarfBuzz 15.0.0 (`uharfbuzz` 0.57.0) Before vs After

| Diagnostic | Cases (before → after) |
|---|---|
| `compare.py` — `DejaVuSans.ttf`, `DejaVuSans-Bold.ttf` | `1/13` → **`13/13`** each (`0` mismatches) |
| `compare.py` — `DejaVuSansMono.ttf`, `DejaVuSansMono-Bold.ttf` | **`11/11`** each (unchanged) |
| `compare.py` — `DejaVuSerif.ttf`, `DejaVuSerif-Bold.ttf` | **`7/7`** each (unchanged) |
| `cluster_diagnostic.py` (`DejaVuSans.ttf`) | `3/20` → **`20/20`** |
| `cluster_diagnostic.py --synthetic` | `exit 1` (unrecorded clusters) → **`16/16`** |
| `arabic_marks_diagnostic.py` | **`23/23`** |
| `hebrew_diagnostic.py` | **`5/5`** |
| `leading_marks_diagnostic.py` | **`8/8`** |
| `normalization_diagnostic.py` | **`30/30`** |
| `decomposition_diagnostic.py` | **`7/7`** |
| `latin_diagnostic.py` | **`5/5`** |
| `mark_order_diagnostic.py` | **`8/8`** |
| `supported_composite_diagnostic.py` | **`8/8`** |
| `thai_lao_diagnostic.py` (`--extended`, `--synthetic-thai-pua`, `--synthetic-thai-mac-pua`) | **`24/24`**, **`16/16`**, **`16/16`** |
| `hangul_diagnostic.py` (default, `--zero-width-tones`, `--dflt-no-dotted-circle`) | **`24/24`**, **`12/12`**, **`8/8`** |

- **`292/292` supported cases match on all six fields**, with `0` mismatches. `ink.py` reports **`60/60`** `same-ink-and-advance` and `outlines.py` **`20/20`** `same-contours-and-advance` on the cluster report: adding the field changed no pixel.
- `make shaping-oracle-self-test`, `test_ink.py` and `test_outlines.py` (unit and FreeType integration) pass.
- The same differentials also pass the probes built with `-fsanitize=address,undefined`.

### Engine Test Suite

- `tests/suites/547-opentype-source-clusters.cpp` (`opentype_source_cluster_indices`) exercises, against a synthetic OpenType face and `DejaVuSans.ttf`: `Multiple` 1→3 and 1→0 at the middle, the start and a shared mark; `Alternate`; ligature across an intervening mark and with a trailing mark on the last component; `Me`, emoji modifier, Regional Indicator pairing, `ZWJ` + pictographic vs `ZWJ` + ASCII, halfwidth sound marks and emoji tags; Hangul composition, decomposition, standalone `L`+`T`, and the Round 2 tone-mark swap; Lao `SARA AM`; Arabic and Hebrew RTL cluster reversal; Greek multi-byte UTF-8 byte offsets; ligature byte offsets; visual RTL and `pre_reversed` Bidi runs; and a multi-face fallback run split across a webfont and the system face.
- All `574` engine test suites pass. The suite registers its temporary webfont under the existing `"Ahem"` family (`tests/wpt/fonts/Ahem.ttf`) and calls `clear_webfonts()`, so it leaves `FontRenderer`'s singleton family list exactly as it found it.

## Remaining Limits

- Indic (`kTagDev2`) two-part matra decomposition, Indic/USE syllable reordering, and per-glyph advance clusters for those scripts remain deferred.
- Cursor and selection semantics on top of these clusters (`TextMeasurer`, caret affinity, cluster-level hit testing) are separate work: this phase publishes the indices, it does not consume them in layout or painting.
- GPOS-less geometric bounding-box fallback mark positioning (`_hb_ot_shape_fallback_mark_position`) remains separate.
