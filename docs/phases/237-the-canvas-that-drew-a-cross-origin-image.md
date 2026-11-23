# Phase 237 — the canvas that drew a cross-origin image

`HTMLCanvasElement.toDataURL()` has been handing scripts every pixel of a
canvas since Phase 144. The standard permits that only while the bitmap is
**origin-clean** (HTML §4.12.5.1), and this engine had no such flag. The
AGENTS.md row that recorded this reasoned carefully about why leaving it out
was the honest choice:

> Today every pixel came from the page's own drawing primitives, because
> `drawImage`, `ImageBitmap`/`createImageBitmap` and `Response.arrayBuffer()`
> do not exist — so there is nothing that could taint a canvas, and no flag to
> consult. […] Deliberately not written as a flag that is always true, because
> a test could not tell that from a flag that works.

That reasoning was right, and it was also out of date. Two of the three
sources it names had landed since it was written — `createImageBitmap` and
`Response.arrayBuffer()` — and a third thing had landed that the row did not
know about: the renderer already fetched and decoded `<img>` pixels under a
real CORS request context (`load_image_for_element`, `RequestMode::Cors`,
`ResourceResponse::cross_origin`). The only missing piece was the **binding**.

So this phase adds the input the flag was waiting for, and then the flag.

## What changed

**`drawImage` reaches script.** The renderer already implemented
`CanvasRenderingContext2D::draw_image` with full CTM support; nothing about it
was exposed to a page. It now accepts the 3-, 5- and 9-argument forms, and
reads its source from the three sites that actually hold pixels:

| source | pixels from | origin-clean when |
| --- | --- | --- |
| `HTMLCanvasElement` | its 2D context's bitmap (or a blank one of its size, for a canvas that never had a context) | the source canvas was |
| `ImageBitmap` | the bitmap state's pixels | it was when it was made |
| `HTMLImageElement` | the layout pass's decoded `ImageData` | the fetch was CORS-same-origin |

A source that is none of those is **refused with a `TypeError`** rather than
drawn as a blank box. A silent blank draw and a thrown error are different
answers, and the standard gives the error.

**The flag.** `CanvasRenderingContext2D` carries `m_origin_clean`, true at
construction. Drawing a source that is not origin-clean clears it. Resizing the
canvas re-establishes it, because §4.12.5.1 gives the canvas a *new* bitmap and
a new bitmap is a clean one — which is also the only way a page can clean a
tainted canvas, so it is observable behaviour and is tested as such.

**`toDataURL()` refuses a tainted bitmap.** It throws `SecurityError` before
looking at the requested type, because the pixels are what is protected, not
the encoding. A type the engine cannot encode still answers `data:,` on a clean
canvas, exactly as before.

**The origin travels with the pixels.** Three places had to stop losing it:

* `Element` gained `m_image_source_cross_origin`, set by the one function that
  performs the fetch, from the one field that reports whether it crossed an
  origin. It is `mutable` and set through a `const` method because the loader
  is handed a `const Element&` — the pixels are already cached on the document,
  so the flag records a fact about them rather than changing the element.
* `createImageBitmap(canvas)` used to build a bitmap of the right size filled
  with **zeros**. That was a real bug, not a missing feature: the canvas had an
  image and the copy did not. It now copies the pixels and the flag.
* `ImageBitmapState` moved into its own header (`include/litebird/js/ImageBitmap.hpp`)
  because two translation units decide what a bitmap may do — the one that
  makes them and the one that draws them — and they must not hold separate
  opinions about `origin_clean`.

## The two pages that matter

The suite (`tests/suites/237-the-canvas-that-drew-a-cross-origin-image.cpp`)
serves one 2x2 red PNG from two origin roots and loads the same page against
each. The URL differs in one word:

```
<img src="https://app.example.test/red.png">     drawImage -> drew, toDataURL -> data:image/png
<img src="https://other.example.test/red.png">   drawImage -> drew, toDataURL -> SecurityError
```

Both halves are required. A flag that is always true and a flag that is never
true are both wrong, and neither can be told from a working one by a single
test. Each was tried against the suite and each fails it:

* check the flag never (`if (false)`) → `read_back == "err:SecurityError"` fails;
* check it always (`if (context)`) → the clean canvas' `toDataURL` throws, and
  the "unaffected canvas" assertion fails.

The third suite in the file pins the pixel work that was never there at all:
scaling, cropping, the position of a pixel that falls outside the source, and
that a canvas drawing from another clean canvas stays readable.

## What this does not do

* **`drawImage` of an `<img>` needs the layout pass to have run**, because that
  is what fetches and decodes. A page that draws an image before layout gets
  the `TypeError`, not a blank draw. This is a real limitation and it is stated
  in the row rather than papered over.
* **`toBlob()` is still absent**, so the second half of §4.12.5.1's rule has
  nothing to apply to yet. When it lands it must make the same check; the flag
  it needs is now there.
* The `ImageBitmap` of an `<img>`, `createImageBitmap`'s crop and resize
  options, and `OffscreenCanvas` transfer remain outside this phase.
* No CORS preflight for an `<img>`: images are fetched with `NoCors` unless the
  element carries `crossorigin`, which is what the standard says and what the
  loader already did.

## Verification

* `make test` — 377 suites, all passing (4 new).
* Both directions of the flag were broken on purpose and the suite failed each
  time, then passed again when restored.
* The cross-origin/same-origin pair was also driven through a standalone
  harness against `BrowserShell`'s loader before the suite was written, so the
  behaviour was observed independently of the test that asserts it.
