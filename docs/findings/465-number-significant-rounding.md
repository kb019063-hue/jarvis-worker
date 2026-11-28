# 465. Significant-digit formatting rounded ties to even

`toExponential` and `toPrecision` used the C library's ties-to-even rounding,
not ECMAScript's larger-magnitude tie rule. Phase 465 rounds an exact decimal
expansion explicitly. Node comparison: **4,418/4,428 → 4,428/4,428**;
test262 for both methods **27/32 → 28/32**, none lost. See
[Phase 465](../phases/465-number-significant-rounding.md). The shortest
exponential path and other number formatters were not audited here.
