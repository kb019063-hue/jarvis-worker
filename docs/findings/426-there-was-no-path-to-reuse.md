# Finding 426 — every shape was drawable and no path could be held

The engine could draw every shape §4.12.5.1.7's `Path2D` can hold. `moveTo`,
`lineTo`, `arc`, `arcTo`, `bezierCurveTo`, `quadraticCurveTo`, `ellipse`,
`rect`, `roundRect`, `closePath` — all present, all working, all tested by the
phases before this one. What did not exist was a way to **hold** one:

```
typeof Path2D  -> undefined
```

This is the third finding in a row of the same family — 406's filter, 417's seven
text properties, this — and the same reason each time: the engine grew by adding
capability where a page asks for it, and the objects a page holds between two calls
were never on the path. The difference here is the size of the thing missing: not a
property, an interface, with a constructor, eleven methods and four call sites on
the context.

## The failure that was quieter than `undefined`

`typeof Path2D === 'undefined'` at least throws the first time a page writes
`new Path2D()`. The quiet one is on the other side:

```js
ctx.fill({});    // no throw. Fills the CURRENT path.
```

`fill`'s first argument is the fill rule, and the implementation read
`args[0]` as a string and compared it to `'evenodd'`. An object meant for a path
matched nothing and defaulted to `nonzero`, so the call was `ctx.fill('nonzero')`
with the page's path object discarded. A page that had built a `Path2D` by hand
from `moveTo` calls — which is what a page does when the constructor is missing —
and passed it to `fill` got a picture of whatever the context's current path was.

That is the general shape worth recording: **an argument that is ignored is worse
than one that throws.** A `TypeError` tells a page its assumption is wrong at the
moment it makes it; an ignored argument lets the page go on believing it works.

## The design decision, and the injection that proves it

The obvious implementation of `Path2D` is to reuse `m_paths`, the context's
current-path storage. It holds `SubPath`s — `std::vector<Point>` — and the
rasteriser, the stroke code and `isPointInPath` all already read them. Almost no
new drawing code.

It is wrong, and silently. `m_paths` holds **device-space** points: `lineTo` maps
its arguments through the CTM when the call is made. The specification keeps a
`Path2D` in user space and applies the CTM when it is used, which is why

```js
p.rect(0, 0, 10, 10);
ctx.scale(2, 2);
ctx.fill(p);      // 20×20, not 10×10
```

Reusing `m_paths` draws 10×10. Every assertion of the form "the path was filled"
still passes, and so does every assertion about colour and position at the centre
of the shape. Only an assertion that the shape **grew** catches it — and that
assertion is the reason the suite has Part 4, and the reason the injection that
disables the CTM at use time was written first among the bite proofs.

So a `Path2D` records the **calls**, and `replay_into` runs them into the context
under the CTM in force. The cost is that a path is re-flattened on every use; the
benefit is that a `Path2D` cannot disagree with `moveTo`+`lineTo` about anything,
because it goes through the same code.

## The bug my own probe found

The first `fill_path` was `replay(path); fill(rule); m_paths.clear();`. That
clears the caller's **current path**, so `ctx.fill(p)` was `beginPath` in
disguise. It showed up in a probe that built a current path, filled a `Path2D`,
and then filled the current path: the second fill painted nothing.

The lesson is about return values. The first fix made `replay` hand the saved path
**back** to the caller, which reads well and is unusable: a caller that has to
remember what a helper saved is a caller that will forget. The second fix splits
the function — `replay_into` appends and saves nothing, and each of the four entry
points does its own save/clear/replay/draw/restore where all five steps are
visible in one place.

## Where the SVG constructor came from

`new Path2D(svgPathData)` needed a parser for path data, and `SvgRenderer.cpp`
already had one: `build_svg_path` is a template over a `Sink`, written so an SVG
`d` attribute could drive either a context or a flattener. A third sink that
records into a `Path2D` was about thirty lines, and it means a `Path2D` and an
`<svg><path>` with the same data are the same shape by construction rather than by
two implementations agreeing.

Getting there cost more than it should have. `build_svg_path` and its helpers are
inside an anonymous namespace that opens at line 22 and closes only at the end of
the file, so a function defined "after" them is still file-local and does not link
— and the failure is a link error naming a symbol that plainly exists in the
source. The fix was to put the exported function between two anonymous-namespace
blocks, with the recorder in the first one; the moment to notice was when `nm`
showed no symbol at all in a freshly built object.

## Still open

* `roundRect`'s four radii are averaged to one, because the context's
  `round_rect` takes one.
* There is no cached flattening; a path drawn a thousand times is flattened a
  thousand times.
* `Path2D` is not reachable from a worker's `OffscreenCanvas`, whose `getContext`
  installs its own subset.

## A note on the phase after this one

`fill` and `stroke` choose their form by the **type** of the first argument. That
is the IDL union's own behaviour, and it is now the only place in the canvas
bindings where an argument is dispatched on type rather than position. If a later
phase adds another union there, this is the pattern to copy — and the reason
`fill({})` no longer quietly fills the wrong path.
