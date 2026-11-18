# Finding 82 — a processing instruction was a comment, and the dump that prints one had never run

## What was wrong

Three faults, and the third is why the first two survived.

**`<?target data>` was parsed as a bogus comment.** The standard now makes a
`ProcessingInstruction` for it — a node type this engine has had since Phase
28, with `target`, `data`, cloning, `createProcessingInstruction`, and an
`<?target data?>` line in the tree dump — but the tokenizer sent every `<?` to
`consume_bogus_comment`, so `<?something>` became `<!-- ?something -->`.
`src/html/HtmlTokenizer.cpp`, the `next == '?'` branch.

**The dump printed the wrong line.** `--dump-dom-webplatform` wrote
`<?target data>`; the corpus's format is `<?target data?>`
(`html/syntax/parsing/resources/README.md`, "Processing instructions"). The
code comment said the format was the one without the `?`, and nothing
contradicted it.

**The scoreboard was a frozen copy.** `tests/html5lib/tree-construction/` held
the last corpus `html5lib-tests` published before the tests moved to
web-platform-tests in June 2026: 1,792 cases, none of them a processing
instruction. Upstream held 1,973 by the time this was read, including
`processing-instructions.dat` with 125 blocks. The repository scored 100.0% of
a corpus that could no longer find either fault; the README said so in prose
("that copy is not vendored yet") and nothing acted on it.

## What changed

- **The tokenizer** (`src/html/HtmlTokenizer.cpp`, `consume_processing_instruction`)
  reads `<?`, then a name: an ASCII letter or `_` first, then letters,
  digits, `-` and `_`, ended by whitespace, `?` or `>`. A name that is not a
  name is not trimmed to its first good character — `<?a$>` is a comment —
  and a target beginning `xml` in any case is reserved to XML and is a comment
  too. The data begins after a run of ASCII whitespace (or at a `?` that ended
  the name) and runs to `>`; a `?` immediately before that `>` is the closing
  `?>`, not data. An unterminated instruction at EOF emits nothing, while an
  empty name still emits its comment: `<? ` is `<!-- ?  -->`, `<?` is nothing.
- **The tree builder** (`insert_comment_at`) makes the node the token asks
  for, and the 17 comment cases in `HtmlTreeBuilderModes.cpp` were given the
  instruction as a second label. Every insertion mode routes the two the same
  way; only the node differs.
- **The dump** writes `?>` for a processing instruction
  (`src/html/TreeDump.cpp`), and the stale comment on the HTML *serializer* —
  which still writes no `?`, because it was written when the parser could not
  produce the node — now says what is true: the text parses back to the same
  instruction, unless the data ends in `?`.
- **The runner** reads `#description` (16 cases begin with one and were being
  dropped), drops the empty last line of a file that ends with a blank line,
  and names the files that failed rather than every file that ran.
- **The corpus** is vendored from
  `web-platform-tests/wpt@37cfee8549b687f1cb0bc8cf772a81ab86de657d`,
  `html/syntax/parsing/resources/`: 62 files, 1,973 cases.

## How it was verified

```
$ make conformance
DOM tree-construction conformance: 1967/1973 (99.7%)  [FAIL: 6 case(s) in 4 of 62 files]

$ make wpt
Web Platform Tests: 9550/9744 subtests pass in 190 files; 194 fail as listed, 0 fail and are not listed

$ make test
All 219 engine test suites passed successfully!
```

The six failures are the scripted cases, which were failing before this
change for the same reason and are a row of their own in `AGENTS.md`.

The oracle for the grammar is `processing-instructions.dat` itself: 125
blocks, one per rule above, with the fallbacks (`<?a$>`, `<?1st-place>`,
`<?XML-stylesheet>`, `<?`, `<? `) as cases and not as prose. The unit-suite
route was not taken because a hand-written suite would restate a corpus that
is already vendored and run by CI on every push.

The old copy was run against the new parser as the control: 1,788 / 1,792,
the four differences being exactly `<?import ...>`, `<?`, `<?COMMENT?>` and
`<?COM--MENT?>`, which the old copy expects as comments. That is the standard
changing, seen from the other side.

## What is still open

- **The six scripted cases** (`scripted_adoption01`, `scripted_ark`,
  `scripted_foster01`, `scripted_webkit01`): a script must edit the document
  while the parser is inside it, and this parser does not run scripts.
- **No test serializes a processing instruction through HTML.** The serializer
  writes `<?target data>` without the closing `?`; whether the standard still
  asks for that form after making the parser produce the node is not measured
  anywhere in the vendored tests. The round trip holds either way, so this is
  a conformance question, not a correctness one.
