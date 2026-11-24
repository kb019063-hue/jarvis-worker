# Finding 382 — no smoothing anywhere

**Severity:** low to medium. §4.12.5.1 gives the 2D context
`imageSmoothingEnabled` and `imageSmoothingQuality`. Neither existed — `typeof`
was `undefined` for both — and every image this engine sampled was taken from its
**nearest pixel**: `drawImage` scaling a 2×2 image to 8×8 came out in 8-pixel
blocks, and a pattern magnified by `ctx.scale(4,4)` repeated each tile pixel four
times. A page that asked for a filtered draw had no way to ask, and no way to
turn one off either.

**Found by:** Phase 370 (#370) named "sampling is nearest neighbour, unfiltered"
as its own still-open item. Measured before starting:

```
typeof ctx.imageSmoothingEnabled   -> undefined
typeof ctx.imageSmoothingQuality   -> undefined
drawImage 2x2 -> 8x8               -> 255,0,0 | 255,0,0 | 255,0,0 | 255,0,0 ...
```

## What was wrong

There was no smoothing state to read, and `draw_image` computed its source index
by **rounding**: `std::round(sx / (dest_w - 1) * (img->width - 1))`. That is a
nearest-pixel rule written as a rounding, with no place to put a filter and no
switch a page could throw.

## What changed

- **The two properties** are live accessors registered in
  `CanvasRenderingContext2D::to_js_value`, beside `globalAlpha` and `lineCap`,
  so they are read from the renderer every time rather than copied onto the
  object. That matters: `fillStyle` and three others are copied, and Phase 280
  and Phase 370 both had to repair state that the copy had shadowed. These two
  cannot go stale the same way.
  - `imageSmoothingEnabled` is a `boolean`, so an assignment is coerced
    (`0` and `""` turn it off) — and it starts `true`.
  - `imageSmoothingQuality` is an enumerated attribute: a value that is not
    `"low"`, `"medium"` or `"high"` is **ignored** and the old one stays, which
    is what the standard's "set to the given value" means for an enumeration. It
    starts `"low"`.
- **Both are carried by `save()`/`restore()`** and put back by `reset()`.
- **A bilinear sampler** with four clamped neighbours and weights summing to 1,
  used by `drawImage` in both of its paths — the identity-CTM fast path and the
  general inverse-mapped one — and by a pattern's tile.
- **The source index is now a fraction.** `(sx + 0.5) * scale` instead of a
  rounded index, so the filter has a sub-pixel position to work with.

## The 1:1 case, which keeps the filter honest

An image drawn at its own size must come out **exactly the same either way**. The
half-pixel placement in the sampler is what gives that: a destination pixel's
centre lands on a source pixel's centre, so the four weights are
`1, 0, 0, 0` and nothing is blended. Asserted for both settings.

The same rule applies to a pattern, one level further out: a tile at 1:1 is its
own pixels. That is **not** the case the sampler can decide alone, because a
pattern has no destination rectangle to read a scale from — it has to be passed
the CTM's scale at the call site. Getting this wrong is how Phase 370's suite
broke: with smoothing on by default, a 1:1 pattern started blending across the
tile edge (`128,128,0` where `0,255,0` was asserted), so the four repetition
rules that phase guarantees stopped holding. The fix compares the effective
scale against 1 and filters only away from it.

## The tell

Two settings and two answers. A filter that ignores the switch passes the scaled
assertion and fails the 1:1 one; a switch that does nothing passes the 1:1 one
and fails the scaled one. Four wrong versions compiled:

| what was put back | assertion that failed |
| :--- | :--- |
| nearest sampling always (the pre-phase state) | `smooth != nearest` |
| always smoothed, switch ignored | `nearest == "255,0,0 \| …"` |
| the quality stored as given, not validated | `g.imageSmoothingQuality == "medium"` after `'bogus'` |
| `save()`/`restore()` not carrying the pair | `g.imageSmoothingEnabled == "false"` after `restore()` |

A mistake in the suite, worth recording because it is the second of its shape in
this area: the nearest-neighbour assertion was written as two pixels per block
(`255,0,0 | 255,0,0 | 0,255,0 | 0,255,0`) when an 8-wide destination for a 2-wide
source gives **four** each. The engine was right and the test was wrong.

## Verification

```
$ make -j2 && make test
All 442 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9570/9824 subtests pass in 190 files; 254 fail as listed, 0 fail and are not listed
```

WPT is unchanged at 9570: no subtest in the corpus exercises smoothing, so this
is an addition no existing measurement covered.

`tests/suites/382-image-smoothing.cpp`.

## What is still open

- **`imageSmoothingQuality` selects nothing.** All three values filter the same
  way; `"high"` is stored and read back but does not choose a better kernel than
  `"low"`. Bilinear is bilinear.
- **The filter is bilinear only.** A shrunk image — 8×8 to 2×2 — averages four
  pixels where a box or a mip filter would average sixteen, so a fine checkerboard
  shrunk far enough still aliases. `drawImage` downscaling is the case that shows
  it.
- **A sheared pattern's scale is an approximation**: the longer axis of the
  CTM's inverse, the same approximation `isPointInStroke` uses for line width.
- **`createImageBitmap`'s `resizeQuality`** does not exist yet either, and it is
  the same question asked of a different object.
- **Not this row's:** `ctx.filter`, `drawImage` with a tainted source, and
  `OffscreenCanvas`.
