# Phase 382 — image smoothing

Phase 370 named its own still-open item: sampling was nearest neighbour,
unfiltered. §4.12.5.1 gives the 2D context two properties that decide how an
image is sampled when it is not drawn at its own size, and neither existed:

```
typeof ctx.imageSmoothingEnabled   -> undefined
typeof ctx.imageSmoothingQuality   -> undefined
drawImage 2x2 -> 8x8               -> 255,0,0 | 255,0,0 | 255,0,0 | 255,0,0 ...   (8-pixel blocks)
```

## What changed

**Two live properties**, registered beside `globalAlpha` and `lineCap` in
`to_js_value`, so they are read from the renderer instead of being copied onto the
object. That is deliberate: `fillStyle` and three others are copied, and Phases
280 and 370 both had to repair state the copy had shadowed. These two cannot go
stale that way.

- `imageSmoothingEnabled` is a `boolean` and starts `true`; assignment coerces,
  so `0` and `""` turn it off.
- `imageSmoothingQuality` is an enumeration and starts `"low"`; a value that is
  not one of the three is **ignored**, not stored.

Both are carried by `save()`/`restore()` and put back by `reset()`.

**A bilinear sampler** with four clamped neighbours and weights summing to 1,
used in three places: `drawImage`'s identity-CTM path, its general inverse-mapped
path, and a pattern's tile. The source index is now a fraction —
`(sx + 0.5) * scale` — where it used to be `std::round(...)`, which was a
nearest-pixel rule with nowhere to put a filter.

## The 1:1 case

An image at its own size must come out **exactly**, either way. The half-pixel
placement gives that: the four weights become `1, 0, 0, 0`. Asserted for both
settings, because it is what a filter that always blurs fails.

A pattern is one level further out and **cannot decide this alone**: it has no
destination rectangle to read a scale from, so the CTM's scale is passed in at
each call site. This is not hypothetical — with smoothing on by default, a 1:1
pattern began blending across its tile edge (`128,128,0` where `0,255,0` was
asserted) and **Phase 370's suite went red**. The four repetition rules that
phase guarantees only hold on an exact 1:1 tile. The fix filters only when the
effective scale is away from 1.

## How it was verified

```
$ make -j2 && make test
All 442 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9570/9824 subtests pass in 190 files; 254 fail as listed, 0 fail and are not listed
```

Four wrong versions compiled and each failed a different assertion: nearest
always; always smoothed with the switch ignored; the quality stored without
validation; `save()`/`restore()` not carrying the pair.

**The suite was wrong once**, and the engine was right: the nearest-neighbour
assertion read `255,0,0 | 255,0,0 | 0,255,0 | 0,255,0`, two pixels per block,
when an 8-wide destination for a 2-wide source gives four each. That is the
second time in this area that a hand-written expectation, not the renderer, was
the thing that failed.

## What is still open

`imageSmoothingQuality` selects nothing — all three values filter identically,
bilinear only. Downscaling is bilinear too, so a fine checkerboard shrunk far
enough still aliases where a box filter would not. A sheared pattern's scale is
the longer axis of the CTM's inverse, an approximation shared with
`isPointInStroke`. And `createImageBitmap`'s `resizeQuality` is the same question
asked of a different object, still absent.
