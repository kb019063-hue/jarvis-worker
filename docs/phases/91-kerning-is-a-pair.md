# Phase 91 — kerning is a pair, and the pen had no memory

Laying out text here has always been a sum: walk the string, add each glyph's
advance, put the next glyph where the pen stopped. That is the right model for a
font with no opinions about its neighbours, and it is the wrong model for every
font that has them. DejaVu Sans — the face this host's `sans-serif` resolves to
— ships a `kern` table naming **2,727** pairs, and the engine had never opened
it. `AV` was drawn 6.4 px too wide at 100 px, and so was every other pair the
file has an answer for.

## The problem, precisely

The advance of a glyph is a property of that glyph. The *gap between two
glyphs* is a property of the pair, and a loop that has already moved on to the
second glyph has forgotten the first. `advance_in_family` takes one codepoint;
`measure_text` and `draw_text` both called it once per character and kept
nothing.

So the first question was not "how do I read `kern`" — that is a table read, and
this engine has three of them already. It was: where does the pair live, when
everything below it is per glyph? The answer is *above* the per-glyph layer.
`advance_in_family` stays exactly what it was, and its callers, which are the
ones that walk a string, ask about the pair between two consecutive glyphs.

## Reading the table

`kern` is optional, and its absence is not a failure. A font that does not say
how its pairs close up is a font whose pairs do not close up, which is a normal
state of affairs and not a broken file. So `load_memory` looks for the table,
finds nothing, and carries on.

What it accepts is deliberately narrow: **version 0** (the Microsoft table —
the Apple version-1 table begins with the 32-bit `0x00010000` and is a different
format inside, so it is left out rather than guessed at) and a subtable whose
coverage says **format 0** in its high byte, **horizontal** in bit 0, and *not*
**cross-stream** in bit 2. Format 0 is a sorted array of records:

| field | size |
|---|---|
| `left` glyph id | u16 |
| `right` glyph id | u16 |
| value | **i16**, in font units |

Sorted by `(left << 16) | right`, which means a pair is a **binary search over
the file's own bytes** rather than a hash of thousands of entries built at load
time for a page that will use twelve of them. The table is held as two numbers —
where the pairs start, and how many there are — and nothing else.

The values are **signed, and they go both ways**. DejaVu Sans closes `AV` by 131
units of its 2048 (`-6.396 px` at 100 px) and *opens* `AA` by 57 (`+2.783 px`).
Any code that treats kerning as "subtract something" is wrong for this font, and
the tests say so out loud.

## Where it is applied

- `TrueTypeFont::kerning_units(left, right)` — the table's own number, in font
  units. Zero for a pair the table does not name, and zero for a `.notdef`
  glyph: a box does not kern against anything.
- `FontRenderer::kerning_px(...)` — that number scaled by the face's own
  `units_per_em`, not by a guess at 1000. Zero when the two codepoints are not
  drawn by the same face: a glyph from a family and a glyph from a fallback were
  never a pair the font had an opinion about.
- `measure_text` counts the adjustment between each pair of neighbours;
  `draw_text` applies the same one before it places the second glyph; the
  canvas's `layoutText` advance and its painter do the same. Measurement and
  painting ask the same question of the same face, which is the whole of the
  guarantee: a line is drawn as wide as it was measured.
- `font_kerning()` lets a caller skip the question for a face that has no table.

## The one thing that went wrong

The first attempt applied the adjustment **after** drawing the glyph it belongs
to — the natural shape, since the loop draws a glyph and then moves the pen. The
result measured correctly and drew unchanged, because `measure_text` sums and
does not care about order, while the painter cared very much: the gap between
`A` and `V` was widened after `V` had already been placed, so the adjustment
moved the *next* glyph instead.

It was caught by a probe that drew `AV` three ways — as the engine sees it, by
hand at the kerned positions, and by hand at the unkerned ones — and found the
engine's pixels identical to the *unkerned* pair. `[51..282]` hand-kerned,
`[51..295]` unkerned, and the engine agreeing with the wrong one. The pair
closes *before* its second glyph goes down. That probe became the third test.

## Verified

`make test` — **244 suites**, three of them new. The oracle is the file: the
test carries its own `cmap`-format-4 and `kern`-format-0 reader, ~120 lines
written out rather than borrowed from the engine, because a test that asks the
engine what the font says and then asks the engine whether it listened is not a
test. Six reverts, six failures:

| reverted | failure |
|---|---|
| the table never read (`m_kern_pairs = 0`) | `kerning_closes_the_pairs_the_table_names`: `Assertion 'font_kerning()' failed` |
| `measure_text` stops counting the pair | same test: `measured == advance_sum + kern` |
| scaled by 1000 instead of `units_per_em` | same test: `kern == expected` |
| binary search runs the wrong way | same test: `kern == expected` |
| values clamped to `min(0, value)` | same test: `kern == expected` on `AA` |
| adjusted after placing the glyph | `the_painter_closes_the_same_pairs_the_measurer_counted` |

The first of those six **passed** the first time it was run: all three tests
bailed out politely when `font_kerning()` said no, and a face that had never
read the table says no exactly like a face that has none. That is finding 97's
second half, and the fix was to make the file the ground truth — the tests now
assert that the table the file has is the table the engine found.

```
[kern] /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf: 2727 pairs in the `kern` table, 2048 units per em
[kern] 11 of 11 pairs named by the file, most closed by -16.992 px, most opened by 2.783 px, at 100 px
[kern] AV: -131 units, engine -6.396 px at 100 px
[kern] tests/wpt/fonts/Ahem.ttf: 0 pairs
[kern] Ahem 'AV' at 100 px = 200.000
[kern] default 'AV' at 100 px = 130.420, advances sum to 136.816
[kern] 'AV' at 200 px: kern -12.79, hand-kerned [51..282], engine [51..282], unkerned [51..295]
```

## Not done

- **`font-kerning`** is a recognised property name with a default of `auto` and
  no consumer. Giving it one means carrying a text style down to
  `measure_text`/`draw_text`, which is the same plumbing finding 84 describes —
  a real phase of its own, and one that `font-feature-settings` will want too.
  Kerning is therefore always on, which is what `auto` means.
- **`GPOS`**, which DejaVu also ships. A font with both usually means `GPOS` is
  authoritative; the `kern` table is what the older path reads, and reading both
  would double-count. `GPOS` is its own phase, with `GSUB` beside it.
- Format 2 (class-based) subtables, and vertical or cross-stream kerning.
- Kerning across an inline boundary or a word space: layout measures and paints
  per item, so a pair split between two boxes is not asked about. Browsers have
  the same seam for shaping runs, and this one moves when runs become the unit.
- `letter-spacing` is unaffected, which is correct — it is added to advances,
  not to pairs.

## Next

`GSUB` ligatures — the same table-directory and coverage machinery, a
different substitution — then bidi, which needs both.
