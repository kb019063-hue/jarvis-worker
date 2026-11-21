# Phase 167 — the AVIF container, read; the picture still open

Task row: **AVIF**. This is the container half of it. Finding 172.

## 1. What the row asked for, and what this is

The row says one word, "AVIF", in a list of formats the engine decodes. What
that would take is an AV1 intra-frame decoder: the arithmetic decoder of AV1
§8.2 with its adaptive CDF, the partition tree, the intra modes, the coefficient
context model, every inverse transform, thirteen intra predictors plus the
recursive filter intra and chroma-from-luma, and the three in-loop filters.
`Vp8Decoder.cpp` is 1,451 lines with 583 of tables and VP8 is a much smaller
codec than AV1. It is not a piece of work that fits in one branch and one pull
request, and a half-decoder that returns a field of grey is worse than none.

So this phase takes the half that is separable and testable on its own: **the
container**. An AVIF file is an ISO base media file with boxes, and the boxes
are a few hundred lines of bounds-checked parsing with a large number of ways to
be wrong at a hostile file's hand. Reading them is what lets the engine answer,
without any decoder at all:

- is this file an AVIF? (`ftyp` brand `avif`, or `avis` for a sequence)
- what size is the picture, so the box can be laid out? (`ispe`)
- does it carry alpha and how deep is it? (`pixi`, and the `auxl` item)
- what are the decoder's parameters? (`av1C`: profile, level, bit depth,
  subsampling)
- what colour are those samples? (`colr`, `nclx`)
- and where exactly are the coded bytes? (`iloc` extents into `mdat`)

The decoder, when it exists, is handed one buffer and a `DecoderConfig`.

**The row stays open.** `ImageDecoder::decode_bytes` now recognises an AVIF and
still returns `nullptr`, and a test asserts that, so the day a decoder lands the
test fails and says so. Nothing here pretends the picture can be produced.

## 2. What changed

- **`include/litebird/render/AvifContainer.hpp`**, **`src/render/AvifContainer.cpp`**
  — new. `is_avif`, `parse`, `status_string`, and a `File` holding the brands,
  the primary item and every item located. The boxes read: `ftyp`, `meta`,
  `hdlr`, `pitm`, `iloc`, `iinf`/`infe`, `iprp`/`ipco`, `ipma`, `ispe`, `pixi`,
  `av1C`, `colr`.
- **`src/render/ImageDecoder.cpp`** — an AVIF is recognised before the SVG
  sniffing and refused with `nullptr`, as it was, but now the container can say
  why.
- **`tests/test_suite.cpp`** — two suites, on real files written by Pillow
  13.0.0 (libavif): a 4x4 and a 32x32.

The structure is read defensively, because every field in it is a length or an
offset chosen by whoever made the file:

- a box whose declared size runs past the end of its parent is `Malformed`;
- an `iloc` extent that points outside the file is `Malformed`;
- a `meta` box smaller than the boxes inside it is `Malformed`;
- more items, extents or properties than a file could hold is `Malformed`;
- a legal feature not read here — `iloc` version 3, `construction_method` 1
  (`idat`), a `grid` primary, an AVIF sequence — is `Unsupported` with a
  sentence, which is a different answer from `Malformed` and says the file may
  be perfectly good.

## 3. How it is verified

- **Real files, every field.** Five AVIFs written by Pillow 13.0.0 parse to the
  values they hold: 4x4, 16x16, 32x32, one with alpha. `ispe` gives the size,
  `pixi` three 8-bit planes, `av1C` profile 0 with 4:2:0 subsampling and 8-bit,
  `colr` `nclx`.
- **The payload is byte-exact.** The item's bytes are compared against the
  contents of the `mdat` box, found independently in the file, not against a
  length.
- **The fixtures' numbers were checked by hand first**, from a box dump: `iloc`
  version 0, offset size 4, length size 4, item 1, extent offset `0x113` and
  length `0x32` — and `0x113` is where the `mdat` payload starts.
- **200,000 mutated files under ASan and UBSan**: 20,681 still parsed as valid,
  56,265 were not AVIF, 111,323 malformed, 11,731 unsupported — no crash, no
  leak, no undefined behaviour. The invariant asserted on every one that parsed:
  the primary item is `av01` and its payload is under the 64 MiB ceiling.
- **`make test`** 321 / 321 suites.
- **Not decoded, and said so in a test**: `ImageDecoder::decode` on both
  fixtures returns `nullptr`.

## 4. What is still open on this row

- **The AV1 decoder itself**, which is the row. The AV1 specification lists its
  default CDF tables normatively (§9.4), so the tables can be taken from the
  source the way `Vp8Tables.cpp` took VP8's from RFC 6386, rather than from
  libaom. The shape of the work is then the same as Phase 57's VP8: arithmetic
  decoder, syntax, prediction, transforms, loop filters, and a `verify-media`
  oracle.
- **Alpha.** An AVIF carries transparency as a second `av01` item that an
  `auxl` reference marks as the primary's alpha plane. `iref`/`auxC` are not
  read here; `items` already holds every located item, so the plane is in hand
  to find.
- **Tiled images.** A large AVIF's primary item may be a `grid` whose cells are
  their own items. That is `Unsupported` here, and the `grid` payload (rows,
  columns, output size) is the next piece of container work.
- **Sequences.** An `avis` file stores frames in a track (`moov`), not in
  `meta`; that is the animated AVIF case.
- **`irot` and `imir`** (rotation and mirroring) and **`clap`** are properties
  that change how the samples are presented and are not read here.
