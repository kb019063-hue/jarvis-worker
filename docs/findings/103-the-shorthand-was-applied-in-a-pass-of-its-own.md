# Finding 103 — the `font` shorthand was applied in a pass of its own

**Severity:** low for a page in the wild, high for the cascade — declaration
order is the first rule after importance, and inside one block it did not hold
for any property the `font` shorthand touches.

**Found by:** writing the test for the shorthand's `font-kerning` reset, which
CSS Fonts 4 requires, and watching it fail in the direction that says the
*earlier* declaration had won.

## What was wrong

`StyleResolver::resolve_style` partitioned a declaration block by property and
applied the partitions in a fixed order:

```
custom properties -> color-scheme -> color -> font-size and font -> the rest
```

The reason was real and is still real: `em`, `ex` and `ch` lengths elsewhere on
the element resolve against its own computed font size, so the size has to be
known before the rest of the block is applied. `font` was swept into that early
pass because it *sets* the size.

The cost was order. `font` was applied before everything else regardless of
where it was written, so:

```css
/* a font-family written after the shorthand lost to it */
font: 16px serif; font-family: monospace;   /* -> serif! */

/* a font-family written before it won */
font-family: monospace; font: 16px serif;   /* -> monospace! */
```

Both are backwards, and the second is the one that made it visible: the
shorthand resets `font-kerning` to `auto`, so a page that said
`font-kerning: none; font: 16px serif` in one block was told `none` by a
declaration the shorthand should have overridden.

Nothing in the suite could see it, because no test had ever put a `font`
shorthand and one of its own longhands in the same declaration block. The
existing `font` tests each used a block with only the shorthand in it.

## The fix

The early pass now applies **only the font size** — the size longhand, or the
`font-size` component of a shorthand expansion — and the block is then replayed
in source order, once:

```cpp
// size first, and nothing but size: em/ex/ch elsewhere on the element resolve
// against it
for (const auto* d : font_decls) { ... apply only the "font-size" longhand ... }
m_em_base = style.font_size;
apply_all(scheme_decls);
apply_all(color_decls);
// ...then the whole block in order, with `font-size` skipped (settled above)
// and a `font` shorthand expanded in place, minus its size component
for (const auto* d : cascade_order) { ... }
```

The size is never applied twice — that would have re-resolved `font-size: 50%`
against the element's *own* size on the second pass, which is the bug the first
version of this change produced and `percentage_font_size_and_line_height`
caught: `50%` of 16 px came out 4 px.

## The tell

Two assertions, the same property, written the two ways round:

```css
font-family: monospace; font: 16px serif;   /* must be serif */
font: 16px serif; font-family: monospace;   /* must be monospace */
```

An engine that passes one of these and fails the other is not implementing a
cascade; it is implementing a priority list and hoping the two agree.

## What this changes

- `src/css/StyleResolver.cpp` — the size pass is size-only, and the block is
  replayed in source order.
- `the_font_shorthand_resets_font_kerning` asserts both orders for
  `font-kerning` and for `font-family`, so the rule is checked on a property the
  shorthand *names* as well as one it only *resets*.
- `font-kerning: none` now survives exactly where CSS says it does, and nowhere
  else.
