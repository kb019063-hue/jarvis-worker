# Finding 518: Date's primitive-conversion method was missing

## What was wrong

Date.prototype had no Symbol.toPrimitive method. A Date-class check in
`js_to_primitive` merely forced string-first default conversion, so the
method could not be borrowed, reflected, or inherited by an ordinary
object. It would also ignore a Date's null/undefined opt-out of the real
method once installed.

## What changed

`src/js/BuiltinsDate.cpp` installs the generic method with the specified
name, length, non-constructor status and read-only/non-enumerable/configurable
property descriptor. It validates the receiver and exact primitive-string
hint, then performs OrdinaryToPrimitive directly, without recursively
asking for Symbol.toPrimitive. Getters and calls happen in order, zero
arguments and the original receiver are passed, noncallables are skipped,
and exceptions and primitive results are preserved.

The only change in `src/js/JsOps.cpp` removes the three-line Date-class
fallback. Dates now obtain their default preference from the actual
prototype method; null/undefined opt-out gets ordinary number-first default
conversion. No other generic conversion behavior changes.

## Verification

The new standalone suite has **93 checks**, producing `ok 93` under Node
v22.22.3 (`--expose-gc`), the fixed plain engine, and an explicit **-O0 -g0
ASan/UBSan** engine build, including collection inside fallback hooks.
The baseline reports that the method is missing.

Same-base test262 at `2e58f87d8350ad51224617a041c6e041d52f8444` improves
**579 → 593 / 610** Date tests, **14 gained and zero lost**. The method's
own directory improves **4 → 18 / 18**. The four baseline passes were
TypeError-only tests and did not establish the existence of the method;
the suite explicitly checks it before testing its behavior.

See [Phase 518](../phases/518-date-symbol-to-primitive.md) for commands,
broader validation and limits.

## What is still open

The shared ToPrimitive helper still mishandles a non-nullish noncallable
exotic method; that separate GetMethod issue is not repaired here. Date
parsing/negative-year formatting, Temporal and realm support remain
separate. This closes the missing method and Date-specific fallback gap,
not every conversion or Date conformance issue.
