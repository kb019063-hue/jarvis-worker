# Finding 201: `OffscreenCanvas.transferToImageBitmap()` was missing

## What was found

`OffscreenCanvas` (Phase 174) and `ImageBitmap` (Phase 179) were implemented, but `OffscreenCanvas.transferToImageBitmap()` was not yet wired up.

## Why it matters

Scripts that render offscreen and synchronously transfer the result to an `ImageBitmap` could not call `oc.transferToImageBitmap()`.

## What the fix does

Exposes `make_image_bitmap()` from `DomWindowBindings.cpp` via `DomBindings.hpp` and implements `OffscreenCanvas.transferToImageBitmap()` in `DomHtmlBindings.cpp`.

## What is still open

- `CanvasRenderingContext2D.drawImage(imageBitmap, ...)`.
