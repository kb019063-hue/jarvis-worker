# Phase 64 — Per-glyph font fallback

## 1. What was wrong

The renderer loaded one TrueType face per weight and asked it for every
codepoint. A codepoint outside that face's `cmap` did not produce a wrong
glyph — it produced **no glyph at all**.

`TrueTypeFont::glyph_index` returns `0` for a codepoint the face does not
cover, and `0` is `.notdef`. Downstream, two things happened:

- `get_glyph` rasterized `.notdef`, which for most faces is an empty outline,
  so `GlyphBitmap::width` and `height` came out `0` and `draw_text` skipped
  the glyph entirely — the character was invisible, not boxed.
- `glyph_advance` fell through to `m_advance_widths[0]`, the `.notdef`
  advance, so the text was measured as if something had been drawn.

A page with a single character the one system font lacks therefore lost that
character silently, and the line was measured as though it were there.

This was Tier 1 item 1 of `ROADMAP.md` → *What "usable" means*: *"A glyph it
lacks paints nothing."*

## 2. What changed

`TrueTypeFont::has_glyph(codepoint)` — `glyph_index(codepoint) != 0`. The
question fallback turns on.

`FontRenderer` now keeps an ordered set of fallback **paths**, discovered once
at construction by walking the system font directories, and loads the faces
**lazily**: a face is parsed when a codepoint needs it and not before, so a
host with hundreds of fonts costs a directory walk and nothing else. Loading
is capped at `kMaxFallbackFaces` (24).

`FontRenderer::face_for(codepoint, bold)` answers which face draws a
codepoint:

1. the primary (bold or regular) when it covers it;
2. else the first already-loaded fallback that covers it;
3. else load more of what discovery found, one at a time, stopping at the
   first that covers it;
4. else the primary — so a codepoint no face covers still draws one
   `.notdef`, from a predictable face, rather than nothing.

`get_glyph` and `glyph_advance_px` both go through `face_for`, so rasterizing
and measuring agree about which face answered. They did not have to before,
because there was only ever one face.

Two things deliberately did **not** change:

- **`em_ascent_descent` stays on the primary.** CSS wants one line box per
  line, from the element's own font — not from whichever face happened to
  supply a rare codepoint inside that line.
- **`draw_text` computes its baseline from the primary**, for the same
  reason. Fallback changes which face supplies a glyph, not where the
  baseline sits.

`set_fallbacks(paths)` replaces the set and **clears the glyph cache**, since
a cached glyph may have come from a face that is no longer in it.

## 3. How it was verified

```
make test    # 207 suites, all passing (was 204 + 80 cross-origin)
```

`tests/test_suite.cpp` → `per_glyph_font_fallback`. The oracle is the
measured `cmap` coverage of two faces, not an assumption about them:

| | `'A'` U+0041 | `'Ā'` U+0100 | `'一'` U+4E00 |
| :--- | :--- | :--- | :--- |
| DejaVuSans (primary) | yes | yes | **no** |
| Ahem (vendored, 278 cps) | yes | **no** | **yes** |

Ahem is the web-platform-tests font in `tests/wpt/fonts/`. It covers 20
codepoints DejaVu does not — `U+3000`, `U+4E00` and other CJK it carries for
the tests — and that is what makes the third case a test of the *chain* rather
than of either face alone. Those numbers came from dumping both `cmap`
subtables (format 4 for Ahem, format 12 for DejaVu), not from reading them.

The suite asserts that with Ahem installed, `U+0041` and `U+0100` still come
from the primary and `U+4E00` comes from Ahem; that `get_glyph(U+4E00)`
produces a bitmap with pixels; and that `measure_text("一")` is positive.

**The suite was proven to run rather than skip.** It has three early returns
(no system font, missing Ahem, or a primary that already covers CJK), and a
skipping test passes for the wrong reason. A deliberate `assert(false)` was
placed on its last line and the build run: it aborted there
(`test_suite.cpp:675`), which proves all three guards were false and every
assertion in between executed. The instrumentation was then reverted and the
suite re-run green.

## 4. What is still open

- **`font-family` is parsed and never used.** `CssParser.cpp` accepts it;
  nothing reads it. Every element gets the one system face. This is the next
  slice of Tier 1 item 1.
- **No `@font-face`.** No webfont has ever loaded, so the discovery walk is
  the only source of faces. WOFF2 needs Brotli; WOFF 1 needs zlib inflate,
  which `src/net/Deflate.cpp` already has.
- **No italic face** is loaded — only regular and bold.
- **No shaping.** `GPOS`/`GSUB` are unread, so no kerning and no ligatures,
  and complex scripts are still wrong. Fallback picks a face per codepoint;
  it does not run a shaper over a run.
- **No bidi** (UAX #9) — Arabic and Hebrew still come out reversed.
- **Discovery is a directory walk, not `fontconfig`.** It finds files, not
  families, and it cannot see a face installed somewhere unusual.
- **The `.notdef` path is still invisible.** When no face covers a
  codepoint, step 4 returns the primary and most faces draw nothing for
  `.notdef`. A real tofu box would at least be visible.
