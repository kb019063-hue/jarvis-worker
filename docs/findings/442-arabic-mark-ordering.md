# Finding 442: Missing Arabic mark ordering and pre-mark decomposition before composition

Agent: Curlew

`TrueTypeFont::shape_glyphs` decomposed precomposed starters before combining
marks and sorted combining-mark spans only for `kTagLatn`, while running Arabic
canonical pair composition directly over raw input order and raw Unicode
canonical combining classes (`CCC`). Consequently, `U+0651` SHADDA (`CCC 33`)
remained after vowels and tanwin (`CCC 27..32`) so `GSUB` `ccmp` mark ligatures
missed reordered `shadda + vowel` sequences; UTR #53 Modifier Combining Marks
such as `U+0655` HAMZA BELOW and `U+0654` HAMZA ABOVE did not promote to the
front of Arabic mark spans (`0627+0653+0655` composed `U+0622 + U+0655` instead
of `U+0625 + U+0653`); and precomposed starters before following marks
(`0622+0655`, `0623+0655`) retained their original composite instead of
interleaving the following mark.

Phase 442 extends same-face canonical decomposition to `kTagArab` (for missing
standalone composites and covered composites before a following `kTagArab`
mark), orders `kTagArab` mark spans by OpenType / HarfBuzz modified combining
classes (`CCC 27..32 -> 28..33`, `33 -> 27`) plus UTR #53 MCM promotion
(`220 -> 25`, `230 -> 26`), and carries those effective classes into canonical
pair composition. Against HarfBuzz 12.1.0 on `DejaVuSans.ttf` and
`DejaVuSans-Bold.ttf`, same-placement matches reach **486/486** on
`arabic_marks_diagnostic.py --matrix` (up from `484/486`) and **16/16** on
`--ordering` (up from `3/16`), with `DejaVuSansMono-Bold.ttf` improving
**368/384 → 370/384** (`--ordering` **8/16 → 15/16**).

Still open: RTL within-cluster `[base, mark]` vs `[mark, base]` output order
(including unattached zero-advance mark origins on unanchored `DejaVuSansMono`
composites), `U+034F` CGJ and other default-ignorable controls, non-Latin /
non-Arabic / non-Hebrew script normalization, `GPOS`-less fallback mark
positioning, and public cluster indices. Details and hashes are in
`docs/phases/442-arabic-mark-ordering.md`.
