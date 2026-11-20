# Finding 116 — A decoder is not an encoder, and a table is both

## The finding

Phase 47 shipped the WHATWG Encoding Standard's decoders and stopped there.
`AUDIT.md` recorded it as "`TextDecoder` and `TextEncoder` are not exposed to
script, and there is no encoder", and the two halves were treated as one
problem: as if the missing script globals were the missing feature.

They are not one problem, and separating them changes what is possible now:

- **The encoder half needed nothing that does not exist.** It is a walk over
  tables this tree already has, generated from `whatwg/encoding @ be17ac5c`.
- **The script half needs typed arrays.** `TextEncoder.encode()` returns a
  `Uint8Array`; `TextDecoder.decode()` accepts an `ArrayBufferView`. Without
  them, any `TextDecoder` global would have to return something no browser
  returns, and the WPT `encoding/` `encodeInto` subtests would fail on the type
  rather than the value.

The first is now done (Phase 110). The second is blocked on PR #132.

## The mistake this finding records

The reason the two were conflated is worth keeping, because it will happen
again. "There is no encoder" reads like a mountain: 40 encodings, 228 labels,
four-byte forms, a stateful one. The mountain is **one function**: the pointer
that a decoder reads a table at is the pointer the encoder finds by scanning
the same table. `index_pointer()` is nine lines. Of the 26 encoders, 27 of the
40 `EncodingId`s are one shared scan; twelve are a coordinate change from a
pointer; one is a state machine.

The tables were never the work. **They are the algorithm.**

## What would have gone wrong without the second oracle

The round trip is not enough on its own. An encoder and decoder that are wrong
in the same way round trip perfectly: `decode(encode(x)) == x` is satisfied by
any consistent pair, including a pair that agrees with neither the standard nor
any browser.

So the checks were layered:

1. **Round trip** over all of Unicode — catches an encoder that maps a code
   point to bytes its decoder reads as something else.
2. **The standard's own bytes**, hand-copied from §10–§14 into the suite —
   catches a pair that is consistently wrong.
3. **An independent implementation** (Python's codecs) — catches a code point
   the tree's tables get wrong.

Layer 3 disagreed 9,378 times and was wrong every time it did — but only after
looking. That is what an oracle is for.

Layer 2 caught the one real surprise: WHATWG's windows-1252 is not
Python's cp1252. The standard maps 0x81 → U+0081 and 0x9D → U+009D; Python
leaves holes there. Both halves of this engine follow the standard's table, so
the encoder was right and the *test written from the Python behaviour* was
wrong. It failed, which is the point.

## The asymmetry table, because it is the specification

Nine code points are encoded but do not round trip. That is not a bug in this
implementation; it is §10–§12 of the standard, and the reasons are deployed
content:

| Code point | Encoding | Bytes | Decodes back as |
| --- | --- | --- | --- |
| U+E78D…U+E796, U+E81E…U+E864 (18) | gb18030, gbk | fixed pairs (0xA6 0xD9 … 0xFE 0xA0) | the 2000-era code point |
| U+00A5 | Shift_JIS, EUC-JP | 0x5C | U+005C |
| U+203E | Shift_JIS, EUC-JP | 0x7E | U+007E |
| U+2212 | Shift_JIS, EUC-JP, ISO-2022-JP | as U+FF0D | U+FF0D |
| U+FF61…U+FF9F (63) | ISO-2022-JP | fullwidth katakana | the fullwidth form |

U+E5E5 is the other direction: gb18030 **refuses** it, because index gb18030
maps 0xA3 0xA0 to U+3000 and the round trip cannot be preserved.

Anyone comparing this encoder to another implementation should start here.

## What is still open

The globals, on typed arrays (PR #132). `src/net/TextEncoding.cpp` is finished
and needs no change to serve them.
