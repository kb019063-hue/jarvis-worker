# Finding 287 — ICU is wrong about three tables, and the fourth check was the one that worked

**Severity:** no engine defect. The engine's decoders agree with their tables
on **46,315 byte positions**, and the two places a sweep had previously filed
as defects were the sweep's fault, as the row said. What this finding adds is
a third and fourth such place, found the same way, and a measurement of what
an index-table sweep can and cannot see.

**Found by:** writing the sweep the row asked for
(`tools/conformance/verify_encoding_tables.py`, Phase 283).

## What the row asked for

> a byte sweep should compare against the index tables already vendored here
> (`kSingleByte`, `kIndexBig5`, …) and not against ICU, which is also wrong on
> `koi8-u` … and `windows-1253`

That is now a tool. It reads `src/net/TextEncodingTables.cpp` — the tables the
decoders themselves index — and drives every one of them back through
`decode_with_encoding`:

| check | positions | mismatches |
|---|---|---|
| table integrity | 3,456 | 0 |
| single-byte, both directions | 7,018 | 0 |
| euc-kr index | 23,940 | 0 |
| big5 index | 19,782 | 0 |
| shift_jis index | 11,280 | 0 |
| gb18030 ranges | 621 | 0 |
| **total** | **46,315** | **0** |

"Both directions" is literal: every byte `0x80..0xFF` is decoded alone and
compared with its table entry, and every mapped code point is then encoded
back and must give the byte it came from. Big5 includes its four two-code-point
entries (pointers 1133, 1135, 1164, 1166) and Shift_JIS includes its 1,880
private-use pointers.

## Where a 0 entry is the answer

The first version of the sweep reported 150 mismatches, every one of them
`table=U+0000 decoder=U+FFFD`. That is the sweep being wrong. In a WHATWG index
file, **0 means "no pointer"** — the standard's decoder returns error on it,
and a non-fatal decode is U+FFFD. Reading 0 as a code point is the same class
of error as the Big5 sweep's: comparing against a description of the table
instead of the table's own contract.

The same mistake, one level up, is why `gb18030` first reported two
mismatches: my sampler extrapolated past the end of the last range, generating
code points above U+10FFFF. Clamping at the Unicode ceiling and honouring
step 6's gap clause (`pointer > 39419 && pointer < 189000` is an error, not a
lookup) took it to 0.

## The third case: windows-1255 byte 0xCA

The Node cross-check — the only check with a second implementation behind it —
disagreed on exactly one byte:

```
node disagrees on windows-1255 byte 0xCA: table U+05BA, node ['U+FFFD']
```

The table has `0xC9 -> U+05B9`, `0xCA -> U+05BA`, `0xCB -> U+05BB`: an
unbroken run. Node (ICU) answers U+FFFD for the middle one and is right about
the two either side. **The engine is correct and ICU is wrong**, and this is a
third encoding to add to the row's two — the same failure mode as `koi8-u`,
where ICU's table predates a code point added to Unicode later.

## What an index sweep cannot see

This is the part worth recording, because I tried to break it and could not
break it the way I expected.

Injecting `0x1234` into `kSingleByte` — a corrupt entry that `make` then
rebuilt — was reported as **0 mismatches by every decoder check**. The decoder
read the same corrupt table, so decoder and table agreed. The encoder agreed
too: an encoder/table consistency check across all 27 tables found 0
disagreements with the corruption in place.

An internally consistent corruption is invisible to any check that reads only
this engine. The thing that caught it was the Node cross-check, because Node
is a second implementation:

```
node disagrees on windows-1250 byte 0x80: table U+1234, node ['U+20AC']
node cross-check  3309 positions  1 disagreements
exit 1
```

So the tool's checks are not redundant: the decoder sweeps verify the decoder
against the table, and the cross-check verifies the table against something
outside. Neither alone is enough, and the file says so.

There was a third way this went wrong, and it is the most embarrassing one:
`--build` reused a harness binary that had been linked against the *old*
object. The injection was in the source, `make` had rebuilt the object, and the
tool still reported 0 because it was running a binary from before the
corruption. It now compares the object mtimes against the binary's and relinks
when they are newer, and the injected run exits 1 (clean: 0). A checker that
can silently test yesterday's code is worse than no checker, because it
reports green.

## The ICU-wrong encodings, confirmed

Both of the row's claims reproduce against Node v22.22.3:

* `koi8-u` pointers 46 and 62 — that is bytes **0xAE and 0xBE**, not 0xAA.
  The table has U+045E and U+040E; Node answers **U+255D and U+256C**, which
  are box-drawing characters, so ICU has the wrong table for those two slots
  rather than an older revision of the right one.
* `windows-1253` byte 0xAA — the table has no pointer, so U+FFFD; Node
  answers **U+00AA**. ICU is wrong, the table is the standard.

Both are listed in `ICU_KNOWN_WRONG` and counted separately, so the run can
tell "the engine disagrees with the standard" from "ICU disagrees with the
standard and the engine is right".

