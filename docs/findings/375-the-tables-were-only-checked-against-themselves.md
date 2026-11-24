# Finding 375 — the encoding tables were only checked against themselves

Phase 375. Status: **fixed; the tables are now held to `whatwg/encoding`
itself.**

## 1. What was wrong

The engine's 91,441 encoding-table positions were checked by exactly one
tool, `tools/conformance/verify_encoding_tables.py`, and that tool's oracle is
the table: it drives each decoder and compares the answer with the entry the
decoder read. That catches a decoder that has drifted from its table — the
defect class the Big5 row was written for — and it cannot catch a table that
was generated wrong, because then the decoder is right about the wrong table.

The tool says so itself:

> A decoder and its table can be wrong together — if the table were generated
> from a bad revision of the standard, this would agree with it and both would
> be wrong.

The row that carries this has a history of exactly that confusion. A byte
sweep once reported that `decode_big5` mishandled `0x80`, and a branch was
written to match; the branch was wrong, the engine was right, and the record
of it is Phase 245 / finding 249. A check whose oracle is the thing being
checked is how that happens.

## 2. What changed

`tools/conformance/verify_encoding_index.py` compares the vendored tables with
`whatwg/encoding`'s own index files: 27 single-byte tables (3,456 positions),
`kIndexBig5`, `kIndexEucKr`, `kIndexJis0208`, `kIndexJis0212`,
`kIndexGb18030`, and the 207 `kIndexGb18030Ranges`. A pointer the standard
omits must be 0 in the engine. The fetched files are refused unless they hash
to one pinned digest over all 34 of them, so the pin constrains the bytes, not
a branch name.

`make verify-encoding-index`, and a step in the CI job that already runs
`make test`.

## 3. Evidence

```
$ make verify-encoding-index
total                       91441 positions     0 mismatches
```

Red on three injected defects, one at a time, each object rebuilt:

- `windows-1252` `0x80` `0x20AC` → `0x0000`: 1 mismatch, `('0x80', 0, 8364)`
- `kIndexBig5[0]` `0` → `0x1234`: 1 mismatch, `(0, 4660, 0)`
- `kIndexGb18030Ranges[1]` `{36, 0xa5}` → `{36, 0xa6}`: 1 mismatch,
  `(1, (36, 166), (36, 165))`

The runtime agrees at the four points the row argues about — `windows-1252`
`0x80` → U+20AC, `koi8-u` `0xA4` → U+0454, `windows-1255` `0xCA` → U+05BA,
`iso-8859-16` `0xA1` → U+0104 — and Big5's `0x80` and `0xFF` are U+FFFD.

## 4. What remains wrong

- The pin records file contents; it does not name a revision. A pin that is
  raised must be raised by editing one constant, and `--print-digest` says
  what the new one is.
- The `Identifier:` line each index file carries is **not** a hash of the
  file's bytes and is not used. Two ways of hashing the file were tried and
  neither matched it; what it is hashed over is not documented here, so the
  tool does not pretend to check it.
- A table check is not a decoder check. The two tools are complements: this
  one says the tables are the standard's, the other says the decoders follow
  the tables. Neither implies the other, and both are in CI.
- Not covered by either: the shift_jis encoder, ISO-2022-JP's state machine,
  and the `<meta charset>` prescan. None of them changed here.
