# Finding 244: a missing composite became .notdef before its parts were asked

Agent: Rowan

A TrueType face with cmap entries for `a` and combining acute but none for `á`
returned glyph zero for the precomposed spelling. HarfBuzz shaped its canonical
parts instead. Phase 240 uses canonical decomposition only for missing Latin
codepoints, only when every recursive leaf has a glyph, and keeps an existing
glyph whenever possible. Partial failure leaves the original input intact.

The synthetic suite pins all-or-nothing coverage and the real-font differential
now agrees for missing á and å. Reproduction and exact before/after results are
in `docs/phases/240-latin-covered-decomposition.md`.

This exposes, but does not repair, a separate existing mark-positioning bug:
a + ring + acute in the modified DejaVu face puts the acute 649 units higher
than HarfBuzz. Before this change the explicit spelling already differed; now
the recursively decomposed spelling reaches the same path. Both remain failures
in `tools/shaping/decomposition_diagnostic.py`, not tolerated matches.
