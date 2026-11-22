# Finding 195: `ImageBitmap` and `createImageBitmap()` were not implemented

## What was found

`ImageBitmap` and `createImageBitmap()` were missing from `Window` bindings (noted as open in Phase 144 and Phase 174).

## Why it matters

Scripts that decode or snapshot canvas/image data into an `ImageBitmap` via `createImageBitmap()` could not do so.

## What the fix does

Adds `ImageBitmapState`, `setup_image_bitmap`, and `createImageBitmap` in `src/js/DomWindowBindings.cpp`, supporting `HTMLCanvasElement`, `ImageData`, and `ImageBitmap` sources.

## What is still open

- `HTMLImageElement` and `Blob` decoding in `createImageBitmap()`.
- `CanvasRenderingContext2D.drawImage(imageBitmap, ...)` and `OffscreenCanvas.transferToImageBitmap()`.
- Crop rectangle arguments (`sx, sy, sw, sh`) and `ImageBitmapOptions`.
