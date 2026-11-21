# Phase 127 — `TextDecoder` and `TextEncoder` reach script

Task row: **`TextDecoder` and `TextEncoder` are still not exposed to script**.
Finding 130.

The encodings were never the problem. Phase 47 built the WHATWG Encoding
Standard's 40 decoders, 228 labels, BOM sniffing and the `<meta charset>`
prescan; Phase 110 built the other direction, 26 encoders over the same
generated tables, and checked 2,310,001 encodings over the whole of Unicode.
What was missing was the *interface*: `typeof TextDecoder` was `"undefined"`,
so a page could not reach any of it.

That interface waited on typed arrays, because it is defined in terms of them:
`TextEncoder.encode()` returns a `Uint8Array`, `encodeInto()` writes into one,
and `TextDecoder.decode()` takes an `ArrayBufferView`. Phase 113 landed the
`TypedArray` constructors, and that is what unblocked this row.

## 1. What was wrong

| probe | Node.js v22.22.3 | this engine, before |
| :--- | :--- | :--- |
| `typeof TextDecoder` | `"function"` | `"undefined"` |
| `typeof TextEncoder` | `"function"` | `"undefined"` |
| `new TextDecoder().decode(new Uint8Array([104, 105]))` | `"hi"` | `TypeError` |
| `new TextEncoder().encode('hi')` | `Uint8Array [104, 105]` | `TypeError` |

The three things that had to be *got right*, rather than merely wired, are all
small and all the kind that a test catches and a build does not.

**The canonical label.** `canonical_encoding_name()` answers the table's own
spelling, `"UTF-8"`. Every browser answers `"utf-8"` for
`new TextDecoder().encoding`, and a page comparing against it — which is what
a page does with it — would find them unequal. So the getter lowercases the
canonical name. This is not cosmetic: the Encoding Standard's canonical names
are those of its tables, and `iso-8859-1` is canonically `windows-1252`, which
is why `new TextDecoder('latin1').encoding` is `"windows-1252"` and not the
label it was given.

**`read` and `written`.** `encodeInto()` answers how many UTF-16 code units of
the source it consumed and how many bytes it wrote. The first version counted
the source's *UTF-8 byte length* instead of its *code-unit length*, so `'hé'`
— two code units, three bytes — reported `read: 3` instead of `2`. `JsString`
has both numbers and they differ for any non-ASCII text: `size()` is the UTF-8
bytes a view holds, `length()` is what script calls `.length`. Using the wrong
one is invisible for ASCII and wrong for everything else.

**Lone surrogates.** A source string can hold an unpaired surrogate, which is
not a code point and cannot be encoded. §8 says it becomes U+FFFD, so
`encode('\ud800')` is the three bytes `239,191,189` — not an empty array and
not a throw. Walking UTF-8 bytes cannot see this at all; the walk has to be
over UTF-16 code units, folding a well-formed pair and replacing a lone one.

## 2. What changed

**`src/js/TextCodec.cpp`** (new) — the whole interface.

- **`TextDecoder(label, options)`**: the label goes through the standard's own
  label table, so it is case- and whitespace-insensitive (`"  UTF-8  "` is
  `utf-8`) and aliases resolve to canonical names. An unknown label is a
  `RangeError`, and so is `"replacement"`: `EncodingId::Replacement` has a
  decoder in the tables but §4 says a label never selects it, so it is
  reachable only by BOM sniff. `fatal` and `ignoreBOM` are read off the second
  argument with ToBoolean, defaulting to `false`.
- **`decode(input)`**: accepts an `ArrayBuffer` or any view — a view's own
  extent, honouring `byteOffset`/`byteLength`, so a `subarray` decodes its
  three bytes and not its parent's five. Anything else is a `TypeError`, which
  is the union type in the IDL rather than a sequence: `decode([104, 105])` is
  a `TypeError` in Node too. No argument means `""`. A BOM is consumed unless
  `ignoreBOM` is set, and `fatal` turns an invalid sequence into a `TypeError`
  instead of U+FFFD.
