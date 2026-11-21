# Finding 140 — the display list had no phases

Phase 135. Status: **resolved** for floats, transformed elements and the
stacking contexts a box owns. §4 is the exact shape of the compromise a flat
display list forces, and §5 is what is still not painted in the right order.

## 1. What was wrong

`DisplayList::build_recursive` was a tree walk. It emitted a box's shadow,
background and borders, then its inline content, then recursed into its
children in document order, with the children grouped into three z-index
buckets. Between them those rules are right for a page made only of blocks, and
they were wrong for everything else:

```
$ # .f { float: left; width: 50px; height: 50px; background: #ff0000 }
$ # .b { width: 100px; height: 40px; background: #0000ff }
$ # the pixel at (10, 10)
before: (0, 0, 255, 255)     the block's blue, over the float
after:  (255, 0, 0, 255)
```

```
$ # .t { width: 40px; height: 40px; background: #ff0000; transform: translate(20px, 10px) }
$ # .n { width: 100px; height: 10px; background: #0000ff }
$ # the pixel at (20, 45), where the moved ink overlaps the next block
before: (0, 0, 255, 255)
after:  (255, 0, 0, 255)
```

Document order is not the order CSS 2.1 Appendix E asks for. The order is:

| Step | What paints |
| :--- | :--- |
| 1 | the element's own background and borders |
| 2 | child stacking contexts with a **negative** `z-index` |
| 3 | in-flow, non-inline-level, non-positioned descendants — their backgrounds and borders |
| 4 | **non-positioned floats** |
| 5 | inline-level, non-positioned descendants |
| 6 | child stacking contexts with `z-index: 0`, and positioned descendants with `z-index: auto` |
| 7 | child stacking contexts with a **positive** `z-index`, ascending |

The walk implemented 1 and a per-box approximation of 5, 6 and 7, and nothing
of 2, 3 and 4. Phase 122's pixel oracle found it and recorded both pages as
`known_failure`; `AGENTS.md` carried it as a row rather than fixing it in
passing. This phase is that row.

## 2. What the fix was

`src/render/PaintCommand.cpp` now emits in the steps above. The walk is split
into small functions, one per step, and each recurses only through boxes that
belong to its own step:

- `emit_chrome` — step 1: shadow, background, borders, `empty-cells` rules.
- `emit_contents` — the box's own ink: its inline formatting context, its text,
  or its image.
- `paint_stacking_contexts(box, list, negative)` — steps 2 and 6+7: the stacking
  contexts a box owns, in document order within each `z-index`, ascending.
- `paint_block_backgrounds` — step 3, recursing through in-flow block boxes and
  stopping at anything a later step owns.
- `paint_floats` — step 4, each float painted as a stacking context of its own.
- `paint_inline_contents` — step 5.
- `paint_atom` — one box as a unit: transform and clip push, chrome, contents,
  the phases, pops.

The visual change that reaches ordinary pages, not just the two reftests, is
that a block's background no longer covers the *text* of a block before it.
That is step 3 before step 5, and it is pinned by a new page,
`a_blocks_background_does_not_cover_earlier_inline_ink`: an inline image in a
line, with a blue block pulled up over it by a negative margin, must still show
the image.

## 2.1 The suite test, and the control that makes it worth having

The pixels prove the fix on two pages; a suite test proves the *order* on a page
built to exercise all four steps at once,
`paint_order_follows_the_appendix_e_steps`. Six boxes, deliberately in the
document order the old walk got wrong: a `z-index: -1` box, two blocks, a float
between them, a transformed box, and a `z-index: 3` box. It prints the sequence
it saw:

```
[paint order] #ffff00 #0000ff #ff00ff #ff0000 #00ff00 #00ffff
```

and the same test against the old display list, with nothing else changed:

```
[paint order] #ffff00 #0000ff #ff0000 #ff00ff #00ff00 #00ffff
      paint order: #ff00ff should be step 3: second block (step 3: second block), but #ff0000 was
```

The float between the two blocks is the whole bug in one line. A test that has
not been seen to fail is a test of nothing.

