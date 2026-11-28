# Phase 535: Canvas filter blur uses a Gaussian kernel

CSS Filter Effects defines `blur(<length>)` as a Gaussian blur whose standard
deviation is the supplied length. `CanvasRenderingContext2D.filter` reached the
renderer in Phase 406, but its `ImageFilter::blur` helper still took an integer
radius and averaged every pixel in a rectangular window equally. The box looked
like a blur, and the existing edge-ramp assertion passed; no test asked for the
shape of the kernel.

## What changed

`ImageFilter::blur` now builds a normalized one-dimensional Gaussian kernel,
truncated at three standard deviations, and convolves horizontally and then
vertically. The parsed CSS length is used directly as σ, preserving fractional
values instead of rounding them to an integer window. The prior bound of the
largest bitmap dimension remains as a σ cap, and source offsets are bounded by
the bitmap so extreme radii cannot walk beyond it.

The convolution treats the area outside the bitmap as transparent. It keeps the
full kernel weight in the normalization but only samples in-bounds pixels; it
never extends the edge by repeating its last pixel. Color is converted to
premultiplied alpha for both passes and unpremultiplied once at the end. This
keeps a red shape red as its alpha fades rather than mixing its straight RGB
channels with transparent black.

Both the `blur()` filter and the blurred `drop-shadow()` filter use this helper.
The distinct `CanvasRenderingContext2D.shadowBlur` property still uses the
three-box approximation in `Canvas2D.cpp`; this phase does not change that path.

## Oracle

`tests/suites/535-canvas-filter-gaussian-blur.cpp` sends a one-pixel impulse
through σ=1 and asserts the discrete two-dimensional Gaussian response: alpha 41
at the centre, 25 one pixel away on an axis, 15 on the diagonal, and 5 two
pixels away. It asserts that the impulse's red RGB remains unchanged at each
fading pixel, places a second impulse at the top-left canvas corner to hold the
transparent-outside rule, and checks that σ=0 is an identity. The same suite
also calls `ImageFilter::drop_shadow` with σ=1: its source stays red and opaque,
while its black shadow has alpha 25 axially and 15 diagonally. This guards the
other consumer of the shared blur helper.

The suite was built and run once against the old helper before the change. It
failed at the first expected-value assertion: the old 3×3 box returned alpha 28
at the centre instead of the Gaussian's 41. The existing integration assertion
in `tests/suites/406-canvas-filter.cpp` was also updated: for its 10×8 rectangle
and `blur(2px)`, the former box result at the interior sample was 255, while the
Gaussian response is 239. The outward ramp and the no-filter hard-edge control
remain in place.

## Cost

On a 512×384 buffer, four-run local averages compared the Phase 535 helper with
the base implementation from `4097af0`:

| σ | base box | Gaussian | change |
| ---: | ---: | ---: | ---: |
| 2 px | 12.7 ms | 17.6 ms | +38% |
| 12 px | 30.9 ms | 76.3 ms | +147% |

The measured cost reflects the longer Gaussian kernel and premultiplied-alpha
work. The convolution remains separable, and its radius is bounded as above;
large-radius performance remains a potential follow-up rather than a reason to
keep returning a box-shaped filter.

## Verification

- `make -j2 test`: all **562 engine suites** and **114** cross-origin
  Window/Location/MessageChannel checks passed.
- `make render-conformance`: **67/68** reftests matched (the remaining AVIF
  decoder case is the corpus's recorded known failure), **41/41** body-origin
  pages matched, and the Canvas `toDataURL` harness passed **45,016/45,016**
  checks.
- `make conformance`: **1,973/1,973** HTML tree-construction cases passed.
- `make js-conformance`: **1,154/1,158** passed, with 0 crashes/timeouts and
  the same four corpus failures; the fixed 90-case probe passed 90/90.
- `make dom-conformance`: **678/678**; `make url-conformance`: **1,188/1,188**;
  `make xml-conformance`: **1,718/1,718**.
- `make wpt`: **9,570/9,824** subtests passed; all 254 failures are recorded
  in the repository's expected-failure list, with no unlisted failures.
- `make diff-trees`: four batches of 1,500 random pages had 0 differences from
  the corrected parse5 reference. `make diff-xml` (with lxml installed in a
  local venv): 3,958/4,000 documents agreed with both Expat and libxml2, and
  all 3,000 XML round trips preserved their trees.
- `python3 tools/ci/check_suites.py`, `python3 tools/ci/check_docs.py`, and
  `git diff --check` passed.

The full `make asan` run did **not** finish: its initial attempt timed out while
compiling the large sanitizer test translation unit. A retry with debug info
disabled built the test binary and reached the ASan runner, but timed out at
`deep_nesting_never_exhausts_the_native_stack` without a final summary. Running
that unrelated deep-nesting suite alone under ASan also exceeded 15 minutes in
this sandbox. No full sanitizer-suite pass is claimed. The Phase 535 test itself
did pass in that sanitizer binary using a single-suite stretch:

```sh
ASAN_OPTIONS=detect_leaks=0 LITEBIRD_SUITE_SHARD=1/3 \
LITEBIRD_SUITE_CUTS=canvas_filter_blur_and_drop_shadow_use_gaussian_weights_and_premultiplied_alpha,color_parsing \
./build-asan/test_suite
```

`make deep` passed **1,332 runs over 465 pages** with 0 failures;
`make asan-deep` passed **882 runs over 315 pages** with 0 failures. After
`build-asan/litebird` was available, `ASAN_OPTIONS=detect_leaks=0 make fuzz`
passed: 400 parser mutations, 400 pipeline runs, 600 XML mutations, 2,400 media
cases, 5,009 CSS Color inputs, and 2,000 Brotli mutations, all with 0 findings.
