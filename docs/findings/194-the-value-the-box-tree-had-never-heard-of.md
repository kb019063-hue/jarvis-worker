# Finding 194 — the value the box tree had never heard of

**Severity:** medium — the engine answered two different `display` values for
the same element depending on who asked, and a page that used `contents` got a
box with padding, a border and a canvas colour it never asked for.

**Found by:** taking the row's own recorded regression (`display: contents` is
not implemented, so a boxless body wrongly colours the canvas) and asking
whether "not implemented" meant "treated as `none`", "treated as `block`", or
"the value never arrived".

## What was wrong

Three separate answers to one declaration, and no two of them agreed:

| who asked | answered |
|---|---|
| `getComputedStyle(el).display` | `"contents"` — the declared string, echoed |
| `StyleResolver::apply_declaration` | matched nine keywords, `contents` not among them |
| `LayoutTree::build_boxes` | made a `BlockBox` |

The resolver's `display` chain ended at `table-caption` and fell through, so the
style kept whatever the cascade had initialised — `Block`. The getter echoed
the *declared* text, so the DOM could report a value the layout engine did not
have a name for. Nothing crashed, and nothing looked wrong until a page asked
for the one thing the keyword exists to do: not make a box.

Then the box-colour propagation in `LayoutTree::build` read the body's style
and never asked whether the body would produce a box, so a `display: contents`
body propagated its red to the canvas. The row had recorded this as a
**regression** in the direction the engine was accidentally right before:
while `contents` was silently ignored, the body was a block and the canvas was
already wrong; the earlier "missing propagation" had merely left it white.

## What the spec says

CSS Display 3 §2.7:

> The element itself does not generate any boxes […] its children […] take
> part in the element's parent's formatting context, as if the element had been
> replaced by them.

The two neighbours are one keyword away and nothing alike: `none` discards the
children as well, `block` keeps a box that paints. Only `contents` is a hole in
the box tree with the children pushed through it.

CSSOM additionally names `contents` where a resolved value is *not* the used
value:

> If […] the resolved value of the `display` property is not `none` or
> `contents`, then the resolved value is the used value. Otherwise […] the
> computed value.

which is why the getter was not lying — it was the resolver that had no name
for the value, so Phase 188's used-value substitution could not be told to skip
it.

## The fix

An enum member and a predicate; the parser accepts the keyword (and the
`@supports` whitelist, so a feature query stops denying a feature the engine
has); the element branch of `build_boxes` splices its children into `out` with
the **parent's** style before any box is created, and the canvas-source
condition asks `!is_contents()` alongside `!is_none()`.

The children take the parent's style, not the phantom element's: with no box
there is nothing for layout to inherit through, and `resolve_style` already
folded the element's inherited properties into them. Using `style.get()` there
would hand every child padding-dependent inherited values the box no longer
has.

## Measured

```
--dump-layout  #wrap{display:contents;padding:50px;border:9px solid}
BlockBox [x=0 y=0 w=800 h=50]
  BlockBox #a [x=0 y=0 w=100 h=20]
  BlockBox #b [x=0 y=20 w=120 h=30]     ← was y=70 inside a wrapper box

render-conformance    35/41 (was 34/41), 6 recorded (was 7), 41/41 accounted
engine suites         343/343 (the new suite among them)
DOM tree-construction 1973/1973 · JS 1146/1158 (99.0%) · DOM 678/678
everyday probes       68/68 and 90/90
```

The corpus page `a_body_with_display_contents_has_no_canvas_color` existed with
a `known_failure` before this phase; it passes, and the `known_failure` is
removed — `make render-conformance` refuses a passing page that still carries
one (`FIXED <name>`, exit 1), which is the guard that makes removal mandatory.

## What is not claimed

- **Flex/grid parents** are not measured against Chromium. The splice puts the
  children directly into the parent's box list, which is right for a block
  parent; whether a flex container should generate anonymous items from them
  the way Chromium does is unmeasured.
- **A spliced `<table>`** puts its rows into its parent, which is what the spec
  says, but no reftest covers it.
- The row's other open items — root background images preventing propagation,
  opacity groups, hidden descendants, inline-body margins, margin collapse —
  are untouched and still recorded.

## A note on how this phase reached the tree

The implementation was first written in a session that then lost GitHub
connectivity. The push never happened, the session was replaced, and the commit
— along with the workspace it lived in — did not survive. The work was redone
from scratch on the new base and re-measured there; every figure above is from
the second run. Nothing was recovered from the first, and no number in this
document is inherited from it.
