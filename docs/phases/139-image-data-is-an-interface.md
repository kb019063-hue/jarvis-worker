# Phase 139 — `ImageData` is an interface, and `putImageData` takes its rectangle

Task row: **the Canvas half of "the four interfaces typed arrays unblocked"**
(`AGENTS.md`). Phase 106 made `ArrayBuffer` and the nine typed arrays exist, and
that row lists what was waiting on them: `fetch()` bodies, WebSocket binary
frames, `TextDecoder`/`TextEncoder`, and the `ImageData` a canvas reads pixels
into. Three of the four now have an owner — `TextDecoder`/`TextEncoder` is
Phase 127 — and the canvas one was still open and named no file that another
open pull request holds. Finding 144.

## 1. What was wrong

A canvas could give a script its pixels, and what it gave was not an
`ImageData`:

```bash
./build/litebird --run-script /tmp/probe-imagedata.js /tmp/probe.html
```

```
typeof ImageData = undefined
data ctor = Array
is Uint8ClampedArray = false
length = 16
zero create = undefined
zero get = undefined
pixel 0,0 = 255 0 0 255
after dirty (1,1) at 0,0 = 0 255 0
after dirty (1,1) at 1,1 = 0 255 0
new ImageData threw ReferenceError
```

Five things, in the order the standard names them:

- **There was no `ImageData`.** `createImageData` and `getImageData` built a
  bare object with `width`, `height` and `data` properties and no prototype at
  all, and `new ImageData(2, 2)` was a `ReferenceError`. So
  `image instanceof ImageData` was not false — there was nothing to be an
  instance of — and a script could not make one to hand to `putImageData`.
- **`data` was an `Array` of numbers**, not a `Uint8ClampedArray`. HTML
  §4.12.5 gives an `ImageData` a `Uint8ClampedArray`, and that is the object
  whose `300` reads back `255`; an `Array` takes the 300 and keeps it, which is
  a silent difference in what a pixel means.
- **A zero dimension answered `undefined`** where the standard throws
  `IndexSizeError` — for all three methods. A page that asked for a 0×0 patch
  got a value it could not tell from a failed read.
- **`putImageData` ignored the dirty rectangle.** The seventh argument of
  `putImageData(imageData, dx, dy, dirtyX, dirtyY, dirtyWidth, dirtyHeight)`
  were not read at all: the whole 2×2 image was written where one pixel was
  asked for, which is the wrong pixels painted on the canvas.
- **`colorSpace` did not exist**, so the sixth field of the interface was
  absent rather than answered.

The pixel work underneath was already right and is not what changed: a
half-alpha red reads back `255 0 0 128` (unpremultiplied, as the standard
says), a pixel outside the canvas reads back transparent black, and
`putImageData` already ignored the transform, `globalAlpha` and the clip —
which is why only the interface and the rectangle needed building.

## 2. What changed

`src/js/DomHtmlBindings.cpp` holds the interface, `src/render/Canvas2D.cpp`
keeps the pixels. The three methods moved out of
`CanvasRenderingContext2D::to_js_value`, which has no realm and so cannot make
a `Uint8ClampedArray`, into `install_image_data_methods()`, called from
`HTMLCanvasElement.getContext('2d')` once per context. This is the division the
file boundary asks for: `Canvas2D.cpp` says what a pixel is,
`DomHtmlBindings.cpp` says what a script sees.

- **`ImageData`**, registered as an interface with `length` 2:
  `new ImageData(width, height)` and `new ImageData(data, sw[, sh])`. The
  second form keeps the array it was given — `image.data === data` — rather
  than copying it, refuses anything that is not a `Uint8ClampedArray` with a
  `TypeError` (checked by `Object.prototype.toString`, so the internal slot is
  what counts and not a shape that looks like one), refuses data that is not a
  non-zero multiple of four bytes, data shorter than the width asked for, and a
  size that disagrees with the data. `width`, `height`, `data` and
  `colorSpace` are read-only attributes. A subclass gets the object it is
  building, so `class Mine extends ImageData {}` and `new Mine(2, 3)` are an
  `ImageData`.
