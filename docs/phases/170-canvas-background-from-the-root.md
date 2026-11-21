# Phase 170 — canvas background from the root

*Finding 175. Task: propagate a solid HTML root/body background to the canvas.*

## 1. What was wrong

A document's layout tree ordinarily starts at `<body>`; the `<html>` element
has no paint box. Nothing selected the root or body background for the canvas.
After Phase 152 moved the body's box to its actual margin, a red body painted
red at `(8,8)` but white at `(0,0)` and `(799,599)`; Chromium paints all three
red. A blue HTML root was ignored altogether. The shell had a separate shortcut:
it cleared the viewport to the body's computed color, even when the root
should win, and then painted the body a second time. An iframe was first
flattened onto white, so a transparent child could not show its parent.

There was a further obstruction: the user-agent sheet made every unstyled body
white. A real browser's initial body background is transparent; treating every
computed white as if it were the default would also erase a deliberately white
author declaration.

## 2. What changed

For a whole HTML document, `LayoutTree::build` resolves the HTML root and the
first HTML body child. A nontransparent solid root background supplies the
canvas; otherwise a body that generates a box supplies it. A `display:none`
root hides both the tree and canvas; `display:none` on the body leaves the
root's own color intact. This is not applied to arbitrary subtree layouts or
non-HTML document roots. The computed style stays intact: a separate flag marks
boxes whose background's *used* value has moved to the canvas. The inline body
case is different from a block: Chromium also paints the inline line fragments,
and tests cover that extra layer and a descendant's own background.

The display list starts with a document canvas command in viewport coordinates,
before body chrome and ink. It does not move with the shell's scroll transform
or inherit the body's border radius, positioning or overflow, but an explicit
enclosing clip still applies. An HTML document with a transparent canvas emits
the command too: the top-level terminal paints a white backdrop so the default
dark text stays legible, while bitmap targets leave transparent pixels alone.
The shell clears its top-level bitmap to white instead of guessing from the body.
An iframe renders to its own transparent bitmap and is composited as an image,
so its color and the parent's color meet once and cannot escape the frame.
Bitmap source-over blending now preserves destination alpha, and rounds opaque
composites to the nearest channel value instead of truncating them. The terminal
also composites translucent colors, respects clips on rectangles and images,
no longer clamps a wholly scrolled-off rectangle or iframe onto an edge cell,
and treats right and bottom edges as exclusive: a 20×20 body on a 10×20 cell
grid paints two cells over the root canvas instead of six. A partially scrolled
image is sampled from the visible part rather than its original left edge.
On this compiler, `sizeof(LayoutBox)` rises
from 344 to 352 bytes for the canvas metadata; the existing `<400` byte unit
assertion remains in force.

The user-agent correction is deliberately at **UA origin**, not an author rule
or a computed-color heuristic. `StyleResolver` appends a transparent body
background to its UA sheet; a universal selector, a layered author rule, or an
explicit white body still wins normally. This small normalizer is temporary:
`src/css/StyleResolver.cpp`, which contains the original white declaration, was
claimed by #183. Once that claim clears, remove the original declaration and
the normalizer together rather than retaining two contradictory UA rules.

## 3. How it was verified

The base binary was built from `5f30f48`. The after tree includes main through
#206; its `Object.assign`, `TextDecoder`, GC, WebSocket and AVIF suites are not
results of this phase.

| Check | Before | After |
| :--- | :--- | :--- |
| `make test` | 319 engine suites, 114 cross-origin checks | 329 engine suites, 114 cross-origin checks |
| Body-origin corpus, with **all** known markers removed | 7/40 pages | 34/40 pages; 28 newly pass, 1 formerly passed by accident and now fails |
| `make render-conformance`, original corpus | 36/38 pages, 2 known | 36/38 pages, 2 known |
| `make render-conformance`, body-origin corpus | 5/7 pages, 2 known on the old corpus | 34/40 pages, 6 known; none unlisted |
| Canvas `toDataURL` | 45016/45016 | 45016/45016 |
| Chromium 153 vs body-origin references | original seven pages, 29/29 assertions | 40 pages, 128/128 assertions |
| Chromium 153 vs scroll fixture | not measured | 12/12 scroll and pixel assertions |

The one lost page is `display:contents`: the engine still treats that value as
a block, and the lack of any body propagation happened to leave its canvas
white before. It now incorrectly propagates the body's red; the corpus lists
that visible regression instead of counting it as a fix. The original-corpus
rounded scaled clip has one changed expected channel, `#ff5151` → `#ff5252`:
all seven assertions on that page were independently checked against Chromium
153 after correcting the renderer's rounding. Against the *corrected* current
original corpus, the base binary scores 35/38 (two known, the scaled clip an
unlisted failure); the other 37 pages retain their outcomes.

