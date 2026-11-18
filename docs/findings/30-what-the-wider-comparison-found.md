# 30. ✅ What the wider comparison found

> Moved out of `AUDIT.md` — written there as a `## 30. ✅ What the wider comparison found` section.
> The text below is the section as it stood, unchanged.


The pages of finding 29 were tags, text and comments. With those five bugs
fixed, the pages were given the rest of what a tree builder reads
(`--extras`: a start tag that closes itself, `type=hidden` and the other
attributes that change a rule, `</br>` and `</p>`, CDATA, U+0000, a doctype
out of place). They were also parsed as fragments, which is what `innerHTML`
does, in 45 kinds of element (`--fragments`), and with scripting disabled
(`--no-scripting`).

| 1,500 pages of each kind | Trees the standard asks for, before | Now |
| :--- | ---: | ---: |
| Tags, text and comments (finding 29) | 1,489 | 1,500 |
| With the extras | 1,371 | 1,500 |
| With the extras, as fragments | 1,371 | 1,500 |
| With the extras, as fragments, scripting disabled | 1,361 | 1,500 |

So 408 of 6,000 pages were wrong, where the plain pages alone had said 11 of
1,500. Most of the difference is the first four rows of the next table,
which are one bug.

### What was wrong

| Page | The standard | Litebird before |
| :--- | :--- | :--- |
| `<p>a<br/>b</p>c` | "a", the break and "b" in the paragraph, "c" after it | the "b" was outside the paragraph, followed by an empty `<p>` |
| `<head><meta charset="utf-8"/><link rel="x"/><title>t</title></head><body>b` | a head with three elements, and a body | the head ended after the `<meta>`, and the `<link>`, the `<title>` and the `<body>` were outside `<html>` |
| `<table><col/><tr><td>x` | a column and a row with a cell | the row and the cell were lost |
| `<ul><li><input type="text" /> label</li></ul>` | the label in the list item | the label after the list item |
| `<nobr><table><marquee></table><nobr>x` | the second `<nobr>` beside the first | inside the first |
| `<template><table><form></table></template>` | the form is kept: there is no form pointer inside a template | dropped |
| `<head><template></template></head><p><frameset>` | a frameset page: the body made for the `<p>` sets the frameset-ok flag back to "ok" | the `<frameset>` was ignored |
| `<head><head>`, then a newline | the second `<head>` is ignored and the newline is in the head | the second `<head>` closed the head |
| `<table>`, a space, U+0000, a space, `<tr>` | U+0000 is dropped and the spaces stay in the table | the spaces were moved out in front of the table |
| `<span><svg><wbr></span>x` | the "x" after the `<span>`: an SVG `<wbr>` is an ordinary element | the "x" inside the SVG `<wbr>` |
| `<table><svg><template><desc><tr>` | the row goes into the table | into the SVG `<template>` |
| `&#X` | the text `&#X` | `&#x` |
| `<!--x--` at the end of the page | a comment "x" | a comment "x--" |
| `<a =>` | an `<a>` with an attribute named `=` | no attribute |
| `</script>` in a fragment for a `<script>`, `</textarea>` in one for a `<textarea>` | text: no start tag was seen, so no end tag ends the text | the end of the text; what followed was parsed as markup |
| `<![CDATA[x]]>` at the top of a fragment for an `<svg>` | the text "x" | a comment |
| Any fragment for a `<template>` | parsed in the "in template" mode | the process ended with `SIGSEGV` |

**A void element written with a slash closed its parent.** `<br/>`,
`<img ... />` and `<meta ... />` are how XHTML wrote an element that has no
end tag, and a large part of the web still writes them so. In HTML the slash
means nothing. The tree builder "acknowledged the self-closing flag" by
popping the current node, but a void element had been popped already, so the
element around it went: the paragraph, the list item, the head. Of the 408
wrong pages, 292 come right when this one line is changed in the old tree
builder and nothing else is. The corpus has no case for it, because no case
there writes a void element with a slash: the same change leaves the old
score at 1,771 of 1,792. `examples/nextgen_showcase.html` in
this repository has an `<input ... />` and was parsed wrongly. And Litebird's
own `innerHTML` writes `<br />`, so markup read from one element and
assigned to another came out different from what went in.

**A fragment for a `<template>`** read the last entry of an empty list of
template insertion modes. This could be reached from the command line
(`--fragment-context=template`) and through
`HtmlParser::parse_fragment_with_context`, not from a page, for the reason
in the next paragraph.

**`innerHTML` did not use any of it.** The fragment parser takes the element
the markup is for, and the corpus runner passes one, which is how the 192
fragment cases of the corpus are scored. `Element::set_inner_html`, which is
what a script's `element.innerHTML = ...` calls, passed `<body>` for every
element. So the cases passed and a page still got:

| Script | Before | Now |
| :--- | :--- | :--- |
| `table.innerHTML = "<tr><td>1</td><td>2</td></tr>"` | the text "12" in the table, no row | a `<tbody>` with a row of two cells |
| `tr.innerHTML = "<td>x<th>y"` | the text "xy" | two cells |
| `svg.innerHTML = '<circle r="4"/><g>...</g>'` | HTML elements, and the `<g>` inside the `<circle>` | SVG elements side by side |
| `style.innerHTML = "a > b { } /* <b> */"` | two nodes, the `<b>` an element | one text node |
| `div.innerHTML = "<form>..."` with the `<div>` in a form | a form inside a form | the inner `<form>` is ignored |

