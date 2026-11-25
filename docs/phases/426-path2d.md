# Phase 426 — `Path2D`

§4.12.5.1.7 is a whole interface and it did not exist:

```
typeof Path2D            -> undefined
new Path2D()             -> ReferenceError: Path2D is not defined
```

Every context path method was there — `moveTo`, `lineTo`, `arc`, `bezierCurveTo`,
`ellipse`, `roundRect`, all of them — so the engine could draw every shape a
`Path2D` can hold. What was missing was a way to hold one, and the four methods
that take one instead of the current path.

The quiet part was `fill`:

```js
ctx.fill({});   // no throw, and it fills the CURRENT path
```

`fill`'s first argument is the fill rule, so a page that believed it was passing a
path filled whatever the context's current path happened to be. Nothing threw,
nothing was logged, and the picture was of a shape the page had not asked for.

## What changed

**`Path2D` records its calls.** The specification keeps a `Path2D` in user space
and applies the CTM when it is *used*:

```js
const p = new Path2D();
p.rect(0, 0, 10, 10);
ctx.setTransform(2, 0, 0, 2, 0, 0);
ctx.fill(p);                 // 20×20
```

The context's own path holds **device-space** points — `lineTo` maps its arguments
through the CTM at the moment of the call. A `Path2D` built on that storage would
draw 10×10 here, and every "did it fill?" assertion would still pass. So
`Path2D` stores `Kind` + the page's own numbers, and `replay_into` runs them
through the context's methods under whatever CTM is in force.

That choice is also what keeps a `Path2D` from disagreeing with `moveTo`+`lineTo`
about how a curve becomes pixels: the flattening, the arc-to-bezier conversion and
the degenerate-radius rules are the context's own code, not a second copy.

**Four methods take a path instead of the current path** — `fill`, `stroke`,
`clip`, `isPointInPath` — chosen by the argument's *type*, exactly as the IDL union
does. None of them consumes or disturbs the current path: `fill(path)` twice draws
the same picture twice, and a page's accumulated current path survives.

**`new Path2D(svgPathData)`** goes through `build_svg_path` in `SvgRenderer.cpp`,
the same walk of the same grammar an SVG `d` attribute takes, with a recorder as
the sink. A page that draws a `Path2D` and an `<svg><path>` from the same data
cannot get two shapes, because there is one parser.

**`addPath(other, transform)`** appends the other path's calls under a matrix
composed with the CTM in force. Composing the matrix rather than transforming
points is what keeps an added `arc` an arc.

## The bug the probes found

The first version of `fill_path` was:

```cpp
replay(path);
fill(rule);
m_paths.clear();     // wrong
```

`replay` restored the caller's path and then `m_paths.clear()` threw it away, so
`ctx.fill(p)` silently became a `beginPath` in disguise — the page's accumulated
path was gone and the *next* `fill()` drew nothing. Measured on a page that built a
path, filled a `Path2D`, and then filled: the second fill painted nothing at all.

The fix is that `replay_into` **appends** to `m_paths` and restores nothing; the
four entry points save the current path, clear, replay, draw, and put the saved
path back. Handing the saved path back from `replay` instead — which was the first
attempt — left the caller unable to tell whether the replay had happened.

## Bite proof

Four injections, each built and restored from a `/tmp` copy:

| injection | caught by |
| --- | --- |
| **A** — `fill(path)` does not restore the current path | Part 3: the later plain `fill()` paints nothing |
| **B** — the CTM ignored when the path is used | Part 4: the 10×10 box stays 10×10 at `scale(2)` |
| **C** — `fill` ignores its path argument | Part 2: the fill paints nothing |
| **D** — `new Path2D(other)` aliases instead of copying | Part 7: the copy grows when the original does |

Injection B is the one worth naming: it is the design. A `Path2D` that stored
device points would pass every other assertion in the suite.

## Still open

* `roundRect`'s four-value radii form is averaged to one radius. The context's
  `round_rect` takes one, and averaging is a limitation rather than a decision.
* Adding to a `Path2D` that has already been drawn re-flattens it on the next use;
  there is no cached flattened form, because a cache would have to be per-CTM and
  that is the same work again.
* A `Path2D` holds a `std::weak_ptr` to nothing and a `std::shared_ptr` to a
  parent when `addPath` is used, so a path that added another keeps it alive. That
  is correct and is what a page would expect; it is noted because it is the one
  place two paths form a graph.
* `Path2D` is not available in a worker's canvas, because `OffscreenCanvas`'s
  `getContext` installs its own subset of methods.

## Files

* `include/litebird/render/Path2D.hpp` — the recorded-call type.
* `src/render/Canvas2D.cpp` — `replay_into`, the four path-taking methods, the
  argument-type dispatch.
* `include/litebird/render/Canvas2D.hpp` — their declarations.
* `include/litebird/js/JsValue.hpp`, `src/js/JsValue.cpp` — a `Path2D` payload slot
  beside `CanvasPattern`'s.
* `src/js/DomHtmlBindings.cpp` — the `Path2D` interface, its constructor's three
  forms, and its methods.
* `src/render/SvgRenderer.cpp`, `include/litebird/render/SvgRenderer.hpp` —
  `parse_svg_path_data`, the recorder, and the grammar walk it reuses.
* `tests/suites/426-path2d.cpp` — 10 parts.
