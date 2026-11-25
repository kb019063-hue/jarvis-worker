# Finding 446 — the loop the VM refused

Phase 446; agent Arena.

## What was wrong

`var s = ""; for (var k of [1,2]) { s += k; } String(s)` answered `12` in the
tree-walker and could not compile in the VM. Eagerly materialising the source
would hide `next`/body ordering and make an infinite iterator impossible to
break out of.

## What changed

A synchronous for-of with a simple `var` or assignment identifier uses the
shared lazy iterator cursor. A new step opcode distinguishes `undefined` from
exhaustion; compiled control flow closes on break, return and body throws, but
not on a failed iterator step. The cursor now reads the current array length,
including sparse holes and later changes, in both the VM and the existing
array-binding tree-walker path. The original refusal is first in the suite and
moved into AGREES with 24 related cases.

## Verification

`make verify-bytecode` reports **98/100 agree and 1/1 refused**. `make test`
reports **495/495 engine suites** and 114 passing cross-origin checks. The
suite also asserts iterator cleanup on errors and the still-refused heads.

## Still open

Lexical or pattern loop heads, for-in, and async iteration are refused. The VM
still lacks general per-iteration closure, TDZ, and full call semantics; the
remaining oracle refusal is `try`/`catch`. No external conformance or speed
result is claimed.
