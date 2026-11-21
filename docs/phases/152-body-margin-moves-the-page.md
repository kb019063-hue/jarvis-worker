# Phase 152 — the body margin moves the page

*Finding 157. Task: the margin of `body` makes the page narrower and moves nothing.*

## 1. What was wrong

For a document, `build_boxes` starts at `<body>`; `<html>` has no layout box.
`LayoutTree::build` placed that root **content** box at `(0, 0)`. Layout already
subtracted margins, borders and padding from the available width, so a margin
made the page narrower without moving it. At 800×600, the default body had
width 784 but its first 100×20 block was at `(0, 0)`, not Chromium's `(8, 8)`;
with `margin:40px` the width was 720, but the block still began at `(0, 0)`.
Putting a border and padding on the body also put its border box at negative
coordinates.

## 2. What changed

`src/layout/LayoutTree.cpp` prepares auto edges before laying out a document
whose layout root really is its body. Once the used widths, margins, border and
padding are known, it positions the body and moves the in-flow boxes, line
rectangles and fragments. Relative offsets are applied; the existing
out-of-flow pass then resolves absolute and fixed descendants against their own
containing block. This is not applied to an arbitrary non-document layout root.

Adding the body margin blindly moved a first child with `margin-top:30px` to
`y=38`; Chromium collapses those top margins to `y=30`. The placement step
therefore handles a **nonempty first-child block chain**, combining the largest
positive and most-negative margins. It corrects auto heights inside out and
moves following in-flow siblings when a child height changes. Padding, borders,
flex/grid parents, containment, clipping, floats, clearance, definite-height
propagation and the other checked boundaries stop the appropriate part of the
shortcut. The walk is iterative and moves each visited box once. This is not a
general block-formatting or margin-collapse implementation; see below.

## 3. How it was verified

Before: a freshly built binary at `fe06a73` (before the change). After: this
branch with `main` through #168. The two binary comparison is about the body
change; the original render corpus was also changed by #187 in between, so its
aggregate increase is **not** attributed to this phase.

| Check | Before | After |
| :--- | :--- | :--- |
| `make test` | 308 engine suites | 315 engine suites and 114 cross-origin checks; the extra seven include main's work |
| `make dom-conformance` | 678/678, everyday 68/68 | 678/678, everyday 68/68; 0 crashes/timeouts |
| `make wpt` | 9567/9824, 257 listed | 9567/9824, 257 listed, 0 unlisted |
| `make render-conformance`, original corpus | 33/36, 3 known | 36/38, 2 known, after #187 changed the corpus and rounded clipping |
| Body-origin corpus in `make render-conformance` | 0/7 match, 3 known; four unlisted failures | 4/7 match, 3 known |
| Canvas `toDataURL` | 45016/45016 | 45016/45016 |
| `make js-conformance` | 1144/1158, everyday 90/90 | 1144/1158, everyday 90/90 |

The ledger's historical JS score is 1143; on this host the regular build
passes the 2,000-deep recursion case, while the sanitizer build runs out of
stack on it and reports 1143. This phase claims no JS gain. The new suites
`document_body_margins_place_boxes_from_the_viewport` and
`body_origin_reaches_pixels_positioned_descendants_and_live_geometry` cover 30
Chromium-derived geometry cases, pixels, absolute/fixed descendants and a style
mutation followed by a fresh geometry read. Examples:

| 800×600 page | Chromium and after: body border box | Chromium and after: first child |
| :--- | :--- | :--- |
| Default | `(8, 8, 784, 20)` | `(8, 8, 100, 20)` |
| `margin:40px` | `(40, 40, 720, 20)` | `(40, 40, 100, 20)` |
| Asymmetric margin, padding and border | `(23, 11, 748, 40)` | `(39, 19, 100, 20)` |
| `width:100px; margin:10px auto` | `(350, 10, 100, 20)` | `(350, 10, 50, 20)` |

The seven pages in `tests/render/body-origin.json` have 29 assertions, including
the three still-wrong pages. Chromium 153 independently matched **29/29**
reference assertions; changing a reference pixel to black made the checker
print `28/29` and exit 1. Reproduce on Node 22+ (these dependencies are
optional and remain outside Git; the packages contain a headless Chromium):

