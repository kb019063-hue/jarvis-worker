# Finding 120 — A published test vector can be the wrong answer

## The finding

RFC 7616 §3.9.2 is the digest example every HTTP client's test suite copies:
`SHA-512-256`, `charset=UTF-8`, `userhash=true`, a non-ASCII username. It prints

```
username="488869477bf257147b804c45308cd62ac4e25eb717b12b298c79e62dcea254ec"
response="ae66e67d6b427bd3f120414a82e4acff38e8ecd9101d6c861229025f607a79dd"
```

Those values are **wrong**. Erratum 4897 (Reported, 2016-12-29) has the
correction, which is also what an independent implementation gives:

```
username="793263caabb707a56211940d90411ea4a575adeccb7e360aeb624ed06ece9b0b"
response="3798d4131c277846293534c3edc11bd8a5e4cdcbff78b05db9d95eeb1cec68a5"
```

## Why it matters beyond one RFC

The whole point of a published vector is that it lets an implementation be
checked against something it did not compute. When the vector is wrong, the
test becomes a commitment to a bug: an implementation that copies it passes and
an implementation that is correct fails. That is worse than having no vector,
because it is authoritative.

The failure mode is specific and worth recognising: **a vector that is
internally consistent can still be the wrong function.** Both the printed and
the corrected values are SHA-512/256-shaped — 256 bits of hex, no padding error
— and both round trip through their own username hash. Only computing the
example from the algorithm's *definition* tells them apart, and only then
because FIPS 180-4 §6.7 and §7 disagree about what SHA-512/256 is (the
former is a distinct initial state; the latter is a truncation).

## What this changed here

`tests/test_suite.cpp` asserts the corrected pair, and the comment says why:

> Writing the published values here would have asserted a bug.

The check that made it safe to write either one is the third layer: a
*different* implementation. `hashlib.new("sha512_256")` is FIPS 180-4 §6.7, it
shares no code with this engine, and it produced the erratum's values to the
last hex digit. Without that, the choice would have been between trusting the
RFC and trusting this code.

## The related trap: what a "known answer" test is *not*

Three of the assertions added in Phase 115 are of the form "X is not Y", and
they are the ones worth keeping:

```
SHA-512/256("abc") != ddaf35a1…   // SHA-512's first 256 bits
SHA-256-sess       != 1dfbe580…   // what the MD5-fold bug answered
```

A positive assertion on its own cannot distinguish "the right function" from
"the wrong function that happens to be what I implemented". The negative one
names the confusion directly, so a future reader who reintroduces the bug gets
a failing test whose message is the bug.

## What is still open

Erratum 4897 is still in **Reported** state, not Verified, twelve years of
RFC reader-time after it was filed — so the published document still carries the
wrong numbers, and nothing here can change that. The phase cites the erratum so
a reader comparing this code to the RFC knows which of the two is authoritative.
