# Phase 499 — `Path2D` in OffscreenCanvas workers

Phase 426 left this note:

> `Path2D` is not available in a worker's canvas, because `OffscreenCanvas`'s
> `getContext` installs its own subset of methods.

The conclusion was right and the reason was stale. `OffscreenCanvas.getContext`
already creates its 2D object through `CanvasRenderingContext2D::to_js_value`, the
same full context used by an HTML canvas. `fill`, `stroke`, `clip`, and
`isPointInPath` already dispatch on `Path2D` there.

The worker failed earlier:

```
typeof OffscreenCanvas  -> "undefined"
typeof Path2D           -> "undefined"
```

There was no reduced path API to repair. There were no objects with which to ask
for it.

## What changed

A non-document `JsInterpreter` already installs worker-global URL, URLSearchParams,
Blob, and File constructors from its DOM realm. It now installs
`OffscreenCanvas` and `Path2D` beside them. Window realms keep their existing
interface exposure; the change is inside the `!m_document` branch.

This engine models a worker's `self` object separately from its global environment
and mirrors host constructors manually. `Worker.cpp` therefore mirrors both
constructors as well:

```js
self.OffscreenCanvas === OffscreenCanvas // true
self.Path2D === Path2D                   // true
```

Bare-name support without the `self` properties would make two views of one worker
global disagree.

## The pixel bridge the first worker run found

Constructor exposure made the path calls run, then the first pixel read failed:

```
TypeError: ImageData needs a realm with Uint8ClampedArray.
```

`getImageData()` creates a `Uint8ClampedArray` through the DOM realm. The helper
looked only at `realm.window`, which is deliberately undefined in a worker. Typed
array constructors were already installed in the worker's global environment;
the DOM realm simply had no route back to it.

Non-document interpreters now record their global environment in
`Realm::global_scope`. `realm_global_binding` still uses `realm.window` first for
a page, and falls back to that scope only when there is no Window. Both ImageData
allocation and constructor validation use it. This lets
`OffscreenCanvas.getImageData()` return an `ImageData` whose `data` is the worker
realm's `Uint8ClampedArray`, without pretending the worker global is a Window.

That distinction matters. Setting `realm.window` to a plain worker global would
make every DOM helper that asks “does this realm have a Window?” answer yes. The
fallback supplies only the global bindings a worker-safe interface needs.

## The worker oracle

`tests/suites/499-path2d-in-offscreen-worker.cpp` runs the same source in a
classic worker and a module worker. Each worker verifies:

* bare and `self` constructor exposure;
* no `document` global;
* `fill`, `stroke`, `clip`, and `isPointInPath` methods;
* red fill pixels from a rectangular `Path2D`;
* green pixels from `new Path2D(svgData)`;
* blue stroke pixels;
* a yellow fill clipped by a `Path2D`, with the outside pixel transparent;
* hit-test true/false points;
* `new Path2D(other)` is a copy, not an alias.

The Window realm is a control: its constructors and fill pixels are unchanged.
Only strings cross the worker boundary; no DOM object is accidentally shared
between agents.

## Bite proof

Four mutations were compiled and restored on the Phase 499 tree:

| mutation | result |
| --- | --- |
| remove worker `Path2D` | classic and module workers report `ReferenceError: Path2D is not defined` |
| remove worker `OffscreenCanvas` | both report `ReferenceError: OffscreenCanvas is not defined` |
| leave bare globals but omit `self` mirroring | both draw correctly, then fail the exact global-surface answer |
| remove the worker global-binding fallback | both fail at `getImageData()` with the ImageData realm error |

The fourth proves that `typeof` plus “method did not throw” is not enough for a
canvas feature. The result must be read back from pixels in the same realm.

## Verification

`make test` runs the standalone worker oracle with the whole engine suite. The
final command and corpus results are recorded here after the repository gates,
not copied from the superseded branch that first found the defect.

## Files

* `src/js/JsInterpreter.cpp` — constructors and worker DOM global scope.
* `src/js/Worker.cpp` — `self` mirroring.
* `src/js/DomHtmlBindings.cpp` — worker-safe global bindings for ImageData.
* `tests/suites/499-path2d-in-offscreen-worker.cpp` — classic/module pixel oracle.

## Still open

* `ImageData` itself is not exposed as a worker-global constructor, although
  `getImageData()` returns a correctly tagged object.
* `OffscreenCanvas.convertToBlob()` still looks for `Promise` through
  `realm.window`; a worker has no Window, so that method needs the same kind of
  worker-global bridge.
* A recorded path is still flattened on every use; there is no per-transform
  cache.
