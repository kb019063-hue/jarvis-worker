# Phase 151 — a clip with a corner is not a rectangle

Task row: **`overflow: hidden` clips to a rectangle, not to the border radius**
(finding 156). The clip command carried `border_radius` since Phase 122 and the
renderer never read it, so a child of a 40×40 `border-radius: 16px`
`overflow: hidden` box painted its own ink into the square corner at `(0, 0)`.

## 1. What was wrong

`paint_atom` builds a `PushClipCommand` with the padding box and sets
`clip.border_radius` from the style. `BitmapRenderer` then stored the clip as a
plain `Rect`:

```cpp
std::vector<Rect> m_clip_stack;
...
m_clip_stack.push_back(current_transform().map_rect(arg.rect));
```

and `inside_clip` tested the pixel against that rectangle and nothing else. The
radius travelled all the way from the style to the clip command and was dropped
at the last step. A rounded background and a rounded border were both
implemented and anti-aliased; only the clip was square, which is why the page
looked nearly right and the corner was the one place it showed.

## 2. What changed

- **`include/litebird/render/BitmapRenderer.hpp`** — the clip stack holds a
  `ClipRegion` (a `Rect` and the radius it was clipped to) instead of a `Rect`.
- **`src/render/BitmapRenderer.cpp`** — three things:
  1. `PushClipCommand` stores the radius, scaled by the transform. The clip has
     already been mapped to device space, so a `scale(2)` on the box is a corner
     of twice the radius on the bitmap; taking the style's radius unscaled would
     have clipped the wrong curve.
  2. `inside_clip` became `clip_coverage`, which answers *how much* of the pixel
     survives rather than yes or no, and uses `rounded_box_sdf` — the function
     the background fill and the shadow already use. The same field is what makes
     the two edges land on the same place instead of merely both being round.
  3. `set_pixel` multiplies the ink's alpha by that coverage, so a pixel on the
     curve keeps the part of itself that is inside.

The last two are the part that is easy to get wrong. A boolean clip would pass
the `known_failure` page — the corner *is* removed — but a background of the
same box paints `(4, 4)` as `#ffc3c3` and a stencil would paint it `#ff0000`.
The two would disagree by a quarter of a pixel along the whole curve, which is
exactly the sort of thing this project's pixel oracle exists to catch.

## 3. How it is verified

- **The recorded page passes.** `overflow_hidden_clips_to_the_rounded_corner`
  was in `tests/render/reftests.json` as a `known_failure` with a sentence
  saying why. `make render-conformance` printed `FIXED overflow_hidden_clips_to_the_rounded_corner`
  — the corpus notices a known failure that starts passing, rather than letting
  it sit there. The entry is gone and the page counts now.
- **Agreement is measured, not asserted from geometry.**
  `a_rounded_clip_stops_where_a_rounded_background_does` puts a rounded
  background and a separately clipped rounded box on one page and checks both at
  the same offsets: blank at 0 and 2, `#ffc3c3` at 4 on both, solid red at 5 on
  both. Dumping the PNG and comparing every pixel on the diagonal gives the same
  tuple on both sides at every offset — `(4,4)` is `(255,195,195)` for the
  background and `(255,195,195)` for the clip.
- **The scaled case.** `a_scaled_rounded_clip_scales_its_corner`: a 16px radius
  on a `scale(2)` box is a 32px curve, so `(8,8)` is blank where it was inside
  the unscaled curve, `(9,9)` is `#ff5151`, `(10,10)` is solid. Rendering the
  PNG by hand gives `(7,7)` blank, `(9,9)` `(255,81,81)`, `(10,10)` red.
- **`make render-conformance` 36 / 38 pages**, from 34, with 2 failing as
  recorded and 38 of 38 accounted for. The canvas `toDataURL` checks are
  unmoved at 45016 / 45016.
- **`make test`** 308 / 308 suites.
- **Cost, since this is the per-pixel path.** An 800×600 box fully covered by
  painted ink, rendered seven times each and the median taken: no clip 35.3 ms,
  clip with no radius 36.3 ms, clip with a 40px radius 39.0 ms. So a rounded
  clip costs about 2.7 ms more than a square one, and a square clip costs about
  1 ms more than none. Two thirds of that is not the arithmetic: a rounded clip
  takes the whole box off the `fill_rect` fast path, which is a row of `memset`
  per scanline, and every pixel goes through `set_pixel` instead.
- **A rewrite that did not pay.** `rounded_box_sdf` takes a square root per
  pixel, so the obvious optimisation is to compare squared distances and skip it.
  Written and measured: 40.4/33.1 ms before, 44.0/34.2 ms after — inside the
  noise, because the cost is the per-pixel call and not the `sqrt`. It was
  reverted rather than kept as complexity that buys nothing.

## 4. What is still open

- **One radius for the whole box.** `border_radius` is a single value, so a box
  with four different corner radii is already approximated by the background and
  border code as well as by the clip. That is the row on per-corner radii, not
  this one.
- **The clip is the padding box.** An `overflow: clip` with
  `overflow-clip-margin` is not implemented at all; that is a separate row.
- **A clip is not intersected with an ancestor's rounded clip to a shape** — the
  coverage simply multiplies, which is right for an intersection of convex
  shapes and stays correct for this one.
