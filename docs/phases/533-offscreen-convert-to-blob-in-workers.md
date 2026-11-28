# Phase 533 — OffscreenCanvas `convertToBlob()` in workers

Phase 499 exposed `OffscreenCanvas` in classic and module workers and taught
ImageData to find typed-array constructors through the realm's global scope. It
left one OffscreenCanvas method behind:

```cpp
JsValue promise = r.window.is_object()
    ? r.window.get_property("Promise")
    : JsValue();
```

A worker deliberately has no Window. Both the successful PNG path and every
error path therefore returned `undefined`, even though the worker global already
had its own `Promise` constructor.

## What changed

`convertToBlob()` now obtains `Promise` through the `realm_global_binding`
introduced by Phase 499. That helper preserves the Window lookup for a page and
uses `Realm::global_scope` when the realm has no Window:

```cpp
JsValue promise = realm_global_binding(r, "Promise");
```

Nothing models a worker as a Window, and no second Promise implementation is
added. The existing method still calls that realm constructor's static
`resolve`/`reject`, with the constructor as `this`, so the result is branded by
the calling worker's JavaScript realm.

The same lookup covers every completion:

* successful PNG encoding resolves to the existing `Blob` interface;
* an unsupported requested MIME type rejects with `EncodingError`;
* zero or over-budget dimensions reject with `IndexSizeError`;
* backing-store and encoder failures retain their existing rejection paths.

## The byte oracle

`tests/suites/533-offscreen-convert-to-blob-in-workers.cpp` runs one classic and
one module worker. Each worker paints a 3×2 OffscreenCanvas and proves:

* `convertToBlob()` returns an object for which `instanceof Promise` is true;
* the resolved value is an `image/png` Blob larger than the PNG header;
* `blob.arrayBuffer()` is also a Promise;
* the bytes begin with PNG's exact eight-byte signature;
* the IHDR width and height read from those bytes are 3 and 2;
* requesting `image/jpeg` rejects with `EncodingError`;
* converting a 0×2 canvas rejects with `IndexSizeError`.

Only a result string crosses back to the page. The Blob, ArrayBuffer, typed array,
and both Promise chains stay in the worker realm.

As in Phase 499, the owner page runs under a fresh heap agent and the two workers
run sequentially. Parent-side wrappers, loader roots, and queued callbacks are
dropped before the previous test agent is restored. The oracle therefore cannot
change a later suite's automatic-GC threshold merely by creating worker realms.

## Bite proof

Two mutations were compiled and restored:

| mutation | result |
| --- | --- |
| restore the Window-only Promise lookup | the classic worker gets `undefined`; `.then` throws `TypeError` before any Blob can be observed |
| suppress `Promise.reject` on invalid MIME/dimensions | the successful PNG path remains, but the first rejection probe gets `undefined` and the same exact oracle fails |

Constructor-presence checks would catch neither the Blob bytes nor rejection
semantics. The complete Promise chain is the observable contract.

## Verification

The comprehensive gate ran after merging `main` at `1b7c8d14`; the required
final integration with `main` at `152e268f` then repeated the normal and
sanitizer unit gates:

* final integrated `make test`: **569/569** engine suites and **114/114**
  cross-origin checks;
* conformance: tree construction **1,973/1,973**, DOM **678/678**, URL
  **1,188/1,188**, XML **1,718/1,718**, JavaScript **1,154/1,158**, and WPT
  **9,570/9,824** with no unlisted failure;
* differential parsers: **6,000/6,000** HTML trees agree with corrected parse5,
  and the XML differential and **3,000/3,000** round trips have no finding;
* `make fuzz`: 400 parser mutations, 400 pipeline runs, 600 XML mutations,
  2,400 media cases, 5,009 colour inputs, and 2,000 Brotli mutations clean;
* `make deep`: **1,332** runs at depth 200,000 on an 8 MB stack, none failed;
* final integrated ASan/UBSan: **569/569** engine suites and **114/114**
  cross-origin checks at the repository's canonical flags; verifier, JS,
  DOM/URL, XML, WPT, generated
  XML, fuzz, and deep sanitizer parts all pass their floors with no sanitizer
  finding;
* repository document, row, suite, number, and CI-part checks pass;
* `python3 tools/ci/check_suites.py` confirms the Phase 533 oracle is one
  standalone suite and no `RUN_TEST` line was added.

Both classic and module worker answers include the exact PNG signature,
dimensions, and rejection names above.

## Files

* `src/js/DomHtmlBindings.cpp` — realm-global Promise lookup.
* `tests/suites/533-offscreen-convert-to-blob-in-workers.cpp` — worker Promise,
  Blob-byte, and rejection oracle.

## Still open

* `ImageData` is returned correctly in a worker but is not exposed there as a
  global constructor.
* OffscreenCanvas supports PNG only; JPEG/WebP encoding is not implemented.
