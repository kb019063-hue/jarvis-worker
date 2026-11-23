# Phase 194 — the byte order mark belongs to a UTF decoder

Task row: **`TextDecoder` and `TextEncoder` reach script, and the decoder reads
in pieces since Phase 163** (`AGENTS.md`), the BOM half of it. The row said the
decoder "strips a BOM"; what it did was strip a byte *prefix* before decoding,
for every encoding, whichever encoding was asked for. Finding 198.

## 1. What was wrong

The WHATWG Encoding Standard's rule is one sentence, in §7.1's *serialize I/O
queue*: the first scalar value a decoder produces is skipped when it is
U+FEFF, and only then. Three things follow that a byte-prefix check gets
wrong, and the engine got all three:

- **`FE FF` is not a byte order mark to a UTF-16LE decoder.** It decodes to
  U+FFFE, a character. This engine deleted the two bytes before decoding, so
  `new TextDecoder('utf-16le').decode([0xFE, 0xFF, 0x00, 0x41])` answered `A`
  where Node answers `\uFFFE鄀`.
- **Only UTF-8 and UTF-16 have one at all.** `windows-1252`'s `EF BB BF` is
  `ï»¿`, and `windows-1251`'s is `п»ї`; the sniff ate them.
- **BOM seen is a decoder's state, not a call's.** A `decode()` without
  `{stream: true}` starts a *new* decoder (§7.2 step 1), so its BOM is sniffed
  again — the engine kept "decided" across calls and answered `\uFEFFB` for the
  second `decode([0xEF, 0xBB, 0xBF, 0x42])`. Two other pieces of state came
  with it: a fatal error must reset the decoder (§7.2 step 5), so the next call
  does not continue the stream that failed; and the ISO-2022-JP escape mode
  leaked from one one-shot call into the next.

```
new TextDecoder('utf-16le').decode([0xFE, 0xFF, 0x00, 0x41])
    before  "A"          after  "\uFFFE鄀"      Node: "\uFFFE鄀"
new TextDecoder('windows-1252').decode([0xEF, 0xBB, 0xBF, 0x41])
    before  "A"          after  "ï»¿A"          Node: "ï»¿A"
```

## 2. What changed

`src/js/TextCodec.cpp` only:

- **`stream_bom()`, the byte-prefix sniff, is gone.** `strip_byte_order_mark()`
  takes the decoded string instead, drops a leading U+FEFF when the decoder
  produces one, and only for the encodings that can: `has_byte_order_mark()`
  is `Utf8`, `Utf16BE`, `Utf16LE`. It runs after `decode_stream_chunk`, which is
  where the standard puts it.
- **`bom_decided` became `bom_seen`** — the standard's "BOM seen", true once a
  scalar value has been looked at, which is not the same thing as a stream
  having started.
- **`do_not_flush` is the standard's flag**, and `restart_decoder()` is §7.2's
  first step: a `decode()` that does not continue a stream clears the held-back
  bytes, the BOM-seen flag and the ISO-2022-JP mode. A fatal error clears them
  too before it throws.
- **A split BOM needs no rule now.** `pending_tail` already holds back an
  incomplete character, so `[0xEF]`, `[0xBB]`, `[0xBF, 0x41]` is a held-back
  two-byte prefix and then the whole character — the special case that waited
  for a possible BOM is deleted.

What did not change: `ignoreBOM` still makes the strip impossible, a BOM in the
middle of a call is still data, and every streaming assertion of Phase 163 —
including the one that cuts every case at every byte and compares with the
one-shot answer — passes untouched.

## 3. How it was verified

The expectations are Node v22.22.3's, taken before the change was written:

```
new TextDecoder('utf-16le').decode([FF FE 41 00])   -> A          (BOM: FF FE)
new TextDecoder('utf-16le').decode([FE FF 00 41])   -> \uFFFE鄀    (data: FE FF)
new TextDecoder('utf-16be').decode([FF FE 41 00])   -> \uFFFE䄀
new TextDecoder().decode([FF FE 41 00])             -> \uFFFD\uFFFDA\0
new TextDecoder('windows-1252').decode([EF BB BF 41]) -> ï»¿A
new TextDecoder('windows-1251').decode([EF BB BF 41]) -> п»їA
```

```bash
make test      # All 346 engine test suites passed successfully!
```

Sixteen assertions were added to `textdecoder_reaches_script_in_pieces`. With
the fix reverted and the suite rebuilt, eight of them fail and name themselves:

```
UTF-16LE reads FE FF as U+FFFE, not as a BOM: got "4100", expected "fffe,4100"
UTF-16BE reads FF FE as U+FFFE, not as a BOM: got "4100", expected "fffe,4100"
UTF-8 keeps FE FF as two replacements:      got "41,0", expected "fffd,fffd,41,0"
UTF-16LE does not read the UTF-8 BOM:       got "fffd", expected "bbef,41bf"
windows-1252 has no BOM at all:             got "41", expected "ef,bb,bf,41"
windows-1251 has no BOM at all either:      got "41", expected "43f,bb,457,41"
a new stream sniffs its own BOM:            got "A|﻿B", expected "A|B"
```

and the ISO-2022-JP one fails on the way to the assertion, because the mode
left behind makes the second call throw.

Nothing else moved:

| Check | Result |
| :--- | :--- |
| `make js-conformance` | 1146/1158 (99.0%), 0 crashes |
| `make conformance` | 1973/1973 |
| `make dom-conformance` | 678/678 |
| `make wpt` | 9567/9824, the same 257 listed |
| `make url-conformance` | 1188/1188 |
| `make xml-conformance` | 1718/1718 |
| `make diff-trees` / `make diff-xml` | 1500/1500 trees equal, no crash |
| `make deep` | 1332 runs, 0 failed |
| `make security-fetch` / `-worker` / `-https` | 3/3, 17/17, 20/20 |
| `make asan` | `build-asan/litebird` built; the changed codec run under ASan+UBSan reports nothing |

`make asan`'s unit step is killed for memory on this machine (3.9 GB; the
suite's own comments say it needs more and CI's runners have 16 GB), so the
sanitizer evidence here is the built binary and the decoder probes run through
it, not the whole suite.

## 4. What is still open

- **`TextDecoderStream` and `TextEncoderStream` do not exist**; nothing here
  needs them, but a page that reaches for one gets `undefined`.
- **The `<meta charset>` prescan** is still one pass over the first 1,024
  bytes.
- **`x-user-defined` and `replacement`**: `replacement` is reachable only
  through the byte prefix the prescan strips, and `TextDecoder` refuses it as a
  label, which is what the standard asks.
- **No suite asserts either global inside a worker.**
