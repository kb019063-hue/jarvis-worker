# Phase 26 Architecture: The Tree Builder Against Pages Nobody Wrote

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 26 Architecture: The Tree Builder Against Pages Nobody Wrote` section.
> The text below is the section as it stood, unchanged.


### 1. Why this phase existed

A corpus says a parser is right about the inputs somebody thought of. Phase
25 added a second kind of check, `tools/conformance/diff_trees.py`: pages of
a few dozen random tags, parsed by Litebird and by parse5, an independent
parser, with the trees compared. Its first run found five bugs in 6,000
pages and Phase 25 left them (AUDIT.md, finding 29). This phase fixed them,
and then gave the pages more to contain. The second step found far more
than the first: 408 of 6,000 pages got a tree the standard does not ask for
(AUDIT.md, finding 30), and the commonest reason has no case in the corpus
at all.

### 2. Namespaces: what an end tag may close

The stack of open elements holds HTML, SVG and MathML elements, and the
standard's rules nearly always mean an HTML one when they name an element.
Two things were wrong.

- **Which elements are "special".** Every SVG and MathML element counted.
  The standard names nine of them: MathML `mi`, `mo`, `mn`, `ms`, `mtext`
  and `annotation-xml`, SVG `foreignObject`, `desc` and `title`
  (`is_special_foreign_element()`). The same nine bound a scope. An `<svg>`
  is an ordinary element: the adoption agency algorithm walks past it, so
  `<a><svg></a>` closes both and moves nothing.
- **Names were compared without the namespace.** `</title>` closed an SVG
  `<title>`; an SVG `<rt>` got an implied end tag; a table row was put into
  an SVG `<template>`. `html_name(element)` returns the tag name of an HTML
  element and an empty string for any other, and every rule that names an
  HTML element now asks it.

`index_open_element()` puts the nine into the scope and special indexes of
Phase 25 (section 4 there), so those indexes are exact with SVG and MathML
open. Before, a count of open foreign elements switched every query to a
walk down the stack as soon as there was one; the count and the walks are
gone. `open_element_closed_by_end_tag()` is the standard's "any other end
tag" loop answered from the index, and `any_other_end_tag()` is the one
place that acts on it: the "in body" rules call it, and so does the adoption
agency algorithm when it finds no formatting element.

### 3. Four rules that were read wrongly

- **`</button>`** closes its element in scope whatever is open inside it,
  like `</div>`. It was missing from that list.
- **"Noah's Ark".** A fourth identical formatting element pushes the
  earliest of the three out of the list of active formatting elements.
  `push_formatting_element()` removed the latest.
- **The adoption agency algorithm** (`adoption_agency()`, now `void`). With
  no formatting element of the token's name after the last marker it does
  what "any other end tag" does; it used to return as if it had handled the
  token. The furthest block is the first special element after the
  formatting element, found from the special index.
- **Text in a table.** The rule that collects text to see whether it is all
  whitespace applies when the current node is a `table`, `tbody`, `tfoot`,
  `thead`, `tr` or `template`, not whenever the insertion mode is "in
  table". U+0000 is dropped before the text is looked at, so it no longer
  turns whitespace into "not whitespace".

The frameset-ok flag had two: a `<noscript>` with scripting enabled does not
touch it, and the body made for the first thing after the head sets it to
"ok". And a second `<head>` start tag is ignored; it used to close the head.

### 4. A start tag that closes itself

`<br/>`. The standard says to "acknowledge the token's self-closing flag",
which means only: do not report it as an error. `acknowledge_self_closing()`
popped the current node. For a void element that had already been popped,
so the parent was closed: the paragraph around a `<br/>`, the list item
around an `<img ... />`, the head around a `<meta ... />`, after which the
rest of the head and the body were attached to the document, outside
`<html>`.

The function and its nine calls are removed. Nothing replaces them, because
nothing has to happen: a void element is popped where it is inserted, a
slash on any other HTML start tag is ignored, and in SVG and MathML the
foreign-content rules pop a self-closing element themselves.

Of the 408 wrong pages, 292 come right with this change alone, and the
corpus score does not move, because none of its 1,792 inputs writes a void
element with a slash. `examples/nextgen_showcase.html` does, and was parsed
wrongly.

### 5. Templates and fragments

- **"Parsing template contents"** (`parsing_template_contents()`) is true
  when a `<template>` is open or the fragment is being parsed for one. The
  form element pointer is then neither read nor set, so a form may be
  nested there and `<template><table><form>` keeps its form. (The standard
  changed the table case in September 2026.)
- **A fragment for a `<template>`** starts with "in template" on the list
  of template insertion modes. The list was empty, `replace_template_mode()`
  read its last entry, and the process ended with `SIGSEGV`.
- **A fragment for an element whose content is text** (`<script>`,
  `<style>`, `<textarea>`, `<title>`): no start tag was seen, so there is no
  "appropriate end tag", and `</script>` in it is text. A fragment for a
  `<noscript>` is text only with scripting enabled.
- **CDATA** is allowed wherever the adjusted current node is not an HTML
  element (`allow_cdata_in_foreign_content()`, asked before each token),
  which includes the top of a fragment parsed for an SVG element.

### 6. `innerHTML` parses for its element

The fragment parser takes a context element, and the corpus runner gives it
one for each of the 192 fragment cases. `Element::set_inner_html`, which is
what `element.innerHTML = ...` in a script calls, went through a helper that
said `<body>` for every element. A `<td>` is dropped in a body, so
`table.innerHTML = "<tr><td>1</td><td>2</td></tr>"` left the text "12" in
the table.

`HtmlParser::parse_inner_html(context, html)` parses for the element it is
given. `HtmlTreeBuilder::parse_fragment(html, context)` builds a stand-in
for it, so the parser cannot reach the tree the element is in, and takes
over the three things about the element that change the result:

| What | Why |
| :--- | :--- |
| Its name and namespace | the insertion mode to start in (rows in a `<tbody>`, cells in a `<tr>`), text content for `<textarea>` and `<style>`, SVG elements inside an `<svg>` |
| The `encoding` attribute | a MathML `annotation-xml` holds HTML when it says `text/html` |
| Whether a `<form>` is above it | the form element pointer: a `<form>` in the fragment is then ignored |

The form is looked for only when the markup has the letters "form" in it:
only a `<form>` or a `</form>` asks, and the walk to the root costs the depth
of the tree. Made at every assignment it turned a script that nests 30,000
levels through `innerHTML` from 0.2 s into 44 s. `make deep` has that page
and it still passed, at 48 of its 60 seconds; the line naming the slowest
run is what showed it, and a unit test bounds the loop now. Markup that does
hold a form still makes the walk (AUDIT.md, finding 28).

A shadow root still parses in a `<body>` context: every element that can
host one amounts to that.

### 7. The tokenizer and the dump

- `&#X` with no digits after it is text as typed; it came out as `&#x`.
- A comment the page ends in (`<!--x--`) does not keep the dashes that had
  begun to close it.
