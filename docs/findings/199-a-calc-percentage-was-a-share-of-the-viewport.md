# Finding 199 — a calc() percentage was a share of the viewport

Status: **resolved for `font-size` and `line-height`; open for every box
length.** Phase 195, `docs/phases/195-a-percentage-of-what-the-declaration-began-with.md`.

## The finding

A percentage inside `calc()` is a share of what the property is measured
against. The resolver made every one of them a share of the **viewport**:

```cpp
// src/css/StyleResolver.cpp, before
if (val.find('(') != std::string::npos) {
    val = resolve_calc_functions(val, m_viewport_width);
}
```

`resolve_calc_functions` replaces each `calc()`, `min()`, `max()` and
`clamp()` by a `px` number, so by the time the property branch ran there was
no percentage left and no way to know what it had been a share of.

For most properties the basis is the containing block, which lives in layout —
so the number was wrong, but plausibly wrong, and the layout consumed it as an
ordinary length. For two properties the basis is in the cascade itself, and
there the number was wrong by a factor of the viewport:

| declaration | parent | a browser | this engine, before |
| :--- | :--- | :--- | :--- |
| `font-size: calc(50% + 2px)` | 20px | 12px | **402px** |
| `font-size: calc(50%)` | 20px | 10px | **400px** |
| `font-size: max(50%, 30px)` | 20px | 30px | **16px** (the fallback) |
| `line-height: calc(100% + 4px)` | 10px | 14px | **800px** |
| `line-height: max(100%, 20px)` | 10px | 20px | **800px** |

Two things made it survive. `getComputedStyle` answered the *right* value for
`font-size` — `DomStyleBindings` evaluated the same `calc()` against the
inherited size — so a page that only read the style saw nothing wrong, and the
box was wrong. And a `min()`/`max()`/`clamp()` was mis-resolved twice over:
`resolve_calc_functions` turned `max(50%, 30px)` into the number it would have
if every percentage were zero, which for `font-size` then failed the parse and
fell back to 16px.

## What was wrong, in one sentence

A declaration's percentages belong to the declaration — to the thing it is
measured against — and the resolver was answering for a thing it is not
measured against.

## The guard

The fix is a basis per property, not a global one:

- `font-size` percentages are of the **inherited** font size (Fonts 4 §2.4);
- `line-height` percentages are of the element's **own** font size (2.1
  §10.8.1);
- everything else keeps the viewport, because the right basis is not in the
  cascade at all. That is what remains open.

The reported values are fixed on the script side too, so the two resolvers
answer the same number: `line-height` with a percentage becomes a length
(it has the font size), and `min()`/`max()`/`clamp()` in a `font-size` go to
the typed evaluator with the percentage substituted first, because a
comparison needs two lengths.

## What a reader should take from it

1. A percentage is not a number until something is named to take a share of.
   `calc()` does not change that; it only hides the percentage inside an
   expression, where a resolver that answers too early cannot see it.
2. The engine had two resolvers that could disagree, and they did — over the
   same declaration, in opposite directions. A test that reads one of them
   cannot see the bug. The suite added here checks the answer against the box
   the layout built, and no case may report a font size its own line box
   contradicts.
3. The suite pins what is still wrong (`calc(100% - 40px)` in a 600px block
   lays out 760, the viewport less 40) so that fixing it is a change in one
   place rather than a mystery.

## Still open

A `calc()` percentage in `width`, `height`, `min-/max-*`, `margin-*`,
`padding-*` and the insets still resolves against the viewport. The resolved
value is stored in `ComputedStyle` and read as a pixel length in about a
hundred places in the layout, so the fix is a change to what those fields
mean — a `calc()` that keeps its percentage through to
`LayoutBox::resolve_percentages`, the way a bare percentage already does.
