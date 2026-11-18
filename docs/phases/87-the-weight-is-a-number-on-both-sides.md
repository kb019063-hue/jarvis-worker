# Phase 87 — the weight is numbers on both sides

`font-weight` is a number in CSS and a number in the font, on the same scale:
`400` means 400 to both. Nothing in this engine had ever used that. The
request was flattened to a bool before the registry was asked, the file's own
`OS/2.usWeightClass` was never read, and the matching between the two was a
nearest-distance guess.

## The request

`face_for` took `bool bold` and asked `pick_face` for `bold ? 700 : 400`. So the
weight that reached the registry was never the weight the page wrote: 300 asked
for 400, 500 asked for 400, 900 asked for 700. Every `@font-face` weight range
narrower than "bold or not" — a thing Phase 74 built carefully — could not be
selected.

It takes the weight now: `int font_weight`, clamped to 1..1000, with `bold`
derived inside where a bool is genuinely the question (which of the host's two
primaries to fall back to, whether to double-strike a synthetic bold, which 8×8
bitmap to use). The same change went through `advance_in_family` and
`get_glyph`, because they are the same question asked per glyph.

The glyph cache keyed on the bool too. It now carries the weight in ten bits of
its own, for the reason it carries the slant: two weights of one family are two
bitmaps of one glyph, and a key that says only "bold" answers one with the
other.

## The font's answer

A face states what it is:

| field | means |
|---|---|
| `OS/2.usWeightClass` | 1..1000, the same scale CSS uses |
| `OS/2.fsSelection` bit 0 | italic |
| `head.macStyle` bit 1 | italic (the bit older fonts set instead) |

All three are read, in the same pass over the table directory that already
looked for `name`. The subfamily words stay as the fallback, for a file with no
`OS/2` table — a foundry that writes "Halbfett" is answered correctly by the
table and by nothing else, and guessing at words was always a way of being right
about this machine and wrong about another.

Nothing on this host disagrees with itself — every DejaVu file's name and table
say the same weight — so the test makes a file that does: the vendored Ahem,
whose subfamily says "Regular" in a name table nobody touches, with its `OS/2`
and `head` rewritten three ways, one field at a time.

## The match

CSS Fonts 4 §5.2.1 is not "nearest weight". It is a search in a declared order:

- target 400..500: up to 500 first, then down, then above 500;
- below 400: down first, then up;
- above 500: up first, then down.

The difference is visible: a family of 500 and 700 asked for 600 is an exact win
for 700 by the spec, and a tie by distance, which a tie-break of "whichever was
registered first" resolves the wrong way half the time. `pick_face` walks the
order and keeps the webfont tie-break it already had.

A face that declares a *range* containing the target is an exact answer —
`100 900` from a variable font is 600 for 600 — and a face that does not is
represented by the nearest end of its range.

## Relative keywords and the scale

`bolder` and `lighter` were synonyms for `bold` and `normal`, which is the one
thing a relative keyword cannot be. They resolve against the inherited weight
through §5.2.2's nine-step table: from 700, `bolder` is 900 and `lighter` is
400; from 300, `bolder` is 400; from 900, `bolder` is 900 because there is
nothing bolder.

The `font` shorthand used to resolve `bolder` to `700` at parse time, before
there was a parent to be relative to. It passes the keyword through now, and the
longhand — which runs where the inherited weight is known — decides.

A number outside 1..1000 is clamped, not taken as written.

## Verified

`make test` — 237 suites, three of them new. Eight reverts, eight failures:

| reverted | failure |
|---|---|
| neither `OS/2` nor `head` read | `a_face_weight_comes_from_the_font_not_its_name`: `Assertion 'has_weight(700)' failed` |
| `describe` ignores `usWeightClass` | same assertion |
| `fsSelection` italic bit | `Assertion 'after_os2 == 1' failed` |
| `head.macStyle` italic bit | `Assertion 'after_head == 2' failed` |
| §5.2.1 order back to nearest-distance | `font_weight_matching_follows_the_css_order`: `Assertion 'at_600 - at_700 < 0.5f' failed` |
| the weight flattened back to `400`/`700` | same test: `Assertion 'floor_350 - floor_500 > 1.0f' failed` |
| `bolder`/`lighter` back to synonyms | `bolder_and_lighter_are_relative_to_the_parent`: `Assertion 'b_from_bold == 900' failed` |
| no clamp | same test: `Assertion ... font_weight == 1000' failed` |

The first version of the ordering test **passed with the order reverted**, which
is how the flattening bug was found: 600 is `bold` by the `>= 600` rule, so the
requested weight never arrived. The test now asserts on 350 as well, which no
bool can express, and on a family whose faces are two different fonts so that
"which face answered" is a width and not an opinion.

## Not done

- `font-stretch` / `OS/2.usWidthClass`, the other axis in the same tables.
- Variable fonts, where one face answers a whole range: the range is honoured
  where `@font-face` declares one, but `fvar` is not parsed and a variable font
  is used at its default instance.
- `font-weight` animation (the transition path still lerps by the old rules).
- Italic and bold *synthesis* decisions are still "if the primary is not
  loaded"; a full `font-synthesis` story needs this phase's facts, which now
  exist.

## Next

Shaping: `kern` first, since DejaVu ships one and the numbers are checkable,
then `GSUB` ligatures, then the complex-script work bidi will need.
