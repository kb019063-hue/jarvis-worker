# Phase 542 — Safe integer radix conversion regression coverage

Finding 542. The repeated double division in nondecimal number conversion
looked susceptible to rounding at large safe integers. The suspicion was
investigated before changing the implementation: no incorrect safe-integer
digits were reproduced, and no runtime code was changed.

`tools/conformance/verify_number_safe_radix.py` compares the engine and Node
against Python's exact integer `divmod` conversion in all radices 2–36. The
seeded cases include positive and negative safe integers, the upper safe
boundary and its neighbors, zero, and the exactly representable 2^53 boundary.
The separate suite `tests/suites/542-number-safe-radix.cpp` protects several
fixed large odd- and even-radix cases, negative values and negative zero in
`make test`. The Python oracle is deliberately restricted to integers: its
exact representation is not a general oracle for floating-point radix output.

## Verification

- `make -j2 all`: passed on the baseline.
- `python3 tools/conformance/verify_number_safe_radix.py`: **11,235/11,235**
  cases agree with Python and Node, both before and after the test addition.
- `make -j2 test`: **573/573** engine suites and **114/114** cross-origin checks passed.
- `python3 tools/ci/check_suites.py` and `python3 tools/ci/check_docs.py`: passed.

This is a regression-coverage change, not a claim that a formatting defect was
fixed. Fractional values and integer values outside the safe range are not
covered by the exact-integer oracle; implementations may choose different
round-tripping representations for the latter.
