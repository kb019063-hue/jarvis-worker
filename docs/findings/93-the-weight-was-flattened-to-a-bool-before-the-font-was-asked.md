# Finding 93 — the weight was flattened to a bool before the font was asked

**Severity:** high — a whole property reduced to two possible answers, plus a
glyph cache that would have answered one weight with another.

**Found by:** writing the test for CSS's weight-matching order, and watching it
pass with the order reverted.

## What was wrong

**The registry was only ever asked two questions.** `FontRenderer::face_for`
took `bool bold`, and `family_face(family_id, bold ? 700 : 400, bold, ...)`. The
page's `font-weight` was already down to a bool by then — `font_weight >= 600` —
so the weights that ever reached `pick_face` were exactly **400 and 700**, in
every family, for every face of every font on the host. A family with a 300 face
could not be asked for it. A page's `font-weight: 300` asked for 400. Every
`@font-face` weight range narrower than "bold or not" was decoration.

Phase 67 built weight selection on top of this and it has been inert ever
since: `FaceEntry::weight_min`/`weight_max` existed, `pick_face` compared
against them, and the two numbers it compared were always 4 and 7.

**The glyph cache keyed on the same bool.** `get_glyph` packed `bold ? 1 : 0`
into the key. With one face per family that was harmless — a family at 400
draws the same glyph whatever weight was asked for — but the moment a family has
a 300 face and a 500 face, one bit of key cannot hold two bitmaps, and the first
rasterized weight answers for both.

**The weight in the font's own table was ignored.** A file states its weight in
`OS/2.usWeightClass`, on exactly the 1..1000 scale CSS asks in; a file states
its slant in `OS/2.fsSelection` bit 0 or `head.macStyle` bit 1. None of it was
read. Instead `weight_and_slant` matched English words against the subfamily
name — `"bold"`, `"semibold"`, `"black"`, `"italic"`:

```cpp
struct Names {
    std::string family;
    std::string subfamily;   // and that was all
};
```

That works for DejaVu and fails for a foundry that writes "Halbfett" or "Gras",
and it is a guess about data the file already states.

**`bolder` and `lighter` were synonyms for `bold` and `normal`.** They are
relative keywords: CSS Fonts 4 §5.2.2 gives a nine-step table, and from 700
`bolder` is 900, from 300 it is 400, and `lighter` from 700 is 400. Being
synonyms made the keyword mean one fixed weight whatever the parent was, which
is the one thing a relative keyword cannot mean.

**And a number outside the scale was taken as written.** `font-weight: 5000`
was 5000.

## What was fixed

- `face_for`, `advance_in_family` and `get_glyph` take the CSS weight, an `int`,
  and derive `bold` where a bool is genuinely what is wanted (the primary face,
  the synthetic-bold doubling, the 8×8 fallback).
- The glyph cache carries the weight, in ten bits of its own.
- `TrueTypeFont::Names` carries `weight` and `italic`, read from `OS/2` and
  `head` in the same directory walk that already found `name`. The subfamily
  words remain as the fallback for a file with no `OS/2` table.
- `pick_face` implements §5.2.1's search order — not nearest-weight, which
  breaks ties the wrong way. Above 500 it looks *up* first, so a family of 500
  and 700 asked for 600 is answered by 700, and a distance rule answers it with
  whichever happened to be registered first.
- `bolder`/`lighter` resolve against the inherited weight; the `font` shorthand
  passes the keyword through instead of flattening it to 700 at parse time;
  numbers clamp to 1..1000.

## The tell

The test that was meant to catch the ordering **passed with the ordering
reverted**. That is what sent me looking: the assertion compared 600 against
700, and 600 is `bold` by the `>= 600` rule, so `face_for` had already thrown
the requested weight away and asked for 700 either way. A test can only observe
a code path that runs, and this one never ran — which is the same shape as
finding 80's `resolve_url` and finding 84's `draw_text`: the layer under test
was fine, and the number arriving at it was not the number the user wrote.
