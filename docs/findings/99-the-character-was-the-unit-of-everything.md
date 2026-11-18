# Finding 99 — the character was the unit of everything below text

**Severity:** high — the shape of the text was decided by a loop that only knew
how to count characters, and nothing below it could express anything else.

**Found by:** asking how a ligature would be drawn at all, and finding that
there was no way to say "this glyph" — only "this character".

## What was wrong

Three layers independently assumed a character is a glyph:

- **Measurement** added one advance per codepoint. True for a character, false
  for `fi`, which is one glyph with one advance.
- **Painting** asked the font for a bitmap *by codepoint*: `get_glyph(cp, ...)`
  mapped through the `cmap` and rasterised the result. A glyph a substitution
  produced is not in the `cmap` and has no codepoint to ask with.
- **The canvas** painted glyph by glyph — it needs each bitmap's ink — so it
  could not reuse the text painter, and had its own loop over codepoints.

So `GSUB` could not be read into anything: even with the table parsed, there was
nowhere to put the answer.

## What it took

A unit between text and glyph: `ShapedGlyph` — glyph id, the face that draws it,
its advance, and whether it must be slanted. `shape_text` builds the list once,
splitting the string into runs by face (ligatures do not cross a face boundary),
mapping each run's codepoints to glyph ids, applying the substitutions, and
resolving each resulting glyph's advance. Measurement sums that list, the text
painter walks it, and the canvas — which cannot call the text painter — is handed
the same list rather than being left to walk its own codepoints.

The cache was addressed the same way everything else was: by codepoint. It now
has a second key shape keyed by **glyph id and face**, because a ligature's glyph
is not reachable from a codepoint and the face that has it need not be the
family's own. That key names the face by address, which is only safe while the
face does not move — so `face_for` empties the cache when it adds a fallback and
the vector reallocates.

## The tell

`fi` in DejaVu Sans is exactly as wide joined as unjoined (1290 units either
way). A width test cannot see the substitution at all: it passes whether the
engine shapes or not. The first draft of the test measured, found the numbers
agreed, and proved nothing.

The test that works compares **pixels**: the engine's `fi` against the drawing of
`U+FB01`, which the `cmap` maps to the same glyph the `GSUB` lookup produces
(5042). Same glyph, same pixels, to the last one — 2491 of them — and different
from `f` followed by `i` (2404). That also makes the oracle independent: the
file says which glyph, and a codepoint that reaches the same glyph is a second
road to the same picture.

## What this changes

- `TrueTypeFont` reads `GSUB`'s `latn`/`DFLT` `liga` and `rlig` type-4 lookups and
  applies them to a run of glyph ids, longest match first.
- `FontRenderer::shape_text` is the unit between text and glyph; `measure_text`,
  `draw_text` and the canvas painter all walk it.
- Kerning is asked of glyph ids. A joined pair loses the letters' kern, because
  the `kern` table names the letters and not the ligature — which is also true of
  a browser using `kern` alone.
- The glyph cache is keyed by glyph id and face as well as by codepoint.
