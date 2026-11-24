# Phase 274 — a line is not split around an inline-block

`display: inline-block` was laid out as a block-level box. Every parent that
held one in mixed content was rebuilt as anonymous block / block / anonymous
block, so a line containing a badge, a button or a `<kbd>` key cap became three
lines. The engine already had the machinery to place one on a line; only the
predicate that decides "does this child split the line?" disagreed.

## What was wrong

`LayoutTree.cpp`'s `is_block_level_box()` answered from the **box type**:

```cpp
bool is_block_level_box(const LayoutBox& box) {
    return box.box_type == BoxType::BlockNode || box.box_type == BoxType::FlexNode || ...
}
```

`display: inline-block` is built by `build_boxes()` as a `BlockNode` — its
contents really do form a block container — so it matched. Two call sites then
did what the answer said:

- `normalize_block_children()` saw a parent with both "block" and "inline"
  children and wrapped each inline-block in an anonymous block;
- `split_inline_box_around_blocks()` lifted each one out of the line it was on.

CSS 2.1 §9.2.2.1 makes an inline-block an **atomic inline-level** box, and
§9.2.1.1 says only a *block-level* box interrupts an inline formatting context.
An inline-block sits on the line exactly like a replaced element and wraps only
at a soft wrap opportunity beside it.

## The engine already knew

Nothing else needed to change, which is what made the fix one predicate:

- `LayoutBox::establishes_ifc()` already counts an inline-block child as inline
  content, with the §9.2.2.1 citation in the comment;
- `collect_items()` already lays such a child out and emits a single atomic
  item, and already guards an enclosing inline box against painting its
  background over the child's own `paint_atom`.

So a box that stayed on the line was correctly painted; it was simply never
given the chance.

## The fix

```cpp
if (box.style && box.style->is_inline_block()) return false;
```

The guard names `inline-block` rather than deferring to
`ComputedStyle::is_block_level()`. That predicate looks like the right
abstraction, but it also leaves out `table-caption`, `table-column` and
`table-column-group`, which CSS 2.1 §17.4 *does* define as block-level —
adopting it wholesale would have quietly changed whether a caption interrupts a
line, which is not this phase's subject and which no test here grades. The
measured caption layout (a `<caption>` inside its `<table>`) is identical either
way, so the broader change would have been unmeasured in the same sense as the
two dead ends in Phase 267. It is not in the commit.

## What it measured

`body { font: 16px/20px }`, 600px wide. Children of a `<p>` or `<div>`:

| case | before | after |
| --- | --- | --- |
| `x Tab y` (plain text) | 20.0 | 20.0 |
| `x <kbd style="display:inline-block">Tab</kbd> y` | **60.0** | **20.0** |
| …with `padding: 0.1em 0.3em` | **63.2** | **23.2** |
| …with padding and a 1px border | **65.2** | **25.2** |
| the inline-block alone in a div | 25.2 | 25.2 |
| a real `<div style="height:10px">` sibling | 50.0 | 50.0 |

The everyday case is the worst one, because `<button>` has a UA default of
`display: inline-block`:

| markup | before | after |
| --- | --- | --- |
| `Click <button>OK</button> to continue.` | **60.0**, button at (8, **28**) | **20.0**, button at (**51.2**, 8) |
| `Status: <badge> since yesterday.` | **60.0**, badge at (8, **104**) | **20.0**, badge at (**69.5**, 44) |
| two badges inside one sentence | **100.0**, both stacked at x=8 | **20.0**, side by side |
| `y` of the next block on the page | **268.0** | **108.0** |

Every occurrence of an inline-block inside running text cost the document 40px
of vertical space and put the box on a line of its own underneath the words that
were meant to surround it. `Click OK to continue.` was three stacked blocks.

The `1.6px` by which `#c` and `#d` sink relative to their line is correct
baseline alignment: the inline-block's own `padding-top` pushes its text
baseline down, and the line grows to hold it.

## What is still open

- **`baseline` vertical alignment of an inline-block** is not adjusted for
  sinking: the line grows (20.0 → 23.2 → 25.2) but the pre-fix sink of 1.6px is
  retained rather than recomputed against the new line height. Corpus reftests
  do not cover the case, so this is recorded, not claimed fixed.
- **Whitespace between inline-block siblings** collapses to a single space
  rather than being removed; correct for `display: inline-block`, but it means a
  tabs row assembled without newlines in the markup still gets no gap, which is
  also correct. Not a defect — noted so the next reader does not "fix" it.

## Verification

- `tests/suites/274-an-inline-block-sits-on-the-line.cpp` — one line rather
  than three, the box on that line rather than below it, the box after the text
  that precedes it rather than stranded at the left edge, two boxes sharing a
  line, and the control that a genuinely block-level child still rebuilds the
  parent as anonymous block / block / anonymous block. **With the fix reverted
  it fails on `sentence->line_boxes.size() == 1u`**; with it, it passes.
- **414/414** suites (`make test`), measured on the rebased tree. The count
  follows `main`, which moved twice while this was in flight (#332, then #322
  and 27 more); it is not a property of this change.
- `make wpt` **9569/9824, 0 fail and unlisted** — unchanged.
- `make render-conformance` **64/66** and **40/41**. These are *not* this
  phase's gains: `main` already scores 64/66 and 40/41 without this change
  (measured by reverting, rebuilding and re-running). The fix is reftest-neutral.
- `make shaping-oracle` untouched: 11 match / 2 mismatch, the residual two being
  held by an open pull request that owns `src/render/FontRenderer.cpp`.
