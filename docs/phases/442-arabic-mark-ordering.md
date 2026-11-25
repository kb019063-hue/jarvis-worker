# Phase 442: Arabic mark ordering, modified combining classes, and pre-mark decomposition

Agent: Curlew

## Finding the problem

Phase 292 added covered Arabic pair composition and `MarkToBase` ligature
coverage, bringing `DejaVuSans.ttf` and `DejaVuSans-Bold.ttf` to **484/486**
same-placement matches on `tools/shaping/arabic_marks_diagnostic.py --matrix`,
while two matrix pairs failed on both faces (`0627-0653-0655` and
`0627-0654-0655`) and a broader 3,588-case probe across precomposed Arabic
starters (`U+0622..U+0626`), three-mark vowel/shadda/tanwin clusters, and
extended marks found **323/3,588** `different-placement` cases (`13/16` on
`arabic_marks_diagnostic.py --ordering`).

Inspecting `TrueTypeFont::shape_glyphs` (`src/render/FontRenderer.cpp`) against
HarfBuzz 12.1.0 (`hb-ot-shape-normalize.cc`, `hb-ot-shaper-arabic.cc`, and
`hb-unicode.cc`) isolated three coupled normalization gaps before `GSUB`:

1. **No Arabic mark-span ordering or modified combining classes.** Only
   `kTagLatn` mark spans were sorted, and canonical composition read raw
   Unicode canonical combining classes (`CCC`). Under OpenType / HarfBuzz
   modified combining classes, `U+0651` SHADDA (`CCC 33`) maps to `27` ahead of
   tanwin and vowels (`CCC 27..32 -> 28..33`, `34 -> 34`, `35 -> 35`) so
   `GSUB` `ccmp` mark ligatures see `shadda + vowel` adjacent regardless of
   input order (`\u0628\u064e\u0650\u0651`, `\u0628\u064b\u064e\u0651`).
2. **No UTR #53 Modifier Combining Mark (MCM) promotion.** After sorting by
   modified combining class, HarfBuzz's `reorder_marks_arabic` promotes a
   leading contiguous prefix of UTR #53 MCMs at `CCC 220` (`U+0655`, `U+06E3`,
   `U+08CF`, `U+08D3`) and `CCC 230` (`U+0654`, `U+0658`, `U+06DC`, `U+06E7`,
   `U+06E8`, `U+08CA`, `U+08CB`, `U+08CD`, `U+08CE`, `U+08F3`) to the front of
   the mark span with effective classes `25` and `26`, without crossing
   equal-CCC non-MCM marks such as `U+0653` MADDAH ABOVE or `U+0656` SUBSCRIPT
   ALEF. That promotes `U+0655` ahead of `U+0653` in `0627+0653+0655` to
   compose `U+0625 + U+0653` while keeping `U+0653` ahead of `U+0654` in
   `0627+0653+0654`.
3. **No `kTagArab` covered canonical decomposition before following marks or
   for missing composites.** `U+0622 + U+0655` (`إٓ`) and `U+0623 + U+0655`
   (`إٔ`) retained their precomposed starter instead of exposing covered
   `U+0627 + U+0653` / `U+0627 + U+0654` parts so `U+0655` could promote and
   recompose `U+0625`.

## Changes

In `TrueTypeFont::shape_glyphs` (`src/render/FontRenderer.cpp`):

- Extend same-face canonical decomposition (`expand`) from `kTagLatn` to
  `kTagArab`: decompose missing standalone `kTagArab` composites only when all
  canonical parts exist in the face, and decompose covered `kTagArab`
  composites before a following nonzero-CCC mark resolved to `kTagArab`.
- Order nonzero-CCC mark spans resolved to `kTagArab` (including leading spans
  and spans after Common characters) by Arabic modified combining class
  (`27..32 -> 28..33`, `33 -> 27`), then apply UTR #53 MCM promotion at
  `220 -> 25` and `230 -> 26` using `std::rotate`.
- Track effective combining classes (`effective_class`) into the font-aware
  canonical pair-composition pass so `last_class < cls` reflects the monotonic
  reordered classes (`25, 26, 27..35, 220, 230`).
- Add `--ordering` (16 targeted Arabic mark-ordering, MCM, blocking, `ccmp`,
  and precomposed-starter cases) to `tools/shaping/arabic_marks_diagnostic.py`,
  update `tests/suites/292-arabic-mark-placement.cpp` for UTR #53 promotion of
  unconsumed `U+0654` ahead of `U+064E`, and add synthetic non-host-font unit
  coverage in `tests/suites/442-arabic-mark-ordering.cpp`.

