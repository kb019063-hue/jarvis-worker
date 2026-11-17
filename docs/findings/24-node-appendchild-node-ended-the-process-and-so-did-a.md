# 24. ✅ `node.appendChild(node)` ended the process, and so did a very deep tree

> Moved out of `AUDIT.md` — written there as a `## 24. ✅ `node.appendChild(node)` ended the process, and so did a very deep tree` section.
> The text below is the section as it stood, unchanged.


```js
var d = document.createElement('div');
d.appendChild(d);
```

The DOM accepted this. The node became its own child and its own parent, and
the next thing to walk the tree (`textContent`, serialisation, layout, the
destructor) recursed until the stack was gone. Appending a node to one of its
own descendants did the same in two steps. The DOM standard forbids both.

`Node::append_child` and `Node::insert_before` are the only two places a tree
grows, and both now refuse an insertion that would make a node its own
ancestor, counting a shadow tree as part of its host. The script bindings throw
`HierarchyRequestError` (code 3), as a browser does. The check costs nothing
for a node with no children, which is every node the HTML parser appends.
Test: `dom_insertion_cannot_make_a_node_its_own_ancestor`.

### Depth (fixed in Phase 25)

A tree does not need a cycle to be too deep to walk. The DOM and the code
that walked it were recursive throughout (`textContent`, `innerHTML`, the
id, tag and selector searches, the dumps, the destructor), and only the
layout tree builder checked the stack. The mutation fuzzers start from the
html5lib inputs, a few lines each, and change them a few bytes at a time.
They never come near the tens of thousands of levels this takes, which is
why they reported nothing.

`tools/fuzz/deep_pages.py` generates the pages they cannot. Each of its
145 pages nests or repeats one construct, most of them 200,000 times, and is
run in a child process with an 8 MB stack, 2 GB of memory and 60 seconds: 49
nest one kind of markup, 29 a CSS value or rule, 43 build their tree from
script, 14 leave 200,000 elements open and then send 100,000 tokens, 5 put
100,000 or 200,000 children in one paragraph, and 5 ask for bitmaps larger
than memory.

| | Before | Now |
| :--- | :--- | :--- |
| 300,000 nested `<div>`, a 3.3 MB file, in each of five output modes | `SIGSEGV` after 0.4 s | exit 0 in 3.7 to 5.0 s |
| `appendChild` in a loop, 20,000 levels | 3.2 s | 0.07 s |
| the same, 40,000 levels | 13.2 s | 0.12 s |
| the same, 200,000 levels | not finished after 120 s | 0.42 s |
| 20,000 levels, then `textContent`, `innerHTML` and `querySelectorAll` on the top, attached to the document, removed, dropped | `SIGSEGV` | 0.12 s |
| the same, 200,000 levels | not finished after 120 s | 0.68 s |
| Runs of the deep pages that fail, of 392 | 264, on 110 of the 145 pages: 139 `SIGSEGV`, 106 out of time, 8 aborted, 7 out of memory, 4 others | 0 |

Three things were done, because a tree gets deep in two ways and is walked by
two kinds of code.

**1. The parser stops nesting at 512.** Nothing the HTML parser attaches is
deeper than `HtmlTreeBuilder::kMaxTreeDepth`, counting `<html>` as 1. An
element that would be deeper is attached to the nearest ancestor that can
still take it, which makes it a sibling of the element it was written inside.
Of 6,000 nested `<div>` elements all 6,000 are in the tree: the first 510
inside one another, the other 5,490 beside the innermost of those. The text
is in the last one, and a paragraph after the closing tags is a child of
`<body>`, where it would be without the limit.

The HTML standard has no such limit. Blink and WebKit both have one, and both
set it to 512. (Gecko was not looked at.) They differ in the rule, and the
difference matters:

- WebKit closes the current node when the limit is reached
  (`HTMLConstructionSite::attachLater`). This was implemented first. It
  leaves the insertion mode describing an element that is no longer open:
  "in select" with no `<select>` on the stack. Of 1,500 random pages written
  to sit on the limit, 12 then crashed, one of them by a road that needs no
  depth at all (finding 27).
- Blink changes only where the node is attached
  (`HTMLConstructionSite::Attach`). The stack of open elements, the insertion
  mode and the list of formatting elements stay what the standard says they
  are, at any depth. This is what Litebird does now. Of 4,500 random pages at
  the limit, none crashes and none has an element deeper than 512; 300 more
  run in CI.

