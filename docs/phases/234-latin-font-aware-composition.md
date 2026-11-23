# Phase 234: compose the Latin pair only when the font has it

Agent: Rowan

## What was wrong

The #284 reducer found `a + U+0301` placed a separately drawn acute 10 font
units left of HarfBuzz's precomposed glyph in DejaVu Sans. The base and total
advance agreed; the shared-rasterizer comparison differed at 11/24/49 pixels
at 16/32/64 px. The fix is not to move that font's mark by ten units.

## What changed

Before mapping codepoints to glyphs, adjacent canonical pairs with a Latin
starter in a Latin script run are composed **only if this face has a nonzero
cmap glyph for the result**. The existing generated Unicode 17.0 composition
pair table supplies the mapping, including composition exclusions; no font
names, glyph IDs or offsets are hard-coded. A missing composite leaves the
base and mark available to the existing GSUB/GPOS path. Successive adjacent
composition works (a + ring + acute), provided each intermediate is supported.
The composed glyph supplies its own advance and is seen by subsequent lookups.

The Unicode table was already linked into the engine. The standalone shaping
probe now links it too; no Unicode data or JavaScript normalization changes.

This is **not full NFC or HarfBuzz normalization**: no decomposition, mark
reordering, composition across an unconsumed mark, fallback-face selection,
Hangul composition, or non-Latin script change is attempted. Common punctuation
in a Latin run is not a Latin starter and is left alone. Input vectors are
not mutated and no source-cluster API is added.

## Verification

```
make test
make build/shaping-probe
python3 -m venv build/shaping-venv
build/shaping-venv/bin/pip install -r tools/shaping/requirements.txt -r tools/shaping/requirements-ink.txt
build/shaping-venv/bin/python tools/shaping/latin_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/latin-after.json
build/shaping-venv/bin/python tools/shaping/ink.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --report build/latin-after.json --output build/latin-ink-after.json
```

With the DejaVu Sans hash recorded in `tools/shaping/README.md`, HarfBuzz 12.1.0
and FreeType 2.13.2, the five Latin cases are **5/5 exact raw matches**, up from
3/5; their 15 case/size ink comparisons all match with zero changed pixels.
The broader 14-case corpus moves from 8 to 9 raw matches; all 13 supported cases
now match absolute placement. Four RTL representation differences and the
unsupported Devanagari case still make the full strict oracle exit 1.

`make test`: **374 suites and 114 cross-origin checks pass**. The new suite is
`tests/suites/234-latin-font-aware-composition.cpp`, with synthetic fonts with
and without composite coverage. It checks exact glyph identity, composite
metrics, chained composition, absent coverage, no compatibility composition,
no skipping an intervening mark, leading marks, empty input and input retention.

Three old optional host-font checks expected a/A/i + acute to remain decomposed.
Their mark/stack/ccmp assertions are unchanged; their inputs are now x/X + acute
and i + vertical-line-above, which have no canonical composites and therefore
still exercise the intended paths. Synthetic GPOS/GSUB fixtures were unchanged.

## Still open

General font-aware normalization, decomposition when a font lacks a precomposed
glyph but has its parts, canonical reordering and blocking across marks,
cluster/caret mapping, and non-Latin normalization. Matching shared FreeType ink
is not a browser screenshot oracle or proof of Litebird rasterizer correctness.
