# Phase 292: Arabic composition and MarkToBase coverage

Agent: Rowan

## Finding the problem

The old `arabic-marks` corpus case has the same glyph origins and final pen as
HarfBuzz on DejaVu Sans; its raw mismatch is a different RTL cluster/output
representation. Reversing arrays to improve a score would not fix placement
and would change an existing engine contract.

A larger oracle instead found real differences. For `ا + fatha + hamza above`,
HarfBuzz uses alef-with-hamza plus a positioned fatha; the engine substituted a
ligature of the two marks on plain alef. It also left marks at the pen for
already-composed alef-with-hamza and yeh-with-hamza. In DejaVu Sans those
composites are GDEF ligature-class glyphs **and are listed in the font's
MarkToBase coverage**. The engine rejected every ligature-class base before
asking that coverage. Joining `lam + alef + hamza` likewise differed because
composition did not happen before the Arabic joining/GSUB pass.

## Changes

The existing canonical-pair, same-face covered composition pass now also
tracks Arabic starters. It can compose through unconsumed lower-class marks;
equal/higher classes block and class-zero or script boundaries reset the
starter. It uses Unicode canonical pairs, not compatibility mappings or
font-specific glyph/offset exceptions. This pass does **not** implement
Arabic-specific modified combining-class reordering or claim full Unicode
normalization.

MarkToBase now consults its coverage even for a non-mark target classified as
a ligature by GDEF. Lookup filtering flags still apply; missing coverage or
anchors still fail. HarfBuzz's MarkBasePos implementation searches for a
non-mark and checks BaseCoverage, without imposing this extra class restriction:
`src/OT/Layout/GPOS/MarkBasePosFormat1.hh` in the HarfBuzz source.

## Verification

```
python3 -m venv build/shaping-venv
build/shaping-venv/bin/pip install -r tools/shaping/requirements.txt
make build/shaping-probe
build/shaping-venv/bin/python tools/shaping/arabic_marks_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/arabic.json
build/shaping-venv/bin/python tools/shaping/arabic_marks_diagnostic.py --matrix \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/arabic-matrix.json
```

The diagnostic intentionally exits 1 while any raw mismatch remains. Use
`--probe build/shaping-probe-before` with a binary built from the parent commit
for a before report. HarfBuzz 12.1.0 / uharfbuzz 0.52.0, DejaVu Sans SHA-256
`abdc775b21b1bc470d50c97e790d276f2054b7504e56e5bd3e64f48d68582322`:

- 23 selected cases: same placement **18 → 23**; exact raw **4 → 5**.
  The 18 remaining raw mismatches include the existing RTL representation.
- 486 exhaustive pairs of nine named marks on six bases: same placement
  **360 → 484**, with 124 improved, zero lost and two still different.
  All 486 raw comparisons differ due to the RTL contract.
- DejaVu Sans Bold SHA-256 `0d977336a6d5fba34eab8e3199eb218327161b5143749f802982c2bc34df0c96`:
  selected same placements **18 → 23**, exact raw 4 → 5.
- DejaVu Sans Mono SHA-256 `0f5db4f1749979d961019838b160bec74abdf7f9eca69553fe1aa856bbff49a4`:
  selected same placements **1 → 5**, exact raw 1 → 5. Eighteen remain
  different: unlike HarfBuzz, the engine retains a nonzero advance for marks
  in this font. That is a separate unfixed defect, not a match.
- Original 14-case corpus: unchanged at 11 raw matches, 2 representation
  differences and 1 unsupported, with 13 supported same placements. Hebrew
  permutation diagnostic: 12/12 same placements, 0/12 exact raw. Latin
  mark-order diagnostic: 8/8 exact.

`make test`: **421 suites and 114 cross-origin checks pass**. New synthetic
coverage checks lower/equal class blocking, adjacent/cross-mark composition,
coverage failure rollback, space/ZWNJ barriers and input immutability; and,
separately, normal-class and ligature-class MarkToBase coverage, a noncovered
ligature-class glyph and IgnoreLigatures filtering. The test uses no host font.
Suite shape, metrics and docs checks pass; README suite count is unchanged.
Full conformance, WPT, differential trees/XML, fuzz and deep-page checks pass
(the WPT run has 255 listed expected failures and none unlisted). A standalone
ASan/UBSan shaping probe completes the 23-case diagnostic with the same results.
The full local `make asan` did **not** complete: its large unit-suite compile
reached almost the sandbox's 3.8 GiB memory limit and was stopped to avoid an
out-of-memory failure. CI's four sanitizer jobs remain required.

## Limits

HarfBuzz's Arabic shaper reorders some marks using modified classes. For example,
`alef + madda/hamza above + hamza below` produces the remaining two matrix
placement differences; this patch leaves that script-specific ordering open.
Mono mark advance, full normalization, other scripts, source clusters,
fallback and RTL raw output contracts also remain open. The origin comparator
ignores drawing order and is not a pixel or browser conformance test.
