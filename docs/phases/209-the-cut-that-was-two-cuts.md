# Phase 209 — the cut that was two cuts

Task: the row that began "`TextDecoder` and `TextEncoder` reach script, and the
decoder reads in pieces since Phase 163". Finding 213
(`docs/findings/213-the-mode-was-scanned-and-thrown-away.md`).

## 1. The row supplied its own oracle, so it was run

The row's advice column ends with the test to use:

> The stream's oracle is the engine's own whole decode — every cut of a byte
> string must answer what one call answers.

That is a measurable claim, and the suite already tested it — with **one cut**.
One cut is not every cut: a byte string of *n* bytes has 2^(*n*−1) partitions,
and the defect below is invisible to all *n* single ones. So the oracle was run
as written, over every partition, of 19 byte strings in ten encodings.

`596` of `2450` differed from the one-shot answer. Node v22.22.3 on the same
input: `0`.

## 2. What the failing partitions had in common

Every one was `iso-2022-jp`, and every one had a cut **after** the decoder had
been left in the double-byte mode by an earlier chunk, with a second cut
between a double-byte pair's lead and trail bytes:

```
1B 24 42   24 22   24 24   1B 28 42   41
└ escape ┘ └ pair ┘ └ pair ┘ └ escape ┘ └ A
     cut 12: [1B 24 42 24] [22 24 24 1B 28 42 41]
                             ^ trail byte with its lead left behind
```

One-shot: `あいA`. Cut the way above: `あ��A`.

## 3. Two halves of one state, disagreeing

`decode_iso2022_jp` correctly carries the escape-chosen mode across calls in
`state->iso_mode`. `pending_tail()` — which decides how many bytes to hold back
because a chunk ended inside a character — did not consult it. Its own comment
claimed the mode was handled "in the same pass, so the mode is a local", and
that local was `Ascii` on every call.

So the scan read the held-back trail byte in ASCII, judged it a whole
character, held nothing, and passed it to a decoder that was in the
double-byte mode — where a trailing byte is a valid lead byte. The pair was
then assembled from the wrong two bytes and neither was in the standard's
table, which is where the two `U+FFFD` came from.

The comment was the tell. It asserted the state was local precisely because the
author had thought about it, and the conclusion was backwards: the mode is the
one thing about this encoding that is *not* local.

## 4. The fix, and the fix that was not needed

`pending_tail` now takes the mode and starts its scan there; the caller passes
`state->iso_mode`, which already existed.

An earlier attempt extended the mode encoding with "inside a pair, lead byte
*N*" and "inside an escape sequence" states, with extra bytes threaded through
the signature. It was reverted unshrunk: once the one-line change measured
0/2450, the extra states had no work left to do. The bytes a resumed chunk
needs are already in front of it — the mode plus a scan that starts in the
right place is sufficient, and adding carried state would have created a second
place for the two halves to disagree, which is exactly the bug.

## 5. The suite now tests the oracle as written

`textdecoder_reaches_script_in_pieces` keeps its single-cut walk and gains the
partition walk over 16 byte strings. Run against the **unfixed** decoder it
fails and says where:

```
every partition of a byte string answers what one call answers (2410 partitions):
got 596, expected 0
first partition that differed: iso-2022-jp partition 12
```

That is the property a suite owes: it fails on the code it guards, not only on
the code after it.

## 6. Measured after

```
partition oracle (2450 partitions, 10 encodings, Node in parallel)   0 differ
engine suites                                                       349/349
DOM tree-construction                                               1973/1973
JS conformance                                                      1146/1158 (99.0%)
DOM conformance                                                     678/678
everyday probes                                                     90/90 and 68/68
```

## 7. What is not claimed

- **Node settles the streaming rule here, not the tables.** The row is explicit
  that ICU and the standard disagree for some Big5 and gb18030 pointers; this
  phase compares only whether a cut changes the answer.
- **`euc-jp` is in the engine's suite but not the cross-engine run** — Node has
  no `euc-jp` label.
- **No worker-context assertion**, and the `<meta charset>` prescan is
  untouched; both remain as the row records them.

## Files

- `src/js/TextCodec.cpp` — `pending_tail` takes the stream's mode; the caller
  passes it
- `tests/test_suite.cpp` — the partition oracle in `textdecoder_reaches_script_in_pieces`
- `AGENTS.md`, `AUDIT.md` — the row and the finding
