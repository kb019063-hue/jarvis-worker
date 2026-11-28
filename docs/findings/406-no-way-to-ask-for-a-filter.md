# Finding 406 — the engine had a filter, and no way to ask for one

`ImageFilter` was written some time ago and implements seven of §4.12.5.1's ten
filter functions. `CanvasRenderingContext2D` did not have a `filter` attribute:

```
typeof ctx.filter -> undefined      (everything else on the context answered)
```

Two hundred lines of working image maths, and no path from a page to any of it.
This is the same shape as finding 391 — the state was there and unreachable —
but the failure is quieter: nothing is *missing* from the renderer, so nothing
looks wrong. A page that sets `ctx.filter = 'grayscale(1)'` and draws a red
square gets a red square, and the only visible symptom is that the page's
intent was ignored.

## What the suite caught that reading the code did not

**The replay's coverage gate is right for a shadow and wrong for a filter**, and
the difference is exactly the pixels the feature produces. `replay_captured`
skipped any pixel whose coverage was zero; a blur's softened edge and a drop
shadow's silhouette are all at zero coverage, so the first working version of
this phase drew an unfiltered shape with no shadow — and every assertion of the
form "is the shape the filtered colour" passed. Only asking about the pixels
*outside* the shape failed. That is the third time in this engine that a correct
guard for one feature has silently disabled another (the pattern fast path in
Phase 370, the opaque fast path in Phase 249 and again in Phase 391).

**A filter is not a per-pixel tint.** The natural first implementation is to
apply the filter inside `set_pixel_safe`, where every other colour effect lives.
That works for the seven matrix filters and is impossible for `blur` and
`drop-shadow`, which read neighbouring pixels. Building it on the existing
capture — a whole primitive's pixels as a buffer — is what makes all ten
possible; a design that had gone per-pixel first would have needed a second
rewrite to reach the other three.

## Two argument-type traps

**`blur(2)` is invalid CSS.** `<length>` requires a unit for a non-zero value, so
`blur(2px)` parses and `blur(2)` does not. The parser rejected `blur(2)` when it
was first written, and the honest reading of that moment is that the *test* was
wrong and the parser was right — a browser would reject it too. The temptation
was to accept the bare number because it is what a person types; doing so would
have made this engine accept a value Chrome refuses, which is a difference that
only shows up when a page finally runs on both.

**`drop-shadow(4px 0px 0px #000000)` took three attempts.** The colour may come
first or last, the blur is optional, and the offsets are lengths while the colour
is a colour. The first version parsed the two offsets and then treated the next
token as a blur *or* a colour and refused anything after it, so every shadow with
a colour failed as invalid — invisibly, because an invalid value leaves the
previous filter in place and the previous filter was `none`.

## What was not caught, and by which test

The injection that let `fast_opaque` ignore the capture depth was **not** caught
by this suite in a normal run: suite 391 fails on it first and the runner stops at
the first failing suite, so my Part 10 never executes. The defect is real and
Part 10 is written to catch it — verified by asking the canvas directly with a
binary built after the injection — but the credit for catching it in a full run
belongs to 391. Recording it this way because the alternative is claiming a
catch that the runner's own order prevented.

## Still open

* `drop-shadow`'s unstated colour: CSS uses the element's `color`, which the
  context does not carry; opaque black is the substitute.
* **Closed in Phase 535:** `blur()` and filter `drop-shadow()` now use a
  normalized, separable Gaussian kernel, with the CSS length as standard
  deviation. The convolution uses premultiplied alpha and treats the area beyond
  the bitmap as transparent; the 3σ support and σ cap at the largest bitmap
  dimension keep it bounded. The separate `shadowBlur` context property still
  uses its Phase 391 three-box approximation.
* `url()` filters, `em`/`rem`/`pt` lengths: rejected, not guessed.
