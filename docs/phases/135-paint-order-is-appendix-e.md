# Phase 135 — paint order is Appendix E

Task row: **Paint order is document order, not Appendix E** (added by Phase 122,
finding 127). Finding 140.

Phase 122's pixel oracle measured floats and `transform` for the first time and
found both wrong: a float was painted under the background of the block after
it, and a transformed element was painted under a later sibling that its ink
overlapped. Both were recorded with a reason and handed on as a row of
`AGENTS.md` rather than fixed in passing. This is that row, and the demonstration
that the ledger works in both directions: the moment the fix landed, the harness
printed `FIXED` against the float page and refused to let the entry stay.

## 1. What was wrong

`DisplayList::build_recursive` painted in document order: a box's chrome, its
inline content, then its children in order with three `z-index` buckets among
them. CSS 2.1 Appendix E has seven steps, and three were missing entirely —
negative `z-index` children, the backgrounds of in-flow blocks as a group, and
floats.

| Page | Before | After |
| :--- | :--- | :--- |
| `a_float_is_painted_above_the_block_that_follows_it` | blue block's background over a red float, `(10,10)` blue | red, as step 4 over step 3 |
| `transform_translate_moves_the_ink_and_not_the_layout` | later block's blue over the moved red ink at `(20,45)` | red, as a stacking context in step 6 |
| every page of blocks only | unchanged | unchanged |

The third row is the reason this was worth doing carefully: the phases must be
a no-op for the tree's 296 suites and 23 passing reftests, or the change is not
a fix.

## 2. What changed

`src/render/PaintCommand.cpp`, 253 → 372 lines, so 250 lines of that file read
differently: the emission code for shadows, backgrounds, borders, inline
fragments, text and images is moved into `emit_chrome` and `emit_contents`
unchanged, and the traversal around it is replaced by one function per Appendix
E step:

```
paint_atom(box)                     transform + clip, chrome, contents, then phases
  paint_stacking_contexts(negative)   step 2
  paint_block_backgrounds             step 3
  paint_floats                        step 4
  paint_inline_contents               step 5
  paint_stacking_contexts(positive)   steps 6 and 7
```

`paint_role` classifies a child as `Block`, `AtomicBlock`, `Float`, `OutOfFlow`
or `Inline`, and each step's walker recurses only through `Block`, so a step
cannot accidentally paint another step's boxes.

The clip/atom compromise, and what it costs, is §4 of finding 140. It is a
property of a flat display list with push/pop clipping, not an oversight, and it
is written down where the next person will look for it.

## 3. Verification

```
$ make -j2 build/litebird                       # clean
$ python3 tools/conformance/render_check.py --binary build/litebird
...
FIXED a_float_is_painted_above_the_block_that_follows_it
       it passes now -- take the known_failure entry out of
       tests/render/reftests.json and let it count
...
Render reftests: 22/27 pages match their reference pixels; 5 more fail as
recorded, with a reason in the corpus (27 of 27 accounted for)
$ # after the entry came out, and the transform page's assertions were re-derived:
Render reftests: 23/27 pages match their reference pixels; 4 more fail as
recorded, with a reason in the corpus (27 of 27 accounted for)
$ # and after rebasing onto the cross-axis work of Phase 134 (36 pages, 133 assertions):
Render reftests: 33/36 pages match their reference pixels; 3 more fail as
recorded, with a reason in the corpus (36 of 36 accounted for)

$ make test
All 296 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).
```

One suite test came with it, `paint_order_follows_the_appendix_e_steps`, which
prints the six colours it saw in the order it saw them and asserts each one's
step; run against the old display list it prints `#ff0000` (the float) between
the two blocks and fails with the step that should have been there. That
negative control is in finding 140 §2.1. The suite count moved 296 → 297.

The corpus grew by two pages and twelve assertions (`28 pages, 112
assertions`), and the second page is the clip leak of §3.3;
the remaining four `known_failure` pages are the cross-axis alignment row
(three, claimed separately) and the rounded clip.

### 3.1 A reftest assertion of mine was wrong again

The transform page still failed after the fix, and the reason was mine: it
asserted that the whole 100×10 strip at `y=40` was blue, but the moved red ink
legitimately covers `x=20..59` of that strip. The assertion now says **where**
blue is — outside the ink — and that `(20,45)` is red, which is the stacking
context doing its job. That is the fourth assertion in two phases that measured
the wrong thing while looking like an engine bug; the corpus's demand that every
assertion carries a `describe` a specification can be checked against is what
makes those cheap to find.

### 3.2 The regression the suite caught

Painted text twice, at the same point, for any box with an inline formatting
context: the walkers painted a box's lines in step 5 and then recursed into its
children, where the child's own lines painted the same run again. The old code
`return`ed at that point; the new one did not. Invisible in a PNG, so the pixel
oracle could not see it — the shaping test that measures rightmost ink caught
it. The walkers now stop at a box with line boxes. Finding 140 §3.

### 3.3 A clip that leaked, fixed by construction

`overflow: hidden` on a block **with text** left its clip pushed for the rest of
the document, because the inline-formatting-context path returned before the
pops. Everything after such a block was clipped to it — `(5,40)` of a following
red block was white. `paint_atom` pushes and pops in one scope, so the paths
cannot diverge; the page `a_clip_does_not_leak_past_the_block_that_set_it` was
checked against the old binary and fails there. Open pull request #158 had found
the same leak independently and patched it with an `end_box()` lambda in the old
function; whichever merges second drops that hunk.

## 4. What is still open

- The clip/atom compromise (finding 140 §4) and the rest of finding 140 §5.
- `overflow: hidden` clipping to the rectangle rather than the radius — the
  third row from finding 127, still unclaimed, still a `known_failure` page.
- Cross-axis alignment — claimed by another pull request while this was in
  flight; its three pages stay `known`.
- No *reftest* yet has two `z-index` siblings in different subtrees, or a
  negative `z-index` child of a positioned parent: the new suite test asserts
  the order of all four added steps on one page, but a pixel page for a
  negative `z-index` inside a plain block (hoisted to the stacking context) and
  for two `z-index` siblings in different subtrees would test what §4 of
  finding 140 compromises on.
