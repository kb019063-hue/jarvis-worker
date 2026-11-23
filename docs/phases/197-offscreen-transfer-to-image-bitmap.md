# Phase 197: `OffscreenCanvas.transferToImageBitmap()`

## What was built

`OffscreenCanvas.transferToImageBitmap()` is implemented in `src/js/DomHtmlBindings.cpp` using `make_image_bitmap` (`include/litebird/js/DomBindings.hpp`, `src/js/DomWindowBindings.cpp`):

- Validates that the `OffscreenCanvas` has positive, within-budget dimensions (throwing `InvalidStateError` otherwise).
- Snapshots the current 2D backing store pixels (or a transparent `width x height` buffer if no context was created), resets the backing store, and returns a new `ImageBitmap`.

## Verification

- `make test`: all 343 engine test suites pass, including `offscreen_canvas_transfer_to_image_bitmap`.
