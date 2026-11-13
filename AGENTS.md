# Working in this repository

Litebird is written by AI agents, at times more than one at once. Each agent
has a branch of its own. No agent is told what another one is doing: an agent
knows what `main` holds and what the open pull requests say, at the moment it
looks, and nothing else. Whatever cannot be said that way goes through the
person the agents work for. This file is what the agents have to agree on so
that two pieces of work do not collide.

## Before starting

1. **An open pull request is a claim.** Run `gh pr list --state open` and read
   the first lines of each: they name the task and the files it changes. (A
   pull request nobody has pushed to for days is not a claim; ask in a comment
   on it before taking its task.)
2. **Claim before doing the work.** Pick a row of the table below that nobody
   has, push a first commit (an empty one will do) and open a pull request
   whose title begins with `WIP:` and whose text begins:

   ```
   Task: <the row of the table>
   Files: <what it will change>
   Phase P, finding F
   ```

   `P` is the next free phase number (the phase table in ARCHITECTURE.md,
   plus the open pull requests) and `F` the next free finding number
   (AUDIT.md, plus the open pull requests). They are taken now so that two
   pull requests do not both call themselves Phase 27.
3. **One task at a time, inside its files.** If the work turns out to need a
   file that another open pull request names, stop before changing it and
   tell the person you work for: the other agent will not see a comment
   unless it happens to look. A bug found outside the task becomes a new row
   of the table; it is not fixed in passing.

## The files every pull request changes

`README.md`, `ARCHITECTURE.md`, `AUDIT.md`, `tests/test_suite.cpp` (one file,
with the suites registered in a list at its end), `Makefile`,
`.github/workflows/ci.yml` and the table below. Two pull requests written at
the same time will both have changed them. So:

1. Add a block of your own: a section, a group of tests. Do not re-wrap or
   reorder what is there.
2. **Before merging, merge `main` into your branch.** Resolve, correct the
   numbers that count things (the unit suites in README.md, the floors in
   `ci.yml` and the Makefile), run the checks again, push, and let CI pass on
   that commit. CI that passed on a branch behind `main` says nothing about
   the merge.

## What "done" means

- The checks: `make test`, `make conformance`, `make js-conformance`,
  `make dom-conformance`, `make url-conformance`, `make diff-trees`,
  `make fuzz`, `make deep`, `make asan`. The last two take
  about eight minutes each. CI runs all of them, and one CI run is about 28
  minutes of runner time: run the checks here first and push when they pass,
  not to find out whether they do.
- A floor in CI is not lowered to let a pull request pass.
  `tests/js-conformance/everyday-*.dat` and
  `tests/dom-conformance/everyday.dat` are not edited. A DOM case that
  fails is not "fixed" by recording another answer: its expectation is
  what jsdom prints, and where jsdom is wrong the case is taken out and
  the standard's answer goes into a unit test
  (`tests/dom-conformance/README.md`).
- The three documents are updated: what was wrong, what changed, how it was
  verified (a command anyone can run, and the number it prints), and what is
  still open. AUDIT.md says what is not fixed as plainly as what is.
- The pull request takes its row out of the table below, or rewrites the row
  to what is left.
- The `WIP:` comes off the title. Merged into `main` with a merge commit
  (`gh pr merge N --merge`) once CI is green, and CI on `main` checked
  afterwards.

## Open tasks

Rows that share files should not be in work at the same time.

| Task | Files | Described in | Note |
| :--- | :--- | :--- | :--- |
| **What the DOM corpus cannot ask**: sizes and positions while scripts run (`getBoundingClientRect()` is zero: layout happens once, after them), style sheet objects (`sheet.cssRules`, `insertRule`), `document.write`, external scripts and stylesheets | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | ARCHITECTURE.md, Phase 27, section 10 | jsdom does none of the first two, so each needs a reference of its own before it is built |
| **Host names outside ASCII in URLs** (`http://faß.example` → `xn--fa-hia.example`): the 38 failing cases of `make url-conformance` | `src/net/UrlParser.cpp` | `tests/url/README.md` | Needs Unicode's IDNA mapping table (UTS #46) and Punycode. Small and self-contained |
| **A block inside an inline box is not laid out**: `<a href="#">x<p>y</p></a>` shows "x" and no "y" | `src/layout/` | AUDIT.md, finding 28 | The open bug a real page is most likely to meet |
| **Floats**: 10,000 with a size in one box take 46 s, and past the first 1,024 they are placed outside the box | `src/layout/FloatTableLayout.cpp` | AUDIT.md, finding 28 | Same files as the row above |
| **Table spans and collapsed borders** (`rowspan`, `colspan`, `border-collapse`) | `src/layout/` | Known gaps, 1 | Pull request #16 (8 November) has an implementation that was never merged and is behind `main`. Same files as the two rows above |
| **Every box carries its own copy of a 920-byte computed style**: 100,000 times `word <b>bold</b>` takes 1.3 GB | `src/layout/`, `src/css/` | AUDIT.md, findings 12 and 28 | Same files as the layout rows |
| **CSS trims spaces one character at a time**: 300,000 spaces inside `:nth-child( )` take 9 s | `src/css/` | AUDIT.md, finding 28 | Small and self-contained: a good first task |
| **`delete` and `shift()` cost the square of the size**: 50,000 deletes take two minutes | `include/litebird/js/JsValue.hpp`, `src/js/JsValue.cpp` (the property map), `src/js/BuiltinsArray.cpp` | ARCHITECTURE.md, Phase 23, section 9 | `JsValue.cpp` also holds the DOM bindings: keep to the property map, or wait for the first row |
| **A regular-expression group repeated over about 12,000 times ends in `RangeError`** | `src/js/BuiltinsRegExp.cpp` | AUDIT.md, finding 26 | A rewrite of the matcher. Shares no file with another row |
| **Memory is reclaimed only when a script calls `gc()`**, and no leak audit has been done | `src/js/Heap.cpp`, and everything that allocates script objects | Known gaps, 4 | Not while the first row is in work |
| **`<select>` content as the standard now has it**: the 11 corpus cases that fail | `src/html/HtmlTreeBuilder.cpp`, `src/html/HtmlTreeBuilderModes.cpp` | AUDIT.md, finding 30 | parse5 has the older rules too, so it cannot be the reference |
| **The corpus has moved to web-platform-tests** (1,973 cases, 1,868 pass): vendor it, read its `#description` sections, add processing instructions | `tools/conformance/`, `tests/html5lib/`, `src/html/`, `include/litebird/dom/` | AUDIT.md, finding 30 | Same files as the row above. A new node type needs a line in the markup serializer, which the first row is rewriting |
| **One element with 50,000 attributes takes 4.5 s** | `src/html/HtmlTokenizer.cpp` | AUDIT.md, finding 28 | Small |
| **External scripts and stylesheets are not loaded** (`<script src>`, `<link rel=stylesheet>`) | `src/net/`, `src/js/JsInterpreter.cpp`, `src/main.cpp` | Known gaps, 9 | Not while the first row is in work: it changes both of the last two files |
