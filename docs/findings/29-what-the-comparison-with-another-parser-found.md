# 29. ✅ What the comparison with another parser found

> Moved out of `AUDIT.md` — written there as a `## 29. ✅ What the comparison with another parser found` section.
> The text below is the section as it stood, unchanged.


The corpus is 1,792 inputs somebody wrote. The work on finding 24 needed to
know more than a corpus score: that a rewritten stack of open elements still
builds the same trees for pages nobody wrote. So
`tools/conformance/diff_trees.py` makes such pages (3 to 39 start tags, end
tags, comments and pieces of text in random order, from the elements the
tree builder has rules of its own for, which leaves most pages misnested)
and compares the tree Litebird builds with the one parse5 builds. parse5 is
an independent parser written from the same standard. On the corpus it gives
the expected tree for 1,764 of the 1,792 cases, and the other 28 all have a
`<select>` in them, whose parsing the standard changed and parse5 8.1 has
not, so the pages leave `<select>` out.

Of 6,000 pages (four seeds), 5,948 got the same tree and 52 did not. The 52
came down to five things. Phase 25 found them and left them alone; **Phase
26 fixed all five**, and the same 6,000 pages now get the tree the standard
asks for (finding 30 says what that is measured against).

| Reduced page | The standard, and parse5 | Litebird before |
| :--- | :--- | :--- |
| `<a><svg></a>` | the `<svg>` stays inside the `<a>`, and both are closed | the `<svg>` was moved out beside the `<a>` and got an `<a>` of its own |
| `<svg><title><em></title><tr>` | `</title>` is ignored: with an HTML element open it may only close an HTML `<title>`. The `<tr>` is ignored as well | the SVG `<title>` was closed, and the `<tr>` became an SVG element |
| `<math><mtext><i></math><mtext>` | `</math>` is ignored, the second `<mtext>` goes inside the `<i>` | the `<math>` was closed |
| `<button><div></button>x` | the button is closed, the "x" follows it | the "x" was inside the `<div>` |
| `<i><i><div><i><i></div>x` | two `<i>` are opened again around the "x" | one |
| `<table><em><table><div>`, then a newline | the newline opens the `<em>` again, inside the `<div>` | the newline was in the `<div>` with no `<em>` |
| `<h2><noscript></noscript><frameset>` | the `<frameset>` replaces the body | the `<frameset>` was ignored |

What each one was:

1. **The first three rows are one family**: an end tag reached an element of
   another namespace than the rules handling it expect. Two things were
   wrong. Every SVG and MathML element counted as "special", where the
   standard names nine (`foreignObject`, `desc`, `title`, `mi`, `mo`, `mn`,
   `ms`, `mtext`, `annotation-xml`), so an `<svg>` stopped the adoption
   agency algorithm like a `<div>` does. And the rules matched tag names
   without asking for the namespace, so `</title>` closed an SVG `<title>`.
   Now the special category goes by namespace and name, and every rule that
   names an HTML element asks `html_name()`, which is empty for an element
   of another namespace.
2. **`</button>`** belongs to the end tags that close their element whatever
   is still open inside it, with `</div>` and `</ul>`. It was missing from
   that list and fell through to "any other end tag", which gives up at the
   first special element. `<button><p>a</button>x` put the "x" into the
   paragraph inside the button.
3. **The fourth identical formatting element.** The list of active
   formatting elements keeps three of a kind, and a fourth pushes out the
   earliest of them ("Noah's Ark"). `push_formatting_element()` searched from
   the end and removed the latest.
4. **Text in a table.** Text directly in a table is collected first, to see
   whether it is only whitespace. That rule was applied whenever the
   insertion mode was "in table". It applies when the current node is a
   table, a row group, a row or a template; here the current node is the
   `<div>`, so the newline is ordinary text and the `<em>` is opened again
   around it.
5. **`<noscript>`** set the frameset-ok flag to "not ok". With scripting
   enabled the standard leaves the flag alone.

Three of the five were in the corpus all along, as one failing case each
among the 21: `<a><svg><tr><input></a>` in `adoption01.dat`,
`<button><p></button>x` in `tests20.dat`, and the first case of
`tests23.dat`, which is the `<i>` row with `<font>`. A failing case says a
bug exists. The random pages say how often a page meets it, and `--reduce`
turns it into a few tags to read the standard against.

**How to see them.** `python3 tools/conformance/diff_trees.py --binary OLD
--reduce` with a binary built from before Phase 26, and `--seed 1`, `2` or
`3` for the other pages. It needs Node.js, and the first time npm and the
network to install parse5 into `build/parse5`.

---
