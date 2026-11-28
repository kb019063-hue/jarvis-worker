# Finding 391 — nothing cast a shadow

**Severity:** medium. §4.12.5.1 gives the 2D context four shadow properties —
`shadowBlur`, `shadowColor`, `shadowOffsetX`, `shadowOffsetY` — and specifies
that shadows are composited **under** their shapes. None of the four existed on
this engine's context: `typeof` was `undefined` for all of them, so no
`fillRect`, path fill, stroke, `fillText` or `drawImage` ever cast a shadow, and a
page that set one got a property assignment onto an object that ignored it.

**Found by:** measuring the context's properties rather than trusting the row.
`globalAlpha` and `imageSmoothingEnabled` answered `typeof`; the four shadow
properties did not.

## What was wrong

There was no state to hold a shadow and no place in the painting path that could
have applied one. The four properties did not exist; the composite step knew only
about a colour, its coverage and `globalAlpha`.

## What changed

- **Four live accessors**, registered in `to_js_value` beside the other canvas
  properties — read from the renderer every time, never copied onto the object.
  - `shadowColor` starts **transparent black**, which is what makes a shadow
    invisible until a page sets one; a colour the parser cannot read is ignored
    rather than stored as transparent, which would silently switch shadows off.
  - `shadowBlur` starts 0 and a negative value is ignored; the two offsets start
    0 and take any finite value, including negative.
- **`ShadowCapture`**: a scoped guard at the top of each primitive that casts one.
  While it is alive, `set_pixel_safe` **records** the primitive's colour and
  coverage instead of painting it; the destructor paints the shadow from that
  coverage and then the primitive over it. That is what puts the shadow *under*
  the shape, which is the point — a shadow painted after the shape would cover
  the shape.
- **A three-pass box blur** on the coverage, which approximates a Gaussian with a
  running sum, so the cost does not grow with the radius.
- The offsets move the shadow **to** `(x + ox, y + oy)`.

## The fast path that hid it, again

`fillRect`'s identity-CTM path writes `m_image->pixels[idx]` **directly** when the
fill is opaque, bypassing `set_pixel_safe` entirely. A capture therefore saw no
pixels at all and the first working build painted no shadow. The gate is now
`m_capture_depth == 0` as well.

This is the **third** time in this file that a fast path has hidden a new
feature: Phase 249's element writes, Phase 370's pattern paint, and this. The
fast path is not wrong — it is blind to anything that needs to see pixels one at
a time.

## Two defects the assertions found in the code

1. **The offset had the wrong sign.** `x - ox` moved a shadow of a shape at (2,2)
   nowhere at all, because the only pixels it moved were already off-canvas. The
   assertion that catches it is a **negative** offset, which is why that case is
   in the suite.
2. **The shadow's alpha was applied twice** — once by the capture when it
   recorded `global_alpha × coverage × colour alpha`, and again by
   `set_pixel_safe`. An opaque shadow of a solid shape came out at 25%
   (measured `0,0,0,64`). The blurred value is now baked into the colour and
   passed with full coverage.

## Three assertions of mine that were wrong, and the engine was right

This is the part worth keeping, because it is the third task in a row where the
test, not the renderer, was the thing that failed:

- **`inside == 255`** for a blurred shadow: with a radius of 2 the kernel reaches
  past an 8-wide shape, so the centre is about 201/255. Physically right.
- **"no partly covered pixel after `reset()`"** on the same canvas: `reset()`
  resets the drawing state, **not the bitmap**, so the shadow drawn before the
  reset is still there. Correct, and the test was asking the wrong question.
- **An opaque default shadow colour passed the suite.** The check copied
  `ck.shadowColor` onto a fresh context and drew with it — a round trip that
  works whether the default is transparent or opaque. Rewritten to ask the
  *original* context to cast a shadow where its default colour would land.
  **Even so, this injection was not caught by my suite**: it was caught by
  suite 272 (`createImageBitmap`), which failed because an opaque default
  blackened a canvas. Recorded as it is rather than claimed as a pass.

## Verification

```
$ make -j2 && make test
All 451 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9570/9824 subtests pass in 190 files; 254 fail as listed, 0 fail and are not listed
```

Four wrong versions compiled: no shadow at all; the offset sign reversed; the
shadow painted over the shape; the default colour opaque. The first three each
failed a different assertion of this suite.

`tests/suites/391-canvas-shadows.cpp`.

## What is still open

- **The blur is a box approximation, not a `feGaussianBlur`.** Three passes of a
  box are close enough that the edge falls off smoothly, but not a Gaussian, and
  `shadowBlur`'s exact mapping (the specification says half the value is the
  standard deviation) is applied as a radius, not as a standard deviation.
- **A shadow is captured per whole canvas, not per bounding box.** The buffers
  are `width × height`; a small rectangle on a large canvas pays for the canvas.
- **Since this finding:** Canvas `filter` arrived in Phase 406, and its
  `blur()`/filter `drop-shadow()` kernel changed to a Gaussian in Phase 535; this
  is separate from `shadowBlur`'s three-box approximation above.
- **Shadows on a `putImageData`** are correctly absent (that method replaces
  pixels), and nothing tests it yet.
- **Not this row's:** shadow on `fillText` with a gradient fill, `OffscreenCanvas`,
  and the seven text properties (`letterSpacing`, `wordSpacing`, `direction`,
  `fontKerning`, `fontStretch`, `fontVariantCaps`, `textRendering`) which are all
  still `undefined` and were measured in the same pass.