## 2.2 A second bug fell out: the clip that leaked

The old code pushed a clip and a transform when it entered a box, and popped
them at the end of the box — except on the inline-formatting-context path,
which `return`ed before the pops. A block with `overflow: hidden` **and text**
therefore left its clip on the stack, and every box painted after it, to the end
of the document, was clipped to it:

```
$ # .p { width:100px; height:30px; overflow:hidden }   text in it
$ # .q { width:100px; height:30px; background:#ff0000 }
$ # the pixel at (5, 40), which is inside .q
before: (255, 255, 255, 255)    .q was clipped away by .p's leaked clip
after:  (255, 0, 0, 255)
```

`paint_atom` pushes and pops in one scope, so the paths cannot diverge. The
corpus pins it with `a_clip_does_not_leak_past_the_block_that_set_it`, which was
verified against the old binary and fails there. Another open pull request
(#158) had found the same leak and fixed it with a `end_box()` lambda inside the
old function; that hunk is subsumed, and the page is the claim both of them were
making.

## 3. The regression the existing suite found

The rewrite painted the text of a `<p>` **twice**, at the same point:

```
$ ./build/litebird --dump-paint arabic.html
Total commands: 3
  DrawText: (0, 0) "ال" Color: #000000
  DrawText: (0, 0) "ال" Color: #000000
```

The old code `return`ed as soon as a box had line boxes, because an inline
formatting context paints its children into its own lines. The new walkers
painted a box's lines in step 5 and then *also* recursed into its children,
where the child box's own line boxes painted the same run again. It was
`canvas2d_and_html_paint_arabic_cursive_and_contextual_glyphs` — a shaping test
that measures the rightmost ink column — that caught it, not the pixel oracle,
because drawing the same glyphs twice is invisible while drawing them in the
wrong order is not:

```
test_suite.cpp:4640: Assertion `right_lig > 0 && right_zwnj > right_lig + 5' failed.
```

Every walker now stops at a box that has line boxes, which is what the removed
`return` did. After that, all 296 engine suites pass.

## 4. The compromise, exactly

The display list is a flat `std::vector<PaintCommand>`, and clipping is a
push/pop pair in that stream. Two phases of one stacking context therefore
cannot be *interleaved* across a clip: if a box pushes a clip in step 3 and its
inline content is painted in step 5, the popped clip has already been left
behind by the time step 5 runs, and any box between the two would be clipped by
it.

So a box that **clips** (`overflow` other than `visible`, or `contain: paint`)
or that **establishes a stacking context** is painted as one atom
(`paint_atom`): its own chrome and ink, then its own phases, all inside its
transform and clip. Floats and positioned descendants are hoisted only as far
as the nearest such atom.

What that costs, in the terms of Appendix E: an in-flow block that clips is
painted in step 3 as a whole instead of its background in step 3 and its inline
content in step 5. A float that is a sibling of that block is therefore painted
above the clipped block's *text*, where the spec would put it above the block's
background and below its text. It is wrong in a narrower way than the bug this
phase fixed: the common shapes — a float beside text, a float under `clear`,
a transformed box, a positioned box over a block — all come out right.

## 5. What is still open

- The clip/atom compromise of §4.
- `overflow: auto` scroll containers still scroll the viewport, not the box, so
  there is no inner content to order (a `LayoutTree` limitation, not a paint
  one).
- `inline-block` and `inline-table` are painted as atoms wherever they appear;
  they are inline-level boxes and should take part in step 5, which they do,
  but their internal phases are not separated from their parent's.
- Negative `z-index` is hoisted to the nearest atom, which is the nearest
  clipping box as well as the nearest stacking context. Where those differ, the
  spec's answer is the stacking context.
- `overflow: hidden` still clips to a rectangle rather than the `border-radius`
  (the `AGENTS.md` row from finding 127 is still open; it is a `BitmapRenderer`
  change, not a paint-order one).
- `opacity`, `filter` and `will-change` do not create stacking contexts here
  because they do not exist in the engine yet.
