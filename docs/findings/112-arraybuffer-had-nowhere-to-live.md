# Finding 112 — the bytes had nowhere to live

**Severity:** high (for the platform) — not a bug in existing code, but the
absence that made a whole class of interfaces impossible to write. `fetch()`,
`WebSocket`, `ImageData` and `TextDecoder` were each either stubbed or
blocked, and each recorded in `AGENTS.md` or `ROADMAP.md` as waiting on this
one thing.

**Found by:** `ROADMAP.md` Wave 2 #8, *"`ArrayBuffer` + typed arrays +
`DataView` (unblocks everything)"*, and the four rows that name it as their
blocker.

## 1. What was wrong

There is no `ArrayBuffer` in the engine, and there was nowhere for one to be.
Every value a script could make was a primitive, a heap object with a property
table, or an array — and an array of 100,000 numbers is 100,000 heap cells plus
a `std::deque`, which is not a byte buffer and cannot be handed to a decoder.

Four separate places said so:

- `src/js/DomWindowBindings.cpp` answers `"arraybuffer"` with `null` for
  `Blob`'s binary read: *"the engine has no ArrayBuffer to give, and says
  null."*
- `AGENTS.md` → Open tasks: WebSocket `"send(ArrayBuffer/TypedArray/Blob) and
  `binaryType = \"arraybuffer\"` message delivery (blocked on Wave 2
  `ArrayBuffer`)"*.
- `ROADMAP.md` row 23: *"`ArrayBuffer`/`Blob` WebSocket binary exposure
  (blocked on typed arrays)"*.
- `ROADMAP.md` row 6: *"`ArrayBuffer` is **not exposed to script**. Blocks
  `fetch()` bodies, WebSocket binary frames, Canvas `ImageData`."*

`TextDecoder` and `TextEncoder` had the same shape of absence: the Encoding
Standard's 40 decoders were implemented in Phase 47 and reachable from C++,
but `decode()` takes a `BufferSource`, so a page could not call one.

## 2. What changed

Phase 106 supplies the buffer and the views; the four consumers above are now
possible and are separate rows. The finding worth recording is the near-miss
in how the object model took it.

### Where the bytes went

The plausible implementation is an array-like object whose elements live in
`array_elements`, because that is where this engine keeps elements and the
generated code for `a[0]` would then work unchanged. That would have meant
editing `JsValue.cpp` and `Heap.cpp` — and both of those were held by open
pull requests at the time, so the work could not have been done at all without
violating `AGENTS.md`'s *"stop before changing a file that another open pull
request names"*.

The `JsExotic` hook already existed for objects that *compute* a property
rather than store it. A typed array is exactly that: its `0` is a decoded
value at an address, not a stored one. Attaching an exotic answers `get`,
`set`, `has`, `remove`, `keys` and `computed_property`, and the indexed
behaviour follows with no edit to either held file. The finding is not "we
were lucky" — it is that an existing abstraction already covered the case, and
the cost of not looking for it first would have been either a collision or a
worse design (an array-shaped typed array cannot be a window on a shared
buffer, which is what `subarray` and two views of one `ArrayBuffer` require).

### The one that bit

The first build of the new file segfaulted on every script, `console.log(1)`
included, and `make js-conformance` reported it exactly:

```
JS conformance, full corpus: 0/1156 (0.0%)  [29 files, 1156 crashes, 0 timeouts]
```

A native function in this engine keeps its own properties in a
`std::shared_ptr<PropertyMap>` inside `JsValue::Extra` and has **no heap cell**
until something needs it as an object in its own right — which is
`JsValue::function_cell()`. Setting `constructor.object_val->prototype` read
`object_val` as null, because for a native function it is null until then. The
same file's own header says so (*"A native function gets it only when
something needs it to be an object in its own right"*), and `BuiltinsObject.cpp`
sets a constructor's prototype chain the same way, through
`ctor.function_cell()`. Worth a finding because the crash was total, obvious
in the corpus and invisible in the binary until the corpus was run — and
because the fix is not "add a null check", it is "ask for the cell".

## 3. How it was verified

`make js-conformance` went from `1138/1156` (up one from `1137`, the
`Uint8Array` case in `collections-iterators.dat`) with `0 crashes`, and
`make test` runs 262 suites with `arraybuffer_and_typed_arrays` among them.
The other five scoreboards are unchanged: tree construction 1972/1973, the DOM
678/678, URL 1188/1188, XML 1718/1718, `make wpt` 9567/9824.

## 4. What is still open

The four blocked consumers, which this finding's fix unblocks but does not
implement: `fetch()` bodies, `WebSocket` binary frames, `ImageData`, and
`TextDecoder`/`TextEncoder`. Also open in the same area: `DataView`,
`SharedArrayBuffer` and detaching by `transfer()`, and the half of
`%TypedArray%.prototype` that is a higher-order function.
