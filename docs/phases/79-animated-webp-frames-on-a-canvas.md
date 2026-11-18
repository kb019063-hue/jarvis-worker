# Phase 79 — Animated WebP: frames, a canvas, and the order they are drawn in

## 1. What was missing

`WebPDecoder` read one image. A still `VP8L` file, or a still `VP8` key frame
with an `ALPH` plane, decoded to a picture; an animated file — a `VP8X` canvas
with `ANIM` and one `ANMF` chunk per frame — was reported as
`Status::UnsupportedAnimation`. The status was honest: an `ANMF` payload is not
an image, it is a rectangle, a duration, two method bits and an image, and
drawing it means keeping a canvas and compositing frame after frame in order.

That made animated WebP the one image format the engine refused while accepting
its GIF equivalent. `GifDecoder::decode_frames()` has walked GIF frames and
composited them since Phase 38.

## 2. What changed

`WebPDecoder` gained the walk and the compositor, and the API that GIF already
had:

```cpp
struct AnimationFrame {
    std::shared_ptr<ImageData> image;  // the whole canvas after the frame
    int duration_ms, x, y, width, height;
    bool blend;                    // alpha-blend the frame over the canvas
    bool dispose_to_background;    // clear the rectangle afterwards
};
struct Animation {
    int canvas_width, canvas_height, loop_count;
    uint32_t background;  // `ANIM`'s colour, 0xAARRGGBB, reported not painted
    bool animated, first_is_lossy;
    std::vector<AnimationFrame> frames;
    std::string error;
};
static Animation decode_frames(const uint8_t*, size_t, size_t max_frames = kMaxFrames);
static std::shared_ptr<ImageData> first_frame(const uint8_t*, size_t);
```

