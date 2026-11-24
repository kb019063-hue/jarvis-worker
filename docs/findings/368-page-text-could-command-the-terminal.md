# Finding 368 — a page's own text could command the terminal

**Resolved by Phase 368** (`docs/phases/368-the-pages-text-is-not-an-instruction.md`).

## 1. What was found

`litebird page.html --dump-dom` printed the document title, every attribute
value, every text node and every comment exactly as the page wrote them.

```bash
$ printf '<!doctype html><title>OK\x1b]0;OWNED\x07X</title>' > t.html
$ litebird --dump-dom t.html | cat -v
#document (title: "OK^[]0;OWNED^GX")
```

Those are not characters to a terminal. `ESC ] 0 ; OWNED BEL` is an
operating-system command: it retitles the window and is not printed at all.
`ESC ] 52 ; c ; <base64> BEL` writes the clipboard; `ESC [ 2 J` clears the
screen; `ESC [ 1 ; 1 H` moves the cursor over a line already written. A page
that can write them into a dump — a file a person then runs the engine on, or
the output of a CI job — can forge the rest of that output. `--dump-dom-webplatform`,
`--dump-css` and `--dump-paint` carried the same bytes.

The engine had the rule already, in the shell's frame and in the uncaught-error
diagnostics. The document dumps were the path where nobody had asked, and no
test carried a page with an escape in it, so nothing failed.

## 2. What was done

One filter, `terminal_safe` (`src/common/TerminalText.cpp`), replacing every
byte a terminal acts on — `ESC`, `BEL`, `CR`, the other C0 controls, `DEL`, and
the 8-bit CSI/OSC/DCS introductions — with U+FFFD, and leaving a tab, a newline
and every byte of well-formed UTF-8 alone. Applied to the title, attributes,
text nodes, comments, processing instructions and doctype identifiers of both
tree dumps. A checker, `make verify-page-text-terminal-safety`, runs the real
CLI over a page with an instruction in each field and fails on any control byte
in the output.

## 3. Evidence

`make test` **433/433**; `make verify-page-text-terminal-safety` **31/31**;
`make diff-trees` 1500/1500 unchanged, because an ordinary page still dumps
byte for byte as it did. With the dump files as `main` has them, the checker is
25/31 — the six failures are the six fields it exists for.

## 4. What is still open

`--dump-css`, `--dump-paint` (`src/main.cpp`, pull request #365) and
`console.log` of a page string (`src/js/JsInterpreter.cpp`, #363 and #366) print
page text raw for the same reason and are not fixed here.
