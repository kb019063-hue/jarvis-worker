# Finding 276 — the crop was optional because nobody passed one

**Severity:** medium. `createImageBitmap` accepted a source rectangle, a resize
and four options, performed none of them, and **resolved successfully anyway**.
A page could not tell the difference between a bitmap of the crop it asked for
and a bitmap of the whole image: both are an `ImageBitmap`, both have a width,
and the promise settles the same way. Two more sources the standard lists were
refused outright.

**Found by:** reading the "still open" clause of the Phase 237 row, which named
`createImageBitmap`'s crop/resize options as the next thing on the canvas row,
and then measuring the call instead of trusting the row's wording.

## What was wrong

The binding was one line, and the line is the bug:

```cpp
I.method("createImageBitmap", 1, [](Realm& r, Args args) {
```

HTML §8.11.2 declares the method **twice**:

```
promise = self.createImageBitmap(image, options)
promise = self.createImageBitmap(image, sx, sy, sw, sh [, options])
```

One argument was read. `args[1]` through `args[5]` were never looked at, so the
crop, the resize, and both of the method's first two steps went unperformed:

| call | answered | §8.11.2 |
| :--- | :--- | :--- |
| `createImageBitmap(cv8x8)` | 8x8 | 8x8, correct |
| `createImageBitmap(cv8x8, 0,0, 4,4)` | **8x8** | 4x4 |
| `createImageBitmap(cv, 0,0, 4,4, {resizeWidth:2, resizeHeight:2})` | **300x150** | 2x2 |
| `createImageBitmap(cv, 0,0, 0,4)` | **300x150, resolved** | rejected `RangeError` |
| `createImageBitmap(cv, {resizeWidth:0, resizeHeight:2})` | **resolved** | rejected `InvalidStateError` |
| `createImageBitmap(new Blob([png]))` | **rejected `TypeError`** | the PNG's size |
| `createImageBitmap(new Image())` | **rejected `TypeError`** | rejected `InvalidStateError` |
| `createImageBitmap(new ImageData(2,2))` | 2x2, **all pixels zero** | 2x2, the data's pixels |

The last row is the same defect wearing a different hat. `ImageData` was
accepted, its dimensions were copied, and its pixels were not — so a page that
made a bitmap of an `ImageData` and drew it got a transparent rectangle of the
right size. Nothing threw, and nothing said so.

## What changed

`src/js/DomWindowBindings.cpp`, the `createImageBitmap` binding only:

- **The arguments.** The two overloads are told apart by count, as WebIDL's
  overload resolution does: five or more is `(image, sx, sy, sw, sh, options)`,
  otherwise `(image, options)`.
- **Step 1** — a zero `sw` or `sh` returns a promise rejected with a
  `RangeError`. It is checked *before* the image is, which is why it is a
  `RangeError` and not the `InvalidStateError` everything else uses.
- **Step 2** — a zero `resizeWidth` or `resizeHeight` is an
  `InvalidStateError`.
- **The `Blob` source**, which §8.11.2 lists and the binding refused. Its bytes
  come from `blob_bytes()` (already exported by `DomFileBindings.cpp`) and are
  decoded by `ImageDecoder::decode_bytes`, the same call the renderer uses for
  an `<img>`. Bytes that are not an image are an `InvalidStateError`: the
  source was the right kind of thing and its data was not.
- **The `<img>` source** via `load_image_for_element`, the same pixels
  `drawImage` reads. An image that has not decoded is step 3's "unusable"
  source — an `InvalidStateError`, where it used to be a `TypeError`.
- **`ImageData`'s pixels**, copied. It is a copy and not a view, which is
  asserted: writing the `ImageData` afterwards does not change the bitmap.
- **The source rectangle**, "cropped to the source rectangle with formatting" —
  so a rectangle larger than the image **pads with transparent black** rather
  than shrinking, and an origin past the edge crops entirely to transparent.
- **`resizeWidth`/`resizeHeight`**, and the aspect ratio when only one is given
  (a 4x2 asked for width 2 is 2x1). Nearest neighbour: this is a geometry fact
  about which pixel a coordinate names, not a resampling claim, and the suite
  asserts colours a nearest sample reproduces exactly.
- **`flipY`**, off by default, reversing rows and not columns.
- The crop is applied **before** the resize, which is the order of the steps; a
  suite asserts the composition because the other order samples the wrong
  pixels and still answers the right dimensions.

## The tell

The dimensions cannot carry this alone, and that is the whole reason the finding
exists: the broken binding returned an `ImageBitmap` with plausible numbers. The
suite therefore reads **pixels** — a 2x2 canvas whose four quadrants are red,
green, blue and white, so which part of the source a pixel came from is
decidable from the pixel — and each single-pixel crop is asserted against its
own colour.

Four wrong versions were compiled and each failed a different assertion:

| what was put back | assertion that failed |
| :--- | :--- |
| the one-argument binding (the whole defect) | `window.c.width + 'x' + window.c.height == "1x1"` |
| the `Blob` source removed (the rest correct) | `window.b.width + 'x' + window.b.height == "4x2"` |
| `ImageData` pixels left zeroed | `pixel_of_bitmap(window.im, 0, 0) == "255,0,0,255"` |
| — | and the suite as a whole |

The third is the one worth keeping: it resolves, it has the right dimensions, and
only a pixel comparison can see it.

## Verification

```
$ make -j2 && make test
All 409 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9569/9824 subtests pass in 190 files; 255 fail as listed, 0 fail and are not listed
```

`tests/suites/272-create-image-bitmap-crop-and-resize.cpp`.

## What is still open

- **`premultiplyAlpha` and `colorSpaceConversion` are parsed and ignored.** Both
  are in the options dictionary; this engine's bitmaps are 0xAARRGGBB that are
  never premultiplied anywhere, so honouring `premultiplyAlpha` means deciding
  that question for the whole canvas stack, and it is not this row's.
- **`resizeQuality`** is likewise unread: it is a quality hint and nearest
  neighbour is a legitimate answer to `"low"`, but `"high"` is not honoured.
- **A `Blob` is decoded synchronously** in the binding rather than in parallel
  as §8.11.2's steps say, so the promise resolves in the same task. The
  observable difference is task ordering, and this engine's `Blob` reading is
  synchronous everywhere else too.
- **`ImageData`'s colour space and `pixelFormat`** are not consulted; both are
  `rgba-unorm8`/`srgb` in this engine and no other value can be built.
- Not done here, and not a claim: `createImageBitmap` in a **worker** scope.
  The binding is installed on the window's global; whether a worker's install
  reaches it is a separate question with its own row.
