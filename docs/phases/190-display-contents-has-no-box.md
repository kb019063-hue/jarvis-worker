# Phase 190 — the element that was supposed to disappear

Task: the row that began "Root layout, background images and general margin
collapse remain open", and the one **regression** it records: `display: contents`
is not implemented, so a boxless body wrongly colours the canvas. Finding 194.

## 1. A value the engine never heard of

`display: contents` does not mean "hide". CSS Display 3 §2.7 is explicit:

> The element itself does not generate any boxes […] its children […] take part
> in the element's parent's formatting context, as if the element had been
> replaced by them.

So an element with `display: contents` is a hole in the box tree with the
children pushed through it. Both neighbours are the same word in a stylesheet
and completely different in the tree: `display: none` throws the children away
too, and `display: block` keeps a box that paints a background, a border and a
padding box.

The engine did neither, because the resolver had no `contents` at all. The
`display` chain in `StyleResolver::apply_declaration` matched nine keywords and
stopped, so `contents` fell through every branch and the element kept the
`Block` the cascade had initialised — a box that painted 50px of padding and a
9px border the page never asked for.

Measured before the fix, and the measurement is the interesting part:
`getComputedStyle(el).display` already answered **`"contents"`**, because that
getter echoes the declared string; the layout tree answered **`BlockBox`**. The
engine disagreed with itself about the same element, and only one of the two
answers could be seen from a page.

## 2. One enum member and the place the box would have been

`Display::Contents` joins the enum (`StyleResolver.hpp`), with `is_contents()`
beside the existing predicates. The parser accepts the keyword, and the
`@supports` feature-test whitelist accepts it too, so `@supports (display:
contents)` now answers true instead of claiming the engine cannot do a thing
it can.

`LayoutTree::build_boxes` is where a box is made, so it is where a box is
declined. The element branch resolves its style as it always did — that is not
optional, because inherited properties still have to reach the children and the
element is still in the DOM tree — and then, before any box exists:

```cpp
if (style->is_contents()) {
    const auto& contents_children = /* shadow root children, else children */;
    for (const auto& ch : contents_children)
        build_boxes(ch, resolver, parent_style, pool, depth + 1, out, split_inline_blocks);
    return;
}
```

The children are built with the **parent's** style, and that is the whole
point: with no box there is no style for layout to inherit through, and the
element's own inherited properties were already folded into the children when
`resolve_style` resolved them against it. Splice them with `style.get()` and
every child would pick up the wrapper's padding-dependent inherited values that
its box no longer has.

Measured after:

```
$ ./build/litebird --dump-layout contents.html      # #wrap: display:contents
BlockBox [x=0 y=0 w=800 h=50]
  BlockBox #a [x=0 y=0 w=100 h=20]
  BlockBox #b [x=0 y=20 w=120 h=30]
```

No box for `#wrap`, `#a` and `#b` are children of `body`, `#b` begins exactly
where `#a` ends — no 50px padding, no 9px border, and nothing paints the
wrapper's red.

## 3. The regression the row recorded

The row's own words: a boxless body "now wrongly colours the canvas; before,
missing propagation happened to leave it white."

The propagation in `LayoutTree::build` read the body's **style**, never asking
whether the body would generate a box at all:

```cpp
if (!body_style.is_none() && !body_style.background_color.is_transparent())
```

With `contents` unimplemented that body was `block`, so the red propagated onto
the canvas. Chromium leaves the canvas white, because a box that does not exist
cannot paint a background to propagate. The fix is the missing half of the same
question that was never being asked:

```cpp
if (!body_style.is_none() && !body_style.is_contents() && …)
```

The corpus already carried the page, held as a `known_failure`:

```json
{"name": "a_body_with_display_contents_has_no_canvas_color",
 "html": "<!doctype html><body style=\"display:contents;background:#ff0000;color:transparent\">hello</body>",
 "assert": [{"kind": "pixel", "x": 0, "y": 0, "colour": "#ffffff", …},
            {"kind": "pixel", "x": 799, "y": 599, "colour": "#ffffff", …}]}
```

It passes now, so its `known_failure` is gone — and `make render-conformance`
exits 1 with `FIXED <name>` if a passing page keeps one, which is the guard
that makes removing it mandatory rather than tidy.

```
Render reftests: 35/41 pages match their reference pixels; 6 more fail as
recorded, with a reason in the corpus (41 of 41 accounted for)
```

**35/41, up from 34**, with 6 recorded instead of 7 and all 41 still accounted
for: one page became a match, no page silently changed category. The canvas
self-test (5/5) and its 45016 toDataURL checks are untouched.

## 4. What the suite pins, and what it cannot

`display_contents_generates_no_box_and_splices_its_children` asserts the whole
chain: the computed value is `Contents` and *not* `Block` and *not* `None`; the
wrapper has no box while both children are `body`'s children; `#b` starts at
y=20 rather than y=70; the pixels where the old padding and border used to be
are white; and a `display:contents` body leaves `canvas_background` transparent
and the raster white at both corners. Both halves fail on the old code — the
first because the wrapper kept a box, the second because the canvas went red.

Two things this does **not** claim. `display: contents` on a flex or grid
container's child still relies on the generic splice, which is right for a
block parent but has not been measured against Chromium's anonymous-item
behaviour for flex; and a `<table>` with `display: contents` now splices its
rows into its parent, which is correct per spec but has no reftest yet. Both are
narrower than what the row recorded and are listed, not claimed.

## Files

- `include/litebird/css/StyleResolver.hpp` — `Display::Contents`, `is_contents()`
- `src/css/StyleResolver.cpp` — parse `contents`
- `src/css/CssParser.cpp` — `@supports (display: contents)` is true
- `src/layout/LayoutTree.cpp` — the splice; the canvas source must own a box
- `tests/test_suite.cpp` — `display_contents_generates_no_box_and_splices_its_children`
- `tests/render/body-origin.json` — `known_failure` removed where it now passes
