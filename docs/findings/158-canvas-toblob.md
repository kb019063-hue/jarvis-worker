# Finding 158: `HTMLCanvasElement.toBlob()` was missing

## What was found

`HTMLCanvasElement` implemented `toDataURL()` (Phase 144, finding 149) but had no `toBlob(callback, type, quality)` method.

## Why it matters

Scripts that export a canvas as a `Blob` (for `URL.createObjectURL`, `FormData`, or `FileReader`) could not do so without manually base64-decoding `toDataURL()`.

## What the fix does

`canvas_to_blob` in `src/js/DomHtmlBindings.cpp` reuses `png_mime_type`, `canvas_bitmap_for_serialization`, `ImageDecoder::encode_png`, and `make_blob` to produce a `Blob` of type `"image/png"` or `null` when the type is unsupported or the canvas is zero-size / over-budget, then invokes `callback` with the result.

## What is still open

- Asynchronous task queueing (currently invokes `callback` synchronously).
- `image/jpeg` and `image/webp` encoders.
- Origin-clean taint tracking (shared with `toDataURL()`).
