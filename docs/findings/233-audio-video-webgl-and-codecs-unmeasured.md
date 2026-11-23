# Finding 233 — Audio/Video stubs, WebCodecs, and WebGL 1/2 had no conformance oracle or pixel reftest

Phase: [`docs/phases/229-media-webgl-and-codecs-oracle.md`](../phases/229-media-webgl-and-codecs-oracle.md)

## What was missing

Litebird's roadmap lists `<video>`/`<audio>` media (row 36) as an open XL milestone, and `src/js/DomHtmlBindings.cpp` already exposes minimal `HTMLMediaElement` / `HTMLAudioElement` / `HTMLVideoElement` / `Audio` bindings alongside `HTMLCanvasElement.getContext` and `OffscreenCanvas.getContext`. Before Phase 229, however, nothing in CI measured either the spec violations inside the existing media stubs or the complete absence of MediaSource, Web Audio, WebCodecs, and WebGL:

1. **Silent stub violations in `HTMLMediaElement` (`src/js/DomHtmlBindings.cpp:924-948`):**
   - `el.volume = 0.25` is a no-op setter (`el.volume` remains `1`), and out-of-range values (`-0.1`, `1.5`) do not throw `IndexSizeError` (WHATWG HTML §4.8.11.15);
   - `el.muted = true`, `el.currentTime = 12.5`, and `el.playbackRate = 1.5` are no-op setters returning `false`, `0`, and `1`;
   - `HTMLMediaElement.NETWORK_*` (0..3) and `HAVE_*` (0..4) constants are undefined;
   - `el.buffered`, `el.seekable`, `el.played` (`TimeRanges`) and `MediaError` are absent;
   - `el.canPlayType(...)` unconditionally returns `""` for every standard audio and video MIME/codec string;
   - `el.play()` unconditionally resolves with `undefined` even when no media source is loaded, instead of rejecting with `NotSupportedError` (WHATWG HTML §4.8.11.8);
   - assigning a valid 100 ms 8 kHz 16-bit mono PCM WAV `data:audio/wav;base64,...` URI leaves `readyState === 0` and `duration === NaN`.
2. **Absent platform stacks with zero CI visibility:**
   - `MediaSource` (W3C MSE) and `AudioContext` / `OfflineAudioContext` (W3C Web Audio) are undefined;
   - all 8 W3C WebCodecs constructors (`VideoDecoder`, `AudioDecoder`, `VideoEncoder`, `AudioEncoder`, `EncodedVideoChunk`, `EncodedAudioChunk`, `VideoFrame`, `AudioData`) are undefined;
   - all WebGL 1/2 constructors (`WebGLRenderingContext`, `WebGL2RenderingContext`, `WebGLShader`, `WebGLProgram`, `WebGLBuffer`, `WebGLTexture`, `WebGLFramebuffer`) are undefined, and `getContext("webgl")` / `getContext("webgl2")` return `null` on both `HTMLCanvasElement` and `OffscreenCanvas`.

## What the oracle measures now

- **`make verify-media-webgl-codecs`** (`tools/conformance/verify_media_webgl_codecs.py`, `tests/conformance/media_webgl_codecs.json`, wired into CI `Build & unit tests` and `asan-verify`): grades **25 normative spec checks** with **5 built-in negative controls (`--self-test`)** under the `PASS` / `KNOWN` / `FIXED` / `FAIL` contract:
  - **`audio_video`:** `4/12` pass, `8` `KNOWN`
  - **`webcodecs`:** `0/5` pass, `5` `KNOWN`
  - **`webgl`:** `2/8` pass, `6` `KNOWN`
  - **Combined:** `6/25` pass, `19` `KNOWN`, `0` `FIXED`, `0` `FAIL`
- **`tests/render/reftests.json`** (`make render-conformance`): adds a paired 2D positive control (`canvas_2d_script_paints_canvas_green`, `PASS`) and WebGL target (`webgl_clear_paints_canvas_green`, `KNOWN`) so that once WebGL lands, end-to-end canvas compositing is immediately graded pixel-by-pixel (`56/66` pass, `10` `KNOWN`; **107 pages / 395 assertions** across both render corpora).

## Still open

- Fixing the 8 `HTMLMediaElement` / `MediaSource` / Web Audio gaps (starting with spec-compliant state properties, range checks, `TimeRanges`/`MediaError`, and a real audio/video demux+decode pipeline — e.g. libav-class stack per `ROADMAP.md` row 36).
- Implementing W3C WebCodecs (`VideoDecoder`, `AudioDecoder`, `VideoFrame`, `EncodedVideoChunk`, etc.) — tracked by the 5 `webcodecs` `KNOWN` cases.
- Implementing WebGL 1/2 (`getContext("webgl")` / `"webgl2"`, GLSL ES shader pipeline, framebuffer compositing into `<canvas>`) — tracked by the 6 `webgl` `KNOWN` cases plus `webgl_clear_paints_canvas_green`.
