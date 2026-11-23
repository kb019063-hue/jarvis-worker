# Phase 200: Hebrew fixed-position points precede the lookups

Agent: Rowan

## What was wrong

The three-codepoint probe from #166, SHIN + QAMATS + SHIN DOT, put qamats
127 font units away from HarfBuzz's origin in DejaVu Sans. Removing either
mark, or reversing their order, made placement agree. This was not a simple
RTL reversal bug: the input order reached contextual substitutions and mark
attachment unchanged.

## What changed

Before mapping a Hebrew script run to glyphs, stable-sort contiguous Hebrew
fixed-position points by shaping order. Shin/sin dots precede dagesh, rafe,
holam, vowels and meteg. This is the SBL Hebrew fixed-position ordering,
also described in HarfBuzz's `src/hb-unicode.hh` (modified CCC 10–25):
https://github.com/harfbuzz/harfbuzz/blob/12.1.0/src/hb-unicode.hh

Equal-ranked points retain order. Bases, controls and marks outside this
explicit set are barriers; nothing is sorted across them. The change happens
before GSUB so contextual substitutions see the ordered points. No Arabic,
Latin or generic normalization rule changes.

## Verification

```
make test
make build/shaping-probe
python3 -m venv build/shaping-venv
build/shaping-venv/bin/pip install -r tools/shaping/requirements.txt
build/shaping-venv/bin/python tools/shaping/hebrew_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/hebrew.json
build/shaping-venv/bin/python tools/shaping/hebrew_diagnostic.py --permutations \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/hebrew-permutations.json
```

On the DejaVu Sans SHA-256 recorded in `tools/shaping/README.md`, uharfbuzz
0.52.0 / HarfBuzz 12.1.0:

- Original five cases: absolute placement 4/5 → 5/5.
- All six permutations of qamats/shin-dot/dagesh after each of SHIN and BET:
  4/12 → 12/12, no previously matching case lost.
- Original 14-case oracle: placement 11 same / 2 different / 1 unsupported
  → 12 same / 1 different / 1 unsupported. Raw matches remain 8/14.
- `make test`: 347 suites plus 114 cross-origin checks pass. New synthetic
  font test checks ordering without relying on installed fonts, stable equal
  ranks, repetitions, base/space/unhandled-mark barriers and empty input.

The diagnostic still exits 1 for raw differences: matching absolute origins
is not a claim of identical raw glyph sequences or rasterized pixels.

## Still open

General Unicode normalization; reordering across Hebrew cantillation or
non-Hebrew combining marks; Hebrew presentation-form fallback and special
meteg sequences; cluster/caret mapping and pixel equivalence. This closes the
reduced fixed-point-order discrepancy, not all Hebrew shaping.
