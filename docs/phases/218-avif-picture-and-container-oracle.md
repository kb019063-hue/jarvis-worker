# Phase 218 — AVIF container and AV1 picture differential oracle

Finding: [`docs/findings/222-avif-picture-decoding-unmeasured.md`](../findings/222-avif-picture-decoding-unmeasured.md)

## Summary

Adds an independent, CI-enforced AVIF / AV1 container and picture differential oracle (`make verify-avif`, `tools/conformance/verify_avif.py`, `tests/media/avif/corpus.json`) plus `media_check` AVIF modes (`--probe-avif`, `--decode-avif`, `--fuzz-avif`, `--fuzz-batch avif`) and an end-to-end HTML `<img src="data:image/avif;base64,...">` pixel reftest in `tests/render/reftests.json`.

No engine source files (`src/`) are modified. Litebird still has no AV1 intra-frame decoder, and this oracle makes that gap continuously measured in CI without counting unimplemented decodes as passes.

## What was built

1. **`tools/media_check.cpp`:**
   - `--probe-avif <in> [payload_out]`: runs `AvifContainer::is_avif` + `AvifContainer::parse`, emits all container fields (`status`, `major_brand`, `primary_id`, `type`, `ispe`, `pixi`, `av1C`, `colr`, `payload_bytes`), and writes the extracted raw AV1 OBU payload to `payload_out`.
   - `--decode-avif <in> <ppm_out> [rgba_out]`: rejects invalid containers with exit `1`, calls `ImageDecoder::decode_bytes(..., "image/avif")`, exits `3` (`no_av1_decoder`) when the container is `Ok` but `ImageDecoder` returns `nullptr`, and writes PPM/RGBA with exit `0` once an AV1 decoder produces a picture.
   - `--fuzz-avif` and `--fuzz-batch avif`: feeds inputs through `AvifContainer::parse` and `ImageDecoder::decode_bytes`.
2. **`tests/media/avif/corpus.json` (6 real AV1-coded AVIF fixtures, 6.4 KB total):**
   - `solid_black_4x4` (4×4 uniform `(0, 0, 0, 255)` — exact across every YCbCr→RGB matrix), `solid_red_1x1` (1×1), `quadrants_16x16_420` (16×16 RGBY), `odd_gradient_13x9` (non-block-aligned odd dimensions), `checker_24x16` (high-frequency checkerboard), and `grayscale_16x16` (luminance ramp).
   - Each fixture embeds its real AVIF bytes (encoded by ImageMagick / libheif 1.15.1) alongside full libheif-decoded ground-truth 8-bit sRGB RGBA pixels and per-fixture pixel tolerances (`max_mae`, `max_channel_diff`, `min_psnr`), marked `picture_status: "unimplemented"`.
3. **`tools/conformance/verify_avif.py` (`make verify-avif`):**
   - **Independent pure-Python ISO-BMFF + AV1 OBU oracle:** parses `ftyp`, `meta`, `pitm`, `iloc`, `iinf`/`infe`, `iprp`/`ipco`/`ipma`, `ispe`, `pixi`, `av1C`, `colr` and walks every AV1 OBU header per AV1 §5.3 (`obu_forbidden_bit == 0`, `OBU_SEQUENCE_HEADER` + coded frame OBU), verifying every `media_check --probe-avif` field and byte-exact AV1 payload equality.
   - **Live ImageMagick / libheif cross-check:** when `convert` supports `AVIF rw+`, re-decodes every corpus fixture to confirm the committed ground-truth RGBA matches live libheif, and live-encodes 3 additional patterns (`8×8` green, `19×11` odd stripes, `32×24` rich gradient).
   - **AV1 picture pixel comparator with `KNOWN` / `FIXED` gate:** records honest `no_av1_decoder` refusals as `KNOWN` (never counted as passes); when `--decode-avif` eventually returns pixels, compares every pixel against libheif RGBA and exits `1` on mismatch (`FAIL`) or on an unannounced pass (`FIXED`), enforcing promotion of `picture_status` to `"implemented"`.
   - **Refusals & mutation fuzzing:** 8 hostile/malformed/unsupported cases (`NotAvif` empty/PNG/ASCII, `Malformed` truncated `meta`/oversized `meta`/out-of-bounds `iloc`, `Unsupported` `grid` primary and `avis`-without-`meta`) plus 200 deterministic mutation fuzz inputs.
   - **5 built-in negative controls (`--self-test`):** verifies refusal of `obu_forbidden_bit=1`, missing `OBU_SEQUENCE_HEADER`, 1-bit/1-channel pixel error, unannounced `FIXED` transition, and truncated container.
4. **`tests/render/reftests.json`:**
   - Adds `a_data_uri_avif_image_scales_to_its_box` (`<img src="data:image/avif;base64,...">` using the 290-byte solid `#000000` 4×4 AVIF scaled to 40×40) as a `known_failure` (64 pages / 258 assertions in `reftests.json`; 105 pages / 389 assertions combined with `body-origin.json`).
5. **CI integration:** wired `make verify-avif` into `.github/workflows/ci.yml` (`Build & unit tests`) and `Makefile` (`asan-verify`, so ASan/UBSan runs the AVIF oracle and fuzz batch too).

## Verification

- `make verify-avif`:
  - `AVIF oracle self-test: 5/5`
  - `9/9 containers + AV1 OBUs exact, 8/8 refusals exact, 200 fuzz mutations crash-free; AV1 picture decode: 0/9 pass, 9 known unimplemented, 0 FIXED, 0 FAIL (3 live-encoded via libheif)`
- `make render-conformance`:
  - `tests/render/reftests.json`: **55/64 pages match**, **9 fail as recorded** (`a_data_uri_avif_image_scales_to_its_box` added; 64 of 64 accounted for).
  - Negative control: stripping `known_failure` from `a_data_uri_avif_image_scales_to_its_box` exits `1` and reports `FAIL` (40×40 at `(0, 0)` painted `#ffffff` instead of `#000000`).
- `python3 tools/ci/check_numbers.py` (`--self-test` 26/26) and `python3 tools/ci/check_docs.py`: 0 failures.
