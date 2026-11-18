# Phase 76 — the corpus web-platform-tests publishes, and the processing instruction

The tree-construction scoreboard read 1,792 / 1,792 (100.0%) and had done since
Phase 49. It was 100.0% of a copy that stopped moving in June 2026, when the
tree-construction tests left `html5lib-tests` for `web-platform-tests`. The
copy upstream publishes now holds **1,973 cases** in 62 files, and 181 of them
have no case in the vendored copy at all: 88 processing instructions, 6 that
need a script to run while the page is parsed, and 87 others.

A score against a corpus that no longer changes is a number about the past. A
corpus that grew by 181 cases while the score stayed at 100.0% is a corpus
nobody was reading.

## What the standard changed

`<?something good>` was a bogus comment, whose data is the text after the
`<`: `?something good`. It is now a **processing instruction**: a node of its
own, with a target and data, which the DOM has had since Phase 28 (the
`ProcessingInstruction` interface, its `target`/`data`, cloning, and the
serializer's `<?target data>`), and which the parser never made.

`processing-instructions.dat` is the oracle for every branch, and the grammar
is entirely ASCII:

| input | node |
| :--- | :--- |
| `<?something>` | `<?something ?>` — target `something`, empty data |
| `<?something good>` | `<?something good?>` — the whitespace run after the target is skipped |
| `<?hey?there>` | `<?hey ?there?>` — `?` ends the target and starts the data |
| `<?hey   there?>` | `<?hey there?>` — a run of spaces, tabs, LF or FF is one separator |
| `<?a$>`, `<?data.v1>`, `<?namespace:tag>` | comments: a name is letters, digits, `-` and `_`, starting with a letter or `_` |
| `<?1st-place>`, `<?-prefix>`, `<?٥-star>` | comments: the first character must be a letter or `_`, and the name is ASCII |
| `<?xml>`, `<?xml-stylesheet>` | comments: `xml`, in any case, is reserved to XML |
| `<body><?start` | nothing at all: an unterminated instruction is dropped, not turned into a comment |

The last row is the one that separates the two fallbacks: `<?` with nothing
after it, or a name with no `>` before the input ends, emits no token, while
`<? ` (an empty name) still emits the comment `? `.

## Where the instruction is inserted

A processing instruction goes wherever a comment goes. That is not a
simplification: the tree-construction rules give both tokens the same slot in
every insertion mode that has one — initial, before html, before head, in
head, in head noscript, after head, in body, in table, in table text, in
caption, in column group, in table body, in row, in cell, in select, in
template, after body, in frameset, after frameset, after after body, after
after frameset — and only the node made differs. So `insert_comment_at` makes
the node, and the 17 comment cases in the modes were given the instruction as
a second label.

## The dump the tests read had never run

`--dump-dom-webplatform` printed a processing instruction as `<?target data>`
while the corpus's format says `<?target data?>` (`resources/README.md`). It
said so in a comment too. Nothing had noticed, because no path through the
HTML parser could produce a node to print: the only PIs in the tree came from
XML documents and `createProcessingInstruction`, and neither is dumped in this
format. The first case of `processing-instructions.dat` failed on the `?`
alone.

## The runner

Three things had to change before the corpus could be read at all:

1. **`#description`.** A case may now begin with one, before its `#data`. The
   runner split cases on `#data` alone, so it silently dropped the 16 cases
   that have one — scoring 1,957 of a corpus it called 1,957.
2. **A file that ends with a blank line.** `processing-instructions.dat` ends
   with two newlines, leaving an empty line in the last case's expected tree.
   No tree's last line is blank (a text node is quoted), so the runner drops
   it, as the format's "a single newline before the end" intends.
3. **The summary line.** `[FAIL on 62 files]` named every file that ran. It
   now names the cases and the files that failed.

## What it does to the scoreboard

The vendored corpus is the corpus, so the number went **down**, from 100.0% to
99.7%, and is worth more:

```
$ make conformance
DOM tree-construction conformance: 1967/1973 (99.7%)  [FAIL: 6 case(s) in 4 of 62 files]
```

The six are the scripted cases: each has a `<script>` that edits the document
while the parser is inside it, and the parser does not run scripts. The
runner already hands the case's `#script-on` / `#script-off` flag to the
binary; nothing in the tree-construction loop can execute one between tokens.

The old copy is still the check that this was a change and not a widening:

```
$ git stash; # the vendored corpus of the day
$ python3 tools/conformance/run_tree_construction.py
DOM tree-construction conformance: 1788/1792 (99.8%)
```

Four cases regress, and they are exactly the four the standard changed:
`<?import namespace="foo">`, `<?`, `<?COMMENT?>` and `<?COM--MENT?>`, which
the old copy expects as comments. That is the same edit seen from the other
side, and it is the reason the corpus had to move rather than be kept.

## How it was verified

```
$ make conformance
DOM tree-construction conformance: 1967/1973 (99.7%)

$ make wpt
Web Platform Tests: 9550/9744 subtests pass in 190 files; 194 fail as listed, 0 fail and are not listed
                                    # 9550, up from 9538: twelve subtests of the
                                    # html5lib-derived html/ files parse a `<?`

$ make test
All 219 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).
```

CI's conformance floor moves with the corpus (`1792 -> 1967`), and `WPT_FLOOR`
with the suite (`9538 -> 9550`). The floor is not lowered to let anything
pass: it is raised to what the corpus now is.

## What is still open

- **The six scripted cases**, row of its own in `AGENTS.md`: a parser that runs
  a script mid-tree is a parser with a scripting engine inside it, and this
  one is a single pass with a tokenizer that never yields to script.
- **The HTML serialization of a processing instruction** still writes no `?`
  before the `>` (`src/dom/Node.cpp`). That used to lose the node — the text
  read back as a comment; now it reads back as the same instruction, so the
  round trip holds, and the stale comment there has been corrected to say so.
  Whether the standard changed this form along with the parser is not measured
  here: no vendored web-platform-test serializes a PI through HTML.
- **`#description` is read but not used as a test name.** The runner prints it
  with a failing case (`about: ...`), which is what it is for.
