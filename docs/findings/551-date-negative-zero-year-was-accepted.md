# Finding 551: the ISO parser erased a negative-zero expanded year

## What was wrong

`parse_iso` read the sign and six year digits, multiplied the integer year
by the sign, then accepted `-000000` as year zero. ECMA-262's expanded-year
representation forbids this spelling. Both Date.parse and the Date
constructor use this parser; pinned test262 has a rejection case for each.

## What changed

Only `src/js/BuiltinsDate.cpp` changes in production. Once the existing
ISO grammar is recognized and validated, the parser tests the retained
sign and zero year. A negative-zero year yields a recognized NaN result,
not `false` (which would ask the caller to try legacy parsing). Positive
year zero and nonzero expanded years keep their existing behavior.

## Verification

`tests/suites/551-date-negative-zero-year.cpp` asserts an explicit final
**`ok 111`** result in C++. The fixed engine passes **111/111** checks in
UTC, America/New_York and Asia/Kolkata. The baseline fails **63** checks.
Coverage includes two parser entry points, plain and boxed strings,
positive-zero/neighboring year controls, TimeClip boundaries, conversion
ordering and exceptions, and collection inside input hooks.

Same-base test262 at `2e58f87d8350ad51224617a041c6e041d52f8444` improves
**593 → 595 / 610** Date tests, **two gained and zero lost**:
`built-ins/Date/parse/year-zero.js` and `built-ins/Date/year-zero.js`.
The parse directory improves **6 → 7 / 8**.

Node v22.22.3 agrees on **103/111** focused checks; the eight differences
are two invalid date-only shapes accepted through its legacy heuristics
(plain/boxed inputs through parse/constructor). Rejecting those forms is
this ISO parser's policy, not a claimed conformance win over Node. All
pinned test262 negative-zero cases are date-times and agree with Node.

The canonical **-O1 -g ASan/UBSan engine** also passes all **111 checks
in three zones** and **595/610** Date test262 cases with no sanitizer
reports. Plain full units pass **583/583** suites and **114/114**
cross-origin checks. See [Phase 551](../phases/551-date-negative-zero-year.md)
for commands, broader gates, the initial parallel-load JS timeouts and
successful standalone rerun, and local validation limits: full local
asan/deep aggregates were not run, and complete default full CI must
pass before merge.

## What is still open

Legacy heuristics, other ISO validation, negative-year formatting,
timezone algorithms and realms are separate. The change recognizes the
forbidden expanded zero year in the existing ISO path; it does not make
the entire Date parser strictly ISO-only.
