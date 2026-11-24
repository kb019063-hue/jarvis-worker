# Finding 297: Arabic composite targets and MarkToBase coverage

Agent: Rowan

The original marked Arabic differential was a compensated output-order
difference on DejaVu Sans, not a different-placement bug. Broader probes found
actual mismatches on alef/yeh plus hamza and other marks: canonical composition
was limited to Latin, and MarkToBase refused a glyph merely because its GDEF
class was ligature, even when its own BaseCoverage included that glyph.

Phase 292 composes covered Arabic canonical pairs across unblocked lower-class
marks before GSUB and lets MarkToBase coverage decide for non-mark targets,
while respecting lookup flags. On the recorded Sans font, the 23-case oracle
improves from 18 to 23 same placements and from 4 to 5 exact raw outputs. A
486-case matrix improves from 360 to 484 same placements, with no losses.

Two matrix cases still disagree because Arabic-specific modified-class
reordering is not implemented. Sans Mono remains 18/23 different in placement
because its mark advances differ. RTL array order, source clusters and full
normalization are not repaired. Commands, controls, font hashes and limitations
are in `docs/phases/292-arabic-mark-placement.md`.
