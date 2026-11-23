# Finding 213 — the mode was scanned, and then thrown away

**Severity:** medium — a `TextDecoder` given `{stream: true}` decoded Japanese
text to replacement characters where one call decoded it correctly. The defect
was invisible to a one-shot decode and to every *single* cut of a byte string;
it needed two cuts, in a particular order.

**Found by:** running the row's own acceptance test. The `TextDecoder` row of
`AGENTS.md` says the stream's oracle is "every cut of a byte string must answer
what one call answers". That is a claim about a decoder, so it was run — over
every *partition*, not one cut — and it failed 596 times out of 2450.

## What was wrong

ISO-2022-JP is the one encoding here whose decoder carries state between
characters: an escape sequence (`1B 24 42`) chooses a mode, and the bytes after
it are read in that mode until another escape changes it.

Two pieces of code had to agree about that state, and did not:

- `decode_iso2022_jp` keeps the mode across calls, in `state->iso_mode`
  (`src/js/TextCodec.cpp`), correctly.
- `pending_tail()` decides **how many bytes to hold back** from the end of a
  chunk because the chunk ended inside a character. Its comment said:

  > ISO-2022-JP is the only one that carries a mode between characters; it is
  > scanned in the same pass, so the mode is a local.

  and it was a local, initialised to `Ascii` on every call — as was the scan
  that consumed it.

Both of those are wrong, and the second is the destructive one. A `{stream:
true}` decode is passed the held-back bytes *prefixed* to the new chunk. When
those bytes are the rest of a double-byte pair, the scan — starting in ASCII —
sees a lone trailing byte, reads it as a whole character (despite the comment
saying a trailing byte cannot be judged alone), decides nothing is held, and
hands the orphaned trail byte to the decoder. In the double-byte mode that
byte is read as a *lead* byte, and the pair it starts is decoded from the wrong
place. Two replacement characters come out where the text had one character.

The single-cut oracle the suite had could not see it: one cut in the middle of
a pair happens on a decoder that is still in a fresh state, and the pair is
held whole. It takes a first cut to leave the decoder in the double-byte mode
and a second to land between a pair's two bytes.

## Measured

19 byte strings — ten encodings, eight ISO-2022-JP shapes including a truncated
pair and a truncated escape — under **every one of their 2450 partitions**:

| | partitions differing from the one-shot answer |
|---|---|
| before | **596** |
| after | **0** |

Node v22.22.3 on the same 2450 partitions: **0**. The engine's own suite now
carries the same oracle over 16 strings (2410 partitions) and, run against the
unfixed decoder, reports `got 596, expected 0` and names the first partition
that differed — so the suite fails on the code it is meant to guard.

Targeted, on the original defect:

```
one-shot                       [3042]      Node [3042]
cut between lead and trail     [3042]      Node [3042]
byte by byte                   [3042]      Node [3042]   (was [fffd fffd])
truncated pair at end          [fffd]      Node [fffd]
truncated escape at end        [fffd]      Node [fffd]
```

## The fix

`pending_tail` takes the stream's mode and starts its scan there:

```cpp
std::size_t pending_tail(EncodingId encoding, std::string_view bytes, int iso_mode_in = 0) {
    ...
    enum class IsoMode { Ascii, Double } iso_mode =
        iso_mode_in == 3 ? IsoMode::Double : IsoMode::Ascii;
```

and the caller passes `state->iso_mode`, which already existed and was simply
not consulted here.

Nothing else was needed. The first attempt went further — extending the mode
encoding to carry "inside a pair" and "inside an escape sequence" — and was
reverted once this smaller change measured 0/2450: the state the second chunk
needs is in the bytes it is prefixed with, so the mode plus a correct scan is
sufficient, and carrying extra state would have been a second place for the two
halves to disagree.

## What is not claimed

- **Node is not a reference for every encoding.** The row is explicit that
  Node's ICU tables differ from the standard for some Big5 and gb18030
  pointers, so the comparison here is used for the *streaming* rule — a cut
  must not change the answer — and not to settle any table entry.
- **`euc-jp` is absent from the cross-engine comparison** because Node has no
  `euc-jp` label (ICU dropped it); it is still in the engine's own suite, where
  the oracle is internal.
- **No worker-context assertion.** The row notes that neither global is
  asserted inside a worker; that is unchanged and still open.
- **The `<meta charset>` prescan** is still one pass over the first 1,024
  bytes, untouched.
