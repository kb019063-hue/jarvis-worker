# 407. Number formatting rejected non-finite values too early

`toExponential` and `toPrecision` checked precision ranges before returning
non-finite receivers. Phase 407 keeps precision conversion observable but
returns the non-finite string before the range check; `toFixed` intentionally
keeps its different ordering. Node comparison: **104/138 → 138/138**.
See [Phase 407](../phases/407-number-format-special.md). Other formatting
accuracy and behavior remain outside this finding.
