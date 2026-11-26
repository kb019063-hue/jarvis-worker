# Finding 466 — an invalid corner still drew

The radius rules were enforced in the wrong layer. `round_rect` was defensive:
non-finite and negative numbers became zero before geometry was emitted. That is a
reasonable last line of defence for an internal drawing primitive. It is not the
JavaScript API's required behaviour.

A page can observe the difference without reading a pixel:

```js
try {
    ctx.roundRect(0, 0, 20, 20, -1);
    continued();
} catch (error) {
    // HTML requires RangeError here.
}
```

Before this phase `continued()` ran, and the current path gained a square
rectangle. The internal clamp turned an invalid request into valid-looking output
and hid the contract violation.

## A clamp is not validation

Clamping answers “what can the rasteriser safely draw?” Validation answers “was
the call accepted?” Those questions have different owners:

* geometry may protect itself from a bad C++ caller;
* a binding must throw the specified JavaScript exception;
* a failed path-building method must not mutate its path.

Moving the check only partway upward is still wrong. A deliberately injected
version called `round_rect`, then examined the resolved radius and threw
`RangeError`. Every exception assertion passed. The current-path pixel assertion
failed because the invalid square had already been appended and was painted after
the page caught the exception.

That is the finding: **an exception test needs a state test when the rejected
operation is mutating.** “It threw” does not prove “it did not happen.”

## Cardinality is also data

The same boundary had two sequence fallbacks:

* an empty list fell through to `rect`;
* a list longer than four used its first four members.

Both make the renderer look robust and make the API wrong. An omitted radii
argument and an explicitly empty sequence are not the same input. A fifth member
is not disposable just because four corners are all the geometry consumes.

This distinction had been documented as a limitation in Phases 443 and 454, and
their tests pinned it so their geometry changes could not make direct and recorded
paths diverge. Phase 466 changes the contract intentionally, updates those two
assertions, and adds the exception/atomicity suite that justifies the change.

## Direct and recorded paths have different state to protect

For a direct context call, the vulnerable state is `m_paths`, the context's
current path. The suite starts with one small valid rectangle, catches an invalid
`roundRect`, fills, and verifies that the invalid rectangle's centre stayed
transparent.

For `Path2D`, the vulnerable state is the operation list. The suite records a
valid rectangle, catches a negative radius in a later sequence entry, records a
second valid ellipse, and fills the path. Both valid shapes paint and the rejected
one does not. Fresh paths rejected for zero and five entries remain empty.

The same `RangeError` therefore guards two different stores. Keeping both checks
at their binding/recording boundaries is clearer than asking shared geometry to
know which object it would have mutated.

## The negative zero edge

The check is `component < 0`, not `component <= 0`. JavaScript's `-0` is a valid
zero radius and remains a square corner. That small control prevents an apparently
stricter check from rejecting a value the numeric algorithm does not classify as
negative.

## The rule to keep

When an internal primitive repairs hostile values, do not assume the public API
inherits the right semantics. Read the binding's error steps, and test three
things separately:

1. the exception **kind**;
2. the rejected boundary values around the accepted range;
3. the mutable state after the exception was caught.

The third is what distinguishes validation from a late complaint after the work
was already done.
