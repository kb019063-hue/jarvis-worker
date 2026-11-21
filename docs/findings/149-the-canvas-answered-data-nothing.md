# Finding 149 — the canvas answered `data:,` for every image it ever painted

**Resolved by Phase 144** (`docs/phases/144-the-canvas-hands-back-its-png.md`).

## 1. What was found

`HTMLCanvasElement.toDataURL()` was bound as a constant:

```cpp
C.method("toDataURL", 0, [](Realm&, Element&, Args) { return str("data:,"); });
```

It ignored its arguments (it declared none), ignored the element it was called
on, and returned the same eleven bytes whatever the canvas held:

```js
var c = document.createElement('canvas'); c.width = 3; c.height = 2;
var g = c.getContext('2d'); g.fillStyle = '#f00'; g.fillRect(0, 0, 1, 1);
c.toDataURL()                 // "data:,"
c.toDataURL('image/png')      // "data:,"
c.toDataURL('image/jpeg')     // "data:,"
```

`data:,` is not an error — it is a valid data URL whose payload is empty. A
page could put it in an `<img src>`, save it, or post it, and get an empty
resource with no indication that its picture existed anywhere but on screen.
The method was the only way out of the engine for a canvas, and it led
nowhere.

## 2. Why it matters

- **It is a lie with a plausible shape.** An exception would have said "not
  implemented"; `data:,` looks like a serialized image to a script that does
  not decode it, and fails silently at the far end.
- **The parts existed.** `ImageDecoder::encode_png` was written and used by
  `--render-png`; `Blob`/`FileReader` and `btoa` each had a base64 loop. What
  was missing between them was wiring and a type check, not a codec.
- **The bitmap is not always the element's size.** Over the raster budget a
  canvas keeps a one-pixel store (`RasterBudget.hpp`). Wiring the writer
  straight to `image_data()` would have answered a 1×1 PNG for a canvas that
  says `20000×2` — a second, quieter lie, in a method that already had one.
- **It cannot be finished alone.** `toDataURL()` must refuse a tainted canvas,
  and nothing in this engine can taint one yet. Shipping the read-back without
  saying so would leave a security check that no future `drawImage` would know
  it owed.

## 3. What the fix does

Phase 144 implements the serialization steps of HTML §4.12.5.4 for the one type
the engine encodes. `image/png` (default, case-insensitive essence) produces
the PNG bytes and a base64 `data:` URL; every other valid type, and every
string that is not a MIME type, answers `data:,` — the standard's answer for a
type the user agent does not support. A zero dimension, or a bitmap that is not
the size the element claims, also answers `data:,`. `toDataURL()` does not
create a context on a canvas that has none.

Measured by `make render-conformance` (45,016 checks; 45,008 pixels read back
by `render_check.py`'s own PNG decoder) and by `make test`
(`canvas_to_data_url_is_the_png_it_painted`).

## 4. What is still open

`toBlob()`; `image/jpeg` and `image/webp` encoders; the origin-clean flag, for
which no source of taint exists yet (`AGENTS.md` carries the row and says what
the first of `drawImage`, `ImageBitmap` or `Response.arrayBuffer()` must set);
`ImageBitmap`, `createImageBitmap` and `OffscreenCanvas`.
