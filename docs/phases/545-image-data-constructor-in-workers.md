# Phase 545 — ImageData constructor in workers

Phase 499 made the `ImageData` objects returned by an OffscreenCanvas usable in
classic and module workers. It recorded each non-document interpreter's global
environment in `Realm::global_scope`, so the existing constructor could find
that realm's `Uint8ClampedArray` and `Object` built-ins. But a worker script
still could not name the constructor:

```js
typeof ImageData // "undefined"
```

The interface and all of its behavior were already registered. Only the two
worker-global surfaces omitted it.

## What changed

A non-document `JsInterpreter` now installs its realm's existing `ImageData`
constructor beside `OffscreenCanvas` and `Path2D`:

```cpp
m_global_env->define("ImageData", dom_realm.ctor("ImageData"));
```

The engine models a dedicated worker's `self` separately from that environment,
so worker setup mirrors the same value there too:

```cpp
self_obj.set_property("ImageData", env->get("ImageData"));
```

No worker is represented as a Window, no constructor is duplicated, and no
ImageData implementation changes. Page realms continue to receive the same
Window interface constructor as before.

## Behavioral oracle

`tests/suites/545-image-data-constructor-in-workers.cpp` runs one source in a
classic worker and a module worker. Each realm proves:

* bare `ImageData` is a function and `self.ImageData === ImageData`;
* `new ImageData(2, 2)` has the worker constructor's prototype, an sRGB colour
  space, and sixteen zero bytes in a worker-realm `Uint8ClampedArray`;
* `new ImageData(bytes, 2)` keeps the supplied typed array and derives a 2×2
  size;
* `OffscreenCanvasRenderingContext2D.putImageData()` accepts that constructed
  object, and `getImageData()` returns the exact red, green, blue, and yellow
  RGBA bytes as another instance of the exposed constructor;
* `createImageData()` also returns an instance of that constructor;
* invalid zero dimensions and incomplete pixel rows retain their existing
  `IndexSizeError` behavior.

Only a result string crosses the worker boundary. Every ImageData, typed array,
canvas context, and pixel read remains in the worker realm. A Window-side
control constructs and inspects ImageData before either worker starts.

As in Phases 499 and 533, the owner page runs under a fresh heap agent, workers
run sequentially, and wrappers, loader roots, and callbacks are released before
the previous test agent is restored.

## Bite proof

Two one-line removals were compiled, run, and restored:

| mutation | result |
| --- | --- |
| omit `ImageData` from the non-document interpreter environment | the classic worker reports `ReferenceError: ImageData is not defined` |
| keep the bare binding but omit its `self` mirror | construction and pixels succeed, but the exact `self.ImageData === ImageData` answer fails |

The second mutation matters because this engine's `self` is deliberately a
separate host object. Installing only a lexical/global binding does not make its
properties appear there automatically.

## Verification

After merging `main` at `40018532`:

* `make test`: **576/576** engine suites and **114/114** cross-origin checks;
* conformance: tree construction **1,973/1,973**, DOM **678/678**, URL
  **1,188/1,188**, XML **1,718/1,718**, JavaScript **1,154/1,158**, and WPT
  **9,570/9,824** with no unlisted failure;
* differential parsers: **6,000/6,000** HTML trees agree with corrected parse5,
  and the XML differential and **3,000/3,000** round trips have no finding;
* `make fuzz`: 400 parser mutations, 400 pipeline runs, 600 XML mutations,
  2,400 media cases, 5,009 colour inputs, and 2,000 Brotli mutations clean;
* `make deep`: **1,332** runs at depth 200,000 on an 8 MB stack, none failed;
* ASan/UBSan: **576/576** engine suites and **114/114** cross-origin checks at
  the repository's canonical flags; verifier, JS, DOM/URL, XML, WPT, generated
  XML, fuzz, and deep sanitizer parts all pass their floors with no sanitizer
  finding;
* repository document, row, suite, number, and CI-part checks pass;
* `python3 tools/ci/check_suites.py` confirms one standalone suite and no new
  `RUN_TEST` line.

The Phase 545 suite also passes alone after both mutations are restored.

## Files

* `src/js/JsInterpreter.cpp` — install the realm's ImageData constructor in
  non-document global environments.
* `src/js/Worker.cpp` — mirror that constructor onto the separately modeled
  dedicated-worker `self`.
* `tests/suites/545-image-data-constructor-in-workers.cpp` — classic/module
  constructor, typed-array, canvas, pixel, and error oracle.

## Still open

* Path2D has no cached flattening.
* OffscreenCanvas encodes PNG only; JPEG and WebP encoding are not implemented.
