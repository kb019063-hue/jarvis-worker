# 27. ✅ What the deep pages found that was not depth

> Moved out of `AUDIT.md` — written there as a `## 27. ✅ What the deep pages found that was not depth` section.
> The text below is the section as it stood, unchanged.


`deep_pages.py` (finding 24) was written to find recursion. Much of what it
found was something else: a page that is hostile by its size, or an old bug
that a generated page happened to walk into. Everything below was on `main`.

**Crashes**

| Input | Before | Now |
| :--- | :--- | :--- |
| `<div style="width: calc(1px">` | `SIGSEGV` | 1px: the `calc(` is closed where the value ends |
| `<template><th><a><button><select><button><tr a=0><button><a>`, 60 bytes | `SIGSEGV`: a read one element before the start of the stack of open elements | parses |
| `<svg width="1e9" height="1e9">` | abort, `std::length_error` | the element keeps its box and is blank |
| `<svg width="100000" height="100000">`, and the same on `<canvas>` | abort, `std::bad_alloc` | the same |
| 20,000 `<svg></svg>`, 180 KB of bitmap each | stopped at the 2 GB the deep pages allow | renders; past the budget they are blank |

- **`calc(`.** The evaluator read past the end of a value that never closed
  its parenthesis. A `calc()` inside a `calc()` also ended the expression
  early: `calc(10px + calc(20px * 2) + (5px + 5px))` was 10px and is 60px.
  Nesting is limited to 100 levels (`kMaxCalcDepth`; Blink's
  `kMaxExpressionDepth` is the same number), and a value nested deeper is
  invalid.
- **The 60 bytes.** No depth is involved. The page gets the parser into the
  "in select" insertion mode with no `<select>` on the stack of open
  elements; "pop until a `<select>` has been popped" then emptied the stack,
  and the adoption agency algorithm read from it. Popping until a given
  element now pops nothing when no such element is open, and the adoption
  agency checks what it indexes. Random pages at the nesting limit found it;
  it reduces to these 60 bytes.
- **Bitmaps.** Every `<svg>` and `<canvas>` was drawn into a bitmap of the
  size the element asked for. One bitmap is now at most 16,384 pixels a side
  and 16 million pixels, and all that are alive at one time 64 million
  (`include/litebird/render/RasterBudget.hpp`). Over that the element keeps
  its size in layout and draws nothing; `canvas.width` reads back what was
  set.

**Wrong results**

- **`<a>` inside `<a>`.** A second `<a>` start tag runs the adoption agency
  for the first and then takes the first out of the list of formatting
  elements. The handler took out whatever stood at the first one's old place
  in the list, which after the adoption agency could be another element.
  `<a><em><div><a>x</div>y` put the last `<a>`, with the "y" in it, outside
  the `<em>` it belongs inside, and `<a X>0<b>1<a Y>2` lost the `<b>` around
  its second `<a>`. The second is an html5lib case, the one gained
  (`tests1.dat`, 108 to 109 of 112). Both trees now equal the ones parse5
  builds.
- **`el.textContent = 'x'`** emptied the element's list of children and did
  nothing else. Each removed child went on naming `el` as its parent:
  `parentNode` was wrong, `isConnected` was wrong, appending `el` to one of
  them was refused as a cycle, and a custom element among them never got its
  `disconnectedCallback`.
- **`textContent`** included the text of comments:
  `<div>one<!--hidden-->two</div>` read "onehiddentwo".
- **Class names with a newline or a tab between them** missed every rule
  whose last compound selector has a class and no id (`.b`, `p.b`,
  `div .b`). The index that picks the candidate rules for an element split
  its class list on spaces only: the tests for the other separators were
  written `'\\t'`, `'\\n'` and so on, constants of two characters that no
  character equals. `class="a` newline `b"` was looked up as one name, so no
  rule for `.a` or for `.b` was a candidate. Markup formatted by a tool
  breaks long class lists this way. No deep page found this one: the
  compiler had been warning about the line on every build.

**Work that grew with the square of the input**

| Page | Before | Now |
| :--- | ---: | ---: |
| `color: var(--x, var(--x, ...`, 20,000 times, never closed | 1.8 s | 0.01 s |
| the same, 200,000 times | not finished after 120 s | 0.12 s |
| `innerHTML` assigned 100,000 elements side by side | 8.8 s | 0.2 s |
| that element emptied again with `innerHTML = ''` | 4.1 s | 0.02 s |
| a fragment with 50,000 children appended | 1.1 s | 0.12 s |
| one paragraph of 100,000 `<img>` | 35 s | 1.3 s |
| one paragraph, `a<br>` 100,000 times | 33 s | 1.3 s |
| one paragraph, `a <b>b</b> ` 100,000 times | 130 s | 3.6 s |
| 200,000 floats of no size, side by side in one box | 20.3 s | 1.2 s |

One cause each:

- `var(`: every unclosed `var(` scanned the rest of the value again. The
  value is now resolved in one pass (the limit of 12 references inside one
  another is unchanged).
- `innerHTML`: the parser moved its nodes from its root element to a
  fragment, and the fragment gave them to the caller, by taking them off the
  front of a list one at a time. Emptying an element did the same. Both hand
  over the whole list (`Node::take_children`).
- The paragraph: after breaking the lines, each text and image child searched
  every fragment of every line for its own pieces. One pass over the
  fragments now notes what each child has on each line.
- The floats: placing a float looks at the band of every float before it,
  and a float of no height left a band that no position can fall inside.
  Such a float still counts for `clear` and for the height of a box that
  contains its floats; it is no longer kept as a band.

The parser's walks down the stack of open elements and the cycle check of
`appendChild` belong here too; they are in finding 24.

**Behaviour that changed**, besides crashes that no longer happen:

- `textContent` leaves out comments, and `textContent = ''` leaves the
  element with no children (it had one empty text node).
- Children removed by `textContent`, `innerHTML` or a replaced fragment have
  no parent afterwards, custom elements among them get `disconnectedCallback`
  when the element was in the document, and a `MutationObserver` gets one
  record that lists them all.
- Appending a fragment that contains the node it is appended to throws
  `HierarchyRequestError` and leaves the fragment as it was.
- An unclosed `calc(` is a value, not a crash; nested `calc()` is evaluated.
- A `<canvas>` or `<svg>` larger than the bitmap budget is blank.
- Rules for a class apply when the class list is separated by newlines or
  tabs.

**Tests.** The pages are part of `deep_pages.py`. Unit tests:
`css_values_survive_hostile_nesting`, `canvas_and_svg_bitmaps_are_bounded`,
`replacing_children_detaches_the_old_ones`,
`inline_children_know_their_lines_in_a_paragraph_of_any_length`,
`floats_of_no_height_cost_nothing_and_still_count`; the 60 bytes and the
`<a>` case are in `html_parser_nesting_limit_holds_for_every_kind_of_element`,
and the class lists in `style_resolver_indexed_candidates_preserve_cascade`.
Six bugs planted in the float change, one at a time, are each caught by a
unit test. With the two layout changes, 1,746 pages (the examples and every
distinct html5lib input) give the same output as before in five modes, 8,730
runs, and so do 305 pages of random floats in three.

---
