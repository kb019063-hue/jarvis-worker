# Phase 67 — `font-family` selects a face

## What was wrong

`font-family` was in the parser's list of known properties, and its value was
kept — `DomStyleBindings.cpp` even took care to preserve its case, which only
matters for a handful of properties. Nothing ever read it back.

So `font-family` was a fact about a stylesheet and not about what came out.
Every element on every page was measured and drawn in the one face the renderer
had loaded at start-up. The UA stylesheet says:

```
pre, listing, xmp, plaintext, code, kbd, samp, tt { font-family: monospace }
```

and a `<pre>` was laid out with DejaVu Sans advances. `WebBrowserServer`'s own
pages ask for `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
sans-serif` and for `ui-monospace, SFMono-Regular, Menlo, monospace`, and got
one face for all of it.

The same was true of the canvas `font` shorthand: `ParsedCanvasFont` had a
`family` member, defaulted to `"sans-serif"`, parsed out of the string — and
never read either.

## What changed

Three things had to exist for a family name to mean anything.

**A font has to be able to say which family it is.** That is the `name` table,
and it is the only place a `.ttf` says so. `TrueTypeFont::read_names()` reads
it out of the file without parsing the outlines: the table directory and one
table, not the whole font. That matters because the question is asked about
every `.ttf` on the host — the point of asking is to decide what to load, so
nothing can be loaded first. It reads name ids 1/2 and 16/17 (typographic
family and subfamily, preferred where present), and decodes UTF-16BE. A record
in an encoding it cannot decode is skipped rather than guessed at: a mangled
name matches the wrong family, and a missing one merely falls through to the
next candidate.

**The host's faces have to be known by name.** `FontRenderer` builds a registry
at construction: every discovered `.ttf` is described from its `name` table,
grouped into families, and the three generic CSS families are mapped to
whatever here fits them — `monospace` to a face with "mono" in its name, and so
on. There is no fontconfig and no dependency was added to get one; this is a
judgement made from the names the host's own fonts declare.

**A list has to resolve to one family.** `family_id_for()` splits on commas
that are not inside quotes, so `"Font, Inc", sans-serif` is two entries and not
three; matches case-insensitively with collapsed whitespace; tries left to
right; and returns `kDefaultFamily` when nothing in the list can be had, which
is what CSS asks for — an unresolvable family is the font the page would have
had anyway, not an error.

Then the choice is used: `face_for`, `measure_text`, `glyph_advance_px`,
`em_ascent_descent`, `get_glyph` and `draw_text` all take the family, and
`ComputedStyle::font_family` carries the list from the cascade, through
inheritance, into layout and paint.

Two details that are the whole difference between this working and looking like
it works:

- The glyph cache key includes the family. Without it the first line of a
  paragraph drawn in one family hands its rasterized glyphs to the next line
  drawn in another.
- `em_ascent_descent` takes the *element's* family, not the family of whichever
  fallback face supplied a rare codepoint inside the line. CSS asks for one
  line box per line, from one font.

## How it was verified

`make test` — 212 suites, two of them new, `font_family_selects_a_face` and
`font_family_reaches_layout_from_css`.

The oracle is **absolute** rather than a comparison between two faces. Every
glyph in Ahem — vendored under `tests/wpt/fonts` for the WPT corpus — is
exactly 1em wide: its advance is 1000 of its 1000 units per em, for all 278 of
its codepoints. So four characters at 16 px measure **64.0 px in Ahem and in
essentially nothing else**. Measured, not assumed:

```
Ahem        upem=1000   'X'=1.0000em  'i'=1.0000em  'W'=1.0000em
DejaVuSans             'i'=0.2778em  'W'=0.9888em
DejaVuSansMono         'i'=0.6021em  'W'=0.6021em
DejaVuSerif            'i'=0.3198em  'W'=1.0278em
```

The generic families are therefore checkable by measurement too: in
`monospace`, `'i'` and `'W'` advance the same amount, and in the default
proportional face they do not.

What the suites assert:

