# Finding 542 — No safe-integer radix defect reproduced

**Severity:** investigation only; no runtime defect demonstrated.

Repeated floating-point division in `js_number_to_string_radix` prompted a
question about whether safe integers could acquire incorrect digits. On the
baseline, `python3 tools/conformance/verify_number_safe_radix.py` finds
**11,235/11,235** results agree with both Python's exact integer arithmetic
and Node, across radices 2–36 and seeded boundary and random inputs. A sample
of 2^53 is included as a representable boundary, not as a safe integer.

No engine implementation was changed. Phase 542 adds a reusable differential
oracle and fixed regression cases. This is sampled evidence, not a proof for
every safe integer. It does not establish correctness for fractions or for
integer values outside the safe range; a difference from Node for large values
alone does not establish a violation.
