# Phase 250: the font's lookup order decides the attachment

Agent: Rowan

## What was wrong

Phase 240 retained two failures: missing ǻ and explicit a + ring + acute in a
cmap-reduced DejaVu Sans face. Glyph IDs and advances matched HarfBuzz, but the
acute was 649 font units higher. The engine executed GPOS in three batches:
non-mark positioning, mark-to-base/ligature, then mark-to-mark. Thus it changed
the font's lookup order even though the script plan already held the active
LookupList indices in order. A later base attachment could never override an
earlier mark attachment.

## What changed

One pass executes the active GPOS plan in its existing lookup-index order,
including types 4, 5 and 6. Kerning enablement, contextual dispatch and deferred
attachment resolution remain intact. A later successful lookup may replace an
attachment; an inapplicable lookup leaves it alone. No glyph offsets, font names
or codepoints are special-cased. OpenType's LookupList processing order, not
lookup-type grouping, is the rule being restored.

## Verification

```
make test
make build/shaping-probe
# Create build/dejavu-missing-composites.ttf as in tools/shaping/README.md.
build/shaping-venv/bin/python tools/shaping/decomposition_diagnostic.py \
  --font build/dejavu-missing-composites.ttf --output build/order-after.json
```

On the recorded DejaVu Sans source, fontTools 4.61.1 / uharfbuzz 0.52.0 /
HarfBuzz 12.1.0: **5/7 → 7/7 exact raw and placement matches**. The 649-unit
acute offset becomes 0, exactly the reference answer. The before binary is
selected with `--probe`. The standard 14-case corpus remains 9 raw matches,
4 RTL representation differences, 1 unsupported; 13 supported placement
matches. All 12 Hebrew permutations still match absolute placement.

`make test`: **394 suites plus 114 cross-origin checks pass**. The new file
`tests/suites/250-gpos-lookup-order.cpp` builds two fonts with identical anchors
but opposite lookup orders. Base→mark gives a second mark y-offset of 500;
mark→base gives 300. The first mark remains at 300, x offsets and advances
stay equal, and a lone mark still attaches correctly in both orders. Both
kerning settings are checked. The old batching algorithm incorrectly gives
500 in both orders, so this is an order-sensitive regression test.

The old mark-rendering suite explicitly asserted that an inverted lookup order
should still stack marks. Its fixture now puts base before mark so its existing
stack-height and bitmap assertions remain valid; the opposite-order case lives
in the new suite. The optional real-font probe is now explicitly DejaVu Sans:
HarfBuzz reports both mark y-offsets as 0 for x + acute + circumflex, whereas
that test had asserted universal stacking. It now asserts the reference result,
not merely absence of a crash.

## Still open

Full font-aware normalization, source clusters, script/language-specific feature
staging, other unsupported OpenType behavior and cross-font fallback remain
separate. This restores the order of the currently selected lookup plan; it
does not claim that every script's plan is complete. Raw RTL representation
mismatches and missing Devanagari coverage remain visible in the strict oracle.
