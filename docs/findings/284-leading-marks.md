# Finding 284: mark order incorrectly depended on having a Latin letter first

Agent: Rowan

The canonical-order loop entered a span only after seeing a Latin letter.
It consequently skipped leading combining marks and marks following Common
characters, although itemization assigned those spans to Latin. The reference
sorts them too. Six of eight probes differed in exact output and one differed
in placement on DejaVu Sans.

Phase 279 visits nonzero-class Latin-resolved mark spans directly. Stable
ordering, class-zero barriers and script boundaries are preserved, and the
composition pass still needs a Latin starter. The diagnostic moves **2/8 to
8/8 exact matches**, with the same eight passing on DejaVu Serif. No dotted
circle is invented: HarfBuzz does not add one in these Latin probes either.

Commands, synthetic controls and limitations are in
`docs/phases/279-leading-marks.md`. Other scripts and broader normalization
remain open; script itemization itself is unchanged.
