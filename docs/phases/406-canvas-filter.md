# Phase 406 — canvas `filter`

§4.12.5.1 gives the 2D context a `filter` attribute and ten filter functions.
None of it existed:

```
typeof ctx.filter          -> undefined
```

so a page could not ask for a blur, a grayscale, a drop shadow or any of the
other seven, and this engine's only painterly effect was the box shadow added by
Phase 391. `ImageFilter` had been sitting in `src/render/ImageFilter.cpp` with
six of the ten functions implemented and nothing reaching it: no parser, no
accessor, and no draw call that consulted it.

## What changed

**One live accessor**, registered beside `imageSmoothingEnabled` and the four
shadow properties and read from the renderer rather than copied onto the object.
It starts at `"none"`. The getter answers the string the page wrote — whatever
the page wrote, once it was accepted — as a browser's getter does.

**A parser for the filter list** (`parse_filter_list`, unnamed namespace in
`Canvas2D.cpp`). The grammar is a space-separated list of `name(argument)`;
arguments run to the matching parenthesis, so `drop-shadow()` may contain
parentheses of its own. The value is accepted or rejected **whole**: an unknown
function, an unparseable number, a negative blur, a stray word, an unbalanced
parenthesis, or a `blur()` whose length has no unit all leave the previous filter
in place. That is the specification's "ignore an invalid value", and the
alternative — salvaging the functions that did parse — would put a
half-understood effect on the canvas, which reads as a rendering bug rather than
a rejected assignment.

**Four argument types, because the filter list has four.** A plain `<number>`
for `brightness`, `contrast`, `invert`, `opacity`, `grayscale`, `sepia` and
`saturate`; a `<percentage>` accepted in place of any of those, since `50%` and
`0.5` are the same request; an `<angle>` in degrees for `hue-rotate`; and a
`<length>` for `blur()` and `drop-shadow()`, which is where the quantity takes a
unit. Canvas draws in CSS pixels, so `px` is the unit that means what it says, and
a bare non-zero number is **not** a length: `blur(2px)` parses and `blur(2)` does
not, matching CSS, even though `blur(2)` is the natural thing for a page to type.

**Two new matrix filters** — `hue-rotate` and `saturate` — in `ImageFilter`,
built on one shared `apply_color_matrix` routine. They were the two of the ten
with no implementation. `hue-rotate` wraps its angle, so 360° is the identity and
−90° equals 270°; `saturate` does not clamp to 1, because `saturate(2)` is legal
CSS and stronger colour.

**Filtering happens on the captured buffer, not per pixel.** `ShadowCapture`
already exists to give a primitive's pixels to the shadow painter as a set; a
filter needs exactly the same thing for a different reason, so the guard now
captures when either a shadow or a filter is set, and `replay_captured` runs the
filter list over the captured pixels before compositing them. That is what makes
`drop-shadow` possible at all — its shadow lies where the primitive did *not*
draw.

## The one thing that nearly went in wrong

`replay_captured` skips pixels whose coverage is zero. That is right for a
shadow: an untouched pixel must not repaint the background. It is wrong for a
filter, and by exactly the pixels the feature exists to produce — a blur's
softened edge and a drop shadow's silhouette both lie outside the original
coverage. The gate now applies only when there is no filter, and the filtered
path skips a pixel only when the filter left it fully transparent. Injection A in
the bite proof below is this line, and the suite catches it on the blur ramp.

A second near-miss: `drop_shadow` had no default for its colour and a
transparent one, so `drop-shadow(4px 0px 0px)` — shadow colour optional in CSS —
was the identity, silently. CSS defaults the colour to the used `color`, which
this context does not carry; opaque black is what the function then paints, and
the alternative (transparent) is a shadow that does nothing.

## Where it still differs from a browser

* `drop-shadow`'s stated colour is taken as written; the CSS default is the
  used `color`, which this engine has no notion of.
* Only `px` is understood as a length unit. `em`, `rem`, `pt` would need a font
  and a resolution the context does not consult, and rejecting the value is
  allowed where guessing would be wrong.
* `url()` filters are rejected — there is no SVG filter primitive tree to refer
  to.
* The blur is a box blur with the radius clamped to the bitmap, so it softens
  far more cheaply and rather less smoothly than a browser's three-pass Gaussian.

## Bite proof

Three injections, each built, each restored from a `/tmp` copy:

| injection | result |
| --- | --- |
| **A** — keep the coverage gate under a filter | suite fails on the blur ramp: `Number(ba(12,8)) > 0` |
| **B** — capture only for a shadow, not for a filter | suite fails on the first assertion: `gp(1,1)` is `255,0,0,255`, not `54,54,54,255` |
| **C** — let `fast_opaque` ignore the capture depth | suite 391 fails first and aborts the run; measured on the canvas directly, Part 10's expected `0,255,255,255` is `255,0,0,255`, and Part 10 is the assertion that holds it |

Injection C is the interesting one, and not for the reason it was meant to be:
it confirms the *engine* defect but not that my suite is the one that would catch
it in an ordinary run, because suite 391 aborts before mine gets a turn. What
makes Part 10 safe is its own measured value, which was checked with a binary
built after the injection — not the suite's exit status.

## The test I wrote wrong

Part 2 asserted that `grayscale(0.5)` leaves the three channels equal at
`154,154,154`. Only `amount = 1` collapses them; the truthful answer is
`155,27,27`, and the engine was right both times. A looser assertion ("it
changed") would have passed and taught nothing, so it was corrected to the exact
values rather than relaxed.

Part 5 had the same shape of mistake: it asked a 4-pixel-wide box to be fully
covered in its middle under a `blur(2px)`. The kernel reaches outside a box that
narrow, so the measured centre is 204 — the same attenuation Phase 391's finding
records for a small shadow. The assertion now uses a 10-pixel box, where the
centre *is* 255, and keeps a hard-edged control beside it.

## Files

* `src/render/Canvas2D.cpp` — the parser, the accessor, the capture gate, the
  filtered replay.
* `include/litebird/render/Canvas2D.hpp` — `filter_source` / `filter_list` on the
  context and on `SavedState`, so `save()`/`restore()` carry the filter.
* `src/render/ImageFilter.cpp`, `include/litebird/render/ImageFilter.hpp` —
  `hue_rotate`, `saturate`, the shared matrix, the transparent-shadow early
  return, the blur clamp.
* `tests/suites/406-canvas-filter.cpp` — 11 parts.
