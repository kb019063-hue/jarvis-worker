# Finding 370 — the pattern painted nothing

**Severity:** medium. `createPattern()` returned a correct `CanvasPattern` and
`fillStyle` accepted it, and the canvas was not painted. A page could check every
property, compare `ctx.fillStyle === pattern` and get `true`, and still draw
nothing.

**Found by:** Phase 280 (#346) added `createPattern()` and named this half as
still open. Measured before starting: a 16×16 `fillRect` with a red/green tile
came out `0,0,0` — the `fillStyle` was stored and never sampled.

## What was wrong

A style was only ever a colour or a `CanvasGradient`. `JsValue::set_property`
recognised exactly two shapes (`canvas_gradient_value()` and everything else is
parsed as a colour) and carried the gradient to the rasteriser as one
`shared_ptr<CanvasGradient>` per style. A `CanvasPattern` was none of these: it
went into the property, `set_property` fell through to `Color::parse(pattern)`
and got black, and it painted black-on-black.

## What changed

- **`JsValue`**: a `CanvasPattern` is a fourth payload in the existing variant,
  with `canvas_pattern_value()`/`set_canvas_pattern_value()` beside the gradient's.
- **`fillStyle`/`strokeStyle`** take three shapes. Setting any one clears the
  other two — otherwise a pattern set once paints under a later colour.
- **The rasteriser** takes the pattern beside the gradient, in `rasterize_paths`
  (path fills and strokes) and in `fillRect`'s fast rectangular path, which are
  three separate places that each had to be told.
- **The pattern is placed in device space.** A gradient is sampled through the
  inverse CTM because it is defined in user space; a pattern's tile is not, and
  its own `setTransform` is the only transform it has.

## Three defects the pixels exposed

None of these could be seen by any test that checked the object rather than the
canvas, which is why the suite asserts pixels.

1. **`no-repeat` wrapped on both axes.** The condition was written
   `repetition != "repeat-y"` for the x axis, which is true of `no-repeat`, so
   `no-repeat` repeated. Nothing could see it while the pattern painted nothing.
2. **`restore()` did not restore a style in the object.** This one **predates the
   phase**: `to_js_value` copies `fillStyle` onto the context object as an own
   data property, and an own property shadows the live read. `restore()` put the
   paint back in the renderer while `ctx.fillStyle` kept answering the colour set
   inside the `save()`. Measured with a gradient too:
   `gradAfterRestore=false` on the tree before any of this work.
3. **Reading a style back answered a marker string.** `get_property` answered
   `"[CanvasPattern]"`, so `ctx.fillStyle === pattern` was `false` for a pattern
   and `true` only by accident for a gradient. §4.12.5.1's getter returns the
   object that the setter took, so the object is now stored with the paint and
   carried by `save()`/`restore()`.

A fourth, in the suite itself: the helper that reads a pixel was written
`return d[0] + ',' + d[1] + ',' + d[2]` — ignoring the `i` it had just computed,
so every assertion read pixel 0. It failed against a correct engine, which is
the useful direction for a mistake in a test to fail, and it is the reason the
`repeat-x` assertions looked wrong while the engine was right.

## The tell

The pre-phase tree passed a test of `ctx.fillStyle === pattern` and of
`typeof pattern.setTransform === "function"`. It fails on
`px(js, 1, 0) == "255,0,0"`. Every assertion in the suite is a pixel, an object
identity, or the clearing of one style by another.

Five wrong versions were compiled, each failing a different assertion:

| what was put back | assertion that failed |
| :--- | :--- |
| the paint removed from the fast path and the rasteriser | `px(js, 1, 0) == "255,0,0"` |
| the old `!= "repeat-y"` repetition condition | `colAt('no-repeat', 9, 0) == "0,255,0"` |
| `restore()` without the object sync | `g.fillStyle === p` |
| a style read as a marker string | `g.fillStyle === p` (in Part 6) |
| a colour no longer clearing the pattern | `px(js, 1, 0) == "0,0,255"` |

## Verification

```
$ make -j2 && make test
All 433 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9570/9824 subtests pass in 190 files; 254 fail as listed, 0 fail and are not listed
```

WPT went from 9569 to 9570 with no test changing its answer, which is worth
recording as a coincidence rather than a claim: no WPT subtest is known to test
pattern painting specifically.

`tests/suites/370-a-pattern-is-a-paint.cpp`.

## What is still open

- **Pattern sampling is nearest neighbour, with no smoothing.** A pattern on a
  transformed canvas — `ctx.scale(2,2)` — samples the tile by flooring the
  device point instead of filtering it, so a scaled-up tile has hard edges where
  a browser would blur them. The tile is not resampled at all.
- **The tile is a copy taken at `createPattern` time.** §4.12.5.1 says a pattern
  made from a canvas reflects that canvas's later changes; this one keeps the
  pixels it had. `CanvasPattern.transform` is applied, but a re-drawn source
  canvas is not seen.
- **`fillPattern`/`strokePattern` are one level deep only.** A pattern set
  inside a `save()` is carried by `save`/`restore`; there is no
  `CanvasPattern`-in-`CanvasPattern` case to test because a pattern cannot be a
  source image.
- **Not this row's:** `ctx.filter`, pattern fills on text, and `OffscreenCanvas`.
