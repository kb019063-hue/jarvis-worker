# Phase 263 — a joiner is an invisible glyph, not an absent one

The AGENTS.md row (L1049) lists what is still open around Latin composition.
This phase took the join-control half of it, because the differential named the
defect exactly and the answer was two lines of spec.

## What the oracle said

`make shaping-oracle` compares `TrueTypeFont::shape_glyphs` against HarfBuzz
through pinned `uharfbuzz`, same font bytes, on 14 single-script runs. On
DejaVuSans before this phase:

```
{'match': 9, 'mismatch': 4, 'unsupported': 1}
  hebrew-marks  mismatch  same-placement
  arabic-marks  mismatch  same-placement
  arabic-zwnj   mismatch  same-placement
  arabic-zwj    mismatch  same-placement
```

Every one of them is `same-placement` — the glyph *positions* agree and the
glyph *identities* do not. That is the signature of a shape-of-the-stream bug
rather than a metrics bug, and it is what pointed at the two joiners.

## The defect

`shape_glyphs` marked any surviving `U+200C`/`U+200D` with a sentinel bit and
then **erased it from the run**:

```cpp
// Strip surviving default-ignorable join controls (`U+200C`/`U+200D`) so
// they never paint `.notdef` boxes or interfere with `GPOS` adjacency.
run.erase(std::remove_if(run.begin(), run.end(), ...), run.end());
```

The intent was right — a joiner must not paint — but erasing it is wrong twice:

1. **The count is short.** For `ب‌ب` (`0628 200C 0628`) the reference has three
   glyphs and this engine had two. For a shaper, the stream is the answer.
2. **Every later mark moves to the wrong base.** `mark_base` is an **index into
   `run`**. Removing element *k* slides everything after it down one, so a mark
   anchored to base *j > k* now reads a `mark_base` that points one element
   earlier. That is a silent misplacement of every mark after a joiner, in text
   no test had covered.

## What changed

One loop, in `src/render/FontRenderer.cpp`, after the advance pass and before
GPOS: a flagged control keeps its slot, takes the **space glyph** (OpenType's
*invisible glyph* for a default-ignorable), and is made inert — zero advance,
zero offsets, no mark base, no ligature or cursive attachment.

Keeping the slot is the whole fix: the glyph count matches, `mark_base` indices
stay true, and nothing paints because the advance and the offsets are zero.

## What the oracle says now

```
{'match': 11, 'mismatch': 2, 'unsupported': 1}
  arabic-zwnj   match
  arabic-zwj    match
```

**9 → 11 exact matches, no case made worse.** `hebrew-marks` and `arabic-marks`
remain `mismatch`; both are mark *ordering and offset* questions inside an RTL
cluster, not stream shape, and neither is touched here.

## Two things this phase deliberately did **not** ship

Both were written, measured, and taken back out because the measurement did not
support them:

1. **A UAX #15 canonical reordering of marks by combining class.** Twenty-odd
   lines, `combining_class` carried on `ShapingGlyph`. It changed the oracle by
   **nothing** — 11 matches with it and 11 without (measured both ways). The
   engine already has an SBL Hebrew rank table for exactly this, so the new sort
   was redundant as well as unmeasured. Removed, along with the field and the
   move of `canonical_combining_class` into `StringUnicodeTables.cpp` that it
   needed.
2. **Reversing a cluster's marks in the RTL emit.** Also measured at zero
   delta. Removed rather than left in on the theory that it looked right.

Shipping either would have been a change no oracle in this repository can
distinguish from a no-op — the sort of code that is wrong for five years and
looks intentional.

## Verification

- `tests/suites/263-a-joiner-keeps-its-slot.cpp` — the slot, the invisible
  glyph, the zero advance, that ZWNJ still prevents Lam-Alef and ZWJ still
  joins, and that a mark after a joiner still lands on its own letter. With the
  fix reverted it fails on `zwnj.size() == 3`; with it, it passes.
- `tests/test_suite.cpp`'s ZWNJ/ZWJ assertions asserted the *old* behaviour
  ("neither leaves a glyph!") and were corrected, not weakened: the slot and the
  zero advance are asserted, the glyph id is not, because a face without a cmap
  entry for a joiner legitimately draws `.notdef` in that slot.
- **400/400** suites, `make wpt` **9569/9824 with 0 unlisted**, `make
  render-conformance` **62/66 and 36/41 — identical to before**.
- `make shaping-oracle` 9 → 11, exit still 1 (the two remaining mismatches are
  the reason it is not a green gate, and it is not one).
