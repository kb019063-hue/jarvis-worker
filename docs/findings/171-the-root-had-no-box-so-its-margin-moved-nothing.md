# Finding 171 — the root had no box, so its margin moved nothing

## What was wrong

`<html style="margin:30px">` moved nothing. The document tree begins at
`<body>` — `LayoutTree` never builds a box for the root element, and
`place_document_body` positions the body from its own margins, border and
padding. The root's style was simply never consulted, so any margin on it was
parsed, accepted by the style resolver and then discarded.

Measured:

```
<html style="margin:30px"><body><div style="width:100px;height:20px"></div>

                          Chromium 153   Litebird
child's rect                 (38, 38)      (0, 0)
```

38 is the root's 30 plus the body's default 8. The engine applied neither.

`tests/render/body-origin.json` already recorded this as `known_failure`, with
the reference pixels measured against Chromium 153:

```
FAIL html_margin_affects_the_body
  known: The html element has no layout box, so its 30px margin does not
         affect the body.
```

## What changed

`place_document_body` takes the root's used margin and adds it to the offset it
computes for the body. Only a root that states a margin contributes; a document
whose `<html>` has none takes the previous path exactly. Percentages resolve
against the viewport, which is the root's containing block.

CSS 2.1 §8.3 puts the body inside the root's content box and the root's margin
outside it, so both edges move by the whole used margin rather than half of it.

## Verification

- `html_margin_affects_the_body` passes; its `known_failure` note was **removed**
  from the corpus, which the row explicitly requires — a recorded failure that
  starts passing must stop being excused.
- The check was shown to bite: with the two added lines disabled and the note
  already gone, the harness reports `FAIL html_margin_affects_the_body` and
  `4/7` instead of `5/7`. It is a real assertion now, not a recorded one.
- `make render-conformance`: 36/38 in the main suite, unchanged; the two
  remaining known failures are the column-flex and `align-self` rows.
  `body-origin.json`: 5/7, against 4/7 on this branch's base.
- `make test`: 318/318.

## What this does not cover

The other two `known_failure` pages in the same file are untouched and still
fail for their own reasons:

- **Background propagation to the canvas** — an opaque body background should
  paint the whole viewport; the engine paints only the body's box.
- **General margin collapse** — an empty block's margins should collapse through
  it. An attempt at this was written, measured and **abandoned**: it made the
  simple sibling case right but regressed
  `body_first_child_top_margin_collapses_through_a_chain`, which had been
  passing, so it was reverted rather than shipped. It is the next piece of this
  row, and it wants the general block-formatting algorithm rather than a
  patch on the existing first-child chain.
