# Phase 245 — the byte sweep read three decoders as one

This phase changed no engine code. It exists because a fix was written, and then
taken back, and the reason it had to be taken back is the thing worth keeping.

## The task

The row said the Big5 decoder "maps two single bytes the WHATWG index defines to
U+FFFD" — 0x80 and 0xFF — and asked for them to be decoded. The method was a
byte sweep: run every legacy decoder over `0x80..0xFF` and compare the answers.
Node's `TextDecoder` was used as the reference.

The sweep found exactly what the row promised. Big5 answered U+FFFD for 0x80 and
0xFF; Node answered `"\u0080"` and something else for 0xFF. A branch was written
giving Big5 a `0x80 -> U+0080` case. It compiled, it matched Node on ten probes,
and `make wpt` stayed at 9567 — the floor — which looked like agreement.

## Why it was wrong

The standard's source, not a browser, settled it. `encoding.bs`, Big5 decoder
steps 4–6:

```
If byte is an ASCII byte, then return a code point whose value is byte.
If byte is in the range 0x81 to 0xFE, inclusive, then set Big5 leading to byte
and return continue.
Return error.
```

No fourth case. 0x80 is neither ASCII nor a lead byte, so it is an error, and an
error is U+FFFD. The engine was right the whole time.

The clause that was assumed to exist does exist — in the decoders beside it.
Shift_JIS step 4 says "ASCII byte **or 0x80**"; gb18030 says "If byte is 0x80,
then return U+20AC (€)". **This engine already has both of those right.** A sweep
comparing the three on one byte therefore finds a difference that is in the
standard, and reads it as a bug in the one decoder that behaves differently.

Two more witnesses:

- **The corpus is silent.** `big5_errors.html` has eight byte sequences and no
  0x80 at all. `make wpt` cannot move, so its not moving proved nothing.
- **The encoder refuses the character.** Big5's encoder gives a byte only for an
  ASCII code point, and U+0080 has no Big5 index entry — so a decoder answering
  U+0080 would produce a character the engine's own encoder cannot write.

ICU is the outlier, not the engine: it answers `fatal:false` for 0x80, `''` for
0xFF, and never throws under `fatal:true`. Checked the same way, the engine also
beats ICU on `koi8-u` and `windows-1253` — the WHATWG index files give the
engine's answers and not Node's.

## What was done

`src/net/TextEncoding.cpp` is **untouched**: the drafted branch was reverted and
the file is byte-identical to `main`.

Added: `tests/suites/245-big5-single-bytes.cpp`, which pins to the standard

- Big5 0x80 and 0xFF both U+FFFD, plus all eight corpus error cases in order;
- Shift_JIS 0x80 → U+0080 and gb18030 0x80 → U+20AC **beside** Big5's error, in
  one block, so that merging the three byte dispatchers cannot pass unnoticed;
- the four pointers the Big5 index spells as two code points (1133, 1135, 1164,
  1166);
- `fatal` throwing for the two error bytes and not for a character;
- the encoder: Big5 refuses U+0080, Shift_JIS returns `0x80` for it, gb18030 uses
  `81 30 81 30`;
- through `new TextDecoder('big5')`: the same answers, `TypeError` under
  `fatal:true`, a lead byte split across `{stream:true}` chunks, five labels.

## How it was verified

```
$ make -j2 build/test_suite && ./build/test_suite
Running test: 0x80_is_an_error_in_big5_and_a_character_in_the_decoders_beside_it... PASSED
All 384 engine test suites passed successfully!
$ make wpt
Web Platform Tests: 9567/9824 subtests pass in 190 files; 257 fail as listed, 0 fail and are not listed
```

The suite was shown to fail in three wrong directions, one at a time, each
reverted before the next: the task's own fix (`0x80 -> U+0080`, fails at
`big5("80") == ff`), ICU's answer for 0xFF (`0xFF -> U+F8F8`, fails at
`big5("ff") == ff`), and a "unified" dispatcher that passes 0x80 through as a
code point (fails at the same first assertion). A guard that caught only the
first would have been satisfied by the wrong one happening to be absent.

## What is still open

- The table row should be struck. "Big5 maps two single bytes the index defines
  to U+FFFD" describes correct behaviour, and will cost the next agent the same
  day.
- A byte sweep should compare against the index tables this repository vendors
  (`kSingleByte`, `kIndexBig5`, …) rather than against ICU. `koi8-u` and
  `windows-1253` are two encodings where the sweep's oracle was wrong and the
  engine was right; four false defects would have been reported from them.
- Whether some *other* decoder disagrees with its own index in a way both ICU and
  the vendored corpus are silent about is a real question. It is not answered
  here and not claimed.
