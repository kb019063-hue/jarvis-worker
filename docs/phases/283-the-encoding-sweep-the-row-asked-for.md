# Phase 283 — the encoding sweep the row asked for

## The row

`AGENTS.md`, "Big5's single bytes are errors, and the sweep that said otherwise
was wrong since Phase 245". Its one open item:

> a byte sweep should compare against the index tables already vendored here
> (`kSingleByte`, `kIndexBig5`, …) and not against ICU, which is also wrong on
> `koi8-u` (`index-koi8-u.txt` pointers 46/62 give U+045E/U+040E, Node gives
> U+255D/U+256C) and `windows-1253` (no pointers 42 or 82, so 0xAA and 0xD2 are
> errors, which Node accepts)

Collision check first: the row was free, and so was `src/net/TextEncoding*`.
The numbers were not — #345 took Phase 279 / finding 284 while this cycle was
running, so the work moved to 282 / 287.

## What was built

`tools/conformance/verify_encoding_tables.py`. It compiles a C++ harness
against the two objects that hold the tables and drives every table back
through `decode_with_encoding`:

* 27 single-byte tables, **both directions** (decode every byte, then encode
  every mapped code point back to its byte);
* `kIndexEucKr` — all 23,940 pointers;
* `kIndexBig5` — all 19,782, including the four two-code-point entries;
* `kIndexJis0208` via Shift_JIS — all 11,280, including 1,880 private-use;
* `kIndexGb18030Ranges` — three samples per range, honouring step 6's gap.

`46,315 positions, 0 mismatches`.

The harness is C++ because the tables are `const` arrays: a second copy parsed
out of the source would be the very mistake the tool exists to avoid.

## The Node cross-check, and why it is not the oracle

The driver also asks Node's `TextDecoder` for every single-byte entry and
reports disagreements, with three encodings counted separately because ICU is
the one that is wrong on them: `koi8-u`, `windows-1253`, and — found here —
`windows-1255` byte 0xCA, where the index has U+05BA in an unbroken run
U+05B9/U+05BA/U+05BB and ICU answers U+FFFD.

That cross-check is not the oracle, and the row is why: ICU is wrong in three
places the engine gets right. It is a *second implementation*, which is exactly
what an index sweep otherwise lacks.

## The injection that mattered

I corrupted `kSingleByte` — `0x20AC` to `0x1234` in windows-1250 — rebuilt, and
ran. **Every decoder check reported 0 mismatches.** The decoder read the corrupt
table and agreed with it; an encoder/table consistency probe across all 27
tables also agreed. A corruption the engine reproduces faithfully is invisible
to a check that only reads this engine.

The Node cross-check caught it:

```
node disagrees on windows-1250 byte 0x80: table U+1234, node ['U+20AC']
node cross-check  3309 positions  1 disagreements
exit 1
```

That is now written into the tool's docstring and its `table_integrity` check:
the decoder sweeps verify the decoder, the cross-check verifies the table.

## Two mistakes of my own, recorded

* The first run reported 150 mismatches, all `table=U+0000 decoder=U+FFFD`.
  0 in an index file means "no pointer", not U+0000. Reading it as a code point
  is the same error class as the Big5 sweep's.
* gb18030 reported two, both from extrapolating past the end of the last range
  into code points above U+10FFFF.

Both are in the tool's docstring so the next person does not re-derive them.

## Checks

* `python3 tools/conformance/verify_encoding_tables.py` — exit 0;
  46,315 positions, 0 mismatches; Node cross-check 3,309 positions, 0
  disagreements, 3 known-ICU-wrong.
* Injected corruption in windows-1250 — exit 1, byte named.
* `make all` — builds, `build/litebird` unchanged.
* Node v22.22.3 as the second implementation.
