# HarfBuzz shaping differential

This is a measurement tool, not a claim that text rendering is correct. It
calls the production `TrueTypeFont::shape_glyphs` through a test-only adapter,
and HarfBuzz through pinned `uharfbuzz`, with exactly the same font bytes.
No engine source is changed. No reference answers come from Litebird.

```sh
python3 -m venv build/shaping-venv
build/shaping-venv/bin/pip install -r tools/shaping/requirements.txt
make shaping-oracle SHAPING_PYTHON=build/shaping-venv/bin/python \
  SHAPING_FONT=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

Choose an existing TrueType font explicitly. Nothing downloads or substitutes
fonts. The JSON report in `build/shaping-report.json` records the font SHA-256,
both reference-library versions, units per em, direction/script and exact
reference/actual arrays for every case. Use that hash when reproducing a run;
a different font version is a different experiment. Generated reports stay
out of Git. Supply other fonts by rerunning the same command.

Each of 14 cases is a single directional script run. HarfBuzz uses OpenType
font functions, default features, language `und`, and a scale equal to units
per em; Litebird uses kerning and visual RTL for RTL runs. Arrays compare
exactly: glyph ID, x/y advance, x/y offset, in font units, without rounding
or tolerance. Missing glyph zero in either output is **unsupported**, never
credited as a match. Empty input is deliberately included as an adapter check.

Exit codes: 0 means every case matches; 1 means a mismatch or unsupported
case; 2 means a missing dependency/font, failed probe, malformed response or
timeout. The full differential is deliberately **not a green CI gate yet**.
CI runs `make shaping-oracle-self-test`: mutation controls detect every array
field, count and order. That checks the comparator, not engine conformance.

## First measurement (2026-11-20)

DejaVuSans.ttf SHA-256
`abdc775b21b1bc470d50c97e790d276f2054b7504e56e5bd3e64f48d68582322`,
uharfbuzz 0.52.0 / HarfBuzz 12.1.0: **8 exact matches, 5 mismatches, 1
unsupported**; exit 1. The unsupported case is Devanagari (this font lacks
coverage), not a measurement of Indic shaping correctness.

Matches: empty, Latin, kerning, ligatures, Greek, Cyrillic, Hebrew without
marks, Arabic without marks. Differences:

- Combining Latin: HarfBuzz composes `a + acute` into one glyph; Litebird
  emits a base and positioned mark. Different sequences need not mean
  different painted pixels.
- Marked Hebrew: glyph order and positioning differ.
- Marked Arabic: marks appear on different sides of their bases in output
  order, with compensating offsets. This can represent the same placement.
- Arabic ZWNJ/ZWJ: HarfBuzz emits an invisible zero-advance space for the
  control; Litebird drops it. This is an output-contract difference, not
  proof of a visible joining defect.

## Absolute placement diagnostic

Each case now also carries `placement` in the JSON report. Starting at pen
`(0, 0)`, a glyph's origin is `pen + offset`, then the pen moves by its
advance. We compare sorted **multisets** of `(glyph ID, origin x, origin y)`
and the final pen in both axes, exactly in font units. Multiplicity is kept.
Sorting deliberately ignores draw order: this is not a compositing test.

Only nonzero glyph IDs whose HarfBuzz font extents have both width and height
zero can be omitted, and only when both advances are also zero. Unknown
extents are not treated as empty. `.notdef` never disappears. The report lists
the verified blank IDs; their original records remain in the raw arrays.

On the same font/hash above: **11 same-placement, 2 different-placement,
1 unsupported**. Of the five raw differences:

- Marked Arabic has identical glyph origins and terminal pen despite its
  different mark order and offsets.
- ZWNJ/ZWJ have identical placement after dropping verified blank,
  zero-advance glyphs. Visible glyphs are not discarded.
- Marked Hebrew has a genuine origin disagreement: glyph 1305 is at
  `(3081, 0)` in HarfBuzz and `(3208, 0)` in Litebird, **127 font units**
  apart. Both runs end at `(4532, 0)`. This narrows the investigation to a
  mark's placement, not merely a reordered output list.
- Combining Latin remains different: composition changes the glyph IDs and
  count, so absolute origins alone cannot establish visual equivalence.

No raw mismatch is hidden or turned into a passing baseline. Exit codes still
use the strict raw comparison. `same-placement` means only equal glyph IDs,
origins and terminal pen under the stated blank-glyph rule, not equal pixels,
paint order, clusters, caret behavior or correctness of either engine.

The self-test covers compensating mark reordering, zero-advance blank removal,
changed glyph IDs and both offsets/advances, duplicate marks, terminal advance,
vertical pen accumulation, nonblank zero-advance glyphs, `.notdef`, and empty
runs. These controls run in the existing CI self-test step without HarfBuzz.

## Limits

No bidi paragraph algorithm, fallback, CSS layout, rasterization, language
selection, feature switches or variable-font axes are tested. Litebird's
public result has no source-cluster indices, so cluster correspondence is
not compared. The small hand-selected corpus is a starting point, not broad
script coverage. A font lacking a script cannot test that script. Run with
additional covered fonts before drawing conclusions about Indic or other
scripts. Engine fixes, broader font fixtures and visual equivalence checks
are separate work.