`decode()` and `decode_detailed()` — what an `<img>` gets — are the first frame
of the animation, exactly as they are for a GIF, so every image path in the
engine (a `data:` URL, a subresource, the layout box's intrinsic size) gained
animated WebP without a caller changing. `Status::UnsupportedAnimation` is gone
from the enum.

The container walk is small: `VP8X` gives the canvas (`u24 + 1` per side, with
the animation flag in byte 0), `ANIM` gives the background colour and the loop
count, and every `ANMF` is peeled the same way an `ANMF` payload's own chunks
are — `read_nested_chunks()` runs the RIFF scan over the payload's bytes, and the
existing still decoders (`Vp8lDecoder`, `Vp8Decoder` with `decode_alpha_plane()`)
decode each frame's image. Geometries are stored in units of two pixels except
the width and height, which are stored minus one.

## 3. The compositor, and why it is a port rather than a reading of the spec

Drawing a frame is: take the previous canvas (or a cleared one), write the
frame's pixels into its rectangle, then alpha-blend the pixels the frame left
translucent over what was underneath. Three details decide whether the output
is bit-identical to libwebp's, and all three were found by differential testing
rather than by reading the specification:

1. **The key-frame rule.** Some frames are drawn on a cleared canvas and are not
   blended at all: the first frame; a full-canvas frame that has no
   transparency or is not blended; and a frame that follows one which cleared
   the whole canvas (`IsKeyFrame()`, `src/demux/anim_decode.c`). Blending over
   transparent pixels is *almost* the identity — the fixed-point divide rounds,
   so a translucent pixel comes out one step off — and a fully transparent one
   loses its colour entirely. A naive compositor therefore differs from libwebp
   by one unit on exactly the pixels nobody looks at.
2. **Which pixels get blended.** Only over pixels that came from an earlier
   frame. When the previous frame disposed to background, the part of the row
   inside its cleared rectangle is left as the frame decoded it, and the blend
   covers the parts either side (`FindBlendRangeAtRow()`); the check is about
   the *previous* frame's rectangle, not the current one's.
3. **The blend itself.** Straight (non-premultiplied) alpha in integers:
   `dst_factor_a = (dst_a * (256 - src_a)) >> 8`, `blend_a = src_a +
   dst_factor_a`, `scale = (1 << 24) / blend_a`, and each channel
   `((src_c * src_a + dst_c * dst_factor_a) * scale) >> 24`, with `src_a == 0xff`
   copying the source and `src_a == 0` keeping the destination. Reaching for a
   float, or a slightly different rounding, is visible in the differential run.

Two canvases are kept (`canvas`, and `previous` with the disposed rectangle
cleared), as libwebp keeps them, so that a frame following a disposal blends
against the canvas *as the disposal left it* rather than against the frame's own
pixels.

Bounds follow the still decoder's: the canvas is at most `kMaxDimension` a side
and `kMaxDecodedPixels` in total, each frame's rectangle must lie inside it and
its image must be exactly that size, and `decode_frames()` stops at
`max_frames` (1024 by default) or after `kMaxAnimationPixels` of frames — the
same two caps `GifDecoder` uses. A file that declares an animation and carries
no `ANMF` is refused; so is a frame that runs past the end of the file, a canvas
over the budget, and an `ANMF` whose image is another size. A walk that stops on
a broken frame keeps the frames it decoded before it, as `GifDecoder` does, and
`ok()` is false because the error is set.

## 4. Verification

`tools/media_check` has `--decode-webp-anim <in> <prefix> [max_frames]`, which
writes one `.rgba` per composited frame and prints the canvas, the frame count,
the loop count, the background colour, and each frame's geometry, duration and
methods — everything a comparison needs, and enough to cross-check the container
against `webpmux -info` field for field.

The decoder was compared with **libwebp 1.5.0's own animation decoder**
(`WebPAnimDecoder`, `MODE_RGBA`, driven through a small harness): 658 animations
and 2,262 frames came out **byte-identical**, including 650 generated ones with
random canvas sizes, rectangles, offsets, durations, loop counts, background
colours, blending and disposal methods, and images from both codecs — lossless,
lossy, opaque, translucent, and mostly transparent. Two traps came out of that
work and are worth recording:

- `anim_dump -pam` is not the oracle. Its PAM writer loses the colour of fully
  transparent pixels, so a frame that keeps a transparent pixel's RGB compares
  unequal for reasons that have nothing to do with the compositor. The raw
  `WebPAnimDecoder` output is the reference.
- ImageMagick's WebP reader blends frames that say *do not blend*, so it can
  only be the oracle for opaque or all-blending animations. `verify-media` uses
  it exactly there, and elsewhere composites the frames itself from ImageMagick's
  still decodes — which the still-image suites have already proven bit-exact —
  using the rules above.

`verify-media` went from 95 to 103 checks: the metadata of a hand-muxed
four-frame animation, the composited frames of that animation, an opaque
animation against `convert -coalesce`, a six-frame transparency/blending/
disposal animation, a still file asked for its frames, `decode()` returning the
first frame, the frame cap, determinism, six refusals, and an animation written
by ImageMagick round-tripping exactly. The animated file that used to be in the
refusal list is now checked as a first frame instead, and `probe()` says `ok`
for it.

`fuzz-media` gained four seeds (three ImageMagick animations with transparency,
disposal and loop counts, and a hand-built animation whose frame carries no
image), and its WebP mutations now run both `decode_detailed()` and
`decode_frames()`. 2,400 cases, no findings; a 16,000-case run with the same
seed was clean too.

The unit suite has `webp_animated_frames_are_composited` (219 → 220 suites): a
four-frame 8x8 animation embedded as 244 bytes, with every field chosen by hand
— a red frame drawn without blending, a half-transparent blue frame blended onto
it, a green frame that clears its rectangle afterwards, and the blue frame again
over the cleared area. Every frame is checked by an FNV-1a hash of its pixels
plus the handful of pixels that name each compositor branch, and the file's four
frames are byte-identical to libwebp's. The malformed-input suite gained the
cut-frame, outside-the-canvas, wrong-size-image and oversized-canvas refusals.

## 5. What is left

Animated WebP is drawn, but nothing plays it: `decode_frames()` exists for the
same reason `GifDecoder::decode_frames()` does, and the document does not yet
animate either format — `<img src="…">` shows frame 0 of both. AVIF and SVG
`<text>` remain the other two items of the `AGENTS.md` row this phase moved.
