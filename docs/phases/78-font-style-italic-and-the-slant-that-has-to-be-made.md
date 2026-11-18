# Phase 78 — `font-style: italic`, and the slant that has to be made

`font-style` was one of the properties this engine has always claimed to know.
It is on `CssParser`'s list of known properties, it is a `CSSStyleDeclaration`
attribute with a keyword grammar, the `font` shorthand expands it, and
`FaceEntry` has carried an `italic` flag since `@font-face` was written. None of
that was joined to anything: nothing stored the computed value, so `<em>` was
upright, `font: italic 16px serif` was upright, and the one line in `pick_face`
that exists to stop an italic face being handed out by accident could never
fire — nothing ever asked for one.

This phase joins it up, and handles the case that makes italic different from
every other font property: on a host with no italic face, there is no file to
select, and the slant has to be made.

## The property

`ComputedStyle::font_style` is a three-valued `FontStyle { Normal, Italic,
Oblique }`. `oblique` is kept apart from `italic` even though the renderer
answers both the same way when it has to synthesise, because a *face* declares
which of the two it is and `@font-face { font-style: oblique }` has to be able
to name one.

It inherits, and it is carried into `inherited_only_style` — the style a text
box is built with. That last clause is the lesson of the previous two phases and
it was applied deliberately this time rather than found by a failing test: a
property that resolves on the element and not on the text box is a property the
line builder and the painter never see.

The UA stylesheet gains the row of HTML §Rendering it was missing:

```css
em, i, cite, dfn, var, address { font-style: italic; }
```

## Choosing the face

`pick_face(family, weight, bold, italic)` now asks the slant question, and asks
it in the only way that matches what browsers do:

- a slanted face is eligible only when the slant was asked for, so
  `font-style: normal` inside an italic paragraph does not land on the italic;
- an upright face is passed over when the family has a slanted one — the family
  question is decided once, for the whole family, not once per face;
- and an upright face still answers when the family has no slanted face,
  because CSS does not say "drop to another family", it says make the slant.

The distinction between "the family's italic answered" and "the family had no
italic, so this upright face is standing in" is reported, not assumed:
`family_face(..., bool* synthetic_oblique)`. Both are a `const TrueTypeFont*`,
and a caller that guessed wrong would either double-slant a real italic or
leave one upright.

A family whose *only* face is slanted, asked for upright, still answers with
that face. The wrong slant is a smaller error than the wrong typeface, and a
page that ships one italic file is a real thing.

## The slant that has to be made

```
skew = 0.25 px of run per px of height  (= tan 14°, CSS Fonts 4's default
                                         oblique angle, and what Chrome uses)
```

The shear is applied to the rasterized bitmap, per row: a row of a bitmap is
the glyph at one height above the baseline, and that is exactly the line a shear
moves along. Rows above the baseline move right; rows below it — a descender —
move left, so the result is given room on both sides and its bearing moves with
it. Clipping that overhang cuts the tail off every `g`.

The advance does not change. A shear moves ink, not the pen, so an italic line
measures exactly as wide as the upright one and words do not overlap or leave
gaps. That is also why a *real* italic face changes the advance: it has its own
`hmtx`, and `measure_text(..., italic)` asks for it.

`italic` is part of the glyph cache key. It has to be: a sheared bitmap and an
upright one are two pictures of one glyph, and whichever was rasterized first
would otherwise answer for both — half the page slanted, half not, depending on
paint order.

## The advance bug this turned up

`draw_text` advanced the pen by `font->glyph_advance(...)` where `font` was the
*primary* face, whatever `font-family` the text was in. Layout measured with the
family's face. So a run of monospace text — the one kind of text whose entire
purpose is that its glyphs line up — was drawn at proportional advances: right
order, wrong spacing, packed into the width of a font nobody asked for. It was
introduced by Phase 67 and survived Phase 74, because both of those tested the
width the line builder produced and neither tested where the ink landed.

The pen now advances by `advance_in_family(cp, ...)`, which is the same call
`measure_text` makes, per codepoint — so a glyph supplied by a *fallback* face
also advances by that face's advance rather than by the notdef of a face that
never had it.

## Verified

`make test` — 223 suites, four of them new. Each of the six fixes was reverted
in turn and its test failed:

| reverted | failure |
|---|---|
| the slant rule in `pick_face` | `a_family_with_an_italic_face_is_not_sheared`: `Assertion '!sheared' failed` |
| `apply_synthetic_oblique` | `font_style_italic_slants_the_glyphs_it_draws`: `Assertion 'italic_lean >= 4' failed` |
| the pen advance in `draw_text` | `text_is_drawn_at_the_advances_it_was_measured_at`: `Assertion 'wide_rightmost - 20 >= 29' failed` |
| `font_italic` on `DrawTextCommand` | `font_style_italic_reaches_the_painter`: `Assertion 'italic_command_seen' failed` |
| `s.font_style` in `inherited_only_style` | `font_style_italic_reaches_the_painter`: `Assertion 'em_text->style.font_style == FontStyle::Italic' failed` |
| the UA `em, i, cite, dfn, var, address` rule | `font_style_italic_reaches_the_painter`: `Assertion 'style_of("em")...' failed` |

The oracle for the shear is the *shape of the ink*, not a font: at 40 px, 'H'
leans 8 px from its bottom row to its top row when slanted and 0 px when not,
and its ink is 23 px wide upright and 31 px slanted. Nothing about that depends
on which fonts the host has, which is the point — this host has six DejaVu files
and not one of them is slanted.

The oracle for face selection is two `@font-face` rules declaring one family
with two slants, which is how a real page declares them.

## Not done

- `font-style: oblique 10deg` — the angle is parsed only as far as the keyword.
  Every synthesised slant is 14°.
- `font-synthesis: none`, and letting a page refuse a synthetic bold or oblique.
- Small caps, which shares the shorthand with italic and is still dropped.
- A real italic face's own metrics are used for the line box, but its
  `OS/2` italic angle and any italic-specific kerning are not read.

## Next

Weight from `OS/2.usWeightClass`, which is the same kind of question — which
face of a family is the right one — answered from the font's own tables instead
of from words in its `subfamily` name. Then shaping.
