# Phase 370 — a pattern is a paint

Phase 280 (#346) gave the 2D context `createPattern()`, and named the half it did
not do: a `CanvasPattern` assigned to `fillStyle` painted nothing. Measured before
starting, on a 16×16 `fillRect` with a red/green tile:

```
pat=true ;; readBack=sameObject ;; px0=0,0,0 ;; px6=0,0,0
```

The object was stored. The canvas was not painted. A page could compare
`ctx.fillStyle === pattern` and get `true` while drawing black on black.

## What changed

`fillStyle` and `strokeStyle` take **three** shapes where they took two: a
colour, a `CanvasGradient`, or a `CanvasPattern`.

- `JsValue` gains a `CanvasPattern` payload in its existing variant, beside the
  gradient's, so `set_property` can tell the third shape when it sees it.
- Setting any one shape **clears the other two**, or a pattern set once would
  paint under a colour set after it.
- The rasteriser takes the pattern beside the gradient in three places that each
  needed telling: `fillRect`'s fast rectangular path, `rasterize_paths` for path
  fills, and `stroke_paths`. Measured separately for that reason.
- A pattern's tile is placed in **device space**. A gradient is defined in user
  space and sampled through the inverse CTM; a pattern's only transform is its
  own `setTransform`, and applying the CTM's inverse to it would move the tile
  with the canvas — wrong in a way that a transform-free test cannot see.

## Three defects the pixels exposed

1. **`no-repeat` repeated on both axes.** The x axis condition was written
   `repetition != "repeat-y"`, which is true for `no-repeat`. Nothing could see
   it while the pattern painted nothing; the first pixel assertions did.
2. **`restore()` did not restore a style on the object**, and this **predates the
   phase**. `to_js_value` copies `fillStyle` onto the context object as an own
   data property and an own property shadows the live read, so `restore()` put
   the paint back in the renderer while `ctx.fillStyle` kept answering the colour
   set inside the `save()`. Measured with a gradient on the tree before this
   work: `gradAfterRestore=false`. The fix removes the own property when the
   style is not a colour, rather than writing a colour over it.
3. **A style read back as a marker string.** `get_property` answered
   `"[CanvasPattern]"`, so `ctx.fillStyle === pattern` was false. §4.12.5.1's
   getter returns the object the setter took, so the object is now kept with the
   paint, carried by `save()`/`restore()`, and cleared by `reset()`.

## How it was verified

```
$ make -j2 && make test
All 433 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9570/9824 subtests pass in 190 files; 254 fail as listed, 0 fail and are not listed
```

`tests/suites/370-a-pattern-is-a-paint.cpp` asserts pixels, object identity and
the clearing rules. Five wrong versions were compiled and each failed a
different assertion: the paint removed; the old repetition condition;
`restore()` without the object sync; a style read as a marker; a colour that no
longer clears the pattern.

**A mistake in the suite is worth recording.** The pixel helper was written
`return d[0] + ',' + d[1] + ',' + d[2]`, ignoring the index it had computed, so
every assertion read pixel 0 and the `repeat-x` case looked like an engine bug.
It failed against a correct engine, which is the useful direction. Beside it,
one probe's tile was drawn with horizontal stripes and could not distinguish
`repeat` from `repeat-x` at all — a tile that varies on **both** axes is the only
one that separates the four repetition rules.

## What is still open

Sampling is nearest neighbour with no filtering, so a tile under `ctx.scale()`
has hard edges; and the tile is a copy taken at `createPattern` time, where
§4.12.5.1 has a canvas-backed pattern reflect later drawing. Both are in the
finding.