- **`TextEncoder()`**: takes no arguments and ignores any passed. `encode`
  walks code units, so the surrogate rule above holds, and answers a real
  `Uint8Array` (`typed_array_from_bytes`), not an array-like.
- **`encodeInto(source, destination)`**: the same walk, with a sink that stops
  at the first code point that does not fit. Stopping mid-code-point is the
  trap — half a character's bytes are never written, and the counts stay at
  the last complete one, which is why 2 bytes of room for `'hé'` is `read: 1,
  written: 1` and not `read: 2, written: 3`.
- Both constructors are **`[[Construct]]`-only**: `TextDecoder()` in sloppy
  code is a `TypeError`, not a codec with a default encoding.
- `encode`'s `input` is optional *with a default*, so `encode(undefined)` is
  the empty string while `encode(null)` is the four bytes of `"null"`. Those
  are different arms of the IDL and the difference is one `is_undefined()`.

**`src/js/TypedArray.cpp`** — three helpers, `typed_array_bytes` (copy out),
`typed_array_from_bytes` (a new view) and `typed_array_write` (write through a
view). The third is separate from the first because a copy out cannot write;
`encodeInto` was reporting byte counts for bytes it had not stored until it
got its own write path.

**`include/litebird/js/TypedArray.hpp`**, **`include/litebird/js/JsOps.hpp`**
(`JsIntrinsics` gains `text_decoder_proto` and `text_encoder_proto`),
**`src/js/JsInterpreter.cpp`** (one install call after `install_typed_array_builtins`),
**`Makefile`** (one object).

**Not done, and deliberately:** there is no `stream` option. The `<meta
charset>` prescan is a single pass over the first 1,024 bytes with no
streaming, so `{stream: true}` could not be honoured, and an option that is
accepted and ignored is worse than one that is absent. Multibyte *encoders*
(gb18030, Big5, Shift_JIS, EUC-JP, EUC-KR, ISO-2022-JP) also remain out of
scope, as they were for Phase 110: `TextEncoder` is UTF-8 by the standard, and
those tables have decoders only.

## 3. How it was verified

- **Every assertion was run through Node.js v22.22.3 first.** the suite body was run as a
  standalone script under Node and under the engine, and prints `ALL PASS` in
  both; it is the `js.eval` block in the suite, verbatim. Two expectations were wrong when written and Node corrected them:
  `decode` of a plain array is a `TypeError` (a union type, not a sequence),
  and euc-kr `가` is the bytes `176,161`, not `190,200` — the latter decodes to
  `안`, which is what the engine said before the fix.
- `make test` → **298 / 298** (297 on `main`), the new suite being
  `text_codec_a_decoder_and_an_encoder_in_script`, 74 assertions.
- `make js-conformance` → **1143 / 1158 (98.7%)**, from 1142 on `main`.
- `make dom-conformance` → **678 / 678**; `make wpt` → **9567 / 9824, 257
  listed fails, 0 unlisted**, unchanged; the cross-origin suite → **114
  checks, 0 failures**.
- The 21-assertion probe written first — BOM stripping, fatal `TypeError`,
  `RangeError` for a bad label and for `"replacement"`, the `read`/`written`
  pairs — was byte-identical to Node's output.

## 4. What is still open

- **No streaming.** `{stream: true}` is not accepted, and the prescan cannot
  decode across a chunk boundary. A page that reads a response in pieces still
  has to concatenate them first.
- **Multibyte encoders** — gb18030, Big5, Shift_JIS, EUC-JP, EUC-KR,
  ISO-2022-JP — have decoders and no encoders, so `TextEncoder` (UTF-8 only)
  is unaffected but a future `encode`-into-a-chosen-encoding cannot exist.
- **`TextDecoder` is not reached from a worker** in any suite here; the global
  is installed on the interpreter's global environment, which the worker
  shares, but nothing asserts it.
