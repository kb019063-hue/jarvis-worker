# Phase 95 — `document.write` writes into the parser that called it

## 1. What was wrong

Three things were missing, and the corpus case only shows the first.

- **`document.write()`, `writeln()`, `open()` and `close()` were one no-op.** `DomDocumentBindings.cpp` registered all four with the comment *"there is no parser to write into any more"*, which was true of how the engine was arranged: `--dump-dom-webplatform` parsed statically, so no script ran during the parse at all, and the shell ran a page's scripts through `JsInterpreter::load_page` where a script is executed as its end tag is read but nothing could put markup back into the tree builder.
- **The dump path ran no scripts.** `src/main.cpp` called `HtmlParser::parse(html, scripting)` and dumped the result. Every `scripted_*.dat` case of the tree-construction corpus was therefore a failure by construction, not by a parser bug: `scripted_adoption01.dat` (a script renames an id), `scripted_ark.dat` (a script rewrites an attribute), `scripted_webkit01.dat` (a script writes markup) and `scripted_foster01.dat` (a script moves the root).
- **A document accepted children a document cannot have.** With the scripts running, the two `scripted_foster01.dat` cases still differed: a script removes `document.documentElement` and re-appends the table, and the parser then inserts a `<b>` (and, in the other case, text) into the **document** — one element child too many, and a text child a document never has. The browser drops those insertions; this engine built them, so the dump began `| <b>` where the expectation begins `| <table>`.

The parser already had everything else: `HtmlParser::parse_into(document, html, scripting, on_script)` and `HtmlTreeBuilder::set_script_handler` hand each classic script to a callback as its end tag is read.

## 2. What changed

### The tree builder can be written into while it runs (`HtmlTreeBuilder.hpp`, `HtmlTreeBuilder.cpp`)

`write_into_parser(text)` tokenizes `text` and dispatches each token through `process_token`, the same path the parser's own loop uses, so the written markup is inserted at the insertion point the parser has reached — inside the element that holds the script. No end-of-file token is taken from the text: the text ends, the document does not.

`parse()` sets `document->set_parser_write(...)` to that method for the duration of the parse and clears it before returning, so a `document.write()` after the load finds no parser.

### `document.write` and `document.writeln` reach it (`Document.hpp`, `DomDocumentBindings.cpp`)

`Document` holds the hook (`ParserWrite`, a `std::function<void(std::string_view)>`). `write()` converts each argument to a string, concatenates them with no separator, and hands the result to the parser; `writeln()` appends the newline it promises. Both keep the `InvalidStateError` an XML document raises. With no parser running there is nothing to write into and they do nothing — the spec's *"open a new document and write into that"* would replace the page under the script that called it, which this engine does not do (see what is still open). `open()` and `close()` keep their previous behaviour.

### The dump path runs the page's scripts (`src/main.cpp`)

`--dump-dom-webplatform` now builds a `Document`, gives it the effective URL, and parses through `JsInterpreter::load_page(html, scripting, nullptr)`, falling back to the static parse when scripting is off or when the interpreter throws. `--scripting=off` is unchanged.

### A document refuses the children it cannot have (`HtmlTreeBuilder.cpp`)

`append_node_at()` and `insert_character()` now ask `document_refuses(parent, node)`: a document has at most one element child and no text children, so an insertion that would break either is dropped, with nothing built. This is what a browser does when a script has moved the root away and the insertion point is left inside the document.

## 3. How it was verified

- **`python3 tools/conformance/run_tree_construction.py --quiet`:** **1972 / 1973 (99.9%)**, up from 1967. Five of the six `scripted_*.dat` cases pass — `scripted_adoption01.dat`, both of `scripted_webkit01.dat` and both of `scripted_foster01.dat`. The one that does not is `scripted_ark.dat`, whose diff is one `<font>` too many (below).
- **`make test`:** **248 / 248** suites, including the new `document_write_inserts_where_the_parser_reached`, which asserts that a written `<b>` lands between the script and the text that follows it, that two writes run together and `writeln` adds its newline, that the written markup is parsed as markup (an unknown tag becomes an element, a written end tag closes it), that `document.write` after the load changes nothing, and that `scripted_foster01.dat`'s tree is the table alone. 114 / 114 cross-origin checks pass. (Later merges have taken the tree to 255 suites; this phase added one.)
- **`make js-conformance`:** 1131 / 1152, unchanged from `main` (measured against a build of `main` in a second worktree, not assumed). The corpus went on growing on `main` while this was open — it is 1,156 cases there, 1,137 passing — and none of that movement is this phase's.
- **`make wpt`:** 9567 / 9824 across 190 files, unchanged. **`make dom-conformance`:** 678 / 678.
- The conformance floor in `.github/workflows/ci.yml` is raised from 1967 to 1972.

## 4. What is still open

- **`scripted_ark.dat`, the last case.** Its script rewrites the `size` attribute of the second `<font>` and the expected tree keeps the *third* `<font>`'s reconstructed copy as it was: this engine reconstructs one `<font>` more than the browser does after the `<p>` that closes the run. That is the adoption agency's formatting-element list, not writing.
- **`document.write` after the load.** A late `write()` still does nothing rather than opening a new document; the cases that ask for that (`document.open` and a replaced page) are not in this corpus.
- **`document.close()` does not end the parse**, and `document.open()` does not start one.
- **Blocking on a written `<script>`:** a script written by `document.write` runs only after the written text has been inserted, not at the point the parser reaches it. The corpus cases do not ask.
- **`document.write` from an external script** fetched while parsing works only if the fetch completes during the parse (it does, synchronously, in `load_page`), and there is no parser-blocking queue.
