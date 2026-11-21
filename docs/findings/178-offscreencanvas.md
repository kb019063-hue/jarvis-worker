# Finding 178: `OffscreenCanvas` was not implemented

## What was found

`OffscreenCanvas` was missing from the DOM/HTML bindings (noted as open in Phase 144).

## Why it matters

Scripts that render offscreen without attaching a `<canvas>` element to the DOM, or that use `convertToBlob()`, could not construct `OffscreenCanvas`.

## What the fix does

Adds `OffscreenCanvasState`, `construct_offscreen_canvas`, and `setup_offscreen_canvas` in `src/js/DomHtmlBindings.cpp`, reusing `Canvas2DContext`, `ImageDecoder::encode_png`, and `make_blob`.

## What is still open

- `transferToImageBitmap()` (requires `ImageBitmap`).
- Worker-scope exposure of `OffscreenCanvas`.
- `image/jpeg` and `image/webp` encoders.
