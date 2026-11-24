# Phase 375 — the encoding tables held to the standard

Task row: Big5's single bytes are errors, and the sweep that said otherwise
was wrong since Phase 245. Finding 375. Its one open item: *"the tool does not
read `whatwg/encoding`'s own tests, so a table generated from a bad revision
would agree with a decoder generated from the same revision."*

## 1. What was wrong

`tools/conformance/verify_encoding_tables.py` asks each decoder whether it
agrees with the table beside it. That is the check that would have caught the
Big5 sweep's mistake, and it is a good check. It also says, in its own
docstring, what it cannot do:

> A decoder and its table can be wrong together — if the table were generated
> from a bad revision of the standard, this would agree with it and both would
> be wrong.

Nothing in the tree recorded which revision `src/net/TextEncodingTables.cpp`
was generated from, and nothing compared it with the standard. The row had
carried that as its last open item. It matters because the Big5 row exists
precisely because a table and a decoder were read as one thing: the byte sweep
that reported `0x80 -> U+0080` was wrong, and the engine was right.

## 2. What changed

`tools/conformance/verify_encoding_index.py` reads `whatwg/encoding`'s index
files — the files the tables were generated from — and compares every
position:

| what | positions |
|---|---|
| 27 single-byte tables, `kSingleByte[row][byte - 0x80]` | 3,456 |
| `kIndexBig5` | 19,782 |
| `kIndexEucKr` | 23,940 |
| `kIndexJis0208` | 11,280 |
| `kIndexJis0212` | 8,836 |
| `kIndexGb18030` | 23,940 |
| `kIndexGb18030Ranges` | 207 |
| **total** | **91,441** |

A pointer the standard leaves out must be 0 in the engine, which is what "no
pointer" means. Big5 has 1,192 such pointers — including pointer 1133, the
bytes `0x88 0x62` — and every one is 0 on both sides, which is the entry the
Big5 row once mistook for U+0000.

The fetched files are refused unless they hash to one pinned digest
(`64 8870f3…`, computed over all 34 files in sorted order, each prefixed by
its name), so a truncated download or an edited upstream cannot pass as
agreement. `--print-digest` prints what a fetch hashed to; `--offline` uses
`build/whatwg-encoding-index/` and touches no network.

`make verify-encoding-index` runs it, and `.github/workflows/ci.yml` runs that
in the same job as `make test`. The network path is not new to CI:
`tools/conformance/run_test262.py` already fetches from `codeload.github.com`
in the same run.

The tool also cross-checks its own row mapping against the sibling tool's, so
the two cannot silently disagree about which `kSingleByte` row belongs to
`iso-8859-10`.

## 3. How it was verified

```
$ make verify-encoding-index
windows-1252                  128 positions  ok
...
kIndexGb18030Ranges           207 positions  ok
total                       91441 positions     0 mismatches
```

The engine agrees with the standard in all 91,441 positions.

It was shown to fail. Three defects were injected into
`src/net/TextEncodingTables.cpp`, one at a time, each object rebuilt and the
tool run:

| injected | reported |
|---|---|
| `windows-1252` byte `0x80`: `0x20AC` → `0x0000` | `windows-1252  128 positions  1 MISMATCH  ('0x80', 0, 8364)` |
| `kIndexBig5[0]`: `0` → `0x1234` | `kIndexBig5  19782 positions  1 MISMATCH  (0, 4660, 0)` |
| `kIndexGb18030Ranges[1]`: `{36, 0xa5}` → `{36, 0xa6}` | `kIndexGb18030Ranges  207 positions  1 MISMATCH  (1, (36, 166), (36, 165))` |

Each was reported as exactly one mismatch, on the table it belonged to. The
third of these is the one that found a bug in the tool as first written: it
compared the ranges by pointer *value* instead of by position, and reported
206 of 207 as missing.

The runtime was checked at the points the row itself argues about, through the
script-facing decoder:

```
windows-1252 0x80 -> "€" (U+20AC)     big5 0x80 -> "\uFFFD"
koi8-u      0xA4 -> "є" (U+0454)      big5 0xFF -> "\uFFFD"
windows-1255 0xCA -> "ֺ" (U+05BA)     iso-8859-16 0xA1 -> "Ą" (U+0104)
```

The last three are the encodings where ICU, and so Node's `TextDecoder`, is
the one that is wrong (finding 288); the engine matches the standard on all
three, and Big5's single bytes are errors, which is what Phase 245 said.

## 4. What is still open

- **The pin is the file contents, not a revision name.** `--ref` says which
  branch to fetch and the digest is what actually constrains it, so a run
  against a future upstream fails loudly rather than reporting a difference
  table by table. Raising the pin is a one-line edit to `PINNED_DIGEST`, and
  this document says how to get the new value (`--print-digest`).
- **The `Identifier:` line each index file carries is not used**, because it
  is not a hash of the file's bytes — both were tried and do not match. What it
  is hashed over is not documented in the repository, so the tool does not
  guess; the combined digest above covers the same ground.
- **This is a table check, not a decoder check.** It says the tables are the
  standard's; the sibling tool says the decoders follow the tables. Both are
  needed and neither implies the other.
- The shift_jis encoder path, `iso-2022-jp`'s state machine and the
  `<meta charset>` prescan are not covered by either tool, and are unchanged.
