# Finding 249 — a byte sweep read three decoders as one

**Severity:** low in the code — nothing was wrong with the engine. High in the
cost of the mistake it invited: a fix was drafted, pushed toward `main`, and had
to be taken back. This finding is about the near-miss, not a defect.

**Found by:** doing what the row asked — sweeping the legacy decoders byte by
byte and comparing them with Node's `TextDecoder` — and then checking the one
answer that sweep disagreed with against the standard's own source instead of
against a browser.

## What was believed, and why it was believed

The row said the Big5 decoder "maps two single bytes the WHATWG index defines to
U+FFFD", and named them 0x80 and 0xFF. Two facts made that look right:

- a byte sweep over `0x80..0xFF` across twenty labels showed Big5 answering
  U+FFFD for exactly those two bytes;
- `new TextDecoder('big5')` in Node 22 answers `"\u0080"` for 0x80 and
  U+FFFD-shaped output for 0xFF, so the engine looked like the odd one out.

The fix drafted from that was a `b == 0x80u` branch giving U+0080. It compiled,
it agreed with Node on ten probe cases, and it left `make wpt` unchanged at the
9567 floor — which read as corroboration.

It was wrong. The WPT floor not moving was not corroboration: it was silence.

## What the standard says

`whatwg/encoding`'s `encoding.bs`, Big5 decoder, steps 4 to 6, verbatim:

```
If byte is an ASCII byte, then return a code point whose value is byte.
If byte is in the range 0x81 to 0xFE, inclusive, then set Big5 leading to byte
and return continue.
Return error.
```

**There is no fourth case.** 0x80 is not an ASCII byte and is not in
`0x81..0xFE`, so it falls through to `error`, which the decoder interface turns
into U+FFFD. That is what this engine does, and has always done.

The clause the row assumed exists is real — in the decoder *next door*.
Shift_JIS step 4:

```
If byte is an ASCII byte or 0x80, then return a code point whose value is byte.
```

and gb18030 has its own:

```
If byte is 0x80, then return code point U+20AC (€).
```

**This engine already implements both of those correctly.** So a sweep that
compares three neighbouring decoders' answers on the same byte finds a
difference, and the difference is in the standard, not in the code. That is how
a table of byte offsets became a bug report.

## Why neither oracle caught it

**The corpus cannot see it.** `tests/wpt/encoding/legacy-mb-tchinese/big5/big5_errors.html`
contains the bytes `ab`, `ab b5 ab`, `ab ab ab`, `ff`, `ab b5 ff`, `ab 31`,
`ab ff`, `c8 f2` — and **no 0x80 at all** (`b'\x80' in file` is `False`). So
`make wpt` passes identically with and without the wrong branch. A green corpus
proved only that the corpus is silent on the question.

**ICU is the deviant.** Against the standard, Node 22 is wrong three ways on this
decoder: `fatal:false` for a lone 0x80 (should be U+FFFD), `''` for 0xFF, and no
throw for either under `fatal:true`. The same is true of `koi8-u` (Node gives
U+255D/U+256C where `index-koi8-u.txt` pointers 46 and 62 give U+045E/U+040E) and
`windows-1253` (Node accepts 0xAA and 0xD2; `index-windows-1253.txt` has no
pointers 42 or 82, so both are errors). In every one of those the engine is
right. **ICU is not an oracle for the legacy encodings**, and a sweep that uses
it as one manufactures defects in whichever decoder is correct.

## The third witness: the encoder

The decoder and encoder are two halves of one standard, and they agree here. Big5
encoder step 2 returns a byte only for an *ASCII code point*, and U+0080 has no
entry in `index-big5`. So `encode_with_encoding(Big5, "\u0080")` refuses the
character — and a decoder that produced it would hand the page a character the
engine's own encoder cannot write.

The neighbours make the same point from the other side. Shift_JIS encodes U+0080
back to the byte `0x80`; gb18030 cannot use `0x80` for it at all, because that
byte is the euro sign there, and writes it as the four-byte form
`81 30 81 30` instead. Three encodings, three different answers for one code
point — none of them interchangeable.

## What was done

**No engine change.** The branch that would have carried one was reverted to
pristine; `src/net/TextEncoding.cpp` is untouched by this phase.

What was added is the guard the near-miss earned: `tests/suites/245-big5-single-bytes.cpp`
pins, to the standard's own steps,

- Big5 `0x80` and `0xFF` both U+FFFD, and all eight corpus error cases;
- Shift_JIS `0x80` → U+0080 and gb18030 `0x80` → U+20AC, **in the same
  assertion block as Big5's error**, so a future "unification" of the three byte
  dispatchers fails a test rather than merely being wrong;
- the four two-code-point pointers (1133, 1135, 1164, 1166) the index cannot
  express;
- `fatal` throwing for the two error bytes and not for a character;
- the encoder refusing U+0080 while Shift_JIS encodes it back to `0x80` and
  gb18030 carries it as `81 30 81 30`;
- through `new TextDecoder('big5')`: the same answers, `fatal:true` throwing
  `TypeError`, a lead byte split across two `{stream:true}` chunks decoding as
  one character, and all five labels resolving to `big5`.

## The tell

The suite was shown to fail in **three** separate wrong directions, not one —
which is what a guard for a mistake of this shape has to do:

| what was changed in the engine | the assertion that failed |
| :--- | :--- |
| `0x80` → U+0080 (the fix this task asked for) | `big5("80") == ff` |
| `0xFF` → U+F8F8 (ICU's answer) | `big5("ff") == ff` |
| `0x80` passed through as a code point (a "unified" dispatcher) | `big5("80") == ff` |

A suite that only checked the first would have been satisfied by the second
being reverted, and vice versa.

## What this changes

- Nothing in `src/`. The Big5 decoder, and its two neighbours, were already
  correct, and this phase's product is the record of *why* they are.
- `tests/suites/245-big5-single-bytes.cpp` is the guard.

## What is still open

- **The row that started this should be struck or re-worded.** "The Big5 decoder
  maps two single bytes the WHATWG index defines to U+FFFD" is not a defect
  description; both bytes are errors in the standard and U+FFFD is the right
  answer. A row that says this again will cost another agent the same day.
- **The byte-sweep method needs an oracle that is not ICU.** `koi8-u` and
  `windows-1253` were checked here against the WHATWG index files and the engine
  won both; a sweep that trusts Node would have reported four false defects in
  three encodings. Any future sweep should compare against the index tables this
  repository already vendors (`kSingleByte`) rather than against a browser.
- Not investigated here, and not this row's: whether any *other* decoder differs
  from its index in a way both ICU and the vendored corpus are silent about. That
  is a real question the method above would answer; it is not a finding yet.
