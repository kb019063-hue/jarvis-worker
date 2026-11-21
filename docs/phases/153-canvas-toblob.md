# Phase 153: `HTMLCanvasElement.toBlob()`

## What was built

`HTMLCanvasElement.toBlob(callback, type, quality)` is implemented in `src/js/DomHtmlBindings.cpp` alongside `toDataURL()`:

- Validates that `callback` is a callable `Function` (throwing `TypeError` otherwise).
- Normalizes `type` (defaulting to `"image/png"` when omitted or `undefined`).
- Encodes the canvas bitmap via `ImageDecoder::encode_png` and constructs a `Blob` via `make_blob(realm, bytes, "image/png")`.
- Passes `null` to `callback` for unsupported MIME types, malformed MIME strings, zero-dimension canvases, or canvases whose backing store is not the element's size (e.g., over `RasterBudget`).
- Does not create a 2D context on a canvas that has none, and does not clear the bitmap.

## Verification

- `make test`: all 309 engine test suites pass, including `canvas_to_blob_is_the_png_it_painted`.
- `make render-conformance`: 33/36 render reftests match (3 known failures accounted for) and 45,016/45,016 `toDataURL` pixel checks agree.
