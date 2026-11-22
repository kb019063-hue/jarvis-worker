# Phase 191: `ImageBitmap` and `createImageBitmap()`

## What was built

`ImageBitmap` and `window.createImageBitmap(image)` are implemented in `src/js/DomWindowBindings.cpp`:

- `ImageBitmap`: exposes `width`, `height`, and `close()` (which releases the backing store and sets `width`/`height` to `0`).
- `createImageBitmap(image)`: returns a `Promise<ImageBitmap>` supporting `HTMLCanvasElement`, `ImageData`, and `ImageBitmap` sources, and rejecting with `InvalidStateError` (closed `ImageBitmap` or zero-dimension source) or `TypeError` (missing/unsupported source).

## Verification

- `make test`: all 336 engine test suites pass, including `image_bitmap_and_create_image_bitmap`.