```sh
npm install --prefix build/body-oracle --no-save \
  @sparticuz/chromium@153.0.0 puppeteer-core@25.18.1 pngjs@7.0.0
export NODE_PATH="$PWD/build/body-oracle/node_modules"
# Only on minimal Linux images missing Chromium's NSS libraries:
mkdir -p build/body-oracle/runtime
node -e "require('fs').createReadStream(process.argv[1]).pipe(require('zlib').createBrotliDecompress()).pipe(process.stdout)" \
  "$NODE_PATH/@sparticuz/chromium/bin/al2023.tar.br" | tar x -C build/body-oracle/runtime
export LD_LIBRARY_PATH="$PWD/build/body-oracle/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
node tools/conformance/verify_body_pixels.cjs
# HeadlessChrome/153.0.8010.0: 29/29 assertions across 7 pages match Chromium
```

The engine-side negative control can be repeated by building `fe06a73` in a
separate detached worktree and pointing the *current* checker at that binary:

```sh
git worktree add --detach /tmp/litebird-body-before fe06a73
make -C /tmp/litebird-body-before -j2 build/litebird
python3 tools/conformance/render_check.py --binary /tmp/litebird-body-before/build/litebird \
  --suite tests/render/body-origin.json --no-self-test
# exits 1: four FAIL pages, three KNOWN
```

Other checks on the tree through #168: HTML tree construction 1973/1973, URL
1188/1188 and XML 1718/1718. On an earlier tree through #181, 6,000 generated
tree comparisons agreed with the corrected parse5 oracle, as did 4,000
generated XML documents and 3,000 XML round trips. A full-depth run on the tree
through #191 passed 1,332 runs over 465 pages at depth 200,000; `make tsan`
passed its string model and thread tests. On the tree through #187, all eight
remaining ASan/UBSan parts
(`asan-verify`, `asan-js`, `asan-dom`, `asan-xml`, `asan-wpt`,
`asan-xml-generated`, `asan-fuzz`, `asan-deep`) passed; `asan-deep` ran 882
checks at depth 10,000. The **default** full `make asan` on the tree through #181 was killed in the
pre-existing deep-nesting unit test at about 3.86 GB RSS in this 3.8 GB sandbox.
Re-running the entire 313-suite sanitizer unit binary and 114 cross-origin
checks with `ASAN_OPTIONS=detect_leaks=0:quarantine_size_mb=8:thread_local_quarantine_size_kb=64:malloc_context_size=5`
passed. Reducing quarantine reduces use-after-free detection coverage, so that
is not represented as a default `make asan` pass; CI runs the default settings.

A small local timing (five `--dump-layout` runs, median) on 20,000 flat blocks
was 0.146 s before and 0.149 s after, and on 600 nested blocks 0.014 s both
ways. This is a coarse smoke measurement, not a browser benchmark.

## 4. What is still open and what regressed

- **A visible regression for opaque body backgrounds:** background propagation
  to the canvas was already missing. With a red body and default margin,
  `(0,0)` was red *by accident* before, but is white after; `(8,8)` is red in
  both, and `(799,599)` is white in both. Chromium paints all three red. The
  third known-failure page records this regression; neither background
  propagation nor the skipped HTML root is claimed fixed.
- `html` margins, backgrounds, width and other root-box properties are still
  ignored. On a 30px HTML margin, Chromium puts the child at `(38,38)` and this
  engine at `(8,8)`.
- Empty blocks and general sibling/bottom margin collapse, min/max-height
  constraints, and other block-formatting cases still need a proper algorithm.
  In the recorded empty-first-block case Chromium paints the second block at
  `y=30`, this engine at `y=58`. A default paragraph's top position also does
  not yet match Chromium. Text and font-dependent line heights can differ.
- The checker covers selected geometry and pixels, not general CSS conformance
  or full-page visual equivalence. A passing WPT count here is mostly DOM and
  CSS parsing, not a layout-reftest score.
