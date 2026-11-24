# Phase 272 — `createImageBitmap`'s crop, resize and the sources it refused

The row said `createImageBitmap`'s "crop/resize options" were still open, which
reads like a missing feature. Measured, it was worse than missing: they were
**accepted, ignored, and resolved**. The binding was

```cpp
I.method("createImageBitmap", 1, [](Realm& r, Args args) {
```

and `args[1]` was never read. HTML §8.11.2 declares the method twice — once as
`(image, options)` and once as `(image, sx, sy, sw, sh, options)` — so the
source rectangle, the resize and the method's first two steps all went
unperformed while the promise settled with a bitmap of the whole image.

That shape of failure is the reason this phase's suite reads pixels and not just
dimensions. `createImageBitmap(cv8x8, 0, 0, 4, 4)` answered `8x8`: a valid
`ImageBitmap`, a plausible width, a resolved promise. A page has nothing to
notice with.

## What changed

Only `src/js/DomWindowBindings.cpp`, and within it only the one binding.

| the call | before | now |
| :--- | :--- | :--- |
| `(cv8, 0,0, 4,4)` | 8x8 | 4x4 |
| `(cv, 0,0, 4,4, {resizeWidth:2, resizeHeight:2})` | 300x150 | 2x2 |
| `(cv, 0,0, 0,4)` | resolved, 300x150 | rejected `RangeError` |
| `(cv, {resizeWidth:0})` | resolved | rejected `InvalidStateError` |
| `(new Blob([png]))` | rejected `TypeError` | the PNG's size |
| `(new Image())` | rejected `TypeError` | rejected `InvalidStateError` |
| `(new ImageData(2,2))` | 2x2, pixels zeroed | 2x2, the data's pixels |

- **The arguments.** The two overloads are separated by count, as WebIDL's
  overload resolution does: five or more is the rectangle form.
- **Step 1** is a `RangeError` for a zero `sw` or `sh`, and it is checked before
  the image is — which is why it is not the `InvalidStateError` that step 2 uses
  for a zero `resizeWidth`/`resizeHeight`. The two kinds are asserted separately,
  because an implementation that returns one for both passes a test that only
  looks at "rejected".
- **`Blob`** is a listed source. Its bytes come from the already-exported
  `blob_bytes()` and are decoded by `ImageDecoder::decode_bytes`, the call the
  renderer makes for an `<img>`; data that is not an image is an
  `InvalidStateError`.
- **`<img>`** reads `load_image_for_element`, the same pixels `drawImage` reads.
  An image that has not decoded is step 3's unusable source.
- **`ImageData`'s pixels** are copied — a copy, so writing the `ImageData` after
  the call does not change the bitmap.
- **The rectangle** is cropped "with formatting": larger than the source pads
  with transparent black, and an origin past the edge crops entirely to
  transparent. Both are asserted, because a bounds check can get either wrong
  while the dimensions stay right.
- **`resizeWidth`/`resizeHeight`**, with the aspect ratio when one is given, and
  **`flipY`**, off by default.
- The crop runs before the resize, which is the order of the steps.

## How it was verified

```
$ make -j2 && make test
All 409 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9569/9824 subtests pass in 190 files; 255 fail as listed, 0 fail and are not listed
```

The suite is `tests/suites/272-create-image-bitmap-crop-and-resize.cpp`: a 2x2
canvas of red, green, blue and white quadrants, so the provenance of a pixel is
decidable from the pixel, and each single-pixel crop asserted against its own
colour. Four versions of the engine were compiled to show it bites — the
one-argument binding failed on the crop's dimensions, a version with the `Blob`
source removed failed on the blob's dimensions, a version that left `ImageData`
zeroed failed on a pixel while **passing** the dimension check, and the crop
before/after order is asserted by the composed case.

## What is still open

`premultiplyAlpha` and `colorSpaceConversion` are parsed and ignored: honouring
premultiplication means deciding that question for the whole canvas stack, which
is not this row. `resizeQuality: "high"` is not honoured (nearest neighbour is a
legitimate answer to `"low"`). A `Blob` is decoded synchronously rather than in
parallel, so the promise settles in the same task; this engine reads `Blob`s
synchronously everywhere. And `createImageBitmap` in a **worker** scope is not
tested here — the binding is installed on the window's global, and whether a
worker's install reaches it is its own question.
