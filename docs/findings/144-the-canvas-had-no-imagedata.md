# Finding 144 — the canvas had no `ImageData`

**Resolved by Phase 139** (`docs/phases/139-image-data-is-an-interface.md`).

## 1. What was found

`AGENTS.md` listed "the four interfaces typed arrays unblocked" as four rows on
the grounds that they share no file. Three of them were taken; the canvas one
was still open, and the note under it — "`CanvasRenderingContext2D` has no
`ImageData` (`createImageData`, `getImageData`, `putImageData`)" — turned out to
be half true and worse than that.

The three methods existed. What they answered was a bare object:

```js
var c = document.createElement('canvas'); c.width = 4; c.height = 4;
var ctx = c.getContext('2d');
ctx.createImageData(2, 2).data instanceof Uint8ClampedArray   // false
ctx.createImageData(2, 2).data.constructor.name               // "Array"
new ImageData(2, 2)                                           // ReferenceError
ctx.createImageData(0, 0)                                     // undefined
ctx.getImageData(0, 0, 0, 0)                                  // undefined
```

and the one thing a script does with an `ImageData` — hand it back — wrote the
wrong pixels:

```js
// a 2x2 green image, asked to write one pixel of it
ctx.putImageData(img, 0, 0, 0, 0, 1, 1);
```

painted all four. The rectangle was not read.

## 2. Why it matters

Three separate consequences, and only the first is cosmetic:

- **An `Array` is not a `Uint8ClampedArray`.** Its elements are not clamped:
  `image.data[0] = 300` kept 300 in the array and `putImageData` then clamped it
  on the way in. Code that reads back what it wrote got a different number from
  the one the standard says is stored, and `image.data.buffer`, `.subarray`,
  `.set` and `.slice` did not exist on it.
- **`undefined` is not an exception.** A `0×0` `getImageData` that answers
  `undefined` is indistinguishable from a call the engine did not understand;
  the standard's `IndexSizeError` is the only thing a caller can test for.
- **The dirty rectangle is the point of `putImageData`'s seven arguments.** A
  sprite sheet, a dirty-rect update loop and a `requestAnimationFrame` canvas
  all write a rectangle into a larger image; writing the whole image instead is
  not slow, it is wrong, and it is wrong silently.

## 3. What was done

Phase 139 built the interface: `ImageData` with its two constructor forms and
four read-only attributes, `createImageData`/`getImageData` returning it, the
dirty rectangle read and clipped, `IndexSizeError` for a zero dimension, and
`colorSpace` answered `"srgb"` with `display-p3` refused. The pixel work
(`get_image_data_rgba`/`put_image_data_rgba`) already existed and did not
change; the three methods moved from `Canvas2D.cpp`, which has no realm and so
cannot make a typed array, into `DomHtmlBindings.cpp`.

## 4. What is still open

- `display-p3`: refused, not converted.
- `ImageData` in a worker realm: no window to find `Uint8ClampedArray` in.
- `HTMLCanvasElement.toDataURL()` still answers `"data:,"`.
- `ImageBitmap`, `createImageBitmap`, `OffscreenCanvas`.
- `getImageData`'s `willReadFrequently` hint, and the `settings` argument
  beyond `colorSpace`.
- Premultiplied alpha is not modelled: this engine stores straight alpha, so a
  round trip through `putImageData` is exact here and need not be in a browser.
