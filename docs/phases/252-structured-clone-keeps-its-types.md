# Phase 252 — structuredClone keeps the type of what it copies

Task row: `structuredClone` flattens every structured type to a plain object.
Finding 256.

## 1. What was wrong

`structuredClone` was one recursive function over own enumerable properties.
It knew two shapes: an array, and "an object". Everything else fell into the
second, so a `Map` came back `{}` — no `size`, no `get`, its entries gone —
and the same for a `Set`, a `RegExp` (its `source` and `flags` were not own
enumerable properties, so nothing survived at all), a typed array and an
`ArrayBuffer` (whose bytes are in C++ and have no properties to walk). A
Date and an Error were carried by copying `internal`, which is why they were
the two that worked. A transfer list was not read at all:
`structuredClone(buf, { transfer: [buf] })` copied the bytes and left the
source whole.

HTML's algorithm (§2.7.6, StructuredSerializeInternal) does none of that. It
switches on the object's type and clones the state that type has, which is
what Node v22.22.3 does for every line of the table below.

## 2. What changed

| Type | Cloned as |
| :--- | :--- |
| `Map`, `Set` | a new `Map`/`Set` in insertion order, keys and values cloned through the same graph |
| `RegExp` | a new `RegExp` from the engine's own pattern and the canonical `dgimsuvy` flags, `lastIndex` 0 |
| `ArrayBuffer` | its bytes, in a new buffer; a transfer list moves them instead and detaches the source |
| typed array | a view of the same element type over the clone of its buffer, at the same offset and length |
| `DataView` | the same, without an element type |
| `Date`, `Error` | as before (time value, `message`/`cause`/`stack`, the constructor's prototype) |
| a function, a host object, a Promise, a WeakMap/WeakSet | `DataCloneError` |
| a `Symbol` | `TypeError` — see §4 |

The graph's shape is kept: the `seen` map means two references to one object
are one object afterwards, a cycle is a cycle, and — the part that needed
work — a view and the buffer it is a window on clone to **one** buffer, so
`clone.v.buffer === clone.b`.

Where the state lives is private to each file, so each file answers for it:
`collection_entries`/`add_collection_copy_entry` in `BuiltinsCollections.cpp`,
`js_regexp_source_and_flags` in `BuiltinsRegExp.cpp`, and
`buffer_view_info`/`typed_array_view_over`/`data_view_over`/
`array_buffer_transfer` in `TypedArray.cpp`. `BuiltinsObject.cpp` keeps the
switches and the refusals.

## 3. How it was verified

`tests/suites/252-structured-clone-keeps-its-types.cpp`, a file of its own in
the form `tests/suites/README.md` asks for, checks every row above against
Node v22.22.3's answers — Map/Set order and object keys, a Map that holds
itself, Regexp source/flags/lastIndex and that the pattern still runs,
`Uint8Array`/`Uint16Array`/`Float64Array`/`ArrayBuffer`/`DataView` identity
and bytes, the shared-buffer identity, the transfer list detaching the
source, and the four refusals — with the Date, Error, cycle, shared-object
and plain-object cases as controls that the change kept what worked.

```bash
make test      # All 399 engine test suites passed successfully!
```

| Check | Result |
| :--- | :--- |
| `make js-conformance` | 1149/1158 (99.2%), 0 crashes — unchanged |
| `make conformance` / `make dom-conformance` | 1973/1973 · 678/678 |
| `make wpt` | 9569/9824, the same 255 listed |
| `make url-conformance` / `make xml-conformance` | 1188/1188 · 1718/1718 |
| `make render-conformance` | 62/66 and 36/41, the same recorded failures |
| `make diff-trees` / `make diff-xml` | 1500/1500 trees equal, no crash |
| `make deep` | 1332 runs, 0 failed |
| `make security-fetch` / `-worker` / `-https` / `-websocket` | 3/3 · 17/17 · 27/27 · 50/50 |
| `make verify-gc-stress` | 11830 |
| `build-asan/litebird` + the same 20-case probe | identical answers, no sanitizer report |

Negative control: with the four source files and their two headers as `main`
has them and this suite in place, it fails on the first assertion — a cloned
`Map` is not a `Map`, and `Array.from(cloned.keys())` throws because the clone
has no `keys`.

## 4. What is still open

- **A `Symbol` is refused with a `TypeError`, where Node throws a
  `DataCloneError`.** Two reasons, neither of them "the standard says
  otherwise": this engine has no symbol *box*, so a symbol-valued key or
  value cannot be carried through any object at all, and the sentence that
  describes it is the engine's own wording. A clone that answered a
  `DataCloneError` here would be claiming to have tried.
- **The message wording is the engine's.** The name — `DataCloneError` — is
  the standard's; what follows the colon is not Node's sentence.
- **The message path is a different clone and still a smaller one.** Worker,
  `MessagePort` and window `postMessage` use `js_write_structured_clone`,
  which is a thread-neutral packet with no `JsValue` in it and deliberately
  supports primitives, ordinary objects and arrays only. Sharing this walk
  with it means giving the packet node kinds for the keyed collections and
  the byte types, and is the ES-module/transferable row's work, not this
  one's. `postMessage(buf, [buf])` still says transfer lists are unsupported.
- **`sharedArrayBuffer`, `Blob`, `File`, `ImageData`, `DOMPoint` and the rest
  of HTML's serializable types** are not in the table above; none of them
  exists in this engine to be cloned.
