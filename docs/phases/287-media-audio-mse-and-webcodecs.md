# Phase 287 — `HTMLMediaElement` WAV decode, MediaSource (MSE), Web Audio API, and WebCodecs

Finding: [`docs/findings/292-media-decode-mse-web-audio-and-webcodecs-were-missing.md`](../findings/292-media-decode-mse-web-audio-and-webcodecs-were-missing.md)

## Scope

Completes the remaining **7** `known_failure` checks in `tests/conformance/media_webgl_codecs.json`, bringing the entire audio/video, WebCodecs, and WebGL conformance oracle to **25/25 PASS (100%, 0 known)**:

1. **Media / Audio / Codecs Core (`include/litebird/render/MediaAudioCodecs.hpp`, `src/render/MediaAudioCodecs.cpp`, `Makefile`):**
   - **PCM / IEEE-float RIFF/WAVE decoder (`decode_wav`, `decode_wav_data_uri`):** parses RIFF/WAVE chunks with strict bounds and memory caps (`<= 32` channels, `<= 64M` total samples), supports 8/16/24/32-bit PCM and 32-bit IEEE float (plus `WAVE_FORMAT_EXTENSIBLE`), computes exact duration, decodes planar normalized `float` samples, and decodes `data:` URIs via Fetch's `process_data_url`.
   - **MSE & WebCodecs MIME/codec support tables:** `media_source_is_type_supported`, `webcodecs_video_codec_supported`, `webcodecs_audio_codec_supported`, pixel/audio plane layout calculators (`video_pixel_format_layout`, `audio_sample_format_info` bounded by `RasterBudget::allows`), and `decode_vp8_to_rgba` bridging LiteBird's existing `Vp8Decoder` to RGBA8 `VideoFrame` buffers.
   - **Web Audio offline DSP renderer (`render_offline_audio_graph`):** renders connected `OscillatorNode` (`sine`, `square`, `sawtooth`, `triangle`), `AudioBufferSourceNode`, and `GainNode` chains into `OfflineAudioContext` output buffers.
2. **`HTMLMediaElement` loading & `TimeRanges` / `MediaError` (`src/js/DomHtmlBindings.cpp`):**
   - Upgraded `TimeRanges` and `MediaError` to carry live ranges and error codes/messages.
   - Wired `HTMLMediaElement.src` / `load()` / `<source>` to decode `data:` WAV resources, populating `readyState = HAVE_ENOUGH_DATA`, `networkState = NETWORK_IDLE`, finite `duration`, `currentSrc`, `buffered`/`seekable` `TimeRanges`, `play()` resolution when loaded (while keeping `NotSupportedError` rejection when no supported source is loaded), and `MediaError(MEDIA_ERR_SRC_NOT_SUPPORTED)` on corrupt `data:` audio.
3. **MSE, Web Audio, and WebCodecs WebIDL bindings (`src/js/DomMediaBindings.cpp`, registered via `register_media_interfaces()`):**
   - **W3C MSE:** `MediaSource` (`isTypeSupported`, `readyState`, `duration`, `sourceBuffers`, `activeSourceBuffers`, `addSourceBuffer`, `removeSourceBuffer`, `endOfStream`), `SourceBuffer`, `SourceBufferList`.
   - **W3C Web Audio:** `BaseAudioContext`, `AudioContext`, `OfflineAudioContext` (`startRendering`), `AudioBuffer`, `AudioNode`, `AudioParam`, `AudioDestinationNode`, `GainNode`, `OscillatorNode`, `AudioBufferSourceNode`, `AnalyserNode`, `BiquadFilterNode`, and `decodeAudioData`.
   - **W3C WebCodecs:** `VideoFrame`, `AudioData`, `EncodedVideoChunk`, `EncodedAudioChunk`, `VideoDecoder`, `VideoEncoder`, `AudioDecoder`, `AudioEncoder` (including static `isConfigSupported`, `configure`/`decode`/`encode`/`flush`/`reset`/`close`, `copyTo`, `allocationSize`, `clone`, `close`, and VP8 keyframe / lossless frame round-trip decoding).
4. **Conformance & Tests:**
   - Promoted all **7** remaining `known_failure` entries in `tests/conformance/media_webgl_codecs.json` (`media_element_wav_data_uri_loads_metadata`, `media_source_and_web_audio_interfaces`, `webcodecs_constructors_exist`, `webcodecs_video_decoder_is_config_supported`, `webcodecs_audio_decoder_is_config_supported`, `webcodecs_video_frame_rgba_roundtrip`, `webcodecs_encoded_video_chunk_roundtrip`) — **25/25 PASS (0 known)**.
   - Added `tests/suites/287-media-audio-mse-and-webcodecs.cpp`.

## Verification

- `make test`: all engine unit suites pass including `media_decode_mse_web_audio_and_webcodecs_work_end_to_end`.
- `make verify-media-webgl-codecs`: **25/25** pass, **0** known, **0** fixed, **0** fail (`audio_video=12/12`, `webcodecs=5/5`, `webgl=8/8`).
- `make render-conformance`, `check_rows.py`, `check_docs.py`, `check_numbers.py`, `check_suites.py`, `check_ci_parts.py`: all 0 failures.