## Verification

```sh
python3 -m venv /tmp/shaping-venv
/tmp/shaping-venv/bin/pip install -r tools/shaping/requirements.txt
make build/shaping-probe
/tmp/shaping-venv/bin/python tools/shaping/arabic_marks_diagnostic.py --matrix \
  --probe build/shaping-probe \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
/tmp/shaping-venv/bin/python tools/shaping/arabic_marks_diagnostic.py --ordering \
  --probe build/shaping-probe \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

HarfBuzz `12.1.0` / `uharfbuzz` `0.52.0` differential results (commands exit
`1` while raw RTL within-cluster order mismatches remain):

- `DejaVuSans.ttf` (`abdc775b21b1bc470d50c97e790d276f2054b7504e56e5bd3e64f48d68582322`):
  - `arabic_marks_diagnostic.py --matrix` (486 cases): same-placement
    **484/486 → 486/486** (`2 → 0` different-placement).
  - `arabic_marks_diagnostic.py --ordering` (16 cases): same-placement
    **3/16 → 16/16** (`13 → 0` different-placement).
  - `arabic_marks_diagnostic.py` (23 cases): **23/23** same-placement (`5`
    exact raw, unchanged).
  - Extended 3,588-case probe (`U+0622..U+0626` + 2 marks, 6 bases × 7 marks³,
    and all 15 covered Arabic marks²): same-placement **3,265/3,588 →
    3,588/3,588** (`323 → 0` different-placement).
- `DejaVuSans-Bold.ttf` (`0d977336a6d5fba34eab8e3199eb218327161b5143749f802982c2bc34df0c96`):
  `--matrix` same-placement **484/486 → 486/486**; `--ordering` **3/16 → 16/16**;
  23-case selected **23/23**.
- `DejaVuSansMono-Bold.ttf` (`2964f6dac8e6e9d71613928340f17bf868e9ea51692cca333c79e74962f02233`):
  supported `--matrix` same-placement **368/384 → 370/384** (`16 → 14`
  different-placement, `102` unsupported lacking `U+0670`); `--ordering`
  **8/16 → 15/16**.
- `DejaVuSansMono.ttf` (`0f5db4f1749979d961019838b160bec74abdf7f9eca69553fe1aa856bbff49a4`):
  `0627-0653-0655` and `0627-0654-0655` now emit the HarfBuzz glyph IDs
  `[1115, 1156]` and `[1115, 1157]` (`U+0625` instead of `U+0622`/`U+0623`),
  while all `26/384` remaining `different-placement` matrix cases are
  unanchored zero-advance marks on `U+0622`/`U+0623`/`U+0625` whose origins
  reflect HarfBuzz's reversed RTL `[mark, base]` buffer (`x = 0`) vs
  Litebird's logical within-cluster `[base, mark]` buffer (`x = 1233`).
- `DejaVuSerif.ttf` (`13e61509f5c81d7c3132810f4f903e3523df89c802bf6e0674621e8f659cdfe1`):
  `486/486` unsupported (no Arabic coverage).
- `compare.py` (13/13 supported same-placement, 11 exact), `mark_order_diagnostic.py`
  (8/8), `supported_composite_diagnostic.py` (8/8), `leading_marks_diagnostic.py`
  (8/8), and `hebrew_diagnostic.py --permutations` (12/12 same-placement) are
  unchanged.

`make test` passes all **493** engine suites (`arabic_mark_ordering_and_pre_mark_decomposition`
and `arabic_composition_and_mark_to_base_coverage` fail when `src/render/FontRenderer.cpp`
is reverted to parent).

## Still open

- **RTL within-cluster glyph order (`[base, mark]` vs `[mark, base]`)**: raw
  RTL glyph arrays still differ from HarfBuzz whenever a cluster retains marks,
  and unattached zero-advance marks on unanchored bases (`26` Mono regular and
  `14` Mono bold `--matrix` cases) inherit `pen = base_advance` instead of `0`.
- **Default-ignorable barriers and other scripts**: `U+034F` CGJ and non-Latin,
  non-Arabic, non-Hebrew script normalization rules (`Syrc`, `Dev2`, `Thai`,
  etc.), fallback mark positioning when `GPOS` is absent, alternate partial
  decompositions, and public source-cluster indices remain separate work.
