# 523. Shortest exponential output used another decimal's exponent

The shortest significand came from one representation and the exponent from a
17-digit expansion, so a rounded power of ten could become ten times too small.
Phase 523 derives both from the same shortest decimal. Node comparison improved
**3,110/3,412 → 3,412/3,412**; test262 stayed 14/15. See
[Phase 523](../phases/523-exponential-shortest.md). The underlying general
shortest-decimal converter was not exhaustively audited.
