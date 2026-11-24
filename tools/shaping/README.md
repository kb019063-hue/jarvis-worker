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

## Hebrew discrepancy reduced to three codepoints

Run the diagnostic with the same CLI and font as the main comparator:

```sh
build/shaping-venv/bin/python tools/shaping/hebrew_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --output build/hebrew-report.json
```

On the DejaVu Sans hash above, four of five cases have the same placement.
The failing minimal input is **U+05E9 U+05B8 U+05C1** (SHIN, QAMATS, SHIN
DOT). Qamats (glyph 1305) has absolute x=0 in HarfBuzz and x=127 in
Litebird. Removing either mark makes placement agree. Swapping the marks to
**U+05E9 U+05C1 U+05B8** also makes placement agree. HarfBuzz produces the
same output for both mark orders, whereas Litebird changes its placement.
The two strings are canonically equivalent (combining classes 18 and 24).
This establishes an order-sensitive shaping discrepancy, not just an RTL
output-array convention or an advance-width disagreement.

Source inspection points to the interaction of mark ordering and attachment:
`shape_glyphs` passes the input order into substitutions/positioning, resolves
mark attachment chains, then reorders RTL clusters. This investigation does
not isolate a particular lookup as the sole cause, nor prove that sorting by
canonical combining class would fix it (the failing input is already in
that order). A Hebrew-specific preprocessing/attachment investigation is
needed before changing the engine. These five probes preserve the evidence
for that separate fix; no engine behavior or expected answer was changed.

### Phase 200 follow-up

The fixed-position Hebrew ordering repair makes all five reduced cases agree
in absolute placement (the earlier measurements above are the before state).
Add `--permutations` to `hebrew_diagnostic.py` for the 12-case permutation
corpus: qamats/shin-dot/dagesh in all orders after SHIN and BET. On the same
font, placement improves from 4/12 to 12/12; raw sequence differences remain
visible and may still make the command exit 1. Full details and remaining
limits: `docs/phases/200-hebrew-mark-order.md`.

## Shared-rasterizer ink diagnostic

`ink.py` takes an existing shaping report and renders **both recorded runs**
through the same FreeType face. It asks whether different glyph representations
produce the same grayscale coverage and terminal advance at selected sizes.
It does not run Litebird's rasterizer, compare browser screenshots, or certify
text selection/caret behavior. A common rasterizer can hide its own defects;
this is a shaping-output comparison, not an independent rasterizer oracle.

```sh
build/shaping-venv/bin/pip install -r tools/shaping/requirements-ink.txt
# First run compare.py as above, retaining its report even when it exits 1.
build/shaping-venv/bin/python tools/shaping/ink.py \
  --report build/shaping-report.json \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --sizes 16 32 64 --output build/shaping-ink.json
python3 tools/shaping/test_ink.py
build/shaping-venv/bin/python tools/shaping/test_ink.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

The font hash and units per em must match the source report. The checked bytes
are passed directly to FreeType. The output records the font hash, source-report
content hash, FreeType and binding versions, source HarfBuzz versions, sizes,
coverage differences, ink bounds and terminal pens. A different FreeType version
is a different experiment. Optional dependency: `freetype-py==2.5.1`; its native
FreeType must also be available (this run reports 2.13.2).

Policy: no hinting or embedded bitmaps, normal 8-bit grayscale masks. Sum pen
positions in font units, round each absolute glyph origin once to 26.6 pixels
(ties away from zero), retain fractional translations, and composite masks in
run order using integer source-over coverage. The sparse canvas has no viewport
clipping or per-run recentering. Final advances are compared in font units, so
blank-space width errors cannot pass just because both masks are empty. A
missing glyph in either run is unsupported even when both .notdef masks agree.
Exit 0: all cases match ink and advance; 1: difference or unsupported; 2: input,
font, dependency or rendering error. Original reports and their strict failures
are never rewritten. This optional diagnostic is not a CI conformance floor.

### Measurement after the Hebrew fix

DejaVu Sans hash recorded above, HarfBuzz 12.1.0, FreeType 2.13.2,
freetype-py 2.5.1: 14 cases at each of 16, 32 and 64 px.

- 12 cases match ink and advance at every size (36 matching case/size pairs).
- The `combining` case differs at each size: **11 / 24 / 49 changed pixels**.
- Devanagari remains unsupported (3 case/size pairs), never counted as a match.

Thus the composed/decomposed Latin output difference is not pixel-identical
under this rendering policy. This does not establish a visible browser defect,
a normalization fix, or a perceptual severity: differences can involve outline
construction, placement quantization and antialiasing as well as positioning.
The raw and placement reports remain the evidence for investigating those.

Controls: hand-computed source-over values and stride/negative-coordinate tests;
changed/missing/shifted coverage, invalid glyph IDs and fields; actual FreeType
identity, missing glyph, half-em shift, advance-only change and .notdef tests;
and rejection of a font hash mismatch. The integration test is explicit and
fails if the font or FreeType is missing; it does not silently skip coverage.

## Font-unit outline diagnostic: reduce the Latin difference

The ink result above is not solely a pixel-rounding effect. With the same
DejaVu Sans hash, HarfBuzz 12.1.0 and FreeType 2.13.2, the reduced input
`a + U+0301` has identical base contours and terminal advance (1255 units),
but the acute's four-point contour is translated **10 font units left** in
Litebird's run. HarfBuzz's contour bounds are `(453,1262)..(932,1638)`;
Litebird's are `(443,1262)..(922,1638)`. Their translation-independent shape
hashes agree. Litebird also has a one-point degenerate contour; the diagnostic
deliberately retains it rather than assuming every contour paints ink.

`latin_diagnostic.py` isolates five inputs: plain `a`, precomposed `á`,
decomposed `a + acute`, `x + dot below + acute`, and the original combined
string. Only the decomposed `a` and original string disagree. The ink diagnostic
reproduces exactly the earlier 11/24/49 changed pixels for both at 16/32/64 px;
the other three match at each size. This is a statement about the specified
font, not a universal rule that a font's separately positioned mark and its
precomposed glyph must have identical outlines.

```sh
# The same optional FreeType requirements as ink.py.
build/shaping-venv/bin/python tools/shaping/latin_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/latin.json
build/shaping-venv/bin/python tools/shaping/outlines.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --report build/latin.json --output build/latin-outlines.json
build/shaping-venv/bin/python tools/shaping/ink.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --report build/latin.json --output build/latin-ink.json
python3 tools/shaping/test_outlines.py
build/shaping-venv/bin/python tools/shaping/test_outlines.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

