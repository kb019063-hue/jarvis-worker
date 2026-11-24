# Finding 278 — the line that was split around an inline-block

**Severity:** correctness, and enormous in aggregate. `LayoutTree.cpp` decided
"does this child interrupt an inline formatting context?" from the child's *box
type*. `display: inline-block` is built as a `BlockNode`, so every inline-block
in running text was lifted onto a line of its own and the line around it was
split in three. A single sentence containing a `<button>` rendered 60px tall
instead of 20, and pushed everything after it down by 40px.

**Found by:** chasing the APG tabs holdout page in Phase 243, and then looking at
a layout dump instead of a screenshot. The dump named it in one line —
`BlockBox #b` holding `AnonymousBlock / BlockBox / AnonymousBlock` where it
should have held one line.

## Why it survived

The venn diagram of things that hide a layout bug is fully shaded here:

- **It looks almost plausible.** A row of `display: inline-block` tabs — the
  pattern the name suggests — is *only* inline-blocks, and a parent whose
  children are all inline-blocks takes the inline-formatting path anyway. So the
  canonical demo page renders correctly. Only mixed content breaks, and mixed
  content is not what people write when they set out to test `inline-block`.
- **The wrong answer was in one helper.** `is_block_level_box()` returned a
  value that was right for six of the seven box types it listed, so any test
  aimed at flex, grid, tables or anonymous blocks passed.
- **The rest of the engine agreed with the spec.** `establishes_ifc()` treated an
  inline-block as inline content and `collect_items()` had a dedicated atomic
  branch for it, complete with a paint-order guard. Two of three components were
  correct, which reads as "this works" to anyone skimming.
- **The reftest corpus does not contain the shape.** 64/66 and 40/41 before and
  after: this fix moves no reference pixel, so the scoreboard that exists to
  catch layout regressions could not see the defect or the repair.

## The number

`Click <button>OK</button> to continue.` in a 600px body at `16px/20px`:

```
before  p height 60.0   button at (8.0, 28.0)     next block at y=268
after   p height 20.0   button at (51.2, 8.0)     next block at y=108
```

The button was not merely mispositioned — it was painted on a separate line
*below* the words `Click` and *above* `to continue.`, in the order of the
anonymous blocks, so the sentence read as three stacked fragments.

## The lesson

A predicate that answers "what does this box do in flow?" from the box's
*construction type* is asking the wrong question. `block`/`inline` is a property
of `display`, not of which struct got built to hold the children; `inline-block`
is precisely the value where those two diverge, and it is the one that was
missing. When a helper's name says one thing and its body reads the other, the
body is where the next bug is.

## What is still open

The baseline adjustment of a sinking inline-block (the line grows to hold its
top padding, but the box's sink is not recomputed against the new line height).
Recorded here rather than fixed, because no test in the tree grades it and the
same rule from Phase 267 applies: a change no oracle here can distinguish from a
no-op is a change that will be maintained as though it mattered.
