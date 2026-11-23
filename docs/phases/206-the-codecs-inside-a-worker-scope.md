# Phase 206 — the codecs inside a worker scope

Task row: **`TextDecoder` and `TextEncoder` reach script, and the decoder reads
in pieces since Phase 163** (`AGENTS.md`). Its last open item was one sentence:
*no suite asserts either global inside a worker*. This is that suite, and what
writing it turned up — the two globals are in a worker's scope already, and the
row's sentence was the only thing that had never been checked. Finding 210.

## 1. What was wrong

Nothing in the engine changed, and that is the finding: the row had listed the
worker case as *open* for four phases, and the honest state was *unasserted*.
`install_text_codec_builtins()` is called from `JsInterpreter`'s setup, and a
worker builds its own interpreter for its own realm, so a worker's global got
the two constructors — but nothing in the repository said so, and one
`if (m_document)` in front of that call would have taken them out of every
worker with all 348 suites still passing. A row that says "not asserted" and a
row that says "not done" are different rows, and this one had been read as the
second.

What the suite does that a window test cannot: it runs the whole interface
*inside the worker*, answers through `postMessage`, and so covers the realm
hand-off as well as the globals — the answer arrives as a structured clone of
an array of strings, not as a value the page could have made itself.

## 2. What changed

- **`tests/test_suite.cpp`**: a new suite,
  `text_codecs_are_installed_in_a_worker_scope`, and a `js_quote_for_suite()`
  helper that turns a C++ string into a JS string literal (the worker's source
  has to survive a literal, then `encodeURIComponent`, then the `data:` URL).
- The suite first asks the window's global, so a failure says which scope is
  missing them rather than only that one is, and then runs nineteen probes in
  the worker: `typeof`, `Object.prototype.toString`, `encoding` for a default
  and for `latin1`, `fatal`/`ignoreBOM`, `TextDecoder()` without `new`, an
  unknown label and the `replacement` label, `decode()` of a plain array, a
  round trip, `encode()`'s `Uint8Array`, `encodeInto`'s counts, a lone
  surrogate, a stream split inside `€`, a fatal error's reset, the BOM in both
  of the ways Phase 194 made them differ, `ignoreBOM`, and `instanceof`.

No engine file is touched: `src/js/JsInterpreter.cpp` is byte-for-byte what
`main` has.

## 3. How it was verified

Every expected line is Node v22.22.3's, and the suite compares the whole
joined answer, so a probe that stops running is a failure too:

```bash
make test      # All 349 engine test suites passed successfully!
```

The suite is not vacuous. Putting `if (m_document)` in front of
`install_text_codec_builtins()` and rebuilding makes it fail and print both
sides; the first line is enough to see why:

```
typeof => undefined,undefined            (expected function,function)
toStringTag => THREW ReferenceError      (expected [object TextDecoder] [object TextEncoder])
…
```

Nineteen lines differ, and the run stops at its assertion. Restoring the call
makes it pass again.

## 4. What is still open

- **`TextDecoderStream` and `TextEncoderStream`** do not exist in either
  scope. They are not on that row and are not claimed here.
- **The `<meta charset>` prescan** is still one pass over the first 1,024
  bytes.
- **No suite runs the codecs in a module worker** or in a nested worker; the
  suite above is one classic worker. The install is per interpreter and the
  same one runs all three, but that is an argument, not an assertion, and it
  is written down as open rather than counted as done.
