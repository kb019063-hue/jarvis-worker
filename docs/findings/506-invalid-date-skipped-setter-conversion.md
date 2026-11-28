# Finding 506: an invalid Date skipped setter conversion

## What was wrong

The shared Date setter helper returned NaN before converting any argument
when the receiver's time was NaN, except for the FullYear methods. User code
in valueOf or Symbol.toPrimitive never ran; even an intended throw vanished.
A FullYear recovery also applied LocalTime to calendar +0, incorrectly
changing its default month/day/time in non-UTC zones.

## What changed

`src/js/BuiltinsDate.cpp` now captures the receiver's time before conversion,
converts all supplied arguments in order, then handles the captured NaN.
Non-FullYear methods return NaN without overwriting a hook's mutation;
FullYear methods calculate from calendar +0 when the snapshot was invalid.
Abrupt conversions propagate without writing the slot. Local FullYear's
LocalTime operation remains before optional month/day conversion. The
receiver check, arithmetic, clipping and valid-date defaults stay intact.

## Verification

The 222-check suite exercises all 14 shared-helper methods, including
mutation/exception identity, optional arguments, conversion ordering and
receiver validation. It passes in **UTC**, **America/New_York** and
**Asia/Kolkata**, in both the plain and an explicit **-O0 -g0 ASan/UBSan**
engine build. The base `c89f70e` fails **150** checks in UTC and **156**
in each of the other two zones.

Same-base test262 at `2e58f87d8350ad51224617a041c6e041d52f8444`:
**555 → 579 / 610** Date tests, **24 gained and zero lost**; the 14 shared
setter directories improve **171 → 195 / 195**. The gains are exactly the
argument-order and invalid-snapshot cases for the 12 non-FullYear methods.

Node is not treated as an infallible reference: Node v22.22.3 has related
bugs, and v26.13.1 agrees on **218/222** but rereads a mutated Date for four
FullYear cases. The expected snapshot semantics are those of ECMA-262 and
test262. See [Phase 506](../phases/506-invalid-date-setters-convert-first.md)
for the pinned specification, reproduction commands and broader validation.

## What is still open

The remaining 31 Date test262 failures involve parsing, negative-year
formatting, missing Date.prototype[Symbol.toPrimitive], Temporal/BigInt,
and realm helpers. Timezone/DST algorithms and `setTime` were not changed.
This closes shared-setter invalid-date handling, not all Date compatibility.
