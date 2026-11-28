# 500. Math.hypot lost many small squared terms

The multi-argument implementation used an uncompensated sum, so small terms
following a larger square could each disappear. Phase 500 compensates the
scaled sum. Against 140-digit Decimal and Node, **249/279 → 279/279** cases
are within two ulps; test262 remained 12/12. See
[Phase 500](../phases/500-math-hypot-accuracy.md). Correct rounding for all
inputs is not claimed.
