# Finding 417 — the spacing was an expando

Seven of §4.12.5.1's nine `CanvasTextDrawingStyles` attributes did not exist:

```
typeof ctx.letterSpacing     -> undefined
typeof ctx.wordSpacing       -> undefined
typeof ctx.direction         -> undefined
typeof ctx.fontKerning       -> undefined
typeof ctx.fontStretch       -> undefined
typeof ctx.fontVariantCaps   -> undefined
typeof ctx.textRendering     -> undefined
```

The failure has no symptom at the point where it happens. Assigning to a property
that does not exist does not throw, does not warn, and does not log: it creates an
own property on the context object, exactly as a page that wanted a private field
would. So

```js
ctx.letterSpacing = '5px';
```

succeeds, `ctx.letterSpacing` reads back `'5px'` afterwards, and
`measureText('ab')` is the same number it was. A page under test sees nothing
wrong, and so does a person looking at the output; the only evidence is a
comparison against a browser.

This is finding 406's shape again — capability present or nearly so, and
unreachable — but quieter. Phase 406 had `ImageFilter` fully implemented and no
accessor; this phase had no implementation either, and the reason nobody noticed
is that a *stored* value looks the same as a *working* one until something
measures.

## The regression that was nearly shipped

The shaper takes a `visual_rtl` flag, and the canvas did not pass it, so it defaulted
to `true`: Hebrew and Arabic runs have been ordered visually for as long as the
shaper has existed, whatever the context said. Passing `direction` into that flag
is the obvious implementation and it is wrong — it would have turned the ordering
off for every page that never set `direction`, which is all of them, and no test in
the tree would have said so. It was written, measured (the Hebrew ink box moved
twenty pixels), and reverted, with the reason in the code so the next person does
not helpfully re-add it.

The general shape: a property whose name matches an existing internal flag is not
evidence that the flag is what the property controls. `direction` is an *inline*
direction — which edge `start` and `end` mean — and the script's own ordering is a
different thing that the shaper was already doing.

## Where a spacing goes, and the off-by-one that hides

The rule is that a letter space follows every character except the first. Written
as "before every character" it is off by exactly one spacing for the whole string,
which for `"ab"` at 5px is 10 instead of 5 — the kind of error that looks like a
slightly loose font. The assertion with no tolerance is the **single-character**
one: `measureText("a")` under a 5px letter spacing must equal `measureText("a")`
with none, because there is no character before the first for a space to follow.

The last character is the mirror image, and it cost a rewrite of Part 8. A spacing
follows the last character too, so it extends the advance past the final glyph
while leaving that glyph's ink where it was. `"ab"` at 6px therefore measures 6px
wider and paints the same box — correct, and it failed the assertion that the
painted box had grown. Three characters show it, because then there is a glyph
after a space.

## `measureText` and the painter must not be able to disagree

The two go through `layout_text`, so the spacing is applied there, once. That was a
decision rather than an accident: the alternative — a `spacing` argument to the
painter and to the measurer — is where a phase like this usually breaks, because
the assertions that "the measurement changed" pass while the picture does not. It
was checked the hard way: an injection that zeroes the spacing inside the painter
(so measuring keeps it and painting loses it) is caught by Part 8 alone.

The first bite proof for that case was **not** caught, and the lesson is worth more
than the fix. The injection was written against `max_width`, on the assumption that
a painter passes one and a measurer does not; both pass it, so the injection
changed nothing on either path, and a "not caught" would have been read as a gap in
the suite that was not there. A bite proof that does not fail says something about
the injection before it says anything about the suite.

## Two tests I wrote wrong

**Subpixel ink is not a width.** Part 6 compared the painted ink box of the same run
at `x = 40` under `ltr` and `rtl`, expecting equal widths — the run moved, which
was the point, and the box should not have changed. It was 40–51 against 27–39: a
run at a different subpixel offset rasterises to a box a pixel different. The
*advance* is what must not change, and `measureText` says it does not
(19.9453125 either way). Part 10 made the same assumption about Hebrew and hid it
better by using `textAlign: "left"`, which does not move the run.

**`1em` must not re-resolve.** The spacing is resolved against the font size at the
moment of assignment, so a later `font` change leaves it alone. That is easy to
write as "re-resolve every time, against whatever the font is now", which looks
more correct and is not what the setter steps say.

## What is stored but does nothing yet

`fontStretch` and `fontVariantCaps` round-trip and reach no face selection: there
is no condensed face and no synthesised small caps. `textRendering` is a hinting
preference and this rasteriser has no hinting. Saying so here is better than
implying that setting `"condensed"` condenses anything — it is the same class of
quiet failure this finding is about, one layer up.

## Still open

* Condensed/expanded faces and small caps: stored, not drawn.
* `ex` is half an em rather than the font's measured `sxHeight`.
* A ligature takes one spacing instead of one per character; `ShapedGlyph` has
  `is_mark` and no cluster index, so a cluster of several characters cannot be
  counted.
* `direction` does not reorder mixed-direction text.
