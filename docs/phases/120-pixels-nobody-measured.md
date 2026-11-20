# Phase 120 — pixels nobody measured

Task row: **Nothing measures a pixel.** Added to `AGENTS.md` → Open tasks by
this phase, because it was not a row: `ROADMAP.md` Tier 1 row 8 carried it as
*"Nothing measures a pixel. Every layout and font claim on this page is
unverified."* Finding 121.

## 1. What was wrong

The engine renders. `--render-png` writes a valid 8-bit RGBA PNG, and the
pixels in it were correct the whole time — a 50×50 `#ff0000` div really did
land on `(0,0)`–`(49,49)`:

```
$ ./build/litebird --render-png /tmp/a.png block.html
Rendered PNG saved to: /tmp/a.png          # IHDR 800x600 depth=8 colortype=6
$ python3 read_png.py /tmp/a.png
(10,10) = (255, 0, 0, 255)      (49,49) = (255, 0, 0, 255)
(60,10) = (255, 255, 255, 255)  (10,60) = (255, 255, 255, 255)
```

Nothing asserted those four numbers. Every scoreboard in the tree — HTML
tree-construction, the DOM corpus, WPT, the XML suite, URL, JS — reads a
*textual* answer. None of them looks at a single pixel. So a change to
`LayoutBox`, `StyleResolver` or `BitmapRenderer` could move every box on the
page and no check would notice:

```bash
find tests/wpt -name '*-ref*' | wc -l     # 0 — not one reftest vendored
grep -rn "render-png" tools/ tests/       # no consumer outside the CLI itself
```

That is the last unmeasured thing in Tier 1, and it is the one the roadmap
predicted would be slow: *"A windowed shell has none. Automatic GC has almost
none. An HTTP cache has none."* Rendering had none either.

## 2. What changed

**`tools/conformance/render_check.py`** — a harness of 379 lines plus a
self-test, whose reader is written out rather than reused. It decodes the PNG
signature, `IHDR`, `IDAT`, all five per-scanline filters (including the Paeth
predictor), and checks every chunk's CRC. `zlib` supplies the decompression and
the Adler-32; everything above it is here.

**Why the reader is not the engine's writer.** An engine asked to grade its own
PNG grades its own bugs — a fault in the writer would be reproduced faithfully
by a checker that shared its code. The two are independent, so a writer fault
shows up as a failed reftest.

**`tests/render/reftests.json`** — 10 pages, 30 assertions, covering the
painting facts that were previously stated in prose only:

| Reftest | What it pins down |
| :--- | :--- |
| `a_block_paints_its_background_exactly` | a 50×50 block is red inside and white one pixel outside, on all four sides |
| `stacked_blocks_stack_in_document_order` | the second block starts where the first ended |
| `margin_left_moves_the_box_and_leaves_its_margin_blank` | the margin is unpainted, and nothing overflows the box |
| `borders_paint_around_the_content_and_the_content_stays_inside` | 4px borders on the padding box, content inside the border, border box 38×28 |
| `padding_moves_the_content_without_painting` | 20px of padding is the outer background, content begins at (20,20) |
| `text_is_painted_where_its_line_box_starts` | ink is present near the line box and not the page colour; the page below it is blank |
| `a_later_block_covers_an_earlier_one` | absolute positioning paints over, and the box underneath still shows |
| `display_none_paints_nothing` | a `display: none` box occupies no pixels |
| `a_percentage_width_is_resolved_against_the_viewport` | `50%` of 800px covers x=399 and stops at x=400 |
| `an_offscreen_box_paints_inside_the_image_only` | a wide box is clipped at the viewport edge, not scrolled into view |

**Exact equality, no tolerance.** Box edges in this engine are not
anti-aliased, so an off-by-one is a bug rather than a rounding. A tolerance
would hide exactly the class of error the harness exists to find. The one
reftest that cannot be exact says so instead of pretending: the glyph's grey is
font-dependent, so it asserts *where* the ink is and that the page below it is
untouched, rather than naming a colour.

## 3. How it was verified

`make render-conformance` — **10 / 10 pages match their reference pixels**.
The engine's own suites are untouched: `make test` is still **271 / 271**.

**The harness is checked before it is trusted.** `--self-test` runs first on
every invocation, and it is not a formality:

- the reader is fed PNGs **written by the self-test** with each of the five
  filter methods in turn (0–4), and must recover the exact original pixels;
- a PNG with one flipped byte in its `IDAT` must be **refused** — it is, with
  `b'IDAT' chunk fails its CRC`;
- every assertion kind must **both accept a true statement and reject a false
  one** — 6 true and 7 false, so `pixel`, `rect`, `uniform`, `not-colour` and
  `differs` each prove they can fail.

**And the harness was made to fail on purpose.** A one-column bug injected into
`BitmapRenderer::fill_rect` (`rw` → `rw - 1`) — a single pixel lost from every
filled rectangle — turned **9 of the 10 reftests red** and failed the build:

```
FAIL a_percentage_width_is_resolved_against_the_viewport
      50% of an 800px viewport still covers x=399: (399, 10) is (255, 255, 255, 255), expected (0, 128, 0, 255)
Render reftests: 1/10 pages match their reference pixels
make: *** [Makefile:280: render-conformance] Error 1
```

Restoring the file returned 10/10. This is written down because a number that
has only ever been green is not evidence of anything.

## 4. What is still open

- **Ten reftests is a start, not coverage.** They pin the box model and text
  placement. Nothing here measures flexbox, grid, floats, `transform`, SVG,
  canvas, borders with radius, or images — the features whose rendering is
  most likely to be wrong, because they are the ones with the most code.
- **No WPT reftests are vendored.** `css/css-color/` and the `*-ref.html`
  pattern of `wpt/css` would be the natural next step: those are written by
  other people, against other engines, which is an oracle better than one this
  repository invents.
- **Text is measured for position, not for shape.** A font that renders every
  glyph at the wrong width would still pass, as long as the ink fell in the
  line box. Glyph-level comparison needs a reference bitmap and a tolerance,
  and that is a bigger decision than this phase should make alone.
- **Nothing yet compares two *builds*.** The reftests are absolute. A mode that
  renders the same page with two binaries and reports which pixels moved would
  make regressions visible without a hand-written assertion for each.