- `measure_text("XXXX", 16, 400, "Ahem")` is 64.0, and the same string with no
  family named is not — so the family was selected *because it was named*.
- Matching ignores case, quoting and repeated spaces.
- `"No Such Font At All, Ahem"` reaches Ahem: a family nobody has is skipped,
  not fatal.
- `"'Font, Inc', Ahem"` reaches Ahem: a comma inside quotes does not split.
- `"sans-serif, Ahem"` does **not** reach Ahem.
- `get_glyph('X', 16, false, ahem)` and `get_glyph('X', 16, false, kDefaultFamily)`
  differ in width: the cache is keyed by family.
- In `monospace`, `"iiii"` and `"WWWW"` measure the same; in the default they do
  not. `serif` and `sans-serif` differ from each other.
- On the CSS side: the property is kept as written, it inherits, a declaration
  on the element beats the inherited one, and an element with no `font-family`
  anywhere yields the default rather than a family called `""`.

The last five assertions are conditional on the host having the faces in
question. Whether a host has a serif or a monospace face is a fact about the
host, and a test that failed there would be failing about the wrong thing; the
Ahem assertions are unconditional, because Ahem is in the repository.

That the suite runs rather than returning early was checked the way it was in
Phase 64, by injecting `assert(false)` at the oracle and watching it abort
there.

## What is still open

- **`@font-face`** — nothing loads a font from a URL. The registry only knows
  files on disk.
- **`font-style: italic`** — `pick_face` reads the slant out of the subfamily
  name and deliberately never hands out an italic face, because nothing asks
  for one yet. `font-style` is still not implemented.
- **Weight is read from the subfamily name**, so "Book", "Bold" and "Bold
  Italic" are understood and "Condensed Light" is 400. The right answer is the
  `OS/2` table's `usWeightClass`, which is not parsed.
- **The generic mapping is a guess about the host**, made from font names. A
  host whose monospace face is not called anything with "mono" in it gets the
  default.
- **Shaping and bidi** remain, and they are the larger half of Tier 1 row 1.

## The three bugs worth remembering

All three are the same mistake — a sentinel that is also a value the thing it
guards can legitimately hold — in three different places. All three produced a
wrong answer rather than an error.

**`kDefaultFamily` was `0`, which is also the id of the first family found.**
Used as the "not chosen yet" marker for the generic families, it meant
`sans-serif` could never be assigned: the right answer (on this host, DejaVu
Sans, family 0) looked unchosen forever, and `sans-serif, Ahem` fell through to
Ahem. Fixed with an explicit `kNoFamily` and an explicit `matched` flag.

**The same sentinel, in the resolution loop.** A generic that legitimately
resolved to family 0 fell through to the next entry in the list, because
`result` never moved off the value that meant "not resolved yet".

**The same sentinel again, one level down.** `family_face` rejected
`family_id <= kDefaultFamily`, so family 0 could not be selected by name at
all. On a host with several fonts that is invisible, because the primary *is*
family 0 and selecting it changes nothing. On a host with no system fonts it is
not invisible: the first family registered — the one a test adds by hand — is
family 0, and `font-family: Ahem` silently drew nothing. Fixed by making
`kDefaultFamily` negative, so no real id can collide with it, and returning the
id from `register_font_file` so a caller does not have to look a family up by
name to use it.

The test now asserts, for every family the registry knows, that its own name
resolves to its own id and to nothing else — which is the guard for the third
bug. Verified by setting `kDefaultFamily` back to `0` and watching that
assertion fail:

```
test_suite.cpp:717: Assertion
`fr.family_id_for(fr.family_names()[i]) != FontRenderer::kDefaultFamily' failed.
```

The generic-family assertions state the correct behaviour in **both** regimes
rather than one: if the host has a face for the generic, the list stops there;
if it has none, CSS's own rule is to keep looking and the fall-through is
correct. Which regime applies is a fact about the host, and a test that assumes
one is a test about the machine it was written on. The suite prints what it
found — the faces, the generic mapping, and the two measurements — so a failure
on a shared runner says which world it was in.