It now parses for the element it is called on (`HtmlParser::parse_inner_html`):
its name and namespace, the encoding of a MathML `annotation-xml`, and
whether it stands inside a form.

That last one cost something the first time. Whether a form is above the
element is a walk to the root, and made at every assignment it turned a
script that nests 30,000 levels through `innerHTML` from 0.2 s into 44 s.
`make deep` has that page and it still passed, at 48 of the 60 seconds a run
may take; the line that names the slowest run is what showed it. The walk is
now made only when the markup has the letters "form" in it, since only a
`<form>` or `</form>` asks, and a unit test bounds the loop.

The rest are smaller. An SVG element that has the name of an HTML one (`rt`,
`template`, `td`, `option`, `wbr`) took the HTML element's rules: implied end
tags, the place a table's rows go, select scope. A second `<nobr>` behind a
marker went through an adoption agency algorithm that returned before doing
what "any other end tag" does. The doctype was printed before the comments
that precede it, which is a fault of the tree dump and not of the tree. An
empty file exited with status 1 and no tree.

### A reference that is itself behind

After these fixes 49 of the 6,000 pages still get another tree than parse5
builds. Each of them is one of nine places where parse5 8.1 does not do what
the standard says:

| Rule | The standard | parse5 8.1 |
| :--- | :--- | :--- |
| A `<form>` in the contents of a `<template>` | kept; no form pointer there | ignored in a table, and never nested (the rule until September 2026) |
| The body made for the first thing after the head | sets frameset-ok to "ok" | leaves it |
| CDATA where the current node is an SVG `<title>`, `<desc>`, `<foreignObject>` or a MathML `<mi>` and the like | text | a comment |
| `<search>` | a special element | an ordinary one |
| Table scope | ends at a `<template>` too | at `<table>` and `<html>` |
| `</tbody>`, `</thead>`, `</tfoot>` in a row | ignored unless that element is open | closes the row if that element or a `<tr>` is open |
| Whitespace directly in a `<template>` that is in a table mode | collected like text in a table | ordinary text |
| A run of U+0000 in SVG or MathML | one U+FFFD each | one U+FFFD for the run |
| A fragment for a `<noscript>`, scripting disabled | markup | text |

A ceiling of "49 known differences" would have hidden a new bug behind an
old excuse, so `diff_trees.py --corrected` compares with a copy of parse5 in
which these nine rules are changed to the standard's (13 edits of a few
lines, listed in the script and applied to a copy in `build/parse5/corrected`
when it runs). Against that copy all 6,000 trees are equal, and `make
diff-trees` fails if one is not.

What vouches for the corrections, since a reference corrected by the party
being checked is worth less than one that was not:

- For the first four rows the web-platform-tests copy of the corpus has
  cases, 15 of them. parse5 as published fails them and the corrected copy
  passes them; on the 1,792 cases vendored here the two give the same trees.
- For table scope and for `</tbody>` in a row, Blink's source does what
  the standard says. For U+0000 in SVG and for the `<noscript>` fragment
  there is the standard's text and no second witness.
- For the whitespace in a template, Blink does what parse5 does, so this is
  the one place where Litebird follows the text against two
  implementations. A unit test pins it, and the difference is a text node's
  position inside a template.
- A wider run: 180,000 pages from five lists of elements (the default one,
  every special element, tables, formatting elements, SVG and MathML), with
  and without the extras, as documents and as fragments, with scripting on
  and off, three seeds. Against parse5 as published 1,548 of them differ;
  against the corrected copy none does.

### What the comparison does not reach

- **`<select>`.** At the time of Phase 26, the standard had changed how `<select>` content is parsed, parse5 had the older rules and so did Litebird (the 11 corpus cases that failed until Phase 49, finding 55, implemented customizable `<select>` and `<selectedcontent>` cloning). Two parsers that were both behind could not check each other; parse5 8.1.1 still keeps the older rules (`--with-select`), while Litebird passes all 1,792 corpus cases.
- **The corpus has moved.** html5lib-tests handed its tree-construction
  tests to web-platform-tests in June 2026, and the vendored copy is the
  last one from before. The web-platform-tests copy has 1,973 cases;
  Litebird passed 1,868 of them after Phase 26 (1,851 before Phase 26; the
  11 `<select>` cases were fixed later in Phase 49). Of the remaining cases,
  88 need processing instructions (`<?x?>` is a node of its own in the
  standard now and a comment here) and 6 need a script to run while the page
  is being parsed. Vendoring that copy is the next step for the scoreboard;
  the runner does not read its `#description` sections yet.
- **Reading `innerHTML`.** The markup Litebird writes does not escape `<`
  and `&` in text, and writes `<br />` where the standard writes `<br>`. So
  text that holds `<b>` reads back as an element. Parsing is this finding;
  writing is not fixed.

---
