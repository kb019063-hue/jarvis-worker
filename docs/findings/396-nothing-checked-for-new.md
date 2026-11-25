# Finding 396 — nothing checked for `new`

Fifteen built-in constructors could be **called**. `Map()` made an empty map,
`Uint8Array(4)` made a typed array, `ArrayBuffer(8)` made eight bytes, and
`DataView(buffer)` made a view — where V8 throws. Every one of them is
constructible and not callable (§10.2.1.1, §24.1.1.1 and the siblings):

```
Map()               -> an empty Map          V8: TypeError: Constructor Map requires 'new'
DataView(buffer)    -> a view                 V8: TypeError: Constructor DataView requires 'new'
Uint8Array(4)       -> a 4-element array      V8: TypeError: Constructor Uint8Array requires 'new'
```

The fifteen: `ArrayBuffer`, `DataView`, the nine typed-array constructors, and
`Map`, `Set`, `WeakMap`, `WeakSet`. They are now refused with V8's words, word
for word — `Constructor <name> requires 'new'` — which was measured, not
guessed: all fifteen messages are byte-identical to Node v22.22.3's.

## Why it survived

The mechanism that refuses a call is one line (`if (!current_is_construct())`)
and it was already in the tree: `Promise` has had it since it was written, and
`Symbol` and the abstract `TypedArray` use the same idea. Nothing else did. The
defect is not that the engine lacked a way to tell a call from a construction;
it is that thirteen implementations were written without asking, and no test
called a constructor without `new`, so nothing said so.

It was found by auditing the neighbouring row — `DataView` and
`%TypedArray%.prototype` — and asking the constructors one at a time whether
they refuse. `new DataView(b)` was right, so the row that names `DataView` was
satisfied; `DataView(b)` was not on the row at all.

## The half that matters: what must NOT be refused

The obvious guard is `!current_is_construct()`, and it is right, but only
because of how the engine routes `[[Construct]]` — and that is the part worth
writing down. `CallContextScope` sets `is_construct = true` for **every** path
into a construction:

- a plain `new Map(...)`;
- a derived `class MyMap extends Map { }` — the native is reached again as the
  parent constructor, with `new.target` set to `MyMap`;
- `Reflect.construct(Map, args)`, which writes no `new` anywhere;
- `Reflect.construct(Map, args, MyMap)` with a different `new.target`;
- `super(...)` inside a derived constructor.

A guard written against the *source* — "was there a `new` before this call" —
would refuse the third case, and a guard written to refuse whenever the native
is not building its own object would refuse the second, fourth and fifth. So
the question got a name of its own, `JsValue::current_requires_new()`, with the
five cases in its comment, rather than being spelled `!current_is_construct()`
at four call sites where the next reader would not know why.

`Array`, `Object`, `Date`, `RegExp`, `Error`, `Function`, `Number`, `String`,
`Boolean` and `Symbol` are callable on purpose (§20.1.1.1 and its siblings) and
are asserted in the suite as such, so that a guard widened to "every
constructor" fails it.

## What it broke on the way

Two kinds of internal call reached a newly-guarded constructor through
`[[Call]]`, and both were found by running the suites rather than by reading:

1. **`ImageData`** built its pixel array with `js_call(array_ctor, …)` on the
   realm's `Uint8ClampedArray` (`DomHtmlBindings.cpp`). `ctx.getImageData(…)`
   throws now, and suite 237 (`an_image_bitmap_carries_the_origin_of_what_made_it`)
   caught it at once. It must construct — the engine is making the array, not a
   script — so it calls `js_construct`.
2. **`SubtleCrypto`** made four `Uint8Array` views of a source's buffer with
   `js_call(u8_ctor, u8_ctor, …)` in `DomCryptoBindings.cpp`, so `digest`,
   `sign` and the rest would all have thrown. All four became `js_construct`.

This is the general lesson of the phase and it is worth keeping: when a
built-in starts refusing to be *called*, every C++ site that reached it through
`[[Call]]` starts throwing too, and the compiler says nothing. The audit is a
grep for `js_call(<ctor>, …)`, and the protection is a suite that runs the
paths — the 47 crypto and ImageData assertions in `tests/test_suite.cpp` were
what said the fix was complete.

## Counted

Differential, two builds of one tree:

- `make test`: **451** suites, from 450 (the new one: `constructors_need_new`)
- test262 at `2e58f87d8350`: 15,153 → **15,153** of 15,686, `0` lost
- `make js-conformance` 1,152/1,158 and `make dom-conformance` 678/678, unchanged

Not a single test262 case moved, which is the honest result: test262 exercises
these constructors through `new` and asks about their prototype and length, and
the call form is a thing this suite had to write down itself.

## Still open

`WeakRef` and `FinalizationRegistry` are not in the engine, so their
constructors take no guard — when they arrive they need one, and
`SharedArrayBuffer`, `BigInt64Array` and `BigUint64Array` likewise.
