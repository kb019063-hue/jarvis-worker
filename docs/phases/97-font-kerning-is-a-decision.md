# Phase 97 — `font-kerning` was a word in a stylesheet

Phase 91 taught the engine to read a font's `kern` table, and left one thing
open in its own write-up: `font-kerning` was a recognised property name —
inherited, with an initial value of `auto`, listed in the CSSOM tables — and no
part of the engine ever read the value. Kerning was always on, which is what
`auto` means, so nothing was visibly wrong; a page that asked for
`font-kerning: none` was asking a question the engine did not hear.

That is the same shape as every property phase before this one: `font-family`
(74), `font-style` (78) and `font-weight` (87) were all in the same tables and
all dropped before anything downstream asked for them. The difference here is
that the value does not change what a glyph looks like. It changes how wide the
text *is*, which is why it has to arrive at layout and at the painter together.

## What the property actually decides

`font-kerning` has three values and two behaviours:

| value | what CSS says | what this engine does |
|---|---|---|
| `auto` | the user agent decides | kern — the `auto` decision, and the one a browser makes |
| `normal` | kern | kern |
| `none` | do not kern | do not kern |

`auto` and `normal` are kept apart in `ComputedStyle` even though they are
answered alike, because `getComputedStyle` has to return the word the author
wrote, and because the difference is one a browser may legitimately resolve
either way. The whole of the behaviour is `kerning_enabled()`, and the whole of
the visible change is that a pair stops being adjusted.

## Where it is carried

The route is the one Phase 91 and finding 97 established for the pair
adjustment, and the one finding 84 is about for inherited text properties:

```
font-kerning: none  ->  ComputedStyle::font_kerning   (inherited, in the style key)
                    ->  LayoutBox measures with kerning_enabled()
                    ->  DrawTextCommand::font_kerning
                    ->  BitmapRenderer -> FontRenderer::draw_text(..., kerning)
```

- `ComputedStyle::font_kerning` is a `FontKerning` enum (`Auto`, `Normal`,
  `None`), inherited like the rest of the font group and part of the style
  cache key — two boxes that differ only in this measure to different widths, so
  they must not share a cached style.
- `measure_text(..., bool kerning)` and `draw_text(..., bool kerning)` take the
  flag last, after the family and the slant. Both default to `true`, so every
  existing call site keeps kerning.
- `LayoutBox` passes `style.kerning_enabled()` at both places it measures text —
  the block text path and the inline formatting context — and `PaintCommand`
  copies it onto the command, and `BitmapRenderer` hands it to the pen. Four
  links, and a missing one anywhere in the chain is a run measured one width and
  drawn another.
- `FontRenderer::kerning_px` is untouched and does not know about the property.
  It answers what the font says about a pair; `font-kerning: none` is the page
  deciding not to ask. Those are different questions and conflating them would
  make the harf-buzz of the two impossible to test separately.

## The finding underneath it

Adding the property was a morning's work. Adding it *correctly* was not, because
CSS Fonts 4 says the `font` shorthand resets every font property it does not
name — `font-kerning` among them — and this resolver applied the shorthand in a
pass of its own, **ahead of the rest of the declaration block**. So
`font-kerning: none; font: 16px serif` kept `none`, and
`font-family: monospace; font: 16px serif` came out monospace: in both cases the
declaration written *before* the shorthand beat the shorthand, which is the
cascade backwards.

That is finding 103, and the fix is in the same pull request: the block is now
applied in source order, once, with only the font-size pulled forward — because
`em`, `ex` and `ch` elsewhere on the element resolve against its own computed
size, which is the single reason the old pass existed.

```
font-size first, for the em base             (only the size part of `font` too)
  -> color-scheme, color                     (currentcolor, as before)
  -> every declaration in source order       (font-size skipped: already settled)
```

## Verified

`make test` — **258 suites**, four of them new (the count includes the four
`GSUB` ligature suites that landed on `main` while this was in review, and this
branch merged `main` before it was finished):

| suite | what it holds |
|---|---|
| `font_kerning_none_reaches_the_box_and_the_painter` | the computed value for all three keywords, inheritance to a child, a child's own declaration overriding the inherited value, the flag on the text box, and the flag on the `DrawText` command |
| `kerning_none_measures_the_plain_advances` | `kerning = false` is exactly the sum of the advances (`AV` 136.816 px at 100 px, against 130.420 px kerned), and the *laid out* width of a `<p>AV</p>` in the same two styles differs by the pair adjustment — 6.40 px |
| `the_font_shorthand_resets_font_kerning` | the shorthand reset, both declaration orders, and the same ordering rule for `font-family` — a longhand the shorthand names |
| `the_painter_honours_font_kerning_none` | a document painted twice at 100 px: `auto` ends at column 129, `none` at 135, which is the 6.4 px pair adjustment in ink |

Eight reverts, eight failures:

| reverted | failure |
|---|---|
| `measure_text` ignores the flag | `kerning_none_measures_the_plain_advances`: `unkerned == advance_sum` |
| `draw_text` ignores the flag | `the_painter_honours_font_kerning_none`: the painted edge does not move |
| `BitmapRenderer` passes `true` | same |
| `PaintCommand` never sets the field | `font_kerning_none_reaches_the_box_and_the_painter`: `unkerned_seen` |
| the inline context measures with `true` | `kerning_none_measures_the_plain_advances`: the laid-out width |
| `font_kerning` does not inherit | `font_kerning_none_reaches_the_box_and_the_painter`: the child's style |
| the shorthand does not reset it | `the_font_shorthand_resets_font_kerning` |
| the block is not replayed in source order | `style_resolver_and_cascade`, then the ordering assertions |

The fifth one caught a mistake in the test as first written: `layout_text` is not
the path a `<p>`'s text takes — the inline formatting context is — so mutating
the former proved nothing. The mutation was moved to the call site that actually
measures, and then it failed.

## Not done

- **`font-feature-settings`** is the same plumbing with a harder value: it is
  a list of four-character tags, and it wants `GSUB`/`GPOS` under it.
- **`ctx.fontKerning`** on canvas. The canvas painter has its own font parser and
  applies the `kern` table unconditionally; giving it the property is a small
  piece of work of its own.
- **`font-optical-sizing`, `font-size-adjust`, `font-variation-settings`** — all
  reset by the same `font` shorthand rule, none of them implemented, so there is
  nothing to reset yet. When one lands, the reset belongs beside the
  `font-kerning` line in the shorthand's expansion.
- **`@font-face { font-kerning }`** does not exist; the property is author-side
  only, which is what the specification says.

## Next

`GSUB` ligatures are in flight on another branch; after that, bidi — the two
things a ligature needs from a shaper, and the reason the character is still the
unit of everything.
