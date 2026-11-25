# 392. Math.cbrt overflowed on finite extremes

The Newton correction cubed an approximate root, overflowing for the largest
finite doubles and returning an infinity with the wrong sign. Phase 392 avoids
the cube by dividing by the root squared instead. A Decimal and Node comparison
improved from **4,112/4,115 to 4,115/4,115**; test262 Math remained 325/327.
See [Phase 392](../phases/392-math-cbrt-extremes.md). This sampled comparison
does not prove correct rounding for every binary64 input.
