# Finding 503 — an inline-block's baseline missed its own top padding

**Severity:** layout correctness. The line grew to contain an inline-block's
`padding-top`, but its baseline position was still chosen by centering fragment
rectangles. The text inside the inline-block therefore sank relative to the
surrounding text.

**Found in:** the open follow-up recorded by Phase 274 (finding 278). That note
identified the line-height growth and retained sink but had no test that could
distinguish a fix from a no-op.

## Reproduction

A 16px font with `line-height: 20px`, followed by a `display:inline-block` with
20px of text line-height and `padding-top: 10px`, makes a 30px inline fragment.
The old centering placed the outside text 5px below the line top, while the
inline-block's own text began 10px below its content top. The two baselines
were therefore 5px apart.

The permanent regression is
`tests/suites/503-inline-block-baseline-padding.cpp`: it checks one parent line,
compares the global baseline of its preceding text to the nested inline-block
line baseline, and checks the 30px line height. The baseline coordinates are
part of the geometry the test checks, not a screenshot-only claim.

## Why it survived

The line builder knew the inline-block's total margin-box height, so it grew
the line correctly. It did not know where the inline-block's baseline was: its
fallback centered the fragment in the newly taller line. A 1.6px top padding
can happen to match half of the line's 3.2px growth, masking the defect; larger
or asymmetric top padding separates the baselines.

## Resolution

Phase 503 calculates baseline ascent and descent, aligns baseline fragments to
the final line baseline, and obtains an inline-block baseline from the last
in-flow nested line box (or the bottom margin edge when there is none). The
engine's font metrics are reused so the line builder and painter agree on the
text baseline. Non-baseline vertical-align handling is unchanged.

## Remaining scope

The all-inline-block tabs vertical-stacking sizing defect noted beside this
finding in Phase 274 is independent and remains open. This finding does not
claim complete CSS vertical-align support.
