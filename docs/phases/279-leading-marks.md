# Phase 279: leading Latin-resolved mark spans

Agent: Rowan

## Evidence

Phase 266 sorted marks only after a codepoint classified as a Latin letter.
Leading marks and marks after Common characters (space, dotted circle) kept
their input order, even when script itemization resolved their span to Latin.
The eight-case `leading_marks_diagnostic.py` exposes six exact-output differences
against HarfBuzz 12.1.0 on the recorded DejaVu Sans: **2/8 exact matches**,
7/8 same-placement matches. The three-class sequence also differs in placement.
HarfBuzz does not insert a dotted circle in these Latin-script probes.

## Repair

Sort each contiguous nonzero-combining-class span whose resolved script is
Latin, rather than looking for a Latin starter first. The sort stays stable;
equal classes preserve order. Class-zero characters and script boundaries end
the span. Composition still requires a real Latin starter: this does not turn
an orphan mark into a base or insert a dotted circle.

The existing itemizer decides which script a leading span belongs to. A span
before Hebrew is not sorted by this Latin pass; Hebrew's existing shaping rules
remain separate. No script-itemization change or claim is made here.

## Verification

```
make test
make build/shaping-probe
build/shaping-venv/bin/python tools/shaping/leading_marks_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/leading.json
```

Setup and recorded font hash: `tools/shaping/README.md`. With uharfbuzz 0.52.0 /
HarfBuzz 12.1.0, all eight probes now match exactly in DejaVu Sans and Serif.
The original 14-case corpus remains 11 exact matches, two RTL representation
differences, and one unsupported; all 13 supported placements agree. The Phase
270 supported-composite diagnostic still gives 8/8 exact matches.

Local tests: **414 suites and 114 cross-origin checks pass**. The new synthetic
suite checks three-class ordering, canonical input, equal-class stability,
leading spans before Latin and Hebrew, space/ZWNJ/explicit-circle contexts,
CGJ and space barriers, unchanged non-Latin runs, no synthesized circle,
zero advance of a lone mark, empty input and input immutability. The Phase 266
assertion documenting its former leading-mark limitation now expects sorted
glyphs. No comparator tolerance or test floor was weakened. README's suite
count is left to the landing session.

## Limits

This addresses leading marks resolved to Latin, not all leading marks in every
script. Non-Latin normalization, alternate partial decompositions, source
clusters, fallback-face selection and script/language feature plans remain
open. Dotted-circle behavior for other script shapers is not implemented by
this patch. Eight exact probes do not imply full NFC or browser conformance.
