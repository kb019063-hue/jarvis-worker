# Finding 533 — the Promise was behind a Window

`OffscreenCanvas.convertToBlob()` already encoded a bitmap, made a Blob, and had
success and rejection branches. In a page it worked. In a worker all of that code
was reachable, but its first dependency was not:

```cpp
r.window.get_property("Promise")
```

The method was exposed on a worker-safe interface while its implementation still
used Window as a synonym for global realm. With no Promise value, it deliberately
returned `undefined`; the complete implementation disappeared without throwing
at the host boundary.

## Global dependency, not Window dependency

`Promise` is an ECMAScript global in both Window and worker realms. The correct
question is “what is this realm's Promise?”, not “does this realm have a Window?”
Phase 499 had already introduced exactly that distinction for ImageData's
`Uint8ClampedArray` and `Object` dependencies. Reusing the same lookup keeps one
rule for both interfaces:

* Window realm: read the binding from `realm.window`;
* non-Window realm: read it from `realm.global_scope`;
* never assign a worker global to `realm.window`.

The last point prevents unrelated DOM code from believing that a worker owns a
document, navigation state, media elements, or other Window-only facilities.

## A success-only probe would still be incomplete

The Promise is needed before MIME validation and dimension validation. Fixing
only the final successful `resolve` would leave these calls returning undefined:

```js
canvas.convertToBlob({type: 'image/jpeg'})
new OffscreenCanvas(0, 2).convertToBlob()
```

Both are asynchronous API failures: `EncodingError` and `IndexSizeError` must be
rejections, not synchronous throws and not missing values. The suite therefore
chains the successful PNG and both failures through `Promise.all` in each worker.
A mutation that suppresses only `Promise.reject` leaves PNG conversion green and
still fails the oracle.

## Bytes, not merely a Blob-shaped object

The worker reads the resolved Blob through `arrayBuffer()` and reports PNG's
signature and IHDR dimensions. That joins the whole path:

```
OffscreenCanvas pixels → PNG encoder → Blob → Promise → ArrayBuffer → Uint8Array
```

A fabricated Blob, a wrong realm Promise, an empty byte payload, or a method that
only avoids throwing cannot satisfy all of those observations.
