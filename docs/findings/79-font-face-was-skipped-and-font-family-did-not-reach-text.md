# Finding 79 — `@font-face` was skipped, and `font-family` never reached the text

**Severity:** high — one rule the parser threw away, and one gap that made
Phase 67's whole `font-family` work invisible on the page.

**Found by:** writing the `@font-face` test. Three separate bugs came out of it,
and only the first is the one being looked for.

## What was wrong

**`@font-face` was not parsed.** `parse_stylesheet_blocks` handles `@keyframes`,
`@media`, `@layer` and `@supports`, and ends with:

```cpp
// Skip other @rules like @import, @charset, @font-face, @container
skip_at_rule_remainder();
```

So a page that brought its own font was rendered in whatever the host happened
to have. For a page whose whole look is its typeface, that is the page not
working.

**`font-family` did not reach the box that measures text.** This is the one that
matters, because Phase 67 closed finding 73 and this was still true afterwards.
`LayoutTree::inherited_only_style` builds the `ComputedStyle` every text box is
given, and it carried `color`, `font_size`, `font_weight`, `line_height` — and
not `font_family`. A text node therefore measured and drew in the default face
whatever its element asked for. `PaintCommand` reads `frag.style.font_family`,
which is the same emptied style, so both measurement *and* drawing were
affected.

**The UA stylesheet had no `pre { font-family: monospace }`.** So even with
inheritance fixed, `<pre>` was laid out proportional. The engine had a
`monospace` generic it could resolve and not one element that asked for it.

**And `resolve_url` corrupted every `data:` URL.** It splits a URL on `/` and
drops empty segments, which is right for a path of segments and wrong for an
opaque one. `data:font/ttf;base64,HAAD//wMg...` came back with its `//`
collapsed to `/` — 17 characters and 13 bytes shorter, decoded to something
that was not a font. Only `http`, `https` and `file` were spared, because they
were the three schemes the function knew about. `mailto:` and `javascript:`
had the same problem, and it was not an edge case: a base64 payload a few
hundred characters long contains `//` about once in every 64.

## Why the tests missed it

`font_family_reaches_layout_from_css` asserted that `ComputedStyle.font_family`
held the value on an *element*. It never asked what a text box got, or what the
line builder measured. A property can be resolved correctly and still not be
used, and the distance between those two facts is where this lived.

There is no substitute for asserting on the observable. The new tests read the
width of the fragment the line builder produced, which is the number the
painter draws with.

## What was done

- `CssParser` parses `@font-face` into a `FontFaceRule`: the family name the
  rule declares, the `src` fallback list with each source's `format()` hint, a
  `font-weight` that may be a range, and `font-style`.
- `FontRenderer::register_webfont` registers a face under the page's name,
  ahead of any host family that shares it. The bytes are fetched by a closure
  the caller supplies, called only when the face is first wanted — a sheet that
  declares twenty families has usually drawn in one of them.
- `DocumentStyles` resolves each `src` URL and registers it. Fonts are fetched
  in CORS mode, as CSS Fonts asks, because a font's bytes end up in surface the
  document can read.
- `inherited_only_style` carries `font_family`.
- The UA stylesheet gives `pre, xmp, plaintext, listing, code, kbd, samp, tt`
  `font-family: monospace`.
- `resolve_url` returns an absolute URL as it stands, whatever its scheme.

## The oracle

Ahem, vendored under `tests/wpt/fonts` for the WPT corpus. Every glyph in it is
exactly 1em wide, so four characters at 16 px measure 64.00 px in Ahem and in
nothing else on this host. The test serves it through a `data:` URL in `src`,
so the bytes arrive the way a browser's would and not through a call into the
rasterizer — which is what found the `resolve_url` bug.

Verified: `make test` — 217 suites, three of them new.
