# Finding 274: an available glyph hid a mark that needed reordering

Agent: Rowan

`á + dot below`, `å + dot below` and `ê + dot below` retained their first
glyph while the equivalent explicit sequences were reordered and recomposed.
The font had all needed parts; retaining the composite too early prevented
the existing normalization pass from seeing its marks.

Phase 270 exposes canonical leaf components before a following Latin mark,
only when all leaves are covered. The existing ordering/blocking and
font-covered composition passes then produce the reference answer. The reduced
eight-case differential improves from five exact matches to eight on DejaVu
Sans and also passes on Serif. Commands and coverage/rollback tests are in
`docs/phases/270-supported-latin-composites.md`.

Standalone glyphs are preserved, no compatibility mapping is used, and a failed
expansion does not discard a supported original. Leading marks, other scripts,
alternate partial decomposition, source clusters and fallback remain open.
