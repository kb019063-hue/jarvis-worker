# Finding 267 — the joiner that was not there

**Severity:** correctness, and a quiet one. `TrueTypeFont::shape_glyphs` deleted
default-ignorable join controls from its own output. That made the glyph count
short, which is visible — and it moved the *indices* that mark attachment is
stored in, which is not. Every mark after a ZWNJ or ZWJ in the same run was
anchored to the base before its own, and no test in the repository noticed.

**Found by:** reading the oracle's verdict column instead of its headline. The
shaping differential said `9 match, 4 mismatch`, and every mismatch was
`same-placement` — the positions agreed and the glyphs did not. A metrics bug
does not do that. A bug in the shape of the stream does.

## The two failing cases

```
arabic-zwnj  'ب\u200cب'
  HarfBuzz: [[1366,1928,0,0,0], [3,0,0,0,0], [1366,1928,0,0,0]]   <- three
  Litebird: [[1366,1928,0,0,0], [1366,1928,0,0,0]]                <- two

arabic-zwj   'ب\u200dب'
  HarfBuzz: [[5258,2011,0,0,0], [3,0,0,0,0], [5259,570,0,0,0]]
  Litebird: [[5258,2011,0,0,0], [5259,570,0,0,0]]
```

Glyph `3` is HarfBuzz's space glyph: the **invisible glyph** OpenType gives a
default-ignorable. It occupies a slot, paints nothing, advances nothing. This
engine had no slot to give it, because of one `erase`:

```cpp
// Strip surviving default-ignorable join controls (`U+200C`/`U+200D`) so
// they never paint `.notdef` boxes or interfere with `GPOS` adjacency.
run.erase(std::remove_if(run.begin(), run.end(),
                         [](const ShapingGlyph& sg) {
                             return (sg.feature_mask & (1u << 31)) != 0u;
                         }),
          run.end());
```

## Why the erase was worse than a missing glyph

The comment's concern — "they never paint `.notdef` boxes" — is the right
concern and the wrong remedy. The remedy is a *zero advance*, which is what
makes a glyph invisible. Erasing is a different operation with a second
consequence nobody was looking at.

`ShapingGlyph::mark_base` is an **index into the vector**:

```cpp
struct ShapingGlyph {
    ...
    int32_t mark_base{-1};   // index into the run, not a pointer
};
```

so `run.erase(run.begin() + k)` renumbers every element after *k*, while every
`mark_base` still means what it meant before. A mark attached to a base at
index *j* now reads the glyph at *j − 1*. In `ب‌ب` there is no mark, so the
damage was only the count. In `بـ‌◌َ` — a base, a joiner, then a vowel — the
vowel lands on the joiner's neighbour and the rendered text is wrong while
every number the engine reports about it is self-consistent.

That is the class of bug this repository's oracles exist for, and it survived
because the only cases with a joiner in them had no marks after it.

## The fix

Keep the slot; make it inert.

```cpp
for (ShapingGlyph& sg : run) {
    if ((sg.feature_mask & (1u << 31)) == 0u) continue;
    sg.glyph = glyph_index(0x0020u);   // the invisible glyph
    sg.x_advance = 0;  sg.y_advance = 0;
    sg.x_offset = 0;   sg.y_offset = 0;
    sg.mark_base = -1; sg.lig_id = 0;  sg.lig_comp = 0;
    sg.cursive_chain = -1; sg.cursive_dx = 0; sg.cursive_dy = 0;
}
```

It runs **after** the advance pass (an earlier attempt put it before, and the
loop that fills every `x_advance` from `m_advance_widths` simply overwrote the
zero — the oracle caught that too, by not moving at all) and **before** GPOS, so
the control is inert to lookups as well as to the painter.

## Result

`make shaping-oracle` on DejaVuSans: **9 → 11 exact matches**, `arabic-zwnj`
and `arabic-zwj` both `match`, **no case made worse**, all 13 supported cases
still `same-placement`.

## The two changes this finding deliberately threw away

Both were written and measured, and both moved the oracle by **zero**:

- a UAX #15 canonical mark sort by combining class (with a `combining_class`
  field plumbed through `ShapingGlyph`, and `canonical_combining_class` moved
  into `StringUnicodeTables.cpp` so the shaping probe could link it);
- reversing a cluster's marks in the RTL emit.

Eleven matches with them, eleven without. The engine already carries an SBL
Hebrew rank table for the same purpose, so the sort was redundant as well as
unmeasured. They are in neither the commit nor the tree. A change no oracle here
can tell from a no-op is a change that will be maintained as if it mattered.

## What is still open

`hebrew-marks` and `arabic-marks` — mark order and mark x-offsets *within* an
RTL cluster. For `שָׁ` (QAMATS then SHIN DOT, ccc 18 then 24) the reference
answers `[1305, 1314]` and this engine answers `[1314, 1305]`, for both input
orders, so its normalization is already order-independent and the disagreement
is the emit order. For Arabic the mark x-offsets differ by more than the mark
swap explains (`-250` vs `-1130`), which points at anchor accumulation rather
than ordering. Both are their own claims, with the differential already in place
to grade them.
