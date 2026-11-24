# Finding 256 — structuredClone flattened every type

**Resolved by Phase 252** (`docs/phases/252-structured-clone-keeps-its-types.md`).

## 1. What was found

`structuredClone` was a walk over own enumerable properties with two special
cases, arrays and "an object that carries `internal`" (Date and Error). Every
other type it was handed became a plain object with whatever enumerable
properties it happened to have — which for a `Map`, a `Set`, a `RegExp`, a
typed array and an `ArrayBuffer` is none at all. Measured on this tree:

```
structuredClone(new Map([["a", 1]]))     -> {} that is not a Map
structuredClone(new Set([1, 2]))         -> {} with no size
structuredClone(/ab+c/gi)                -> {} with no source
structuredClone(new Uint8Array([7, 8]))  -> {} with no elements
structuredClone(new ArrayBuffer(4))      -> {} with no byteLength
structuredClone(buf, {transfer: [buf]})  -> a copy, source left whole
```

Node v22.22.3 answers all five with the value it was given, cloned. Nothing
in the repository recorded this, because nothing had asked: the WPT set that
would exercises `structuredClone` in a window, and the message path has its
own, smaller clone with its own tests.

## 2. What was done

The walk switches on the type now, the way HTML §2.7.6 does: keyed collections
clone their entries in order, `RegExp` its pattern and flags, an
`ArrayBuffer`/typed array/`DataView` its bytes and its geometry, and the
transfer list moves rather than copies. The `seen` map keeps the graph's
shape, so a view and its buffer clone to one buffer. Functions, host objects,
Promises and the weak collections are refused with a real `DOMException`
named `DataCloneError`; a `Symbol` is a `TypeError`, for the reason in the
phase write-up.

## 3. Evidence

`make test` **399/399**, with
`tests/suites/252-structured-clone-keeps-its-types.cpp` holding every row to
Node's answer. On `main`'s four files the suite fails at its first assertion.
The JS corpus, the DOM corpora, WPT, the URL and XML suites, the tree
differential, `make deep` and the four security checkers are unchanged.

## 4. What is still open

`Symbol` is a `TypeError` here and a `DataCloneError` in Node; the message
wording is the engine's; and the worker/`MessagePort`/window message path
still uses its own packet clone, which supports fewer kinds by design.
