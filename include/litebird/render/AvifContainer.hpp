#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace litebird {

/// The AVIF container — everything around the coded picture, and not the
/// picture.
///
/// An AVIF file is an ISO base media file (ISO/IEC 14496-12) with the AV1
/// image format on top of it (ISO/IEC 23000-22 and the AV1 Image File Format
/// specification). The bytes that are an image live in one *item*, and the
/// metadata that finds it and describes it lives in a `meta` box beside it:
///
///     ftyp  the brand, which says this is an AVIF and which features it needs
///     meta
///       hdlr   must say `pict`
///       pitm   the primary item — the one that is the image
///       iloc   where each item's bytes are: a file offset and a length
///       iinf / infe   what each item is (`av01` is an AV1 image)
///       iprp / ipco   the properties in play, in order, each a box of its own
///            ispe   the image's size, which is known before decoding
///            pixi   the bit depth of each plane
///            av1C   the decoder configuration: profile, level, bit depth,
///                   and the chroma subsampling
///            colr   the colour information (nclx, or an ICC profile)
///       ipma   which of those properties belong to which item
///     mdat  the item's bytes, where `iloc` says they are
///
/// This module parses that structure and stops at the first byte of AV1. It
/// is deliberately not a decoder: a picture cannot be produced from it, and no
/// entry point here pretends otherwise. What it gives the engine is everything
/// `<img>` and the layout need before any samples exist — whether the file is
/// an AVIF at all, the size the box lays itself out at, whether the pixels
/// carry alpha, and the one payload to hand to a decoder when there is one.
///
/// It is separate from `ImageDecoder::decode_bytes` for the reason the WebP
/// work was separate: the container is a few hundred lines of bounds-checked
/// box walking with a lot of ways to be wrong at a hostile file's hand, and it
/// is worth testing on its own before anything codes samples.
class AvifContainer {
public:
    /// What `parse` concluded.
    enum class Status {
        Ok,                  ///< a primary `av01` item was found and located
        NotAvif,             ///< not an ISO base media file with an AVIF brand
        Malformed,           ///< an AVIF brand, but the structure does not hold
        Unsupported,         ///< well formed, but a feature this parser does not read
    };

    /// How a box is parsed when it is not one of the ones we read: not at all.
    struct ColourInfo {
        /// `colr` with `nclx`: the primaries, transfer characteristics and
        /// matrix coefficients, each an ISO/IEC 23091-2 code point.
        bool present = false;
        bool is_nclx = false;
        uint16_t primaries = 0;
        uint16_t transfer = 0;
        uint16_t matrix = 0;
        bool full_range = false;
    };

    struct DecoderConfig {
        /// `av1C`: the four bytes the AV1 Image Format defines, unpacked.
        uint8_t seq_profile = 0;
        uint8_t seq_level_idx_0 = 0;
        bool high_bitdepth = false;
        bool twelve_bit = false;
        bool monochrome = false;
        /// The subsampling the `av1C` declares: 1 means chroma is subsampled
        /// by two in that direction, 0 means it is not.
        bool subsampling_x = false;
        bool subsampling_y = false;
        uint8_t chroma_sample_position = 0;
    };

    /// A located item: the bytes that are the coded picture, and where.
    struct Item {
        uint16_t id = 0;
        std::string type;              ///< `av01`, `grid`, `Exif`, `mime`, …
        /// The item's bytes, concatenated in the order `iloc` lists them.
        /// An `iloc` with more than one extent is legal and used; the extents
        /// are joined, because the AV1 Image Format codes one item as one
        /// sequence of OBUs whatever the extents.
        std::vector<uint8_t> payload;
        bool has_ispe = false;
        uint32_t width = 0;
        uint32_t height = 0;
        bool has_pixi = false;
        std::vector<uint8_t> bit_depth;
        bool has_av1c = false;
        DecoderConfig av1c;
        ColourInfo colour;
        bool hidden = false;           ///< `infe` flags bit 0
    };

    /// Everything the file said, as far as this parser reads it.
    struct File {
        Status status = Status::NotAvif;
        /// A sentence saying what was wrong, empty when `status == Ok`.
        std::string error;

        std::string major_brand;       ///< `avif`
        std::vector<std::string> compatible_brands;

        /// The primary item (`pitm`), when there is one and it is an image.
        uint16_t primary_id = 0;
        Item primary;

        /// Every item `iloc` located and `iinf` named, primary included. This
        /// is what a caller reads to find the alpha plane (an `auxl` item
        /// referring back to the primary) and the `grid` item a tiled image is
        /// split across.
        std::vector<Item> items;

        bool has_primary() const { return status == Status::Ok; }
    };

    /// True when the first box is `ftyp` and the brand is an AVIF one. Cheap,
    /// and every other entry point calls it first.
    static bool is_avif(const uint8_t* data, size_t size);

    /// Parses the structure. Anything this parser does not understand is
    /// `Unsupported` with a sentence rather than a guess.
    static File parse(const uint8_t* data, size_t size);

    /// A sentence for a status, in the same shape the WebP decoder's is.
    static const char* status_string(Status status);
};

} // namespace litebird
