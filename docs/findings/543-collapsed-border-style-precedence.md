# Finding 543 — equal-width collapsed borders ignored style precedence

**Severity:** wrong border appearance. When two adjacent cells had borders of
equal width but different styles, the resolver chose whichever candidate it
visited last. It propagated that candidate's colour and width, but not its
style; each cell's own style was later used to paint its half of the edge.

**Repro:**

```html
<table style="border-collapse: collapse">
  <tr>
    <td style="border-right: 4px dashed red">left</td>
    <td style="border-left: 4px solid blue">right</td>
  </tr>
</table>
```

With equal widths, CSS 2.1 gives `solid` precedence over `dashed`. The shared
edge should therefore be blue and solid on both sides, not dependent on which
cell happened to be visited last and not half dashed / half solid.

**What changed:** the resolver compares CSS 2.1 style priority after width,
including `hidden` above the visible styles, and carries the resulting style
alongside the width and colour through layout into `DrawBorderCommand`. The
suite checks the style-rank transitions
and confirms the renderer paints the winner. See [Phase 543](../phases/543-collapsed-border-style-precedence.md).

**Scope:** cell-border equal-width style conflicts only. Border-origin
precedence, hidden suppression against wider borders, table/row candidates,
and same-style colour tie-breaking are not claimed as implemented.
