# Phase 74 — `@font-face` brings the font with the page

A page may bring its own typeface. `@font-face` declares a family name and
where the bytes are; everything downstream — the cascade, `font-family`
matching, measurement, drawing — is what Phase 67 built and needs no change.
What was missing was the declaration itself, and two bugs that only came out
once it was tested.

## The descriptor set

```css
@font-face {
    font-family: "Page Font";
    src: url("a.woff") format("woff"), url(b.ttf) format("truetype");
    font-weight: 100 400;
    font-style: italic;
}
```

The family is the name the *rule* declares and not the name inside the file.
That is the whole difference between a webfont and a face found on disk: a page
may serve "Body Text" from a file that calls itself something else entirely,
and several rules may serve several weights of one family from several files.

`src` is a fallback list, tried in order: a source whose bytes will not parse
is not a failure of the rule, it is an instruction to try the next one. That is
the point of `url(a.woff) format("woff"), url(a.ttf) format("truetype")` — and
`format()` is a promise about the bytes, so a promise of `woff` or `woff2` is a
source this engine skips rather than fetches for the parser to reject.

`local()` names a font already on the host, which is answered out of the
registry: the common idiom is `local("Font"), url(font.ttf)` — use what is
installed, fetch if it is not.

## Nothing is fetched until it is wanted

`register_webfont` takes a closure per source, not bytes. The closure knows the
URL and the origin policy to fetch it under; the rasterizer calls it the first
time a face is needed and not before. A stylesheet that declares twenty
families has usually drawn in one of them by the time layout is over, and a
webfont is a network round trip.

It also keeps `FontRenderer` free of any idea of a network. The caller that
parsed the rule knows the URL, the document's origin and the CORS mode, and
captures them; the rasterizer knows only that it asked for bytes and may be
told no.

Fonts are fetched in CORS mode. CSS Fonts asks for this even where an image
would not be, because a font's bytes end up in surface the document can read
back; a cross-origin font has to say so. Nothing is loosened — a sheet that
already asked for stricter handling keeps it.

## A webfont outranks a host family of the same name

If a page declares `@font-face { font-family: Ahem }` on a host that has Ahem
installed, CSS's answer is the page's. `register_webfont` puts the face at the
head of the family's list, and `pick_face` lets a webfont win ties outright —
and, having won, not be displaced by a host face that happens to be nearer the
weight asked for.

Weights are ranges. A face on disk declares `400` or `700`; a rule may declare
`100 400`, and the face is eligible only for weights inside it, at distance
zero.

## The three bugs this uncovered

**`font-family` never reached the text.** `inherited_only_style`, which builds
the style every text box is given, carried `font-size` and `font-weight` and
not `font-family`. So Phase 67 was inert for page text: resolved correctly on
the element, used nowhere. `font_family_reaches_layout_from_css` had asserted
the property on an element and never on what a text box got — a property can be
resolved correctly and still not be used, and the distance between those two
facts is where this lived.

**The UA stylesheet had no `pre { font-family: monospace }`.** Even with
inheritance fixed, `<pre>` was laid out proportional. The engine had a
`monospace` generic it could resolve and no element that asked for it.

**`resolve_url` corrupted every `data:` URL.** It split on `/` and dropped
empty segments, so a base64 payload's `//` collapsed to `/`. Only `http`,
`https` and `file` were spared, because they were the schemes it knew about.

The last one is worth dwelling on: it was found by the test, and found only
because the test serves the font through a URL rather than handing bytes to the
rasterizer. Testing the way a user arrives is what finds the bugs in between.

## Verified

`make test` — 217 suites, three of them new:

- `font_face_declares_the_family_the_page_names` — the descriptors, the `src`
  fallback list, `local()`, weight ranges, and that an `@font-face` block does
  not leak into the cascade.
- `font_face_brings_the_family_the_page_names` — the whole path: CSS, URL
  resolution, fetch, parse, family matching, measurement, and the width the
  line builder produces. Includes a `src` list whose first entry is four zero
  bytes, and a webfont that shares a name with a host family.
- `font_family_reaches_the_box_that_measures_text` — `<pre>` measures 'i' and
  'W' the same, and `<p>` beside it does not.

## Not done

- WOFF and WOFF2: the parser reads TrueType `glyf` outlines. A `format()` hint
  naming those is skipped rather than fetched.
- `unicode-range`: a rule's range is not consulted, so a family is asked for
  codepoints it does not have and per-glyph fallback covers for it.
- `font-display`, and the swap period: a face is fetched when it is wanted and
  the text waits.
- A webfont declared inside a shadow root is not registered: `build_resolver`
  walks the document tree, which does not cross into one.
- Webfonts are scoped to the process and not to the document, because the
  rasterizer is a singleton. `clear_webfonts()` exists for the embedder that
  wants a clean slate between pages; per-document scoping needs a per-document
  rasterizer.

## Next

`font-style`, then shaping: `liga`, kerning, and the cluster-forming that turns
a string of codepoints into a string of glyphs.
