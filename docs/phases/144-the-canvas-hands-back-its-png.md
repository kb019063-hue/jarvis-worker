# Phase 144 — the canvas hands back the PNG it painted

Task row: **`HTMLCanvasElement.toDataURL()`**, the item the Phase 139 row
("The four interfaces typed arrays unblocked") left open. Finding 149.

## 1. What was wrong

`toDataURL()` existed, took no arguments, and answered the same string for
every canvas the engine ever produced:

```cpp
C.method("toDataURL", 0, [](Realm&, Element&, Args) { return str("data:,"); });
```

`data:,` is a legal data URL for nothing at all: a page that asks a canvas for
its image, saves it, or hands it to an `<img>` gets an empty resource. The
engine could already write the PNG — `ImageDecoder::encode_png` is what
`--render-png` and `ImageData`'s tests use — and could already base64 a run of
bytes twice over (`Blob`/`FileReader` and `btoa`), but none of it was reachable
from the one method a page uses to export a canvas.

Three things were checked before the stub was replaced, because each is a place
an honest implementation could lie:

- **A canvas nobody drew on.** The standard's canvas bitmap exists from the
  element, at its `width`/`height`, transparent black. Serializing it must not
  be the same as serializing nothing.
- **A canvas with no context.** `toDataURL()` must not create one: a page that
  only exports a canvas should not find a `CanvasRenderingContext2D` on it
  afterwards.
- **A canvas over the raster budget.** Such a canvas keeps a one-pixel backing
  store that drawing does nothing to (`RasterBudget.hpp`). Serializing that
  store would answer a 1×1 PNG for an element that says `20000×2` — a picture
  of nothing, labelled as the picture.

## 2. What changed

**`toDataURL(type, quality)` serializes the bitmap.** HTML §4.12.5.4: the type
is a MIME type, the essence is what selects an encoder, and the bytes go into
`data:<type>;base64,<bytes>`.

- **`image/png` is the one type the engine encodes**, through the existing
  `ImageDecoder::encode_png` (8-bit RGBA, filter 0, a stored-block zlib stream).
  A missing or `undefined` type means `image/png`; the essence is
  ASCII-lowercased and trimmed, so `"IMAGE/PNG"` and `" image/png "` are the
  same request. Parameters are carried into the URL unchanged.
- **Any other valid type answers `data:,`** — `image/jpeg` and `image/webp`
  included, because there is no encoder for them. The standard asks for
  `data:,` when the user agent does not support the type, so this is the
  standard's answer and not a refusal invented here. So does a string that is
  not a MIME type at all (`"not a mime type"`, `""`, `null`).
- **`quality` is ignored for PNG**, which is what the standard says.
- **No context is created.** With a context the bitmap is the one the context
  draws into; without one, a blank `ImageData` of the element's size is built,
  and the element still has no context afterwards.
- **A bitmap that is not the size the element is** (the over-budget one-pixel
  store) answers `data:,`, as does a zero `width` or `height`. Both are what
  Chrome answers for a canvas it cannot serialize.

The base64 loop moved to `include/litebird/common/Base64.hpp`, and
`DomFileBindings.cpp`'s local copy went away; that was the second copy in the
tree, and `toDataURL` would have been the third. `btoa` keeps its own loop
because it also has to reject a code unit above `0xFF` before any byte exists.

## 3. How it was verified

**The unit suite** — `make test`, `canvas_to_data_url_is_the_png_it_painted`:

- the prefix is `data:image/png;base64,`; the decoded bytes begin with the PNG
  signature, and the IHDR says 3×2, 8 bits a channel, colour type 6 (RGBA);
- `ImageDecoder::decode_png` reads the bytes back to exactly the six pixels
  that were painted — `0xFFFF0000`, `0xFF00FF00`, transparent, transparent,
  transparent, `0xFF0000FF`;
- two calls are the same URL, including `image/png` with a quality and
  `IMAGE/PNG`; `image/jpeg`, `image/webp`, `image/gif`, `"image"`, `"not a mime
  type"`, `""` and `null` all answer `data:,`;
- a 2×1 canvas that never had a context serializes a transparent 2×1 image, and
  `canvas_context()` is still `nullptr` afterwards;
- `width="0"` and a `20000×2` canvas over the raster budget answer `data:,`;
- serializing does not clear the bitmap.

**An independent reader** — `make render-conformance`, which now runs
`tools/conformance/canvas_to_data_url.py` beside the reftests. It runs the
binary on a probe page, takes the data URL, decodes the base64 and reads the
PNG with `render_check.py`'s own decoder — all five scanline filters, per-chunk
CRC, no engine code — and compares every pixel. It prints:

```
Canvas toDataURL harness self-test: 5/5 (read, wrong colour refused, CRC refused, empty refused)

Canvas toDataURL: 45016/45016 checks agree (45008 pixels read back by render_check.py's own PNG reader)
```

The 45,008 pixels are the 300×150 default canvas, the 3×2 painted one and the
2×1 blank one; the other eight are the types, the casing and the `data:,`
answers. The harness self-tests first: a 1×1 red PNG is written with `zlib`,
read back, compared with the wrong colour (which must fail), corrupted in its
IDAT (which must raise), and an empty file is refused. When the engine's writer
and the reader are the same code, a bug in one answers the other; here they are
not.

## 4. What is still open

- **`toBlob()`** — the same serialization into a `Blob`, with its callback in a
  task. `Blob` exists and `make_blob` is exported, so what is missing is the
  asynchronous delivery, not the image.
- **`image/jpeg` and `image/webp`** — no encoders exist, so both answer
  `data:,`. A page that asks for a JPEG-serialized canvas gets nothing, which
  the method's own tests say out loud.
- **The origin-clean flag.** A canvas that has drawn a cross-origin image may
  not be read back, and this engine has no way to draw one: `drawImage`,
  `ImageBitmap`/`createImageBitmap` and `Response.arrayBuffer()` are all still
  open, and every pixel a canvas holds today came from the page's own drawing.
  So the check `toDataURL()` must make has no input yet. It was deliberately
  not written as a flag that is always true — a test cannot tell that from a
  flag that works — and the row in `AGENTS.md` says what the first of those
  three to land has to set.
- **`ImageBitmap`, `createImageBitmap`, `OffscreenCanvas`** — unchanged, still
  absent.
- **`toDataURL()` on a detached or worker-owned canvas** — no `OffscreenCanvas`
  and no transferable canvas exist, so there is no such receiver.
