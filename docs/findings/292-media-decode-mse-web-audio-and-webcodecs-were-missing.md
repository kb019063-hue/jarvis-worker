# Finding 292 — `HTMLMediaElement` resource decoding, MediaSource, Web Audio API, and WebCodecs were missing

Phase: [`docs/phases/287-media-audio-mse-and-webcodecs.md`](../phases/287-media-audio-mse-and-webcodecs.md)

## What was missing

After Phase 253 (`HTMLMediaElement` state/constants) and Phase 275 (WebGL 1/2 software pipeline), **7** of the 25 normative checks in `tests/conformance/media_webgl_codecs.json` remained `known_failure`:

- `HTMLMediaElement` never decoded media resources, leaving `readyState === 0`, `duration === NaN`, and `buffered`/`seekable` permanently empty even for a self-contained PCM WAV `data:` URI (`media_element_wav_data_uri_loads_metadata`).
- W3C Media Source Extensions (`MediaSource`, `SourceBuffer`, `SourceBufferList`) and W3C Web Audio API (`BaseAudioContext`, `AudioContext`, `OfflineAudioContext`, `AudioBuffer`, `AudioNode`, `GainNode`, `OscillatorNode`, `AudioBufferSourceNode`) did not exist (`media_source_and_web_audio_interfaces`).
- None of the 8 W3C WebCodecs interfaces (`VideoDecoder`, `AudioDecoder`, `VideoEncoder`, `AudioEncoder`, `EncodedVideoChunk`, `EncodedAudioChunk`, `VideoFrame`, `AudioData`) were implemented (`webcodecs_constructors_exist`, `webcodecs_video_decoder_is_config_supported`, `webcodecs_audio_decoder_is_config_supported`, `webcodecs_video_frame_rgba_roundtrip`, `webcodecs_encoded_video_chunk_roundtrip`).

## Resolution

- Added a bounded C++20 media/audio/codecs module (`include/litebird/render/MediaAudioCodecs.hpp`, `src/render/MediaAudioCodecs.cpp`) implementing a RIFF/WAVE PCM & IEEE-float decoder, `data:` URL WAV loading, MSE/WebCodecs MIME & codec support validation, WebCodecs pixel/audio plane layout geometry, a VP8-to-RGBA bridge over LiteBird's existing `Vp8Decoder`, and an offline Web Audio DSP graph renderer.
- Wired `HTMLMediaElement` (`src/js/DomHtmlBindings.cpp`) to decode `data:` WAV resources into live `readyState`, `networkState`, `duration`, `currentSrc`, `buffered`/`seekable` `TimeRanges`, `MediaError`, and `play()`/`pause()` state.
- Added `src/js/DomMediaBindings.cpp` exposing the W3C MSE, Web Audio API, and WebCodecs WebIDL surfaces and registered them from `register_html_interfaces()`.

## Remaining limitations

- `HTMLMediaElement` currently decodes RIFF/WAVE PCM/IEEE-float `data:` URIs; remote HTTP streaming demuxers (MP4/WebM/Ogg/MP3) and compressed audio decoders (Opus/AAC/Vorbis/MP3) remain open.
- `AudioContext` has no physical audio output device in headless mode; real PCM synthesis and graph rendering run through `OfflineAudioContext.startRendering()` and `decodeAudioData()`.
- `VideoDecoder` decodes real VP8 keyframes (via `Vp8Decoder`) and internal `VideoEncoder` packets; hardware/compressed H.264/VP9/AV1 video and Opus/AAC compressed bitstreams are not yet decoded.
