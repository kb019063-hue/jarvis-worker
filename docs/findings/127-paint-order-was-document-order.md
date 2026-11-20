# Finding 127 — paint order was document order

Phase 122. Status: **found and measured, not fixed.** The three symptoms below
are recorded as `known_failure` entries in `tests/render/reftests.json`, with
the reason on each, and handed off as rows of `AGENTS.md`'s Open tasks table.
The finding is that the tree had no idea any of them were there.

## 1. What was wrong

Phase 120 put a pixel oracle in the tree and measured ten pages: block
backgrounds, `margin-left`, borders, padding, `display: none`, a percentage
width, an offscreen box. It said what it had *not* measured, and the first
thing on that list was every layout mode added after 1998 — flexbox, grid,
floats, `transform`, `border-radius`, images.

Sixteen pages later, three separate statements turned out to be false, and
none of them was a claim written down anywhere: they were simply the engine's
behaviour, unwatched. Two of them are one bug.

### 1.1 A float is painted under the block that follows it

```html
<style>
  body { margin: 0 }
  .f { float: left; width: 50px; height: 50px; background: #ff0000 }
  .b { width: 100px; height: 40px; background: #0000ff }
</style>
<div class="f"></div><div class="b"></div>
```

The float's position is right — it is laid out at `(0,0)`–`(49,49)` and the
block beside it starts at `x=0`, overlapping, exactly as CSS 2.1 says. The
*ink* is wrong: `(10,10)` is `#0000ff`. `DisplayList::build_recursive` walks
children in document order and emits each box's own background as it arrives,
so the float's rectangle is painted first and the following block's background
covers it.

CSS 2.1 Appendix E does not paint in document order. Step 4 paints the
backgrounds of in-flow, non-positioned block-level descendants; **step 5**
paints the float's background; step 7 paints inline content. A float is above
every block background in its stacking context, whatever its position in the
tree.

`clear: both` works: the block after the float was correctly pushed to `y=50`.
So this is not "floats are unimplemented" — it is one ordering rule.

### 1.2 A transform is painted in document order too

```html
<style>
  body { margin: 0 }
  .t { width: 40px; height: 40px; background: #ff0000; transform: translate(20px, 10px) }
  .n { width: 100px; height: 10px; background: #0000ff }
</style>
<div class="t"></div><div class="n"></div>
```

Everything about this works except the last rule of it. The ink moves and the
layout does not: `(0,0)` is white, `(20,10)` is red, `(19,10)` is white, and
the next block still starts at `y=40` because a transform is a paint-time
`PushTransformCommand`, not a layout offset. Where the moved ink overlaps that
next block, though, `(20,45)` is blue: the later sibling is painted over it.

A non-identity `transform` creates a **stacking context** (CSS Transforms 1 §3),
so the element is painted in the positioned layer — Appendix E step 8 — above
in-flow block backgrounds. Same root cause as §1.1: the display list has no
phases, only a tree walk.

### 1.3 `align-items` is not read

```html
<style>
  body { margin: 0 }
  .c { display: flex; align-items: center; width: 100px; height: 100px; background: #eeeeee }
  .a { width: 50px; height: 20px; background: #ff0000 }
</style>
<div class="c"><div class="a"></div></div>
```

The item is at `y=0`, not `y=40`. `align-items: center` is ignored. The
default, `stretch`, is ignored as well: a flex item with no height of its own
is **0 tall and paints nothing at all**, which is why an empty flex row renders
an empty page rather than a blank band:

A flex row of empty boxes therefore renders as a blank band — the container's
own background, and nothing else — and a page whose whole content is such a row
renders as an empty page.

Grid items are not stretched to their area for the same reason, so
`grid-template-rows: 30px 20px` positions the *cells* correctly and the items
inside them paint nothing. The main axis is fine — `justify-content:
space-between`, `center`, `flex-end`, `flex-direction: column` and `flex-wrap`
all place their items exactly — which is what makes the cross axis conspicuous:
half of flexbox is right and nothing said which half.

The one thing to be careful about when reading that: this is not "flexbox
paints nothing". It is that an item which relies on its container for a
dimension gets zero of it, so a page of empty boxes shows nothing whatever the
engine's flex algorithm does. `flex-basis` and `flex-grow` were probed both as
shorthands and as longhands and neither changed the result, so the zero height
is the whole of it.

### 1.4 `overflow: hidden` clips to a rectangle, not to the radius

```html
<style>
  body { margin: 0 }
  .p { width: 40px; height: 40px; overflow: hidden; border-radius: 16px }
  .c { width: 40px; height: 40px; background: #ff0000 }
</style>
<div class="p"><div class="c"></div></div>
```

`(0,0)` is red. A 16 px radius on a 40 px box takes the corner out of the
shape, and the child should be clipped to what is left; instead the clip is
its own square `padding_box`. The `PushClipCommand` carries `border_radius` and
the corner is not removed.

This one contradicts `ARCHITECTURE.md`, which listed "rounded-corner clipping"
as missing — and it was right, but for a narrower reason than the line
suggested. `border-radius` on a **background and a border** is implemented and
anti-aliased: a 40×40 box with a 12 px radius has blended pixels at all four
corners and a solid straight edge between the arcs, which is why the reftest
for it asserts `not-colour` at the corners rather than an exact white. Only the
*clip* is square.

## 2. What the fix was

Nothing in the engine. The fix for *this* finding is that these four statements
stopped being invisible:

- `tests/render/reftests.json` carries 26 pages and 100 assertions, up from 10
  and 30, covering flex main axis, flex cross axis, wrap, grid tracks and gap,
  a float, `clear`, absolute positioning in a relative parent, `overflow`
  clipping, a data-URI image scaled, `border-radius`, and `transform`;
- the six pages that do not match carry a `known_failure` string saying why,
  and `render_check.py` reports them as `KNOWN` instead of passing them or
  failing the build;
- an entry attached to a page that *passes* is reported as `FIXED` and **fails
  the run**, so the corpus shrinks as bugs are fixed instead of accumulating
  excuses. That direction is self-tested (`self_test()` case 4) and was
  demonstrated: a page that passes with an entry added by hand exits 1;
- `AGENTS.md` gains a row per symptom, so the fixes are claimable work rather
  than a footnote in a write-up.

The three measured behaviours that are *correct* stayed as passing tests, so
the corpus says what works as well as what does not: `transform` moves paint
and not layout, `border-radius` rounds the corner, `clear: both` clears.

## 3. What is still open

Everything above, plus: inline formatting and line breaking have no reftests;
table cell backgrounds and the collapsed border model are unmeasured; SVG and
canvas are unmeasured; glyph *shape*
is not compared, only the rectangle ink lands in; **no WPT reftest is
vendored** (`find tests/wpt -name '*-ref*'` is still 0); there is no two-build
pixel diff.

Three of the four statements are now rows in `AGENTS.md` (§1.1 and §1.2 are one
row, because they are one bug); the rest are Phase 38's shape, not this
phase's.
