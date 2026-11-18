# Finding 84 — `font-style` was parsed, expanded, and thrown away

**Severity:** high — a property the engine advertised and never implemented, and
a drawing bug underneath it that Phase 67 and Phase 74 both missed.

**Found by:** implementing `font-style` and then asking what `draw_text` was
actually doing with the family it had been given.

## What was wrong

**`font-style` had no destination.** It was in `CssParser`'s known-property
list, in the CSSOM's keyword table, in `DomStyleBindings`' computed-style
output, and in the `font` shorthand's expansion list — and `ComputedStyle` had
no field for it. So `apply_declaration` matched no branch, the value was
dropped, and `<em>` was upright.

The `font` shorthand was worse than silent: it expanded six longhands and had a
branch whose entire body was

```cpp
else if (lowered == "italic" || lowered == "oblique" || lowered == "small-caps") continue;
```

which is a comment saying the token is not needed.

**`pick_face`'s slant guard could never fire.** It read

```cpp
// ...`font-style` is not implemented, so nothing asks for italic,
// and an italic face must not be handed out by accident.
if (e.italic != bold && e.italic) continue;
```

Nothing ever asked, so the line was dead. Worse, it compares the slant to
`bold` — two unrelated properties — so the first time anything *had* asked, it
would have handed out an italic face to every bold request.

**The `font` shorthand never set the family either.** It emitted `font-weight`,
`font-size` and `line-height`; the family list at the end of the shorthand was
read token by token and discarded. `font: 16px monospace` set a size and left
the family empty.

**`draw_text` advanced the pen with the wrong face.** Layout measured a run of
text with the face `font-family` selected; `draw_text` fetched each glyph from
that face and then advanced by the *primary* face's advance. Monospace text came
out as monospace glyphs at proportional spacing — every glyph in the right
order, packed into the width of a font nobody asked for. Under per-glyph
fallback the same bug moved a fallback glyph's neighbours by the notdef advance
of a face that never had the codepoint.

## Why Phase 74's tests did not catch the last one

They asserted `laid_out_width` — the number the *line builder* produced — and a
pixel count that only asked whether there was ink. Both are true when the ink is
in the wrong place. The new test asserts where the ink ends: the last of four
1 em glyphs must be drawn at least three advances in, and under the old code it
is drawn at three *notdef* advances in.

This is the same shape as finding 80's `resolve_url` bug: the test that would
have caught it has to look at the thing the user sees, not at the number the
layer below agreed on.

## What was fixed

- `ComputedStyle::font_style`, a `FontStyle { Normal, Italic, Oblique }`, with
  its cascade, inheritance, `inherited_only_style` and paint plumbing.
- The UA row HTML §Rendering has for italic text: `em, i, cite, dfn, var,
  address`.
- `font`'s italic and oblique tokens, and its family list.
- `pick_face` taking the slant as its own question, and reporting back whether
  the face that answered needs to be sheared.
- A synthesised oblique (14°) applied to the bitmap, in the glyph cache key.
- `draw_text` advancing per codepoint by the face that supplied each glyph.

## The one that will bite next

`LayoutBox::layout_inline_formatting_context` builds a `base_inline_style`,
fills it with `color`, `font_size`, `font_weight`, `text_align` and
`text_decoration`, and passes it to `collect_items`, which ignores it —
`parent_style` is `(void)`-cast at the top of that function. It is dead code
that looks like the place inherited text properties are applied, and it has
already gone three phases without `font_family`, `font_style` or `line_height`.
Anyone adding a fourth inherited text property will put it there and it will do
nothing.
