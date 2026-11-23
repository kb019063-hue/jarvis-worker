# Phase 240: a missing Latin composite can use covered canonical parts

Agent: Rowan

## What was wrong

Phase 234 chose a font's composite when it existed, but the inverse case still
mapped a precomposed Latin character to .notdef even when that same face could
shape all its canonical parts. This is a single-face shaping issue; font-family
fallback selection is a different layer and is not changed here.

## What changed

Before the existing composition pass, a missing Latin codepoint is looked up
in the generated Unicode 17 canonical decomposition table. Recursion stops at
supported glyphs, preserving available intermediate composites. An expansion
is committed only when **every part is supported**; on any failure all tentative
parts are discarded and the original codepoint remains. Existing glyphs are
never decomposed. Compatibility mappings and other scripts are not consulted.
Depth (16) and expansion (32 scalars) bounds are defensive limits over trusted
generated data, not advertised Unicode coverage limits.

## Verification

`make test`: **384 suites and 114 cross-origin checks pass**. New standalone
suite `tests/suites/240-latin-covered-decomposition.cpp` uses synthetic fonts:
covered base+mark, recursive a-ring-acute, preserving direct and intermediate
composites, rollback when a mark is absent, no compatibility ligature expansion,
other-script exclusion, unchanged input and empty input. No existing test was
weakened or changed.

The reproducible real-font experiment is in `tools/shaping/README.md`: remove
only the Unicode cmap mappings for á, å and ǻ from a local DejaVu Sans copy,
leaving glyph outlines and GSUB/GPOS intact, then run
`tools/shaping/decomposition_diagnostic.py` against that same copy in both
engines. FontTools 4.61.1; uharfbuzz 0.52.0 / HarfBuzz 12.1.0.

Before: **3 matches, 1 mismatch, 3 unsupported**. After: **5 matches, 2
mismatches, 0 unsupported**. The two simple missing composites now match
HarfBuzz exactly. The recursive composite now emits the correct glyph IDs,
but reaches the pre-existing stacked-mark positioning discrepancy also exposed
by the explicit-parts control; it is not counted as passing.

The original 14-case DejaVu Sans corpus is unchanged: 9 raw matches, 4 RTL
representation differences, 1 unsupported; 13 supported absolute-placement
matches. Suite-shape, docs and number checks pass; README count is untouched.

## Still open

Stacked a-ring-acute: Litebird puts the acute 649 font units above HarfBuzz in
the altered face, with both engines emitting the same glyph IDs and advances.
The explicit decomposed input had this disagreement before the change. The
new diagnostic retains both cases and exits 1. Also open: full normalization,
canonical ordering and blocking, other scripts, source clusters and fallback
selection before a face is chosen. This phase does not claim full HarfBuzz
agreement or browser pixel conformance.
