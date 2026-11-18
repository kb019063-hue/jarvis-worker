# 88. SVG text was invisible even when Canvas could draw the same glyphs

**Wrong:** A `<text>` element with valid coordinates and fill was rendered as transparent pixels by `SvgRenderer`, because the shape builder has no text geometry and the tree walk ignored text nodes. Splitting a run into `<tspan>` would have been lost as well. The gap affected `<svg>` in layout and decoded SVG images alike.

**Changed:** Phase 82 adds a document-order cursor for `<text>`/`<tspan>` with inherited font and basic SVG paint, relative and absolute positioning, text-anchor for a single run, whitespace normalization, and Canvas 2D glyph drawing. Existing SVG transforms and clips still bound the render. The walk is recursion- and input-length-bounded.

**Measured:** `make test` prints **241/241** suites; `make verify-media` prints **105** checks (including 7 SVG checks), zero failures. The native test compares each pixel with a separately drawn Canvas text run for anchor, translation, tspan paint/position and whitespace. The media check independently asserts exact translation of the glyph alpha mask and separate red/blue paint, without depending on which host font was selected. This phase does not change or claim a WPT score.

**Still open:** Multi-run `text-anchor` chunk layout, per-character positioning and rotation lists, `textPath`, full SVG whitespace rules and text gradients, complex shaping/bidi, and AVIF decoding. Rendering these few supported runs is not a claim of complete SVG text support.
