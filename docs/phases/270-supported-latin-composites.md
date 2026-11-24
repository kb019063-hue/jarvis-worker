# Phase 270: expose a composite's parts before following Latin marks

Agent: Rowan

## What was wrong

The Phase 266 pass ordered explicit Latin marks but retained a supported
precomposed glyph. Thus `á + dot below` disagreed with `a + acute + dot below`
and HarfBuzz: the acute hidden inside á could not be reordered with the dot.
The same issue appeared for å and ê. The eight-input diagnostic had five
exact matches and three differences.

## What changed

A Latin codepoint immediately followed by a nonzero-combining-class mark in
its Latin run now attempts full recursive canonical decomposition even if it
has a glyph. Every leaf must be covered by the same face. Only a complete
expansion is committed; any missing part preserves the original codepoint.
Existing canonical ordering and font-aware blocked composition then operate
on the exposed sequence. Standalone supported composites remain untouched,
as do composites separated from marks by spaces or class-zero controls.

The missing-composite path from Phase 240 still stops at supported intermediate
components when no following mark requires full expansion. Compatibility
mappings, other scripts and fallback-face selection are unchanged. No font
names, offsets or glyph IDs are special-cased.

## Verification

```
make test
make build/shaping-probe
build/shaping-venv/bin/python tools/shaping/supported_composite_diagnostic.py \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf --output build/supported.json
```

Dependency setup and font hash are in `tools/shaping/README.md`. With HarfBuzz
12.1.0 and the recorded DejaVu Sans, the eight cases move **5/8 → 8/8 exact
raw and placement matches**; DejaVu Serif also gives 8/8 after the change.
The original 14-case corpus remains 11 exact matches, 2 RTL representation
differences and 1 unsupported; all 13 supported cases agree in placement.

`make test`: **409 suites and 114 cross-origin checks pass**. A synthetic-font
suite checks composed versus explicit spellings, recursive decomposition,
equal-class recomposition, preservation of standalone glyph metrics, space
and join-control boundaries, absent-leaf rollback and input immutability.
No existing test or README suite count changed. Suite shape and metrics checks
pass.

## Still open

This is a covered-parts Latin repair, not full Unicode NFC or complete HarfBuzz
normalization. A font missing a required leaf retains the original composite;
we do not search alternate partial representations. Leading marks, non-Latin
normalization, source clusters, fallback selection and script/language feature
plans remain separate. Shared glyph-output agreement is not a browser pixel
conformance claim.
