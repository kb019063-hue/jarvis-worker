# Phase 193 — inline formatting in the pixel oracle

Task row: **Pixels are measured since Phase 120**, with inline formatting and
line breaking still incompletely measured. Finding 197.

## 1. What was missing

The original pixel corpus had 45 pages and 179 assertions, but its one text
case only checked for ink near the start of one line. It did not pin down a
forced line break or a soft wrap of inline replaced elements. An engine could
place the third inline image on the first line, or move the second line one
pixel, without either fact being asserted by these pages.

## 2. What changed

Three cases and eleven exact rectangle assertions were added to
`tests/render/reftests.json`. Each uses a solid red one-pixel PNG scaled to a
20 by 20 CSS pixel inline image, a 45-pixel containing block, zero font size
and a 20-pixel line height; they therefore do not assume a host font's glyph
shape or advance.

- `inline_br_starts_a_second_line_of_replaced_elements` places an image on
  each side of `<br>` and checks both lines and the surrounding white area.
- `inline_replaced_elements_wrap_to_the_next_line` fits two images on the
  first line and places a third at the start of the second. The spaces have
  zero advance but remain break opportunities.
- `inline_nowrap_keeps_replaced_elements_on_one_line` asks the same three
  images to stay on one line and overflow their 45-pixel container. It is a
  **known failure**: Litebird wraps the third image despite the `nowrap` on
  its containing span. It is recorded rather than fixed here.

The rectangles are specified from the sizes and line-breaking rules rather
than from the engine's own output. Chromium 153 separately agreed with all
of them, including the expected overflow on the known failure.

## 3. Verification

```
$ make render-conformance
Render reftests: 47/48 pages match their reference pixels; 1 more fail as recorded, with a reason in the corpus (48 of 48 accounted for)
Render reftests: 35/41 pages match their reference pixels; 6 more fail as recorded, with a reason in the corpus (41 of 41 accounted for)
Canvas toDataURL: 45016/45016 checks agree (45008 pixels read back by render_check.py's own PNG reader)
```

The original corpus was 45/45 with no known failure before this change; the
separate body-origin corpus was 34/41 with seven recorded failures. Main
then merged Phase 190, which fixed one body-origin page (35/41). The three
new pages take the combined count from 86 to 89 and assertions from 310 to
321. They do not change the body-origin corpus or the canvas harness.

For an independent engine, the repository's existing Chromium checker can
check just these cases (it needs `@sparticuz/chromium`, `puppeteer-core` and
`pngjs`, as described in `tools/conformance/verify_body_pixels.cjs`):

```
python3 - <<'PY'
import json
s = json.load(open('tests/render/reftests.json'))
json.dump({'tests': [t for t in s['tests'] if t['name'].startswith('inline_')]},
          open('/tmp/inline-reftests.json', 'w'))
PY
node tools/conformance/verify_body_pixels.cjs /tmp/inline-reftests.json
# HeadlessChrome/153.0.8010.0: 11/11 assertions across 3 pages match Chromium
```

The expected rectangles were also challenged: changing the first red
rectangle to green in each passing case made the harness exit 1, and
removing `known_failure` from the nowrap case made it exit 1 on both of its
assertions. The tracked corpus was not changed by these negative controls.

## 4. What is still open

The `nowrap` bug is not fixed; the known-failure entry must be removed when
it passes, and the harness prints `FIXED` and fails until that happens. These
three pages do not establish general text wrapping, glyph shape, font
fallback, bidi, inline background fragmentation, or interactions with other
CSS properties. Full-corpus comparisons with an independent browser and WPT
reftests remain open. No engine code was changed.