- **`createImageData(w, h)`** returns a new `ImageData` of transparent black,
  and **`createImageData(imageData)`** returns one of the same size with its own
  pixels — never the same object, never the same bytes.
- **`getImageData(sx, sy, sw, sh)`** returns an `ImageData` whose `data` is a
  `Uint8ClampedArray` filled from the bitmap: unpremultiplied RGBA per pixel,
  transparent black for anything past the canvas edge, and a negative `sw`/`sh`
  runs left/up from the corner given, as before.
- **`putImageData(..., dirtyX, dirtyY, dirtyWidth, dirtyHeight)`** writes the
  dirty rectangle and nothing else. A negative `dirtyWidth`/`dirtyHeight` counts
  back from the origin of the rectangle, as §4.12.5 says; a rectangle that
  reaches outside the image is clipped to it; one entirely outside writes
  nothing. It still consults neither the transform, nor the clip, nor
  `globalAlpha`.
- **A zero dimension is an `IndexSizeError`** in all three — the constructor,
  `createImageData` and `getImageData` — and the area ceiling the C++ side
  already kept (16M pixels, `kMaxImageDataPixels`) is now an `IndexSizeError`
  rather than an `undefined`.
- **`colorSpace` is `"srgb"`.** This engine stores one colour space, and the
  settings argument that asks for `display-p3` gets a `TypeError` rather than a
  silent lie: there is no conversion to make, so it is refused.

## 3. How it was verified

```bash
make test
#   ... All 297 engine test suites passed successfully!  (114 cross-origin checks, 0 failure(s))
```

The new suite is `image_data_is_an_interface` (`tests/test_suite.cpp`), 27
checks, every expectation the standard's: the constructor's two forms and its
five refusals, the read-only attributes, the prototype identity, all three
zero-dimension `IndexSizeError`s, the pixel bytes read back off a drawn canvas,
the dirty rectangle in its four shapes (inside, negative, outside, whole), the
round trip `getImageData` → edit → `putImageData` → `getImageData` exact
through 64 bytes, the transform/`globalAlpha`/clip immunity, the `TypeError` on
a `putImageData` of something that is not an `ImageData`, the illegal-invocation
`TypeError` on `ImageData.prototype.data` called on `{}`, and a subclass.

The five probes from §1 now print:

```
typeof ImageData = function
data ctor = Uint8ClampedArray
is Uint8ClampedArray = true
length = 16
zero create threw IndexSizeError
zero get threw IndexSizeError
after dirty (1,1) at 0,0 = 0 255 0
after dirty (1,1) at 1,1 = 255 0 0
new ImageData(2,2) = 2
```

and the rest of the scoreboard is unchanged, which is the point of a phase that
adds an interface rather than a behaviour:

```bash
make js-conformance     # 1143/1158 (98.7%), 29 files, 0 crashes, 0 timeouts
make conformance        # 1973/1973 (100.0%), PASS on 62 files
make check-numbers      # 0 failure(s)
python3 tools/ci/check_docs.py --base origin/main   # 0 failure(s)
```

## 4. What is still open

- **`display-p3`** is refused, not converted. A second colour space is a
  colour-management project, not an interface one.
- **`ImageData` in a worker realm** has no window to look the
  `Uint8ClampedArray` constructor up in, so it throws a `TypeError` there. No
  worker in this engine has a canvas either, so nothing can reach it yet; the
  fix belongs with the realm's global environment, not here.
- **The `settings` argument of `getImageData`/`createImageData` and the third
  field of `putImageData` are read and refused**, not honoured: the standard
  lets them ask for a colour space and this engine has one.
- **`HTMLCanvasElement.toDataURL()` is still `"data:,"`** — a canvas can hand a
  script its pixels and still cannot hand a page a PNG.
- **`ImageBitmap`, `createImageBitmap`, `OffscreenCanvas` and
  `getImageData`'s `willReadFrequently` hint** do not exist.
- **Premultiplied alpha is not modelled.** Browsers store pixels premultiplied
  and lose precision at low alpha; this engine keeps straight alpha, so a round
  trip through `putImageData` here is exact where a browser's may not be. That
  is a difference in the engine's favour and it is still a difference.
