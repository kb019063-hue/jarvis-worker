# Finding 222 — AVIF container extraction and AV1 picture decoding had no differential media oracle or pixel reftest

Phase: [`docs/phases/218-avif-picture-and-container-oracle.md`](../phases/218-avif-picture-and-container-oracle.md)

## What was missing

Phase 172 implemented `AvifContainer` and verified two Pillow-generated files in `tests/test_suite.cpp`, leaving the AV1 picture decoder open (`ImageDecoder::decode` returns `nullptr`). However, unlike JPEG, GIF, WebP, and VP8:

- `tools/media_check.cpp` had no `--probe-avif`, `--decode-avif`, or `--fuzz-avif` modes;
- no differential harness cross-checked `AvifContainer` fields and extracted AV1 OBU payloads against an independent ISO-BMFF + AV1 §5.3 OBU parser or ImageMagick/libheif;
- no pixel comparator held libheif ground-truth RGBA pixels ready to grade an AV1 intra-frame decoder the moment one lands;
- `tests/render/reftests.json` had no `<img src="data:image/avif;base64,...">` pixel reftest recording the missing AV1 picture end-to-end.

## What the oracle measures now

1. **Container & AV1 OBU parity (9/9 exact):** `make verify-avif` independently parses ISO-BMFF boxes (`ftyp`, `meta`, `pitm`, `iloc`, `iinf`/`infe`, `iprp`/`ipco`/`ipma`, `ispe`, `pixi`, `av1C`, `colr`) and validates AV1 OBU framing (`obu_forbidden_bit == 0`, `OBU_SEQUENCE_HEADER` + coded frame OBU) across 6 committed fixtures (`tests/media/avif/corpus.json`) plus 3 live libheif-encoded patterns when `convert` supports `AVIF rw+`, confirming byte-exact payload extraction and metadata parity with `AvifContainer::parse`.
2. **AV1 picture decode gate (0/9 pass, 9 `KNOWN`):** every fixture holds full libheif-decoded ground-truth RGBA pixels and per-image tolerance thresholds (`MAE`, `PSNR`, `max_channel_diff`). Honest `no_av1_decoder` refusals (exit `3`) are logged as `KNOWN`; any future AV1 decoder is automatically graded pixel-by-pixel, failing on wrong pixels (`FAIL`) or on an unannounced pass (`FIXED`).
3. **End-to-end HTML `<img>` pixel reftest:** `a_data_uri_avif_image_scales_to_its_box` in `tests/render/reftests.json` renders a 4×4 solid `#000000` AVIF (`Y=0, Cb=128, Cr=128`, invariant across every YCbCr→RGB matrix) scaled to 40×40 and records `known_failure` until `ImageDecoder::decode` paints it.
4. **Hostile refusals & ASan fuzzing:** 8 refusal cases (`NotAvif`, `Malformed`, `Unsupported`) and 200 deterministic mutations run in both standard CI and `asan-verify`.

## Still open

- **AV1 intra-frame picture decoding itself** (AV1 §8.2 entropy decoder, §9.4 default CDF tables, partitions, intra prediction, inverse transforms, loop filters, YCbCr→RGB) — still unimplemented; now continuously tracked by both `make verify-avif` (9 `KNOWN`) and `make render-conformance` (1 `KNOWN`).
- **AVIF container extensions:** `auxl` alpha plane binding (`iref`/`auxC`), `grid` tiled primaries, `avis` sequences (`moov`), and presentation transforms (`irot`/`imir`/`clap`).