Two things go beyond Blink's rule. The depth is measured in the tree and not
as the length of the stack, because the two differ: `</form>` takes the form
off the stack and leaves what is open inside it, so `<form><div></form>`
repeated nests two levels for each element the stack gains. And the adoption
agency algorithm, which moves a block with everything in it under a new
element, is held to the same limit: it used to carry content past it.

With the stack no longer capped, every question the standard answers by
walking down it would cost the depth of the page for every token. Finding 16
gave the scope queries an index. The walks that were left ("which `<li>` does
this `<li>` close", "what does this end tag close", "is anything on the stack
foreign", resetting the insertion mode) now have one too. 20,000 elements
left open, followed by 20,000 times the same token, parsed and printed as a
tree:

| Token | Before | Now |
| :--- | :--- | :--- |
| `<table></table>` | 63.1 s | 0.79 s |
| `<li>x</li>` | out of memory: killed by the system at 3.6 GB, after about 40 s | 1.19 s |
| `<b>x</b>` | out of memory: killed at 3.6 GB, after about 15 s | 1.16 s |
| `</x>`, an end tag that closes nothing, after `<span>` | 14.6 s | 0.35 s |

The dump of such a tree used to grow with the square of its depth as well,
because each line is indented by how deep its node is (finding 16). With the
limit a line is indented by at most 1 KB.

What the limit does not always keep is the order of content. An element
written inside another is attached beside it, and misnested formatting tags
can then move the outer one: `<section>x<p>y</p>` inside 300 open `<b>` at
the limit, followed by their end tags, reads "yx". Everything written is in
the tree. This was not compared with a browser. Below the limit nothing
changes: the html5lib corpus, whose deepest tree has 15 levels, went from
1770 to 1771 of 1792, the one being a parser bug found on the way (finding
27).

**2. A tree built by script has no limit, so nothing that walks the DOM
recurses.** A browser lets a script `appendChild` 200,000 levels deep, and so
does Litebird. The destructor releases a subtree with a loop and a list of
what is left to release, and `textContent`, `innerHTML`, `querySelector(All)`,
`getElementById`, the tag and class searches, the propagation of "is
connected" and the tree dumps are loops over
`include/litebird/dom/TreeWalk.hpp`. A stack check would not have done here:
a `textContent` that stops halfway returns a wrong answer, where layout that
stops halfway draws less.

The check that an insertion makes no cycle (the first half of this finding)
walked from the new parent to the root, which made building a chain cost the
square of its depth: the 3.2 s and 13.2 s above. It now walks up from the
parent and down from the node in turn, and stops when either ends. For a node
with few descendants that is at once.

**3. Layout, painting, SVG rendering and hit testing still recurse, and ask
`stack_is_exhausted()` at every level** (finding 5). What lies deeper than
the stack reaches is not laid out. For a parsed page in a normal build that
cannot happen: 512 levels of `build_box` are 1.4 MB. A tree that a script
built deeper than that is in the DOM, answers every query, and is laid out as
far down as the stack goes. `--dump-dom` prints 1,024 levels and then says
how many nodes it left out and how deep they go.

**How it stays fixed.** `make deep` runs the deep pages and 300 random pages
at the nesting limit, and is a CI job. At 200,000 levels on an 8 MB stack a
walk has 42 bytes of stack for each level, which stops every recursion but
one with a very small frame. The unit test
`dom_trees_of_any_depth_can_be_walked_and_released` closes that gap for the
DOM: it walks and releases a chain of a million nodes, which leaves 8 bytes a
level, and a function that calls itself uses at least 16. It runs in the
normal build and in the sanitizer build. From script, a chain of 2,000,000
(4 bytes a level) is built, read through `textContent`, `innerHTML` and
`querySelectorAll`, attached, removed and dropped in 10 s; it takes 2 GB,
about 1 KB a level.

`make asan` runs the deep pages in the sanitizer build as well, for memory
errors and not for recursion: a twentieth as deep, which keeps a build that
is several times slower to a few minutes, on a quarter of the stack. The
other unit tests are
`html_parser_nesting_limit_holds_for_every_kind_of_element` and
`deeply_nested_markup_does_not_overflow_the_stack`, which used to require
6,000 nested levels to survive and now requires 6,000 elements, none deeper
than 512.
