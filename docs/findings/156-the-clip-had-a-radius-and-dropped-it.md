# Finding 156 — the clip had a radius and dropped it

The radius was never missing. `paint_atom` read `s.border_radius` and set it on
the `PushClipCommand`, and the field had been there since Phase 122 added the
clip. `BitmapRenderer` stored the clip as a plain `Rect` and forgot the rest:

```cpp
std::vector<Rect> m_clip_stack;
m_clip_stack.push_back(current_transform().map_rect(arg.rect));   // radius: gone
```

So the value made a complete journey — style, command, renderer — and was
discarded by the first line of the last step. That shape of bug is worth naming:
it is invisible to every check that asks whether a property is *carried*, because
it was carried the whole way. Only the pixel showed it.

## Why it hid for so long

A rounded background and a rounded border were both implemented and
anti-aliased, so a box with `border-radius` and `overflow: hidden` and a uniform
child looked right everywhere except the four corners, where the child's ink ran
into the square. On a page with real text in it that is a handful of pixels, and
the corpus had no page that asked about them until Phase 122's oracle wrote one.

The page it wrote was kept as a `known_failure` with a sentence saying what the
standard asks. That is why this took an afternoon and not a search: the failing
case, the reason, and the expected pixels were already written down, and
`make render-conformance` says `FIXED` when a recorded failure starts passing.

## The trap underneath

The obvious fix — reject a pixel outside `rounded_box_sdf` — passes the recorded
page, because the corner really is removed. It is still wrong. A background of
that box paints `(4, 4)` at a quarter coverage, `(255, 195, 195)`; a boolean clip
paints the child there at full strength, `(255, 0, 0)`. The page would go green
and the clip and the background would disagree by a quarter pixel along the
entire curve — a difference a person can see on a large enough corner and this
project's own oracle would catch the moment anyone wrote the comparison down.

So the clip answers with a fraction. `clip_coverage` returns how much of the
pixel survives, `set_pixel` multiplies the ink's alpha by it, and the two edges
become the same edge rather than two edges that are both round. The comparison
is a reftest now: background and clip at the same offsets, `(4,4)`
`#ffc3c3` on both sides, checked by dumping the PNG and comparing tuples as
well.

## The radius has to move with the geometry

The clip is pushed into device space — `current_transform().map_rect(arg.rect)` —
so a box under `scale(2)` has a clip 80 wide for a 40-wide padding box. A radius
stored unscaled is then half the corner the box should have. It is scaled by the
same transform, and the test asserts the consequence: with `border-radius: 16px`
and `scale(2)`, `(8,8)` is blank where the unscaled curve would have painted it,
`(9,9)` is a fraction, `(10,10)` is solid — a 32px curve, which is what the
background under the same transform already drew.

## What the numbers say

`make render-conformance` 34/36 before, 36/38 after — one page fixed, two added,
2 still failing as recorded, 38 of 38 accounted for. `make test` 308/308.
