# Phase 396 — a constructor called without `new`

*Finding 396.* Fifteen built-in constructors were ordinary functions, so the
call form quietly made an object where the language throws. Every one of them
is now refused with V8's words.

## The defect

```
Map()             -> an empty Map              V8: TypeError: Constructor Map requires 'new'
DataView(buffer)  -> a view                    V8: TypeError: Constructor DataView requires 'new'
Uint8Array(4)     -> a 4-element typed array   V8: TypeError: Constructor Uint8Array requires 'new'
ArrayBuffer(8)    -> eight bytes               (WeakMap and WeakSet the same)
```

The fifteen: `ArrayBuffer`, `DataView`, the nine typed-array constructors, and
the four collections. They were found by asking every global constructor the
same question — call it with no `new`, and see whether it refuses — while
auditing the neighbouring `DataView` / `%TypedArray%.prototype` row. `new
DataView(b)` was correct, so that row was satisfied; `DataView(b)` was simply
not a thing the row, or any test, had ever asked.

## The fix

`JsValue::current_requires_new()` is new, and it is the one-line answer to
"was this native reached by a call rather than by a construction". The four
call sites (`TypedArray.cpp` for the typed arrays, `ArrayBuffer` and
`DataView`; `BuiltinsCollections.cpp` for `Map`, `Set`, `WeakMap`, `WeakSet`)
throw `TypeError: Constructor <name> requires 'new'`.

The predicate names a question that is *not* the same as
`!current_is_construct()`, even though it is that today:

- every `[[Construct]]` sets `is_construct` — plain `new`, a derived
  `class X extends Map`, `Reflect.construct`, `Reflect.construct` with another
  `new.target`, and `super()`;
- a native that builds a `new.target` object under `Reflect.construct` cannot
  use the absence of `is_construct` to mean "no new", and three of the fifteen
  do exactly that (`construct_view` and `ArrayBuffer` both branch on
  `current_is_construct() ? current_derived_this() : …`).

So the name says what the check means, and `requires_new` cannot be changed to
something else by accident the way a bare negation at four sites could.

The ten deliberately-callable constructors — `Array`, `Object`, `Date`,
`RegExp`, `Error`, `Function`, `Number`, `String`, `Boolean`, `Symbol` — are
untouched and asserted in the suite, so a guard widened to every constructor
fails it.

## What it broke, and why that is the interesting half

Making a constructor refuse a call breaks every C++ site that called it. Two
did, both invisible to the compiler:

1. `ImageData`'s pixel array was a `js_call` of `Uint8ClampedArray`
   (`DomHtmlBindings.cpp`), so `ctx.getImageData(0,0,1,1)` threw. Suite 237
   caught it: *"Uncaught TypeError: Constructor Uint8ClampedArray requires
   'new'"* under `an_image_bitmap_carries_the_origin_of_what_made_it`.
2. Four `Uint8Array` views in `DomCryptoBindings.cpp` were built the same way,
   so `subtle.digest`, `sign`, `encrypt` and `decrypt` would all have thrown.

Both now construct (`js_construct`), because the engine is making the object
and not a script. The audit for this class of break is a grep for
`js_call(<constructor>, …)`; the protection is a suite that runs the paths.

## Measured

| | before | after |
|---|---|---|
| `make test` | 450 suites | **451** |
| test262 (15,686) | 15,153 | **15,153**, 0 lost |
| js-conformance | 1,152 / 1,158 | 1,152 / 1,158 |
| dom-conformance | 678 / 678 | 678 / 678 |

test262 did not move at all, and that is the honest measurement: it asks about
these constructors through `new`, never through a call. The evidence that the
fix works is the new suite, which fails 14 of 15 cases on the unfixed binary
(`UNFIXED: 1 refuse, 14 allowed` against `FIXED: 15 refuse, 0 allowed`).

## Still open

`WeakRef`, `FinalizationRegistry`, `SharedArrayBuffer`, `BigInt64Array` and
`BigUint64Array` do not exist in the engine; each needs this guard when it
arrives. The DOM constructors that are absent (`MutationObserver`,
`OffscreenCanvas`, `ImageBitmap`) are the DOM corpus's row, not this one.
