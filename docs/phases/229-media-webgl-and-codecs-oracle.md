# Phase 229 — Audio/Video, WebCodecs, and WebGL Spec-Backed Conformance Oracle

**Date:** 2026-11-23  
**Finding:** [`docs/findings/233-audio-video-webgl-and-codecs-unmeasured.md`](../findings/233-audio-video-webgl-and-codecs-unmeasured.md)  
**Target:** `make verify-media-webgl-codecs` (wired into CI `Build & unit tests` and `asan-verify`) and `make render-conformance`

## Goal

Close the measurement blind spot across the three major missing browser platform stacks — **HTML Audio/Video + MediaSource + Web Audio**, **WebCodecs**, and **WebGL 1/2** — without modifying engine source files (`src/`):

1. Inventory what Litebird actually implements today vs. where `HTMLMediaElement` stubs silently violate the WHATWG HTML specification (no-op `volume`/`muted`/`currentTime`/`playbackRate` setters, unconditional `play()` resolution with no media source, always-empty `canPlayType`, missing constants/`TimeRanges`/`MediaError`) and where `MediaSource`, Web Audio, WebCodecs, and WebGL 1/2 are entirely absent.
2. Add a fast, CI-enforced, spec-backed conformance harness (`tools/conformance/verify_media_webgl_codecs.py`, `tests/conformance/media_webgl_codecs.json`) that grades **25 normative checks** under the strict **`PASS` / `KNOWN` / `FIXED` / `FAIL`** contract with **5 built-in negative controls (`--self-test`)**.
3. Add a paired positive-control + target WebGL pixel reftest pair (`canvas_2d_script_paints_canvas_green` [PASS] and `webgl_clear_paints_canvas_green` [`known_failure`]) to `tests/render/reftests.json` so WebGL-to-compositor pixel output is measured end-to-end.

## What was built

### 1. Spec-backed conformance corpus (`tests/conformance/media_webgl_codecs.json`)

25 normative cases derived strictly from WHATWG HTML §4.8.11 & §4.12.5, W3C Media Source Extensions §3, W3C Web Audio API §1.2, W3C WebCodecs §§3–9, and Khronos WebGL 1.0 / 2.0:

| Domain | Cases | Current baseline | What passes today | What is tracked as `KNOWN` |
| --- | ---: | --- | --- | --- |
| `audio_video` | 12 | **4/12 pass, 8 known** | `<audio>`/`<video>`/`new Audio()` inheritance & `preload="auto"`, initial playback/network state, attribute reflection (`src`, `preload`, `autoplay`, `loop`, `controls`, `defaultMuted`, `poster`, `width`, `height`), `canPlayType("")`/unknown `""` | `HTMLMediaElement` `NETWORK_*`/`HAVE_*` constants, `volume` persistence + `IndexSizeError`, mutable `muted`/`currentTime`/`playbackRate`, `TimeRanges` + `MediaError`, standard audio/video codec `canPlayType`, `play()` rejecting with `NotSupportedError` without source, real 100ms 8kHz mono PCM WAV `data:audio/wav` metadata decode, `MediaSource` + `AudioContext`/`OfflineAudioContext` |
| `webcodecs` | 5 | **0/5 pass, 5 known** | — | Global constructors (`VideoDecoder`, `AudioDecoder`, `VideoEncoder`, `AudioEncoder`, `EncodedVideoChunk`, `EncodedAudioChunk`, `VideoFrame`, `AudioData`), `VideoDecoder.isConfigSupported` (VP8), `AudioDecoder.isConfigSupported` (Opus), `VideoFrame` RGBA round-trip, `EncodedVideoChunk` `copyTo` round-trip |
| `webgl` | 8 | **2/8 pass, 6 known** | `getContext("not-a-real-context-id") === null` on Canvas & OffscreenCanvas, 2D context locking out `"webgl"`/`"webgl2"` on the same canvas | `WebGLRenderingContext` / `WebGL2RenderingContext` / resource constructors, `canvas.getContext("webgl")` singleton & 2D lockout, `canvas.getContext("webgl2")`, `OffscreenCanvas.getContext("webgl")`, `clearColor` + `clear` + `readPixels` RGBA green, GLSL ES 1.00 shader compile + program link + VBO quad draw + `readPixels` red |
| **Total** | **25** | **6/25 pass, 19 known** | **6** | **19** |

### 2. Verifier and negative controls (`tools/conformance/verify_media_webgl_codecs.py`)

- Executes all 25 synchronous and async Promise-returning cases in a single `build/litebird --run-script` run (~0.2 s) using a programmatically generated, valid 100 ms 8 kHz 16-bit mono PCM RIFF/WAVE `data:audio/wav;base64,...` fixture.
- Runs **5 built-in negative controls (`--self-test`)** on every invocation:
  1. wrong observed value on a passing case -> `FAIL`;
  2. unexpected JS exception on a passing case -> `FAIL`;
  3. matching output while `known_failure` is still set -> `FIXED` (exit `1`);
  4. missing output line (silent crash/truncation) -> `FAIL`;
  5. structural verification of the generated RIFF/WAVE `fmt ` + `data` PCM WAV header and exact 800-sample payload.
- Supports `--strict` (fails on any `known_failure`, verified exiting `1` with 19 `FAIL`s).
- Wired into `Makefile` (`verify-media-webgl-codecs` and `asan-verify`) and `.github/workflows/ci.yml` (`Build & unit tests`).

### 3. Paired 2D / WebGL pixel reftests (`tests/render/reftests.json`)

- `canvas_2d_script_paints_canvas_green` (**PASS**, 3 assertions): proves inline `<script>` runs during `--render-png` and composites a 40×40 green `<canvas>` to exact `#00ff00` inside `[0..40, 0..40]` and `#ffffff` outside.
- `webgl_clear_paints_canvas_green` (**`KNOWN`**, 3 assertions): identical `<canvas width="40" height="40">` calling `gl = c.getContext('webgl'); if (gl) { gl.clearColor(0, 1, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); }`, isolating missing WebGL context creation and compositing as the sole variable under test.
- Brings `tests/render/reftests.json` to **56/66 passing, 10 known** (**264 assertions**; **107 pages / 395 assertions** across both render corpora).

## Verification

- `make verify-media-webgl-codecs` -> `self-test: 5/5 negative controls verified`; `6/25 pass, 19 known, 0 fixed, 0 fail` (`audio_video=4/12`, `webcodecs=0/5`, `webgl=2/8`).
- Negative control `python3 tools/conformance/verify_media_webgl_codecs.py --binary build/litebird --strict --quiet` -> exits `1` (`6/25 pass, 0 known, 0 fixed, 19 fail`).
- Live `FIXED` gate control: initial run while `media_element_interfaces_and_inheritance` carried a `known_failure` exited `1` with `FIXED [audio_video] media_element_interfaces_and_inheritance` because `new Audio()` already sets `preload="auto"`, confirming automatic promotion enforcement.
- `make render-conformance` -> `56/66` pass, `10` known (`reftests.json`); `35/41` pass, `6` known (`body-origin.json`); `45016/45016` `Canvas toDataURL` checks agree.
- `python3 tools/ci/check_numbers.py` and `python3 tools/ci/check_docs.py` -> 0 failures.
