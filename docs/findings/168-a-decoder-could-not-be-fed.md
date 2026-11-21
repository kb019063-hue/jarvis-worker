# Finding 168 — a decoder could not be fed

**Resolved by Phase 163** (`docs/phases/163-textdecoder-in-pieces.md`).

## 1. What was found

`TextDecoder` was installed in Phase 127 without the `{stream: true}` option,
and an option that is accepted and ignored is worse than one that is absent, so
the row said it was refused by being absent. The cost of that was visible in
one line:

```js
const d = new TextDecoder();
d.decode(new Uint8Array([0xE2]), {stream: true})   // ""      in a browser
d.decode(new Uint8Array([0x82, 0xAC]))             // "€"
// here: "\uFFFD" then "\uFFFD\uFFFD" — one replacement character per chunk boundary
```

Anything reading a decoder from a socket, a file or a `fetch()` body got a
replacement character wherever a character straddled two chunks.

## 2. What was done

The decoder keeps the bytes a chunk ends inside of, whether the BOM has been
decided, and — for ISO-2022-JP, whose mode is chosen by an escape sequence that
can arrive in an earlier chunk — the mode. `decode_iso2022_jp` grew
`mode_in`/`mode_out` and is exported as `decode_stream_chunk`; every other
encoding ignores them.

`pending_tail()` decides what to hold by scanning the chunk **forward**, one
whole character at a time. The first version scanned backwards over the
trailing bytes and was wrong for EUC-KR and Big5, where a trailing byte can be
a lead — found by the sweep, not by reading.

## 3. Evidence

`make test` **321/321**, new suite `textdecoder_reaches_script_in_pieces`,
whose second half requires that a stream cut **anywhere** answer what one call
answers: 164 splits, 0 differ. Through the CLI, on seeded random byte strings
over all thirteen encodings: **800 splits and 300 four-chunk streams, 0
differ**. The BOM, error-mode and single-character cases were recorded from
Node.js v22.22.3 and all match. `make wpt` 9567/9824 (same 257 listed),
`make js-conformance` 1144/1158.

## 4. What is still open

- `TextEncoder`'s stream option: it would change nothing, since `encode()`
  answers UTF-8 only.
- The `<meta charset>` prescan still reads one 1,024-byte pass.
- Node's ICU tables differ from the standard for some Big5 and gb18030
  pointers; the whole-decode answers are Phase 47's and were not changed here.
