# Finding 97 — the pair was adjusted after the glyph it belonged to

**Severity:** medium — text was measured one width and drawn another, the same
class of drift as finding 84, and the tests could not see it.

**Found by:** drawing `AV` three ways in a throwaway probe, after the kerning
test was already green.

## What was wrong

**The adjustment was applied one glyph late.** `draw_text` walks a string,
places each glyph at the pen, and advances the pen. Kerning is the gap *between*
two glyphs, so it has to be added to the pen after the first glyph and **before
the second one is placed**. The first version added it after the second:

```
draw V at pen            <- V placed at the unkerned position
pen += advance(V)
pen += kern(A, V)        <- moves whatever comes after V
```

The measured width was right, because `measure_text` adds numbers to a total and
a sum does not have an order. The painted width was not, because a position
does. So `measure_text("AV")` said 130.4 px and the painter put the same two
glyphs down as if it had said 136.8 — a page would lay out one line and draw
another, and a box sized to the text would have a hole in it that no test
looked at.

The fix is one line's worth of ordering: the pair closes before its second glyph
goes down, and the check that keeps it honest is a pixel comparison, not an
arithmetic one — `[51..282]` hand-kerned against `[51..295]` unkerned, with the
engine's own output required to equal the first.

## The second half: a test that skips proves nothing

The first version of the three kerning tests all began with

```cpp
if (!fr.font_kerning()) return;
```

which is reasonable — a host whose font has no `kern` table has nothing to
check — and which made the suite **blind to the table never being read at all**.
Reverting `m_kern_pairs = pairs;` to `m_kern_pairs = 0;` left the suite green,
because a font that was never asked says "no table" exactly like a font that has
none. This is the same shape as finding 93's test that passed with the ordering
reverted: the assertion was downstream of a branch that had already given up.

The fix is to make the *file* the ground truth. The test now carries its own
`cmap` format 4 and `kern` format 0 reader — written out in `tests/test_suite.cpp`
rather than borrowed from `FontRenderer` — reads the host face's 2,727 pairs
directly, and asserts three things that cannot all hold if the feature is off:

- the file has a table, so the engine must report one (`font_kerning()`),
- for each pair the file names, `kerning_px` equals `units * size / upem` with
  the file's own units and the face's own `units_per_em`,
- `measure_text` for the pair equals the two advances plus exactly one
  adjustment.

A test whose oracle is the engine's own output tests that the engine agrees with
itself. A test whose oracle is the file tests that it agrees with the font.

## The tell

`kerning_px('A','V', 100) == -6.396` and the painter drawing the pair 6.4 px too
wide, at the same time, in the same build. When measurement and painting
disagree about a number they both computed, the number is not the problem —
where it is applied is.

## What this changes

- The pair adjustment is applied when the glyph after it is placed
  (`draw_text`, and the canvas painter in `Canvas2D.cpp`), not when the glyph
  before it is finished.
- Kerning tests read the font file directly, so "the table was never opened"
  fails as loudly as "the value is wrong".
- Values are compared as **signed**: `AA` is `+57` units in DejaVu Sans, and a
  `min(0, value)` clamp is one of the reverts the suite now catches.
