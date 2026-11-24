# Phase 368 — the page's text is not an instruction

Task row: a page's own text reaches the terminal raw.
Finding 368.

## 1. What was wrong

A dump prints the page's text. That text is the page's to choose, and a
terminal acts on some bytes instead of printing them, so the dump was an
input channel to the terminal:

```bash
$ printf '<!doctype html><title>OK\x1b]0;OWNED\x07X</title>' > t.html
$ litebird --dump-dom t.html
#document (title: "OK<ESC>]0;OWNED<BEL>X")
```

On a real terminal that is not a title, it is the OSC `0` sequence: the window
is retitled, and nothing of "OWNED" is printed. With `]52;c;` in place of
`]0;` every copy of the dump writes the operator's clipboard; with `[2J` a
page clears the screen; with `[1;1H` it moves the cursor over a line that was
already written. The same bytes came out of both tree dumps, and out of the
CSS and paint dumps.

The engine already knew this rule and applied it in two places — the shell's
frame (`tools/conformance/verify_terminal_shell_safety.py`, Phase 241) and the
uncaught-error diagnostics (`terminal_diagnostic` in `src/js/JsOps.cpp`,
`make verify-js-error-terminal-safety`) — but the dumps of a parsed document
had never been checked, and no test carried a page with an escape in it.

## 2. What changed

A filter with one rule, in `include/litebird/common/TerminalText.hpp` and
`src/common/TerminalText.cpp`: **a byte a terminal acts on is not printed;
everything else is.**

| Byte | What a terminal does with it | In the dump |
| :--- | :--- | :--- |
| `ESC` 0x1B | starts every escape sequence | U+FFFD |
| `BEL` 0x07 | ends an OSC string; a bell | U+FFFD |
| `CR` 0x0D | returns to column zero | U+FFFD |
| other C0, `DEL` | `BS`, `VT`, `FF`, `NUL`, … | U+FFFD |
| 0x9B, 0x9D, 0x90 | 8-bit CSI, OSC, DCS | U+FFFD |
| `TAB`, `LF` | layout the dump writer uses itself | kept |
| printable ASCII | text | kept |
| well-formed UTF-8 | text, in any script | kept, whole |

U+FFFD rather than deletion, so a reader can see that a byte was there and the
page cannot hide the fact that it tried. Everything else is untouched, which
is what the tools that read these dumps need: `#text: "` in
`tools/fuzz/deep_pages.py`, and the WPT tree dump that `make wpt` and
`make diff-trees` compare byte for byte against the corpus.

It is applied in the dump a person reads — `Document::dump`, the
`--dump-dom` report:

- `Text::dump_open` (`src/dom/Node.cpp`) — the writer's own `\n` and `\t`
  escapes first, so a newline in a text node still prints as two characters,
  then the rest through the filter; the text between them accumulates so that
  a character is filtered whole instead of a byte at a time.
- `Element::dump_open` (`src/dom/Node.cpp`) — attribute names and values.
- `Document::dump_open` (`src/dom/Document.cpp`) — the title.
- `Comment` and `ProcessingInstruction` (`src/dom/Comment.cpp`).

`dump_tree_webplatform` (`src/html/TreeDump.cpp`) is **not** filtered, and the
first draft of this phase did filter it. That dump is not a report: it is the
html5lib corpus's data format, and `make conformance`, `make wpt` and
`make diff-trees` compare it byte for byte with
`tests/html5lib/tree-construction/`. Filtering it broke
`plain-text-unsafe.dat` case #0, whose input is a carriage return and whose
expected tree is a text node of **two lines**:

```
|     "FOO
| ZOO"
```

`make conformance` went 1973/1973 → 1972/1973, and the landing session's own
run found it because that job is not part of a pull request's short run. A
filter belongs where text goes to a terminal, not where text goes to a
comparing tool — the same reason `#text: "` must keep matching for
`tools/fuzz/deep_pages.py`.

`make verify-page-text-terminal-safety` is the check, in the shape of the
js-error one: the real CLI, a page carrying an introduction in each of the
four fields in `--dump-dom`, and the output searched for any byte a terminal
acts on. It names the offset and the byte class and never prints the
bytes themselves — on a terminal that read this tool's output, the attack
would be the output. The one control sequence `--dump-dom` writes itself, its
yellow `--- [DOM Tree] ---` heading, is pinned by its exact bytes (as the
shell's frame is) rather than exempted, so an SGR cannot hide inside it.

It also pins the split in both directions: the corpus dump keeps its carriage
return (`"FOO\rZOO"`), and the human dump does not.

`src/main.cpp` — where `--dump-css` and `--dump-paint` print the page's own
declaration values and `DrawText` strings — belonged to pull request #365, so
those two dumps are **not** fixed here and are recorded open below.

## 3. How it was verified

`tests/suites/368-page-text-cannot-command-the-terminal.cpp`, a file of its
own, parses one page carrying an instruction in the title, an attribute, a
text node and a comment, dumps it, and asserts that no byte a terminal acts on
survives and that each field's own text does; that the same page without its
instructions dumps identically once the U+FFFD marks are removed; and that the
corpus dump keeps its carriage return while the human dump does not.

```bash
make test                                        # All 441 engine test suites passed
make verify-page-text-terminal-safety            # 21/21 checks pass
make verify-js-error-terminal-safety             # 6/6 still pass
```

Negative control: with the dump files as `main` has them and the checker in
place, every "prints no control byte" check fails — one per field — and the
ones that pass are the ones that must: the page's text, the heading, the
ordinary page, the corpus's carriage return. (When both dump formats were
filtered, the control was 25/31 with three dump formats' worth of checks;
the split above is what those runs became.)

| Check | Result |
| :--- | :--- |
| `make test` | 441 suites, all pass |
| `make verify-page-text-terminal-safety` | 21/21 |
| `make conformance` | 1973/1973 |
| `make dom-conformance` | 678/678 |
| `make diff-trees` | 1500/1500 trees equal |
| `make wpt` | 9570/9824, the same 254 listed (unchanged from main) |
| `make js-conformance` | 1150/1158 (99.3%) |

## 4. What is still open

- **`--dump-css` and `--dump-paint` still print page text raw.** A declaration
  value (`font-family: "A<ESC>]0;V<BEL>"`, `background: url("…")`) and a
  `DrawText` string reach the terminal as written; both are printed by
  `src/main.cpp`, which pull request #365 holds. The fix is one call to
  `terminal_safe` at each `std::cout`, and
  `tools/security/verify_page_text_terminal.py` is where the case goes.
- **`console.log` of a page string is the same defect** and is unchanged.
  `src/js/JsInterpreter.cpp` holds it (pull requests #363 and #366), and the
  fix is the same call around the argument loop. Until then a page can still
  command the terminal by logging; it is recorded as its own row.
