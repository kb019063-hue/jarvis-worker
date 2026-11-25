# 429. toFixed erased a negative rounded zero

The formatter suppressed the sign when rounded digits were all zero, so
`(-0.0001).toFixed(2)` returned `0.00` instead of `-0.00`. Phase 429 chooses
the sign before rounding, while keeping actual negative zero unsigned. Node
comparison: **115/144 → 144/144**. See
[Phase 429](../phases/429-to-fixed-zero-sign.md). Other decimal rounding
behavior was not exhaustively audited.
