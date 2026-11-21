# Phase 163 — `TextDecoder` reaches script in pieces

Task row: **`TextDecoder` and `TextEncoder` reach script, but not in pieces**
(`AGENTS.md`), the `{stream: true}` half. Phase 127 installed the two globals
and left the stream out on purpose; this phase puts it in. `src/js/TextCodec.cpp`
and `src/net/TextEncoding.cpp` were named by no open pull request. Finding 168.

## 1. What was wrong

The WHATWG Encoding Standard's `TextDecoder.decode(input, {stream})` is the
same decoder either way; `stream: true` only says what to do with the bytes a
chunk **ends inside of** (§11.1.1, "serialize stream"):

```js
const d = new TextDecoder();
d.decode(new Uint8Array([0xE2]), {stream: true})   // ""       — half of "€"
d.decode(new Uint8Array([0x82, 0xAC]))             // "€"
```

Here the option was read by nobody, so the first call answered `"\uFFFD"` and
the second `"\uFFFD\uFFFD"`; a page that fed a decoder from a stream of chunks
got one replacement character per chunk boundary. `TextEncoder`'s
`encodeInto()` already reads in pieces, so the decoder was the only half
missing.

## 2. What changed

**`src/js/TextCodec.cpp`** — `decode()` reads `stream` (ToBoolean, an options
argument that is not an object meaning false), and keeps in the decoder:

- **the bytes a chunk ended inside of** (`DecoderState::stream_bytes`), which
  are put back in front of the next chunk;
- **whether the BOM has been decided** (§11.1.1 "BOM seen"), so a BOM is
  sniffed once per stream and a *second* one is data — and so a BOM split
  across chunks (`EF` then `BB BF 41`) is waited for rather than decoded;
- **the ISO-2022-JP mode**, the one decoder state that genuinely crosses a
  chunk boundary: `1B 24 42` in one chunk says the next chunk's bytes are
  double-byte, and §4's decoder would otherwise start in ASCII.

**`src/net/TextEncoding.cpp`** — `decode_iso2022_jp` grew `mode_in`/`mode_out`
(defaulting to zero, so `decode_with_encoding` is unchanged), exported as
`decode_stream_chunk`; every other encoding ignores both and is decoded exactly
as before.

### Which bytes are held back

`pending_tail()` reads the chunk **forward**, taking one whole character at a
time exactly as the decoder does, and stops at the first character the chunk
does not finish. A backwards walk over trailing bytes does not work, and the
first version of this phase wrote one and was caught by the ICU-backed
comparison: `0xA1` is a lead byte in EUC-JP and a *trail* byte in EUC-KR, and
`0x88`'s meaning in Big5 depends on nothing but its position. Forward, the
boundary is never ambiguous. The rules are, per encoding:

| Encoding | Held back |
| :--- | :--- |
| UTF-8 | 1–3 bytes: a lead byte and its continuations |
| UTF-16LE/BE | 1 byte of a code unit, or a whole high surrogate and no low one (2) |
| gb18030 | 1–3 bytes: a lead, `lead 30-39`, or `lead digit lead` |
| Big5, Shift_JIS, EUC-KR | 1 byte: a lead byte |
| EUC-JP | 1 byte, or `8F` and a lead byte (2) |
| ISO-2022-JP | an escape sequence (1–2), or the first byte of a double-byte pair |
| single-byte, `x-user-defined` | nothing: every byte is a character |

## 3. How it was verified

The suite is `textdecoder_reaches_script_in_pieces` in `tests/test_suite.cpp`.
Its second half is the check that a stream cannot be a second, worse decoder:
**every** cut of twelve byte strings in twelve encodings, in both error modes,
must produce exactly what one call produces — 164 splits, 0 differing:

```bash
make test     # All 320 engine test suites passed successfully!
```

The same invariant, at scale, through the CLI:

```bash
./build/litebird --run-script /tmp/fuzz_stream.js /tmp/probe.html
#   800 splits checked, 0 differ
./build/litebird --run-script /tmp/fuzz_multi.js /tmp/probe.html
#   300 four-chunk streams checked, 0 differ
```

(each is a seeded generator over all thirteen encodings, 400 and 300 random
byte strings, cut at every position and at three random positions.)

The single-character cases, the BOM cases and the error modes were read off
**Node.js v22.22.3's** `TextDecoder` before they were written down, and the
engine agrees with it on all of them — including the ones that look wrong
until one reads §11.1.1: a BOM in a chunk after the first is kept as
`U+FEFF`, a fatal stream that ends inside a character throws on the flush, and
a one-shot `decode()` flushes `U+FFFD` where a stream holds.

**Where Node is not the oracle.** Node's ICU-backed decoders differ from the
standard for some legacy ranges — Big5 `88 62` is `U+F325` there and
`U+00CA U+0304` here, which is what the vendored WHATWG index says, and Node's
`gb18030` accepts pointers the standard's range table refuses. Those are the
Phase 47 decoder's answers, unchanged by this phase, and a stream must match a
*whole* decode of the same engine rather than another engine's table. That is
why the sweep above compares here-to-here.

Nothing else moved: `make wpt` 9567/9824 with the same 257 listed failures,
`make js-conformance` 1144/1158, DOM trees 1973/1973.

## 4. What is still open

- **`TextEncoder` has no `{stream: true}`** either. Its encoder is stateless
  for every encoding it accepts (`encode()` only ever answers UTF-8, where a
  lone surrogate is always `U+FFFD`), so the option would change nothing —
  worth saying in the write-up rather than writing.
- **The `<meta charset>` prescan** is still one pass over the first 1,024
  bytes. It could now use this decoder in a stream, but the prescan's rules
  (WHATWG HTML §13.2.3.2) are not the decoder's.
- **A stream that is abandoned** keeps its held-back bytes until the object
  dies; there is no `decode()` with `stream: false` reset of the BOM state
  beyond the sniff, which is what the standard says.
