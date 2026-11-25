# Phase 391 — canvas shadows

§4.12.5.1 gives the 2D context four shadow properties and says shadows are
composited **under** their shapes. None of the four existed:

```
typeof ctx.shadowBlur      -> undefined
typeof ctx.shadowColor     -> undefined
typeof ctx.shadowOffsetX   -> undefined
typeof ctx.shadowOffsetY   -> undefined
```

while `globalAlpha` and `imageSmoothingEnabled` answered. So nothing this engine
drew — rectangle, path, stroke, text or image — ever cast a shadow.

## What changed

**Four live accessors**, registered beside the others and read from the renderer
rather than copied onto the object. `shadowColor` starts **transparent black**,
which is what keeps a shadow invisible until a page sets one; `shadowBlur` starts
0 and ignores a negative value; the offsets start 0 and take any finite value.

**`ShadowCapture`** is a scoped guard at the top of every primitive that casts a
shadow. While it is alive `set_pixel_safe` records the primitive's colour and
coverage instead of painting it; on destruction it paints the shadow from that
coverage and then the primitive over it. That ordering is the whole feature: a
shadow painted afterwards would cover the shape.

**A three-pass box blur** with a running sum, so the cost does not grow with the
radius, and the offsets move the shadow to `(x + ox, y + oy)`.

## The fast path, for the third time

`fillRect`'s identity-CTM path writes the pixel array **directly** when the fill is
opaque. A capture saw nothing, and the first working build painted no shadow at
all. The gate now includes `m_capture_depth == 0`.

That is the third feature in this file hidden by a fast path — Phase 249's element
writes, Phase 370's pattern paint, and this. The fast path is not wrong; it is
blind to anything that needs to look at pixels one at a time.

## What the assertions found

Two real defects and three wrong tests:

| found | in |
| :--- | :--- |
| the offset applied with the wrong sign (`x - ox`) | the code |
| the shadow's alpha applied twice, 25% instead of 255 | the code |
| `inside == 255` for a blur whose kernel reaches past the shape | the test |
| "nothing partly covered after `reset()`" — `reset()` clears state, not the bitmap | the test |
| an opaque default colour passed, via a getter/setter round trip | the test |

**The third one matters most**: making the default `shadowColor` opaque black was
not caught by this suite even after it was rewritten — it was caught by suite 272,
`createImageBitmap`, which blackened a canvas. It is recorded that way rather than
claimed as a pass.

## Verification

```
$ make -j2 && make test
All 451 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9570/9824 subtests pass in 190 files; 254 fail as listed, 0 fail and are not listed
```

WPT is unchanged: no subtest in the corpus covers shadows.

Four wrong versions compiled — no shadow, the offset sign, the shadow over the
shape, and the opaque default — and the first three each failed a different
assertion of this suite.

## What is still open

The blur is a box approximation rather than a Gaussian, and `shadowBlur`'s
half-value mapping is applied as a radius rather than a standard deviation. The
capture buffers are canvas-sized, not bounding-box-sized. `ctx.filter` is still
absent, and the seven text properties (`letterSpacing`, `wordSpacing`,
`direction`, `fontKerning`, `fontStretch`, `fontVariantCaps`, `textRendering`)
were measured in the same pass and are all still `undefined`.
