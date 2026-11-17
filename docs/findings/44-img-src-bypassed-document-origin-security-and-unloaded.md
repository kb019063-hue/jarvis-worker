# 44. ✅ `<img src>` bypassed document-origin security, and unloaded images without dimensions laid out as 100×100 boxes

> Moved out of `AUDIT.md` — written there as a `## 44. ✅ `<img src>` bypassed document-origin security, and unloaded images without dimensions laid out as 100×100 boxes` section.
> The text below is the section as it stood, unchanged.


**The hole.** Phase 33 moved `fetch()` and `Worker` behind
`ResourceLoader::load_for_page`, but `LayoutTree::build_element` (`src/layout/LayoutTree.cpp`)
still passed an `<img>` element's raw `src` attribute straight to
`ImageDecoder::decode_file(src)`. `decode_file` opened any non-`data:` string
with `std::ifstream(path, std::ios::binary)`. Consequently, a remote web
document (`https://attacker.example/page.html`, or one smuggling
`<base href="file:///...">`) could embed `<img src="/path/to/local.png">` or
`<img src="file:///path/to/local.bmp">`, read local disk images in-process, and
observe their decoded intrinsic pixel dimensions via layout or
`getBoundingClientRect()`/`offsetWidth`/`offsetHeight` side channels.

In addition, when an `<img>` without explicit `width`/`height` attributes or
CSS dimensions failed to decode (or was blocked by origin policy),
`LayoutBox::layout_image` (`src/layout/LayoutBox.cpp`) sized the box to a
hardcoded `100×100` placeholder, pushing surrounding content down by 100 px,
and inline-formatted `<img>` boxes (`InlineFormattingContext`) never wrote their
laid-out fragment coordinates back to the DOM element (`set_layout_box`), so
`img.getBoundingClientRect()` returned `0×0` even for valid inline images.

**The fix.**
1. `LayoutTree` now resolves and loads every `<img src>` through
   `ResourceLoader::load_for_page(src, doc.base_uri(), doc.url(), /*enforce_cors=*/false)`
   and caches decoded bitmaps per resolved URL on `Document`
   (`lookup_decoded_image` / `cache_decoded_image`). Web documents (`http:`,
   `https:`, `data:`) cannot read `file:` URLs or local filesystem paths, even
   when `<base href="file:///...">` is present; trusted local `file:` documents
   keep relative local image loading.
2. `ImageDecoder::decode_bytes` now decodes PNG (`decode_png` / `encode_png`,
   backed by the pure-C++20 RFC 1950/1951/1952 `Deflate` engine with 64 MiB
   decompression and 16384×16384 / 64M-pixel bounds), wires Phase 38's
   `JpegDecoder` and `GifDecoder`, and rasterizes SVG subresources alongside
   BMP, PPM, and `data:image/*;base64,...` URLs.
3. Unloaded or blocked `<img>` elements without explicit `width`/`height` now
   lay out as `0×0` instead of `100×100` (explicit HTML `width`/`height`
   attributes or CSS `width`/`height` are still honoured), and
   `InlineFormattingContext` writes laid-out `ImageNode` border-box coordinates
   back to the `Element` so `getBoundingClientRect()`/`offsetWidth` report live
   inline image geometry.

**How verified.**
```bash
./build/test_suite
make security-fetch
```
`make test` passes **153/153** suites (and **153/153** under ASan+UBSan),
including `img_subresources_enforce_document_origin_security` (proving a web
page at `https://app.example/index.html` — with and without
`<base href="file:///...">` — is denied local PNG access and lays out the
blocked `<img>` at `0×0`, while a local `file:` document decodes the same PNG
at its `24×18` intrinsic size and reports `24×18` from
`img.getBoundingClientRect()`). `make security-fetch` passes `3 checks; 0 failure(s)`.

**What is still approximate.**
- WebP, AVIF, and ICO `<img>` payloads are not decoded yet (PNG, JPEG, GIF, SVG,
  BMP, and PPM are decoded).
- `HTMLImageElement` script properties and events (`naturalWidth`,
  `naturalHeight`, `complete`, `decode()`, `load`/`error` events on `<img>`,
  `srcset`, and `<picture>`) are not implemented yet.
- Unloaded `<img>` elements with non-empty `alt` text and no explicit
  dimensions collapse to `0×0` rather than synthesizing an inline `alt`-text
  fallback box.
- Cross-origin `<img>` loads follow the standard default `no-cors` subresource
  model, but `<canvas>` taint tracking (`crossorigin="anonymous"`) is not
  enforced yet because `drawImage` does not yet take `HTMLImageElement` bitmaps.

---