The first three commands intentionally exit 1 on differences. No original
report is modified. Outline exit 2 is an input/dependency error; missing glyphs
are unsupported and never pass by matching .notdef shapes.

`outlines.py` verifies font SHA-256 and units per em, loads those exact bytes,
and asks FreeType for unhinted, unscaled, expanded outlines. It adds the recorded
pen and offset in font units, before any pixel quantization. It compares a
multiset of contours, preserving duplicates, point tags (on/conic/cubic),
winding and fill-rule flags while ignoring only the cyclic start of a contour.
JSON preserves every unmatched point, its control-point bounds (not exact
Bezier ink bounds), and a translation-independent shape hash. Terminal advances
are compared separately, and reference versions and source-report hash recorded.

Limits: matching contour multisets does **not** guarantee equal pixels. Glyph
grouping, draw order and overlap compositing are not compared. Different Bezier
representations can describe the same curve and still fail this strict test.
FreeType expands composites and supplies both outlines; this is not independent
verification of its outline parser, nor of Litebird's rasterizer. Arbitrary
font/case resource usage is not benchmarked. General normalization and a
font-independent engine repair remain separate work; no font-specific 10-unit
adjustment belongs in the engine.

### Phase 234: adjacent Latin composition repair

The earlier Latin measurements remain the before state. The engine now consults
Unicode's canonical composition pairs for adjacent Latin input and substitutes
only a composite present in the current face. On the recorded DejaVu Sans hash,
the five-case Latin reducer now gives **5/5 exact raw matches**, and all 15 ink
case/size pairs match with zero changed pixels. The 14-case corpus gives 9 raw
matches, 4 raw differences and 1 unsupported; all 13 supported cases match
absolute placement. Full normalization is not claimed. Coverage guards,
synthetic tests, reproducible commands and remaining limitations are in
`docs/phases/234-latin-font-aware-composition.md`.

## Missing-composite coverage probe (Phase 240)

Create a local test font with only three cmap mappings removed. Outlines and
positioning tables remain intact. Do not distribute it as the original font.
Generated fonts and reports belong in ignored `build/`.

```sh
build/shaping-venv/bin/pip install fonttools==4.61.1
build/shaping-venv/bin/python - <<'PY'
from fontTools.ttLib import TTFont
f = TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', recalcTimestamp=False)
for table in f['cmap'].tables:
    if table.isUnicode():
        for cp in (0x00e1, 0x00e5, 0x01fb):
            table.cmap.pop(cp, None)
f.save('build/dejavu-missing-composites.ttf')
PY
build/shaping-venv/bin/python tools/shaping/decomposition_diagnostic.py \
  --font build/dejavu-missing-composites.ttf --output build/decomposition.json
```

Using the source DejaVu Sans hash recorded above and HarfBuzz 12.1.0, Phase 240
changes seven raw comparisons from 3 matches / 1 mismatch / 3 unsupported to
**5 matches / 2 mismatches / 0 unsupported**. Exact generated font bytes are
identified in the JSON report. Missing á and å now match; ǻ decomposes but
retains a positioning disagreement also present for explicit a + ring + acute:
Litebird's acute y-offset is 649, HarfBuzz's is 0. The latter input already
failed before the fix. The command correctly exits 1; do not interpret absence
of .notdef as correct positioning. A build of the parent can be selected with
`--probe` for the before run. The existing 14-case corpus remains unchanged.

Scope: missing Latin codepoints only, canonical mappings only, complete
same-face coverage required. No changes to fallback-face selection, existing
composite glyphs, other scripts, normalization ordering or stacked-mark GPOS.

### Phase 250: the two stacked-mark mismatches are repaired

The Phase 240 before/after figures above are historical. The engine had grouped
GPOS by lookup type, forcing mark-to-mark last regardless of the font's order.
Executing the active lookup sequence in order changes the seven-case missing-
composite diagnostic from **5/7 to 7/7 exact matches** with HarfBuzz 12.1.0.
For both ring/acute spellings the acute y-offset is now 0 rather than 649.
The font construction and diagnostic commands are unchanged; there is no
allowed-failure baseline. The broader 14-case corpus and 12 Hebrew placement
controls are unchanged. Details: `docs/phases/250-gpos-lookup-order.md`.