The three new unit suites check the selected canvas and unchanged computed
style, document/subtree scope, transparent-target alpha and clipping, terminal
background and scrolling, and `BrowserShell` scrolling, relayout and resize.
Other checks after merging #202: `make conformance` 1973/1973,
`make dom-conformance` 678/678 (everyday 68/68), `make url-conformance`
1188/1188, `make xml-conformance` 1718/1718, and `make wpt` 9567/9824
(257 listed failures, none unlisted). `make diff-trees` agrees with corrected
parse5 on 6,000 generated pages; `make diff-xml` reports no disagreement on
4,000 generated documents and 3,000 round trips. Examples, at 800×600 (all
pixels are RGB):

| Page | Outside body | Inside body |
| :--- | :--- | :--- |
| transparent root; half-red block body | `(255,127,127)` | `(255,127,127)` — not painted twice |
| half-blue root; half-red body | `(127,127,255)` | `(191,63,127)` |
| blue parent; half-red iframe | blue outside frame | `(128,0,127)` within frame |

Run the independent reference check on Node 22+; packages are optional and
stay under ignored `build/`:

```sh
npm install --prefix build/browser-oracle --no-save \
  @sparticuz/chromium@153.0.0 puppeteer-core@25.18.1 pngjs@7.0.0
export NODE_PATH="$PWD/build/browser-oracle/node_modules"
# Only on minimal Linux images missing Chromium's NSS libraries:
mkdir -p build/browser-oracle/runtime
node -e "require('fs').createReadStream(process.argv[1]).pipe(require('zlib').createBrotliDecompress()).pipe(process.stdout)" \
  "$NODE_PATH/@sparticuz/chromium/bin/al2023.tar.br" | tar x -C build/browser-oracle/runtime
export LD_LIBRARY_PATH="$PWD/build/browser-oracle/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
node tools/conformance/verify_body_pixels.cjs
# HeadlessChrome/153.0.8010.0: 128/128 assertions across 40 pages match Chromium
node tools/conformance/verify_canvas_scroll.cjs
# HeadlessChrome/153.0.8010.0: 12/12 scroll and pixel assertions match Chromium
```

The first verifier accepts a corpus path. Changing the body-canvas origin's
reference to black made it report **127/128** and exit 1. For the rounded-clip
check, extract the one page and pass it to that same verifier:

```sh
python3 - <<'PY'
import json
suite = json.load(open('tests/render/reftests.json'))
suite['tests'] = [t for t in suite['tests'] if t['name'] == 'a_scaled_rounded_clip_scales_its_corner']
json.dump(suite, open('build/rounded-reference.json', 'w'))
PY
node tools/conformance/verify_body_pixels.cjs build/rounded-reference.json
# HeadlessChrome/153.0.8010.0: 7/7 assertions across 1 pages match Chromium
```

For the two-binary comparison, make a source archive rather than switching the
working branch, build it, and remove *all* known markers from a copy of the
current corpus. Both checker invocations intentionally exit 1 because this
exposes the six remaining failures rather than suppressing them:

```sh
mkdir -p build/canvas-before-src
git archive 5f30f48 | tar -x -C build/canvas-before-src
make -C build/canvas-before-src -j2 build/litebird
python3 - <<'PY'
import json
suite = json.load(open('tests/render/body-origin.json'))
for test in suite['tests']:
    test.pop('known_failure', None)
json.dump(suite, open('build/canvas-all-unmarked.json', 'w'))
PY
python3 tools/conformance/render_check.py --binary build/canvas-before-src/build/litebird \
  --suite build/canvas-all-unmarked.json --no-self-test --verbose  # 7/40
python3 tools/conformance/render_check.py --binary build/litebird \
  --suite build/canvas-all-unmarked.json --no-self-test --verbose  # 34/40
```

## 4. What is still open

- **Background images are not represented or painted.** A non-`none` root
  image should prevent body propagation even when its color is transparent;
  the `root_background_image_blocks_body_propagation` page shows the wrong red
  canvas. This phase covers solid color, not CSS Backgrounds in full.
- `display:contents` is not a layout display value; its boxless body must not
  provide a canvas but currently does (the regression above).
- Root opacity groups and descendant `visibility:hidden` are not implemented
  in the painter. The canvas's separate behavior under hidden or zero-opacity
  *body* is measured, but that does not fix visibility of the body's children.
- An inline body's default horizontal margin starts its fragments at `x=0`
  instead of Chromium's `x=8`; the two-layer canvas behavior is tested with
  an explicit zero margin, and the default-margin failure remains recorded.
- Empty blocks and general sibling/bottom margin collapse remain incomplete.
  The HTML root still has no full layout box: Phase 166 handles a root margin,
  not every width, border, padding and positioning rule.
- Legacy HTML presentational `bgcolor` is not handled here; background-image,
  blend, filter and general opacity-group semantics remain separate work.
  The pixel corpus samples particular colors and geometries; it is not a
  general screenshot or WPT rendering score.
