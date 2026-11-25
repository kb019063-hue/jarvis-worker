# Phase 417 — the seven canvas text properties

§4.12.5.1's `CanvasTextDrawingStyles` mixes in nine attributes. `font`,
`textAlign` and `textBaseline` existed; the other **seven** answered `undefined`:

```
typeof ctx.letterSpacing     -> undefined
typeof ctx.wordSpacing       -> undefined
typeof ctx.direction         -> undefined
typeof ctx.fontKerning       -> undefined
typeof ctx.fontStretch       -> undefined
typeof ctx.fontVariantCaps   -> undefined
typeof ctx.textRendering     -> undefined
```

The damage was quiet in the way finding 406's was. Assigning to an absent property
is an ordinary expando: no throw, no error, no picture. Measured on a fresh
context before starting,

```js
g.font = '10px sans-serif';
g.measureText('ab').width;      // 12.4755859375
g.letterSpacing = '5px';
g.measureText('ab').width;      // 12.4755859375  — unchanged
g.letterSpacing;                // '5px'        — only an expando
```

so a page that spaced its text got the string back and no spacing.

## What changed

**Seven live accessors**, registered beside `font`/`textAlign`/`textBaseline` and
read from the renderer rather than copied onto the object. Two are CSS
`<length>`s and five are enumerated attributes; each has the initial value the
standard gives it — `"0px"`, `"0px"`, `"inherit"`, `"auto"`, `"normal"`,
`"normal"`, `"auto"`. Note that `direction` starts at `"inherit"` and
`fontKerning` at `"auto"`, neither of which is the first item of its own list, so
a setter that defaulted to `"ltr"` or `"none"` would draw correctly and read back
wrongly.

**One spacing routine, used by both the measurer and the painter.**
`apply_text_spacing` takes the shaper's output and adds, after each character but
never the first, the letter spacing, plus the word spacing when that character is
U+0020. It runs inside `layout_text`, which `measureText` and `fillText` both go
through — the one place they cannot disagree. The space goes after the **last**
glyph of a character, so a base is not pushed away from its own mark.

**A CSS `<length>` parser for the two spacings.** `px`, `em`, `ex` and `rem` are
resolved, the first three against the font size in force at the moment of
assignment — which is what the specification's setter does, and why changing
`font` afterwards does not re-resolve a spacing given in `em`. A bare number is
accepted only for zero, which is the one length CSS lets stand without a unit, and
`5` is rejected. `5pt`, `5%`, `calc()`, `inherit` and empty strings are rejected
too: the setter returns, the previous value stays.

**`direction` reaches two places.** It is what makes `start`/`end` mean the left
edge or the right one, which is the only change it makes to a picture in a context
with no bidi paragraph. For `"inherit"` the context walks its own element's
ancestors for a `dir` attribute; the context is told which element it belongs to
by `set_owner_element`, called once by the binding that hands it to script.

**`fontKerning` reaches the shaper.** `"none"` turns kerning off, and the proof
is a measurement: `AV` at 40px is 55.78px with kerning and 57.77px without.

## The thing that was nearly a regression

The shaper has a `visual_rtl` parameter and the context does not pass it, so it
defaults to `true` — RTL **script** runs (Hebrew, Arabic) have been ordered
visually since long before this phase, whatever the context said. The obvious
implementation of `direction` is to pass it there. That would have turned the
ordering **off** for every page that never set `direction`, which is every page.
It was written that way first, and the two-line change was reverted with a comment
saying why. `direction` is an inline direction for `start`/`end`, not a switch for
the script's own ordering.

## Two of the three tests I wrote wrong

**The first-character exception.** `letterSpacing: 5px` on `"ab"` adds 5, not 10:
the spacing follows a character, and there is no character before the first. An
implementation that spaces before every glyph is off by one spacing and looks
entirely plausible. The suite asserts the single-character case separately —
`measureText("a")` must be unchanged — which is the assertion that has no
tolerance for the off-by-one.

**The last-character spacing is real, and invisible in the ink.** A spacing is
added after *every* character, including the last, so `"ab"` at 6px measures 6px
wider and paints an ink box the same size — the final glyph has not moved. Part 8
asserted the painted box grew on `"ab"` and failed on a correct engine. Three
characters later it is right, and the reason is in the suite.

**Subpixel ink is not a width.** Part 6 compared the ink boxes of the same run at
`x = 40` under `ltr` and `rtl` and expected equal widths. They were 40–51 and
27–39: the run moved (which was the point) and the glyphs rasterised at a
different subpixel offset, so the box differs by a pixel. The advance is the thing
`direction` must not touch, and that is what is asserted now, with `measureText`.
The same mistake in Part 10 was caught by using `textAlign: "left"` so the run
does not move at all.

## Bite proof

Five injections, each built and restored from a `/tmp` copy:

| injection | caught by |
| --- | --- |
| **A** — no first-character exception | Part 2: `"ab"` gains 10 instead of 5 |
| **B** — the painter zeroes the spacing before laying out | Part 8: the painted run does not widen |
| **C** — `direction` never reaches `start`/`end` | Part 6: the `rtl` run stays right of x |
| **D** — `fontKerning: "none"` never reaches the shaper | Part 7: `AV` measures the same either way |
| **E** — `direction` routed into `visual_rtl` | Part 10: the Hebrew run flips |

Injection B is worth recording twice. It was **not** caught on the first attempt,
because the injection was written against `max_width`, which does not distinguish
measuring from painting — both paths pass it. The injection that does distinguish
them (zero the spacing inside the painter) is caught by Part 8, which is the part
written for it. A bite proof that is not caught says something about the
injection before it says anything about the suite.

## What is still open

* `fontStretch` and `fontVariantCaps` are stored and read back, and **nothing
  draws with them yet**: there is no condensed face selection and no synthesised
  small caps. `textRendering` has no effect either, which is the honest state of a
  hinting preference in a rasteriser that has no hinting.
* `ex` is taken as half an em, which is what most Latin faces have and is not
  measured from the font's `sxHeight`.
* Each character's spacing is applied to the last glyph of its cluster by
  `is_mark`, which is the information `ShapedGlyph` carries; a ligature is one
  glyph for several characters, so it takes **one** spacing rather than one per
  character. Browsers differ here and the specification leaves it to the shaper.
* `direction` does not reorder mixed-direction text; it chooses the alignment edge
  and nothing more. The shaper's own RTL handling is unchanged, which was
  deliberate.

## Files

* `src/render/Canvas2D.cpp` — seven accessors, `parse_css_length_px`,
  `serialize_px`, `apply_text_spacing`, `text_direction_is_rtl`, the alignment
  change, `save`/`restore`/`reset`.
* `include/litebird/render/Canvas2D.hpp` — the seven fields, the `SavedState`
  fields, the owner-element pointer, the two helpers.
* `src/js/DomHtmlBindings.cpp` — one call to `set_owner_element` in `getContext`.
* `tests/suites/417-canvas-text-properties.cpp` — 10 parts.
