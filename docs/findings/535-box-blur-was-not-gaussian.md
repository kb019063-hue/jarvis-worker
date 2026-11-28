# Finding 535 — Canvas `filter` blurred with a box

The `CanvasRenderingContext2D.filter` `blur()` path called
`ImageFilter::blur`, which performed a horizontal and vertical box average. Each
tap in the window had equal weight, and the integer radius was rounded before
filtering. That produces a flat-edged kernel rather than the Gaussian required
by CSS Filter Effects; at transparent edges it also averaged straight RGB and
alpha independently, darkening a solid colour as its coverage faded.

The existing filter test noticed that a blur spread beyond a shape, but not what
kernel produced the spread. Its 10-by-8 rectangle still reached alpha 255 at
one interior pixel under the old box filter. The one-pixel impulse that tells a
Gaussian from a box had never been asserted.

**Phase 535** replaces that box average with a separable, normalized Gaussian
kernel. The CSS blur length is used as the standard deviation, fractional values
are preserved, taps are truncated at three standard deviations, and sigma stays
bounded by the largest bitmap dimension. Pixels beyond the bitmap are
transparent: only in-bounds samples are accumulated, while the kernel's full
weight remains in the normalization. The two passes use premultiplied alpha and
unpremultiply only at the end, so the red of a red impulse stays red while its
alpha fades. Filter `drop-shadow()` uses the same helper; the separate
`CanvasRenderingContext2D.shadowBlur` implementation is unchanged.

The new suite, `tests/suites/535-canvas-filter-gaussian-blur.cpp`, holds the
sigma-one impulse to alpha 41 at the centre, 25 one pixel away on an axis, 15
diagonally, and 5 two pixels away. It also asserts that the RGB remains the
source colour, repeats the impulse at the canvas corner to verify transparent
outside samples, and checks that zero radius is the identity. It also calls
`ImageFilter::drop_shadow` at σ=1, asserting the Gaussian shadow weights while
the original source remains untouched. Before the fix, the center assertion
failed: the old 3-by-3 box result was alpha 28. The existing
Canvas integration suite now expects alpha 239, not 255, at the interior of its
10-by-8 `blur(2px)` rectangle; its outward ramp and hard-edge control remain.

The `shadowBlur` property still uses its own three-pass box approximation and
its `shadowBlur / 2` mapping is still applied as a radius rather than a standard
deviation. Those are separate from the `filter` blur corrected here.
