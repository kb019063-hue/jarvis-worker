# Finding 180 — the error came too early to stop the work

## What was seen

Two failures that looked unrelated:

```js
var count = 0, base = null;
try { base.prop = count += 1; } catch (e) {}
count;                       // Node: 1. This engine: 0.
```

```js
var iter = { next() { return {done: false}; }, return() { returnCount += 1; } };
try { var y; [ y = thrower() ] = iterable; } catch (e) {}
returnCount;                 // Node: 1. This engine: 0.
```

One is a write to a member that does not exist; the other is a pattern that
throws while binding. Both are the same sentence of the standard: **when an
operation ends abruptly, the work up to that point has already happened, and
the cleanup after it still has to.**

## Why

Both functions did their checking at the *start*.

`resolve_reference` refused a nullish base while resolving it:

```cpp
if (ref.base.is_nullish()) js_throw_type_error("Cannot set properties of ...");
```

That is right for a compound assignment — `base.p += 1` reads `base.p` first,
so the base must be usable before anything else — and wrong for `=`, where the
standard resolves the reference, evaluates the right-hand side, and only then
calls `PutValue`. The check was in a helper used by both, so the one place that
needed it late got it early, and the RHS was silently skipped. `typeof count`
still said `number`; it was simply `0`.

`bind_pattern` had the opposite shape. `IterationCursor::close()` existed and
the success path called it, but every `throw` between `IterationCursor cursor(value);`
and that call left the function through the `catch` of whatever was above it.
The cleanup was written at the end of the happy path instead of at the exit.

## What made it visible

The census, not the tests. Both are one-line probes against Node:

```
base.prop = count += 1      count 0  ->  1
[ y = thrower() ] = it      ret   0  ->  1
```

and then the whole `language/expressions/assignment` directory, which turned up
25 failures sharing the second shape.

## The rule the census taught twice in this phase

The first `IteratorClose` fix closed the iterator on *every* throw and lost
seven cases: an iterator whose `next` throws is the one case that must not be
closed (`array-elision-iter-abpt.js` asserts `returnCount === 0`). The lesson is
not "close on error" but "know which completion the error is". A throw from the
step is not a throw from the pattern.

The same caution applies to the other half: `check_reference_base` had to stay a
separate call, because compound assignment genuinely needs it early and plain
`=` genuinely needs it late, and a helper that guesses which call is which would
have brought the bug back for `++base.p`.
