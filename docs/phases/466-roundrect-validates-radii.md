# Phase 466 — `roundRect` validates its radii

HTML accepts one to four non-negative radius values. The engine accepted every
invalid form at that boundary:

```js
ctx.roundRect(0, 0, 20, 20, -1);                // no throw
ctx.roundRect(0, 0, 20, 20, {x: -1, y: 2});    // no throw
ctx.roundRect(0, 0, 20, 20, []);                // no throw
ctx.roundRect(0, 0, 20, 20, [1, 2, 3, 4, 5]);  // no throw
```

The same four calls on a `Path2D` also returned normally. Negative components
reached `round_rect`, whose defensive geometry clamp replaced them with zero. An
empty sequence became a square rectangle. A fifth value was silently discarded.
The output was plausible in all three cases, but the API contract is a
`RangeError`, not a corrected picture.

After Phase 466 all eight calls throw `RangeError`. `-0` is not negative and
remains valid; one- and four-entry sequences remain the valid cardinality
boundaries.

## Validation is before geometry

The direct context binding resolves each scalar, DOMPointInit, or retained inner
array point and rejects a negative x or y before it calls `round_rect`. It checks
sequence length before selecting the 1/2/3/4 spreading form.

The `Path2D` recorder performs the same checks before `Path2D::add_op`. This order
is the important part of the change. An exception is not enough if the failed
call has already changed the path:

```js
ctx.beginPath();
ctx.rect(2, 2, 8, 8);
try { ctx.roundRect(28, 2, 12, 12, {x: -3, y: 4}); } catch (_) {}
ctx.fill(); // only the first rectangle
```

Likewise, a rejected `path.roundRect(...)` records no operation; valid operations
before and after it remain reusable.

The geometry's non-finite/negative clamps stay as an internal defence for C++
callers. They are no longer the JavaScript binding's validation strategy. An API
error and a geometry fallback have different observable effects: exception class,
control flow, and whether a current path grew.

## Sequence cardinality

Omitting the fifth argument is still the zero-radius form:

```js
ctx.roundRect(x, y, w, h); // valid square rectangle
```

Explicitly supplying `[]` is different: it is a radius sequence with zero
members, and is rejected. This closes the square fallback that Phases 443 and 454
had deliberately pinned while their own geometry tasks were in progress. Their
suites now retain the omitted-argument control and expect `RangeError` for the
explicit empty list.

Five or more values are rejected rather than using the first four. The check runs
on direct and recorded routes, so a fifth value cannot disappear differently when
a path is reused.

## Test

`tests/suites/466-roundrect-invalid-radii.cpp` has one suite in seven parts:

1. negative scalar and valid `-0`;
2. negative x and y in DOMPointInit;
3. negative components in retained inner-array points, including a later corner;
4. zero/five invalid cardinalities and one/four valid boundaries;
5. direct current-path atomicity;
6. `Path2D` recording atomicity;
7. Phase 454's accepted 20×5 ellipse remains unchanged.

The historical Phase 443 and 454 suites were updated only where they asserted the
old empty-list fallback. Their circular, elliptical, spreading, overlap and CTM
assertions remain intact.

## Bite proof

Four mutations were compiled and restored:

| mutation | caught by |
| --- | --- |
| remove negative-component checks | Part 1: direct `-1` returns normally |
| throw `TypeError` instead of `RangeError` | Part 1: exception identity differs |
| direct binding calls geometry and validates afterwards | Part 5: the rejected rectangle paints `(32,7)` |
| `Path2D` accepts a fifth entry and truncates it | Part 4: no `RangeError` |

The third is the design proof: testing only the exception name would pass a
binding that threw after corrupting the current path.

## Files

* `src/render/Canvas2D.cpp` — direct radius/component and cardinality checks.
* `src/js/DomHtmlBindings.cpp` — the same checks before recording a `Path2D` op.
* `tests/suites/466-roundrect-invalid-radii.cpp` — exception and atomicity oracle.
* `tests/suites/443-path2d-roundrect-radii.cpp` and
  `tests/suites/454-roundrect-elliptical-radii.cpp` — close the former explicit
  empty-sequence fallback assertions.

## Still open

A negative rectangle width or height is normalised to positive dimensions without
swapping which radius belongs to each physical corner. That is corner identity,
not radius validation, and remains a separate task.
