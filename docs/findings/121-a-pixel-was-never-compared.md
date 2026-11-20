# Finding 121 — a pixel was never compared with anything

Phase 120. Status: **resolved** for the box model and text placement; §3 says
what is still unmeasured rather than implying the whole of rendering is covered.

## 1. What was wrong

`ROADMAP.md` Tier 1 row 8 carried it in one sentence: *"Nothing measures a
pixel. Every layout and font claim on this page is unverified."*

It was accurate. The engine could render — `--render-png` wrote a correct 8-bit
RGBA PNG — but nothing in the tree read one back, so:

- a change to `LayoutBox` that moved every box by one pixel was invisible to
  every check in the repository;
- the documents' claims about `margin-left`, `padding`, borders, percentage
  widths and text placement were prose, not measurements;
- `find tests/wpt -name '*-ref*' | wc -l` was **0**. Not one reftest was
  vendored, so the suite other engines are held to was not being run at all.

The other scoreboards do not cover this and cannot: tree construction, the DOM
corpus, WPT's script tests, the XML suite, URL and JavaScript all read a
*textual* answer. A layout bug prints nothing.

## 2. What the fix was

`tools/conformance/render_check.py` renders each page in
`tests/render/reftests.json`, decodes the PNG **with its own reader** — all five
scanline filters, per-chunk CRC, no code shared with the engine's writer — and
checks the stated pixels exactly.

`make render-conformance` runs it, and CI has a `render-conformance` job.

The harness is held to three standards of evidence, because a harness is the
easiest thing in a repository to fool:

1. **It is checked before it is trusted.** `--self-test` runs first: the reader
   decodes PNGs written with each of the five filter methods, a broken CRC is
   refused, and all five assertion kinds must accept a true statement and
   reject a false one.
2. **It can fail.** Injecting a one-column bug into `BitmapRenderer::fill_rect`
   turned 9 of 10 reftests red and failed the build; restoring the file
   returned 10/10.
3. **It states what it cannot claim.** The text reftest does not name a grey,
   because the grey belongs to the font, not the engine. It asserts where the
   ink is and that the page below is blank.

## 3. What is still open

| What | Why it is left |
| :--- | :--- |
| flexbox, grid, floats, `transform`, SVG, canvas, border radius, images | ten reftests pin the box model; the features with the most code are unmeasured |
| WPT reftests | `wpt/css/**/​*-ref.html` are written by other people against other engines, which is a better oracle than one this repository writes; none are vendored |
| glyph shape | ink in the right place passes even if every glyph is the wrong width; comparing shape needs a reference bitmap and a tolerance decision |
| two builds | the reftests are absolute; a mode that renders one page with two binaries and reports which pixels moved would catch regressions with no hand-written assertion |

The honest summary: Tier 1 row 8 is no longer "nothing". It is ten pages and
thirty assertions, which is enough to catch a one-pixel regression in the box
model and nowhere near enough to call rendering verified.
