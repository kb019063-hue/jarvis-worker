# Phase 217 — three lines, one drawn

Task: `text-decoration` has three lines and the painter drew one of them.
Finding 221.

## 1. What was wrong

```html
<span style="text-decoration:line-through">SS</span>
<span style="text-decoration:overline">OO</span>
```

`getComputedStyle` answered both correctly — `line=line-through`,
`line=overline`, `underline line-through` — and the display list was text with
no line at all:

```
  DrawText: (29.2773, 0) "SS" Color: #222222
  DrawText: (54.668, 0) "OO" Color: #222222
  DrawText: (86.1523, 0) "BB" Color: #222222
  DrawRect: [86.1523, 23.4, 27.4414, 1.6] Color: #222222   <- the underline only
```

`ComputedStyle` has all three bits, `InlineFragment` carries the style, and the
information arrived at the painter. The inline branch asked
`fs.has_underline()` and nothing else, so `line-through` — after the link
underline the most common decoration in any page — and `overline` were dropped
at the last step. `text-decoration: underline line-through` painted one line.

This is not a new discovery: `docs/findings/77-shared-computed-style.md` has
listed "`DisplayList` still paints only `has_underline()`, not
`has_line_through()` or `has_overline()`" as open since Phase 71. What was
missing was a way to say where the other two lines go.

## 2. Where a line goes

The underline had a y of `rect.height - 0.08em`, a fraction of the line box. It
put the stroke below the glyphs at most sizes and on them at others, because a
line box is `1.25em` tall and the text inside it is not.

CSS 2.1 leaves the position to the user agent; CSS Text Decoration 4 says the
underline is the font's `underlinePosition` and the line-through runs through
the x-height. This engine has no `post` table reader, so the position is taken
from the ink it does have: **the glyphs this renderer actually draws**.

`FontRenderer` gained one metric, `decoration_lines(font_size, weight, family,
italic)`, which returns the x-height and cap height of the face as signed
offsets from the baseline. It rasterises `'x'` and `'H'` at the same pixel size
`draw_text` will use and reads the topmost row with enough coverage, so the two
upper lines land on the pixels the text occupies:

| line | y | why |
| :--- | :--- | :--- |
| `overline` | `baseline - cap_height` | the top of a capital |
| `line-through` | `baseline - x_height` | through the lowercase, as CSS 3 asks |
| `underline` | `baseline + descent / 2` | half a descent under the baseline |

with the line box's baseline the same `ascent + leading / 2` `draw_text` uses to
place glyphs. At 40px serif that is overline at 8.85px, line-through at
16.85px, underline at 43.56px — and the `M` glyphs the renderer draws run from
y=9 to y=38, so each line is inside the band the argument says it is.

A face with no lowercase (`'x'` with no ink) borrows `0.7 * cap_height`; a face
with no capitals falls back to the ascent. Neither can be reached with the
embedded fallback, but a `.ttf` on disk can do it.

## 3. What changed

- `FontRenderer::decoration_lines` — the x-height and cap height of the face,
  from its own rasterised ink.
- `emit_text_decoration(style, rect, list)` in `PaintCommand.cpp` — draws each
  of the three lines the style asks for, in the order overline, line-through,
  underline, each a `DrawRectCommand` of `max(1, 0.08em)` thickness.
- Both text paths call it: the inline-formatting-context fragment path (which
  knew about the underline and nothing else) and the stand-alone text-node
  fallback (which knew about none of them).

Each line is inset half a stroke from the fragment's left edge and stops half a
stroke short of its right edge, so the pieces of one word join up instead of
leaving a gap at every fragment boundary.

## 4. Result

```
  TEXT at 0.0 0.0 sz=40.0            <- underline
  RECT 1.6 43.6 78.7 3.2
  TEXT at 0.0 50.0 sz=40.0           <- line-through
  RECT 1.6 67.8 78.7 3.2
  TEXT at 0.0 100.0 sz=40.0          <- overline
  RECT 1.6 108.8 78.7 3.2
  TEXT at 0.0 150.0 sz=40.0          <- all three
  RECT 1.6 158.8 78.7 3.2            <- overline
  RECT 1.6 167.8 78.7 3.2            <- line-through
  RECT 1.6 193.6 78.7 3.2            <- underline
```

## 5. How it was verified

```
make test               All 355 engine test suites passed successfully!
make js-conformance     JS conformance, full corpus: 1146/1158 (99.0%)
make dom-conformance    DOM conformance, full corpus: 678/678 (100.0%)
make wpt                Web Platform Tests: 9567/9824 subtests pass in 190 files
make render-conformance Canvas toDataURL: 45016/45016 checks agree
make check-docs         check_docs: 0 failure(s)
make check-numbers      check_numbers: 0 failure(s)
```

The new suite is `every_text_decoration_line_is_painted_at_the_faces_own_ink`.
It builds four boxes — underline, line-through, overline, none — counts the
decoration rectangles in the display list (three, not one), checks each against
its own box's baseline arithmetic and against `decoration_lines`, and checks
that `text-decoration: none` paints nothing. It also checks the stand-alone
text-node path, which used to paint no decoration at all. With the painter
reverted it fails at its first assertion, `lines.size() == 3`.

## 6. What is still open

- `text-decoration-style` (`dashed`, `dotted`, `double`, `wavy`) is not parsed;
  every line is a solid rectangle.
- `text-decoration-color` and `text-decoration-thickness` are not parsed; the
  line is the text's colour and `0.08em` thick.
- `text-underline-position` and `text-underline-offset` are not parsed.
- Decoration does not propagate from a block to its inline descendants, and a
  child's `text-decoration: none` cannot cancel an ancestor's — the fragment's
  own style is the only input.
- A fragment split across two lines gets one decoration rectangle per line
  fragment, which is right, but the rectangles are not merged where two
  fragments of one line sit side by side.
