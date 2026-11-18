# Phase 93 — a ligature is one glyph, and the pen had one advance per character

Text layout counted one advance per character. Every character has one, so this
worked, and it is the wrong unit: `f` and `i` are two characters and, in a font
that says so, **one glyph** — and that glyph's advance is its own. DejaVu Sans
asks for five such joins in `GSUB` (`ff`, `fi`, `fl`, `ffi`, `ffl`), and the
engine had never opened the table.

Neither had it any way to draw such a glyph: everything below text painting was
addressed by *codepoint*. A ligature has no codepoint. So this phase is two
things at once — read `GSUB`, and give the renderer a unit it did not have: the
shaped run.

## The table

`GSUB` is three lists: scripts, features, lookups. The engine reads

- the **`latn`** script record, or **`DFLT`** when a font has none, and its
  default language system's feature indices (plus its required feature),
- the features tagged **`liga`** (common) and **`rlig`** (required) — nothing
  else, so `dlig`/`hlig`/`salt`/`aalt` stay off, which is what a browser does
  until a page asks for them,
- and the **type-4** lookups those features name, following a **type-7**
  (extension) wrapper to the type it points at.

A type-4 subtable is a coverage set (format 1 or 2) of first glyphs, each with a
set of substitutions `(produced glyph, the glyphs consumed after the first)`.
All of them, from every lookup the features name, are merged into one table
sorted by first glyph, and each set is sorted **longest first** so that `ffi`
wins over `ff` at the same position. The table's own order is not a promise
about which match to prefer.

Then `apply_ligatures` is a left-to-right longest match over a run of glyph ids —
which is exactly what the rule says, and about fifteen lines.

## The new unit

`FontRenderer::shape_text` turns a string into `ShapedGlyph`s:

| field | what it is |
|---|---|
| `glyph` | a glyph id, which after a substitution is not any character's |
| `face` | the face that will draw it, resolved once |
| `advance` | that glyph's advance, from that face's `hmtx` |
| `shear` | whether a synthesised oblique has to slant it |

A run is the stretch one face draws; ligatures are applied inside a run and
never across one, because `latn`'s table has an opinion about `f` followed by
`i`, not about whichever face happens to supply the `i`.

`measure_text`, `draw_text` and the canvas's painter all walk that list now.
The canvas is the interesting one: it paints glyph by glyph because it needs
each bitmap's ink extents, so it could not simply call `draw_text`. It gets the
list instead, and three small accessors (`shape_text`, `kerning_px_shaped`,
`get_glyph_shaped`) — a second painter walking its own codepoints is how a
string measures one width and draws another.

The glyph cache grew a second key shape: glyph id, pixel size, shear, and 16
bits of the face's address, because the face that has a ligature need not be the
family's own (a fallback run) and a codepoint is not available to look it up
with. `face_for` empties the cache when it adds a fallback: the vector
reallocates, and an entry naming a face by address is stale the moment one is.

Kerning is now asked of **glyph ids** (`kerning_units_for_glyphs`), with the
codepoint overload a convenience that maps through the `cmap` first. After a
ligature there is no codepoint, and the `kern` table's opinion about `f` + `i`
is not an opinion about the `fi` glyph — so a joined pair loses whatever kern
the letters had. That is what a `kern`-only font means, and `GPOS` is where the
interesting pairs live.

## Measured

```
[liga] /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf: 5 substitutions in `liga`/`rlig` for `latn`
[liga] ff  -> glyph 5041: file 68.896 px, letters 70.410 px, engine 68.896 px
[liga] fi  -> glyph 5042: file 62.988 px, letters 62.988 px, engine 62.988 px
[liga] fl  -> glyph 5043: file 62.988 px, letters 62.988 px, engine 62.988 px
[liga] ffi -> glyph 5044: file 96.680 px, letters 98.193 px, engine 96.680 px
[liga] ffl -> glyph 5045: file 96.680 px, letters 98.193 px, engine 96.680 px
[liga] the file joins `fi` into glyph 5042; U+FB01 is glyph 5042
[liga] `fi` drawn as 2491 pixels, U+FB01 as 2491, f+i as 2404
[liga] canvas 'ff': measured 41.338 px (file 41.338), 816 pixels vs 816 as U+FB00 vs 826 as f+f
```

`fi` and `fl` are the same width joined as unjoined in this font — the ligature
is a *shape*, not a width — so width alone cannot see them. The test that can is
the ink: the engine's `fi` is compared **pixel for pixel** with the drawing of
`U+FB01`, the codepoint the `cmap` gives for the same glyph, and it must be
identical to that and different from `f` followed by `i`. The canvas gets the
same treatment against `U+FB00`.

## Verified

`make test` — **249 suites**, four of them new. The oracle is the file: a
`GsubProbe` in `tests/test_suite.cpp` reads `GSUB`'s script, feature and lookup
lists itself (~150 lines, not borrowed from the engine) and says what each
string should become:

| reverted | failure |
|---|---|
| `GSUB` never read | `ligatures_join_the_glyphs_the_table_names`: `measured == expected` |
| `apply_ligatures` returns without substituting | same |
| `measure_text` back to summing codepoint advances | same (via the kerning suite as well) |
| the painter back to one glyph per codepoint | `a_ligature_is_drawn_as_the_glyph_the_table_names`: `shaped == as_glyph` |
| the canvas's shaping skips ligatures | `the_canvas_measures_the_ligature_it_paints`: `painted == as_codepoint` |

## Not done

- `GPOS`: kerning proper, marks, and the values that most modern fonts carry
  only there. `kern` is what this reads, and a font with both usually means
  `GPOS` is authoritative.
- Other lookup types: single (1), multiple (2), alternate (3), contextual (5/6)
  — so no `ccmp`, no `locl`, no `calt`, and no substitution that depends on
  context.
- Script choice is `latn`, then `DFLT`: a page of Cyrillic or Greek gets the
  Latin substitutions (harmless — they only name Latin glyphs) and not its own.
- `font-variant-ligatures` and `font-feature-settings` have no consumer, so
  `no-common-ligatures` cannot be asked for. Same plumbing as `font-kerning`
  (finding 84).
- Marks and cluster boundaries: a run is a run, and no character class is
  consulted, so `f` + combining mark + `i` does not join — true to the tables
  here, wrong for the marks some fonts put between.

## Next

`GPOS` (kerning, and marks), then bidi, which needs shaped runs as its unit.
