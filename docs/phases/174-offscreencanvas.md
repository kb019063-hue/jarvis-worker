# Phase 174: `OffscreenCanvas`

## What was built

`OffscreenCanvas(width, height)` is implemented in `src/js/DomHtmlBindings.cpp`:

- Constructor `new OffscreenCanvas(width, height)` clamps dimensions to `[0, 2147483647]` and throws `TypeError` if fewer than 2 arguments are provided.
- `width` and `height` accessors read and update the dimensions, resizing the 2D context if one has been created.
- `getContext('2d')` lazily creates and caches a 2D rendering context with `ImageData` methods installed; other context types return `null`.
- `convertToBlob(options)` returns a `Promise` that resolves with a `Blob` (`image/png` by default) or rejects with `EncodingError` (unsupported MIME type) or `IndexSizeError` (zero or over-budget dimensions).

## Verification

- `make test`: all 327 engine test suites pass, including `offscreen_canvas_draws_and_converts_to_blob`.
