# Phase 25 Architecture: Trees of Any Depth

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 25 Architecture: Trees of Any Depth` section.
> The text below is the section as it stood, unchanged.


### 1. Why this phase existed

Phase 23 made deep JavaScript end in a `RangeError` instead of a crash. The
DOM had no such protection. A file of 300,000 nested `<div>` elements, 3.3 MB
and no script, ended the process with a `SIGSEGV` in every output mode, and a
script got there with `appendChild` in a loop. The fuzzers start from the
html5lib inputs, a few lines each, and change them a few bytes at a time, so
they never came near it.

`tools/fuzz/deep_pages.py` generates the pages they cannot: 145 pages
that each nest or repeat one construct, most of them 200,000 times, run in a
child process with an 8 MB stack, 2 GB of memory and 60 seconds. On `main`
264 of its 392 runs fail. Not all of them fail on depth:
the same pages found a crash in `calc(`, a 60-byte page that crashes the
parser, bitmaps as large as an element asks for, and loops that cost the
square of their input. AUDIT.md has the measurements under findings 24, 27
and 28.

### 2. Who may build a deep tree, and who walks it

| Tree | How deep | What walks it |
| :--- | :--- | :--- |
| Built by the HTML parser | 512 levels at most | anything |
| Built by script (`appendChild`, `attachShadow`) | no limit | DOM code, which does not recurse; layout, painting, SVG and hit testing, which recurse and check the stack |

A browser limits the first and not the second, and so does Litebird. The two
kinds of walker need different answers. A DOM query that stops halfway
returns a wrong result, so the DOM walks became loops. Layout that stops
halfway draws less, which is acceptable for a tree no person wrote, so layout
kept its recursion and asks `stack_is_exhausted()` at every level
(`include/litebird/common/StackLimit.hpp`, from AUDIT.md finding 5). A fixed
depth would not work there: a level of `build_box` is 2.8 KB in a normal
build and 34 KB under AddressSanitizer.

### 3. The nesting limit (`HtmlTreeBuilder`)

`kMaxTreeDepth` is 512, counting `<html>` as 1. It is the number Blink
(`kMaximumHTMLParserDOMTreeDepth`) and WebKit use; the HTML standard has no
limit, and Gecko was not looked at.

Every insertion goes through `place_for_new_node()`, which is the standard's
"appropriate place for inserting a node" passed through
`within_depth_limit()`. When the node would be deeper than the limit, the
place moves up to the nearest ancestor that can take it. The new element is
still pushed on the stack of open elements, and the insertion mode, the list
of formatting elements and the form and head pointers are untouched. So the
parser's state is what the standard says at any depth; only the tree is
flatter. 6,000 nested `<div>` give 6,000 elements: 510 inside one another and
5,490 beside the innermost.

This is Blink's rule (`HTMLConstructionSite::Attach`). WebKit's
(`attachLater`), which pops the current node instead, was implemented first
and abandoned: it leaves the insertion mode naming an element that is no
longer open, and 12 of 1,500 random pages written to sit on the limit crashed.

Three details:

- **Depth is measured in the tree.** The length of the stack is not the
  depth: `</form>` removes the form from the stack and leaves what is open
  inside it, and a `<template>`'s contents hang off the template.
  `element_depth()` caches the depth of each open element in `m_open_depths`.
- **The adoption agency algorithm obeys it.** It moves a block and everything
  in it under a new element, then moves the block's children under a clone.
  Both attachments are limited, and at the limit the clone goes beside the
  block instead of inside it.
- **Order is not always kept at the limit.** An element written inside
  another is attached beside it, and when misnested formatting tags then move
  the outer one, the inner one stays. `<section>x<p>y</p>` inside 300 open
  `<b>` at the limit reads "yx" once the `</b>` tags arrive. All the content
  is there.

### 4. The stack of open elements is indexed

Without a cap on the stack, anything the standard answers by walking down it
costs the depth of the page per token. 20,000 elements left open followed by
20,000 `<table></table>` took 63 s. Earlier work (AUDIT.md, finding 16)
indexed the scope queries. Now every per-token question is answered from
state kept as elements are pushed and popped:

| Question | Kept in |
| :--- | :--- |
| Is a `<p>`, `<li>`, `<table>`... open, and where is the nearest one | `m_open_html_tag_indices`: for each tag, the positions on the stack that hold it |
| Where on the stack is this element | `m_open_positions` |
| Where is the nearest boundary of a scope, and the nearest "special" element | `m_scope_boundary_indices[0..4]`: the default, list item and button scopes, every "special" element, and the same without `address`, `div` and `p` (what `<li>` and `<dd>` stop at) |
| Is any open element SVG or MathML | `m_open_foreign_elements`, a count. Gone since Phase 26: it guarded a walk that is no longer needed, because the SVG and MathML elements that are special are in the indexes above |
| How deep in the tree is this open element | `m_open_depths`, measured only near the limit |

Removing an element from the middle of the stack (the adoption agency,
`</form>`) reindexes from that position up
(`unindex_open_elements_from` / `index_open_elements_from`). The rule for new
code: no loop over `m_open_elements` on a path that runs per token.

### 5. The DOM without recursion (`include/litebird/dom/`)

- `TreeWalk.hpp` has the two walks everything else is built from.
  `for_each_descendant(root, visit)` visits a subtree in tree order and keeps
  its place in a vector, so its depth is limited by memory, like the tree
  itself. `subtree_elements(root)` returns the elements as a list, for walks
  that call into script (custom element callbacks) and may find the tree
  changed under them.
- `~Node` releases its subtree with a loop. A node destroyed while another is
  being destroyed puts its children on a queue (`ReleaseQueue`) instead of
  destroying them, and the outermost destructor works through the queue one
  list at a time. No destructor runs inside the destructor of its parent.
- `text_content`, `inner_markup`, `get_element_by_id`, the tag, class and
  selector searches and `notify_subtree_connected` are loops. `dump` prints
  `kMaxDumpDepth` (1,024) levels and then one line saying how many nodes it
  left out and how deep they go.
- The cycle check in `append_child` walks up from the new parent and down
  from the node being inserted in turn, and stops when either walk ends. From
  the parent alone it cost the depth of the tree for every insertion, which
  made a chain built by script quadratic: 40,000 levels took 13 s.
- `take_children()` detaches all children in one step and
  `replace_children(nodes)` swaps them. `textContent`, `innerHTML`, fragment
  insertion and the fragment parser use them. Removing children from the
  front one at a time made `innerHTML` with 100,000 elements take 9 s.

### 6. Limits on what a value can ask for

- **Bitmaps** (`include/litebird/render/RasterBudget.hpp`). A `<canvas>` or
  `<svg>` is drawn into a bitmap. One bitmap may be 16,384 pixels a side and
  16 million pixels; all that are alive, 64 million. An element over the
  budget keeps its size in layout and is blank, and gets a bitmap when it is
  resized to something that fits. Before, `<svg width="100000"
  height="100000">` was a `std::bad_alloc`.
- **`calc()`** nests 100 levels (`kMaxCalcDepth`, Blink's number) and is
  invalid beyond that. A `calc(` that is never closed ends where the value
  ends; it used to read past it.
- **`var()`** is resolved in one pass over the value. Each unclosed `var(`
  used to scan the rest of the value again.

### 7. Two loops in layout

Both were found by pages that the nesting limit had turned from deep into
long.

- **Inline children.** After a block breaks its inline content into lines,
  each text child is told which of its words are on which line and each image
  where it was put. Every child searched every fragment of every line; one
  pass over the fragments now collects it for all of them. A paragraph of
  100,000 images went from 35 s to 1.3 s.
- **Floats of no height.** Placing a float looks at the band of every float
  before it in the box. A float with no height left a band that no position
  falls inside. It is no longer kept as a band, and what it was still read
  for is noted as the floats are placed: `clear` goes below it and a box that
  contains its floats reaches down to it. 200,000 floats of no size in one
  box went from 20 s to 1.2 s.

### 8. How this is verified

| Check | Result |
| :--- | :--- |
| `make deep`: 145 generated pages, each run three ways (the DOM, layout and paint dumps with a bitmap, in one process; the terminal renderer; for a page without script, the parser alone), then 300 random pages at the nesting limit | 1,292 runs, 0 failed, under 10 minutes on two cores; the slowest run takes 21 s of the 60 allowed. On `main`, 264 of the 392 runs over the generated pages fail: 139 by `SIGSEGV`, 106 out of time, 8 aborted, 7 out of memory, 3 with a wrong result, 1 with more than 4 GB of output |
| The same pages in the ASan + UBSan build, for memory errors: 10,000 levels on a 2 MB stack (`make asan-deep`, part of `make asan`) | 842 runs, 0 failed, no sanitizer report. The same at 50,000 levels, here and in CI, where it added 14 minutes to the job |
| ASan + UBSan: unit tests, the JavaScript corpus, both fuzzers (`make asan-core`) | 95 / 95; corpus 839 / 845, as before; 400 + 400 fuzz runs; no report |
| A chain of 1,000,000 nodes walked and released in a unit test, in both builds | passes with 8 bytes of stack a level, less than a function that calls itself uses |
| Random pages at the limit, from the tags the tree builder treats specially | 4,500 over three seeds: no crash, no element deeper than 512 |
| Unit tests (`make test`) | 95 / 95; seven are new and one was rewritten |
| html5lib tree construction | 1771 / 1792, one more than before (an `<a>` inside an `<a>`) |
| 1,746 pages (every distinct html5lib input and the 16 examples) through five output modes, `main` against this | 8,730 outputs; 4 differ, all one page, whose tree now equals parse5's |
| `make diff-trees` (new): random pages of 3 to 39 misnested tags parsed here and by parse5, an independent parser, trees compared | 1,489 of 1,500 equal, 1,478 on `main`. Over four seeds 5,948 of 6,000, 5,919 on `main`, and no page that was equal there differs now. The 52 that differ are five bugs older than this phase (AUDIT.md, finding 29) |
| JavaScript corpus | 840 / 845, the same five failures as before; 845 / 845 under Node 22 |
| Bugs planted in the float change, one at a time | 6 of 6 caught by a unit test |
| 915 runs over 305 pages of random floats, before and after the float change | identical output |

`make deep` is a CI job of its own. The random pages are generated from a
fixed seed (`--seed`), so a failure names a page that can be generated again.
In its first two runs that job took 8 and 12 minutes, 7 and 10 of them in
`make deep`. It runs beside the sanitizer job, which went from 9 minutes to
11, and the pipeline as a whole from 11 minutes to 13.

### 9. What is still open here

AUDIT.md, finding 28, has the list with measurements. In short:

- **A block inside an inline box is not laid out**
  (`<a href="#">x<p>y</p></a>` paints only "x"). Fixed in Phase 36: the inline
  is split around the block, so the block paints. The nesting limit makes it
  easier to meet.
- **Many floats with a size in one box** used to cost far more than their
  number: 10,000 took 46 s, and past 512 slide steps a float was placed
  where it stood, running out of the box to the right. Float placement was
  rewritten as a sweep of the band edges: the float lands at the exact bottom
  edge of the float in the way, at any row, with no step cap; 10,000 sized
  floats run end to end in about 5 s. (AUDIT.md, finding 28.)
- **A tree a script built deeper than the stack reaches** is laid out as far
  as the stack goes.
- **Smaller quadratic loops**: removing the first child of very many, one at a
  time; very long runs of spaces inside a selector or a CSS function; an
  element with tens of thousands of attributes.
- **Memory.** A word and an inline element with a word in it cost about
  13 KB between them (1.3 GB for 100,000), because each box, line item and
  fragment copies a 920-byte style (AUDIT.md, finding 12).

---
