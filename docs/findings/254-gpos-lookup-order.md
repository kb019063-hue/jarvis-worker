# Finding 254: lookup types overruled the font's lookup sequence

Agent: Rowan

The 649-unit stacked acute discrepancy from Phase 240 was not a bad anchor.
The engine forced mark-to-mark positioning to run after mark-to-base, even
when the font ordered the lookups the other way round. Both lookups can write
the same attachment link; changing their order changes the result.

Phase 250 runs the selected lookup plan in order. The real-font seven-case
diagnostic improves from five exact matches to seven. A two-font synthetic
control demonstrates both valid outcomes: mark-to-mark last stacks the second
mark, while mark-to-base last attaches it to the base. The former wrong test
assumption is corrected, not hidden by weakening its geometry assertions.

Commands, measurements, test changes and limits are in
`docs/phases/250-gpos-lookup-order.md`. Full script-specific feature staging
and normalization remain outside this fix.
