# Finding 198 — the BOM was a byte prefix

**Resolved by Phase 194** (`docs/phases/194-the-byte-order-mark-belongs-to-a-utf-decoder.md`).

## 1. What was found

`TextDecoder.decode()` looked for `EF BB BF`, `FE FF` or `FF FE` at the front
of the bytes and deleted what it found, before any decoder ran. The standard
does not: §7.1 skips the first *scalar value* when it is U+FEFF, and only a
UTF-8 or UTF-16 decoder can produce one.

So `new TextDecoder('utf-16le').decode([0xFE, 0xFF, 0x00, 0x41])` answered `A`
where Node answers `\uFFFE鄀` — the two bytes are U+FFFE, a character, and the
engine had eaten them. `windows-1252`'s and `windows-1251`'s `EF BB BF` are
`ï»¿` and `п»ї`, and the engine ate those too.

The state went with it. "BOM decided" was kept across calls, so a second
`decode()` — which §7.2 says starts a *new* decoder — answered `\uFEFFB` where
the standard answers `B`; a fatal error did not reset the decoder, so the call
after one threw when it should have read `A`; and ISO-2022-JP's escape mode
leaked out of a finished one-shot call into the next.

## 2. What was done

The sniff is deleted. `strip_byte_order_mark()` looks at the decoded string and
drops a leading U+FEFF — for `Utf8`, `Utf16BE` and `Utf16LE` only, and only
before anything else has been produced. `do_not_flush` and `restart_decoder()`
are §7.2's flag and first step, so a call that is not a stream continuation
clears the held-back bytes, the BOM-seen flag and the escape mode; a fatal
error clears them before it throws. The split-BOM special case is gone because
`pending_tail` already holds back an incomplete character.

## 3. Evidence

`make test` **344/344**. Sixteen assertions in
`textdecoder_reaches_script_in_pieces`, with the answers taken from Node
v22.22.3: `FE FF` is U+FFFE under UTF-16LE and `FF FE` is U+FFFE under
UTF-16BE, `FF FE 41 00` is two replacements and `A\0` under UTF-8, the
`windows-1252` and `windows-1251` spellings survive, a second BOM is data, a
new stream sniffs its own, and a fatal error is followed by a working decoder.

With the fix reverted and the suite rebuilt, eight of the sixteen fail and
print what they got.

## 4. What is still open

`TextDecoderStream`/`TextEncoderStream`, the `<meta charset>` prescan's single
1,024-byte pass, and a suite for either global inside a worker.
