# Finding 85 — Animated WebP was refused by a decoder that could have drawn it

**Severity:** low — nothing was drawn wrong; a format the engine claimed to
support was reported as unsupported, and `<img src="…webp">` with an animation
had no image at all.

**Found by:** the "Animated WebP, AVIF, and SVG `<text>`" row of the open-task
table in `AGENTS.md`, worked from the top down as usual. The `verify-media`
oracle asserted the refusal in so many words ("animated `ANIM`/`ANMF` … must be
reported, not drawn"), so the gap was documented in two places at once.

## 1. What was wrong

`WebPDecoder::probe()` and `decode_detailed()` looked for `ANIM`, `ANMF` or the
`VP8X` animation flag and returned `Status::UnsupportedAnimation`. The refusal
was accurate about the code: the decoder read exactly one image, and an animated
file carries several, drawn one over another on a canvas. It was not accurate
about the work: every piece the compositor needs was already in the tree — the
`VP8L` decoder, the `VP8` key-frame decoder, the `ALPH` plane, the RIFF chunk
walk, and, in `GifDecoder`, a compositor with the same shape.

The engine could show an animated GIF's first frame and could not show an
animated WebP at all, which is the more common file on the web today.

## 2. What changed

Phase 79 adds `WebPDecoder::decode_frames()` and `first_frame()`, walks the
`ANMF` chunks, decodes each frame with the existing still decoders, and
composites them onto the `VP8X` canvas — the first frame, then each one blended
or written over what is there, with disposal applied after the frame is handed
back. `decode()` gained animated WebP for free by returning that first frame,
and `Status::UnsupportedAnimation` no longer exists.

## 3. Why it was worth doing carefully

The compositor looks like a loop with a `memcpy` and an alpha blend, and the
first version of it was one. It agreed with libwebp on 653 of 658 test
animations — and the five that differed were the interesting ones:

- blending the first frame over the cleared canvas rounds, so translucent
  pixels came out one step light (libwebp does not blend key frames at all);
- a frame after a disposal must not blend over the rectangle that was just
  cleared, or the frame's own transparent pixels lose their colour (libwebp
  splits the row around the previous rectangle);
- the key-frame rule's last clause is about the *previous* frame's rectangle,
  not the current one's, and getting that backwards costs a whole canvas's worth
  of pixels in one frame;
- the blend's fixed-point arithmetic has an exact form; the plausible variants
  are one unit off.

All four are recorded in `docs/phases/79-animated-webp-frames-on-a-canvas.md`
with the source of each rule, and `verify-media` now checks the compositor on
every run instead of asserting a refusal.