- `=` where an attribute name should begin starts the name: `<a =>` has an
  attribute called `=`.
- The doctype is dumped where it stood among the comments before `<html>`
  (`Doctype::preceding_children`); the tree was right, the dump was not.
- An empty file is parsed like any other page. It used to exit with status
  1 and no tree.

### 8. A reference that can be corrected

parse5 8.1 is itself behind the standard in nine rules (AUDIT.md, finding
30, has the table), so after the fixes 49 of 6,000 pages still differed
from it. A ceiling of 49 would let a new bug in behind an old excuse.
`diff_trees.py --corrected` instead compares with a copy of parse5 in which
those nine rules are changed: `CORRECTIONS` in the script is 13 edits, each
a piece of parse5's source that must occur exactly once and its
replacement, applied to a copy in `build/parse5/corrected`. The script
refuses to run them against any other version than the one they were
written for. `make diff-trees` uses the corrected copy and allows no page to
differ; each line it prints also says how many pages equal parse5 as
published.

The cost is stated in the script and in the audit: on those nine rules both
sides were written here from the same text. For four of them the
web-platform-tests copy of the corpus has 15 cases, which parse5 fails as
published and passes corrected. For two more Blink's source agrees with the
text. Three have the text alone, and on one of those (whitespace directly in
a `<template>` that is in a table mode) Blink does what parse5 does.

### 9. How this is verified

| Check | Result |
| :--- | :--- |
| `make diff-trees`: 1,500 pages of each of four kinds (plain; with the extras; those as fragments in 45 kinds of element; those with scripting disabled) against the corrected parse5 | 6,000 of 6,000 trees equal; 5,592 on `main`. Against parse5 as published 5,951, the 49 others being the nine rules |
| The same pages of finding 29 (plain, four seeds) | 6,000 of 6,000; 5,947 on `main` |
| A wider run: five lists of elements, with and without the extras, documents and fragments, scripting on and off, three seeds | 180,000 pages, none differs from the corrected parse5; 1,548 differ from parse5 as published |
| html5lib tree construction (`make conformance`) | 1781 / 1792, ten more than before; the 11 left are all `<select>` |
| The web-platform-tests copy of the corpus, 1,973 cases (not vendored) | 1,868, from 1,851; the 105 left are 88 that need processing instructions, 6 that need a script run during parsing, 11 `<select>` |
| parse5 itself against the vendored corpus (`--check-reference`) | 1,764 of 1,792 as published and corrected alike; the 28 are `<select>` |
| Unit tests (`make test`) | 103 / 103; eight are new |
| 1,746 pages (every distinct html5lib input and the 16 examples) through five output modes, `main` against this | 8,730 outputs; 37 differ, on 9 pages: 8 inputs of corpus cases that pass now, and `examples/nextgen_showcase.html`, whose `<input ... />` no longer closes the row it stands in |
| JavaScript corpus | 840 / 845, as before |
| `make fuzz` | 400 + 400 runs, 0 findings |
| `make deep` | 1,292 runs, 0 failed; the slowest takes 17 s of the 60 allowed |
| `make asan` (ASan + UBSan): unit tests, the JavaScript corpus, both fuzzers, the deep pages at 10,000 levels | 103 / 103; 839 / 845, as before; 400 + 400 runs; 842 runs, 0 failed; no sanitizer report |

`make diff-trees` is part of the conformance job in CI from this phase on,
next to a floor on the corpus score that is now the exact number of passing
cases (1,781) instead of 98%.

### 10. What is still open here

- **`<select>`**: the 11 corpus cases. The standard's new rules for its
  content (customizable `<select>`, `<selectedcontent>`) are not
  implemented, and parse5 does not have them either, so the random pages
  leave `<select>` out.
- **The corpus has moved to web-platform-tests** and grown. Vendoring it
  needs the runner to read `#description` sections, and passing it needs
  processing instructions (`<?x?>` is a node in the standard now, a comment
  here) and scripts that run while the page is parsed.
- **Writing markup.** `innerHTML` read back does not escape `<` and `&` in
  text and writes `<br />`. The parser reads that correctly now, but text
  that holds `<b>` still reads back as an element.
- **One place follows the text against two implementations**: whitespace
  directly in a `<template>` that is in a table mode is collected like text
  in a table, as the standard says; parse5 and Blink treat it as ordinary
  text. The difference is where a text node sits inside a template.

---
