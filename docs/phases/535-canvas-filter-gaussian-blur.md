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

`make test` passes all 562 engine suites and the 114 cross-origin
Window/Location/MessageChannel checks. `python3 tools/ci/check_suites.py`,
`git diff --check`, `make render-conformance`, and the Documentation shape
checks are recorded on the pull request once their final runs complete.
