// The AVIF container: the boxes around the picture, and not the picture.
//
// Read the header for what an AVIF file is. What matters here is that every
// field in that structure is a length or an offset written by whoever made the
// file, and a browser is handed files it did not make. So each box is read
// against the range its parent declares, and a length that runs past the end of
// its parent is `Malformed` rather than a read past the buffer. The three
// places that would otherwise be unsafety are:
//
//   - a box whose declared size is larger than what is there,
//   - an `iloc` extent that points outside the file,
//   - a `meta` box that says it is smaller than the boxes inside it.
//
// A `size` of 0 means "to the end of the parent" and a `size` of 1 means a
// 64-bit size follows; both are legal ISO base media and both are handled. The
// `ipma` property indices are one-based and a file may number them in either 7
// or 15 bits, which is a byte-flag per association.

#include "litebird/render/AvifContainer.hpp"

#include <algorithm>
#include <cstring>

namespace litebird {
namespace {

constexpr uint32_t kMaxItemPayload = 64u * 1024u * 1024u;   // same ceiling as ImageDecoder
constexpr int kMaxBoxDepth = 8;
constexpr size_t kMaxItems = 4096;

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
uint64_t be64(const uint8_t* p) { return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4); }

/// Reads an `iloc`-sized integer: 0, 4 or 8 bytes, big-endian.
bool read_sized(const uint8_t* p, size_t avail, int size, uint64_t& out) {
    if (size == 0) { out = 0; return true; }
    if (size != 4 && size != 8) return false;
    if (avail < static_cast<size_t>(size)) return false;
    out = (size == 4) ? be32(p) : be64(p);
    return true;
}

struct Box {
    std::string type;
    const uint8_t* payload = nullptr;
    size_t size = 0;      ///< payload bytes, after the header
};

/// What a walk concluded. `Stopped` and `Malformed` are both "not all boxes
/// were visited", and conflating them was a real bug here: `ftyp` is the first
/// box, so a walk that stops when it finds it looked exactly like a walk that
/// failed on its first box, and every well-formed file was reported as
/// unreadable.
enum class Walk { Ok, Stopped, Malformed };

/// Walks the boxes in `[data, data+size)` and calls `visit` with each. `visit`
/// returns false to stop early. Every box's payload length is checked against
/// the range it sits in before it is handed over.
template <typename Fn>
Walk walk_boxes(const uint8_t* data, size_t size, Fn&& visit) {
    size_t off = 0;
    while (off + 8 <= size) {
        const uint32_t declared = be32(data + off);
        const std::string type(reinterpret_cast<const char*>(data + off + 4), 4);
        size_t header = 8;
        uint64_t total = declared;
        if (declared == 1) {
            if (off + 16 > size) return Walk::Malformed;
            total = be64(data + off + 8);
            header = 16;
        } else if (declared == 0) {
            total = size - off;                      // "to the end of the parent"
        }
        if (total < header) return Walk::Malformed;  // a box must hold its header
        if (total > size - off) return Walk::Malformed;   // and it must fit
        Box box{type, data + off + header, static_cast<size_t>(total - header)};
        if (!visit(box)) return Walk::Stopped;
        off += static_cast<size_t>(total);
        if (total == 0) return Walk::Malformed;      // cannot make progress
    }
    return Walk::Ok;
}

/// The `ftyp` brands. `is_avif` is the cheap answer and this is the detail.
bool read_ftyp(const uint8_t* data, size_t size, std::string& major,
               std::vector<std::string>& compatible, std::string& error) {
    Box box;
    bool found = false;
    if (walk_boxes(data, size, [&](const Box& b) {
            if (b.type == "ftyp") { box = b; found = true; return false; }
            return true;
        }) == Walk::Malformed) {
        error = "a box header runs past the end of the file";
        return false;
    }
    if (!found || box.size < 8) {
        error = "no ftyp box, so this is not an AVIF file";
        return false;
    }
    major.assign(reinterpret_cast<const char*>(box.payload), 4);
    compatible.clear();
    for (size_t off = 8; off + 4 <= box.size; off += 4) {
        compatible.emplace_back(reinterpret_cast<const char*>(box.payload + off), 4);
    }
    return true;
}

bool is_avif_brand(const std::string& brand) {
    // `avif` is a still AVIF; `avis` is a sequence. Both are AVIF, and a still
    // decoder reads the first frame of a sequence, so both are accepted here
    // and the difference is recorded rather than rejected.
    return brand == "avif" || brand == "avis";
}

/// `ispe`: a full box whose payload is a version/flags word then width, height.
bool read_ispe(const Box& box, uint32_t& w, uint32_t& h) {
    if (box.size < 12) return false;
    const uint8_t* p = box.payload;
    if (be32(p) >> 24 != 0) return false;            // version 0 only
    w = be32(p + 4);
    h = be32(p + 8);
    return w != 0 && h != 0;
}

/// `pixi`: version/flags, a plane count, then one bit depth per plane.
bool read_pixi(const Box& box, std::vector<uint8_t>& depths) {
    if (box.size < 5) return false;
    const uint8_t planes = box.payload[4];
    if (planes == 0 || box.size < 5u + planes) return false;
    depths.assign(box.payload + 5, box.payload + 5 + planes);
    return true;
}

/// `av1C`: four bytes, and the first must carry the marker and version.
bool read_av1c(const Box& box, AvifContainer::DecoderConfig& out) {
    if (box.size < 4) return false;
    const uint8_t* p = box.payload;
    if ((p[0] >> 7) != 1 || (p[0] & 0x7f) != 1) return false;   // marker, version 1
    out.seq_profile = static_cast<uint8_t>(p[1] >> 5);
    out.seq_level_idx_0 = static_cast<uint8_t>(p[1] & 0x1f);
    out.high_bitdepth = (p[2] & 0x40) != 0;
    out.twelve_bit = (p[2] & 0x20) != 0;
    out.monochrome = (p[2] & 0x10) != 0;
    out.subsampling_x = (p[2] & 0x08) != 0;
    out.subsampling_y = (p[2] & 0x04) != 0;
    out.chroma_sample_position = static_cast<uint8_t>(p[2] & 0x03);
    return true;
}

/// `colr`: the colour type, then either `nclx`'s four fields or `prof`/`rICC`.
bool read_colr(const Box& box, AvifContainer::ColourInfo& out) {
    if (box.size < 4) return false;
    const std::string kind(reinterpret_cast<const char*>(box.payload), 4);
    out.present = true;
    if (kind == "nclx") {
        if (box.size < 11) return false;
        out.is_nclx = true;
        out.primaries = be16(box.payload + 4);
        out.transfer = be16(box.payload + 6);
        out.matrix = be16(box.payload + 8);
        out.full_range = (box.payload[10] & 0x80) != 0;
        return true;
    }
    // `prof` and `rICC` carry an ICC profile, which is real and not read here.
    out.is_nclx = false;
    return true;
}

} // namespace

bool AvifContainer::is_avif(const uint8_t* data, size_t size) {
    if (!data || size < 12) return false;
    if (std::memcmp(data + 4, "ftyp", 4) != 0) return false;
    const std::string major(reinterpret_cast<const char*>(data + 8), 4);
    if (is_avif_brand(major)) return true;
    // A file whose major brand is generic can still list `avif` as compatible.
    for (size_t off = 16; off + 4 <= std::min<size_t>(size, 8 + be32(data)); off += 4) {
        if (is_avif_brand(std::string(reinterpret_cast<const char*>(data + off), 4))) {
            return true;
        }
    }
    return false;
}

const char* AvifContainer::status_string(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotAvif: return "not an AVIF file";
        case Status::Malformed: return "malformed AVIF structure";
        case Status::Unsupported: return "an AVIF feature this parser does not read";
    }
    return "unknown";
}

AvifContainer::File AvifContainer::parse(const uint8_t* data, size_t size) {
    File file;
    if (!data || size < 12) {
        file.status = Status::NotAvif;
        file.error = "too short to hold a file header";
        return file;
    }
    if (!read_ftyp(data, size, file.major_brand, file.compatible_brands, file.error)) {
        file.status = Status::NotAvif;
        return file;
    }
    bool avif_brand = is_avif_brand(file.major_brand);
    for (const auto& b : file.compatible_brands) avif_brand = avif_brand || is_avif_brand(b);
    if (!avif_brand) {
        file.status = Status::NotAvif;
        file.error = "ftyp lists no avif or avis brand";
        return file;
    }

    // A still AVIF has a `meta` box; a sequence has `moov` and its frames are
    // in a track. This parser reads stills, so a sequence is `Unsupported` with
    // a sentence rather than a half-read.
    const Box* meta = nullptr;
    Box meta_box;
    const bool malformed =
        walk_boxes(data, size, [&](const Box& b) {
            if (b.type == "meta") { meta_box = b; meta = &meta_box; return false; }
            return true;
        }) == Walk::Malformed;
    if (!meta) {
        file.status = malformed ? Status::Malformed : Status::Unsupported;
        file.error = malformed ? "a box header runs past the end of the file"
                               : "no meta box: an AVIF sequence, not a still";
        return file;
    }

    // Inside `meta`: a full-box header, then boxes.
    if (meta->size < 4) {
        file.status = Status::Malformed;
        file.error = "the meta box is too short to hold its own header";
        return file;
    }
    const uint8_t* m = meta->payload + 4;
    const size_t msize = meta->size - 4;

    bool have_pitm = false;
    uint16_t primary_id = 0;
    bool pitm_is_32 = false;
    const uint8_t* iloc = nullptr;   // points at the full-box word
    size_t iloc_size = 0;
    bool have_iloc = false;
    const uint8_t* iinf = nullptr;
    size_t iinf_size = 0;
    bool have_iinf = false;
    const uint8_t* iprp = nullptr;
    size_t iprp_size = 0;
    bool have_iprp = false;
    std::string hdlr;

    const Walk walk = walk_boxes(m, msize, [&](const Box& b) {
        if (b.type == "pitm") {
            if (b.size < 4) return false;
            const uint8_t version = static_cast<uint8_t>(b.payload[0]);
            if (version == 0) {
                if (b.size < 6) return false;
                primary_id = be16(b.payload + 4);
            } else if (version == 1) {
                if (b.size < 8) return false;
                primary_id = static_cast<uint16_t>(std::min<uint32_t>(be32(b.payload + 4), 0xffff));
                pitm_is_32 = true;
            } else {
                return false;
            }
            have_pitm = true;
        } else if (b.type == "iloc") {
            iloc = b.payload; iloc_size = b.size; have_iloc = true;
        } else if (b.type == "iinf") {
            iinf = b.payload; iinf_size = b.size; have_iinf = true;
        } else if (b.type == "iprp") {
            iprp = b.payload; iprp_size = b.size; have_iprp = true;
        } else if (b.type == "hdlr") {
            if (b.size < 12) return false;
            hdlr.assign(reinterpret_cast<const char*>(b.payload + 8), 4);
        }
        return true;
    });
    if (walk == Walk::Malformed) {
        file.status = Status::Malformed;
        file.error = "a box inside meta does not fit";
        return file;
    }
    if (!hdlr.empty() && hdlr != "pict") {
        file.status = Status::Unsupported;
        file.error = "the meta box's handler is '" + hdlr + "', not 'pict'";
        return file;
    }
    if (!have_pitm || !have_iloc) {
        file.status = Status::Malformed;
        file.error = have_pitm ? "no iloc box, so no item's bytes can be found"
                               : "no pitm box, so there is no primary item";
        return file;
    }
    (void)pitm_is_32;

    // --- iinf: item id -> type name -----------------------------------------
    std::vector<std::pair<uint16_t, std::string>> item_types;
    if (have_iinf) {
        if (iinf_size < 4) {
            file.status = Status::Malformed;
            file.error = "the iinf box is too short";
            return file;
        }
        const uint8_t version = static_cast<uint8_t>(iinf[0]);
        size_t off;
        size_t count = 0;
        if (version == 0) {
            if (iinf_size < 6) {
                file.status = Status::Malformed;
                file.error = "the iinf box is too short";
                return file;
            }
            count = be16(iinf + 4);
            off = 6;
        } else {
            if (iinf_size < 8) {
                file.status = Status::Malformed;
                file.error = "the iinf box is too short";
                return file;
            }
            count = be32(iinf + 4);
            off = 8;
        }
        if (count > kMaxItems) {
            file.status = Status::Malformed;
            file.error = "iinf declares more items than a file can hold";
            return file;
        }
        std::vector<std::pair<uint16_t, std::string>> seen;
        const Walk iinf_walk = walk_boxes(iinf + off, iinf_size - off, [&](const Box& b) {
            if (b.type != "infe") return true;
            if (b.size < 8) return false;
            const uint8_t v = static_cast<uint8_t>(b.payload[0]);
            uint16_t id = 0;
            size_t at = 0;
            if (v < 2) {
                id = be16(b.payload + 4);
                at = 6;
            } else {
                if (b.size < 10) return false;
                id = be16(b.payload + 4);
                at = 8;   // version 2/3 add an item_protection_index
            }
            if (at + 4 > b.size) return false;
            seen.emplace_back(id, std::string(reinterpret_cast<const char*>(b.payload + at), 4));
            return true;
        });
        if (iinf_walk == Walk::Malformed) {
            file.status = Status::Malformed;
            file.error = "an infe box does not fit";
            return file;
        }
        item_types = std::move(seen);
    }

    // --- ipco: the properties, in order -------------------------------------
    std::vector<Box> properties;
    const uint8_t* ipma = nullptr;
    size_t ipma_size = 0;
    if (have_iprp) {
        bool iprp_bad = false;
        walk_boxes(iprp, iprp_size, [&](const Box& b) {
            if (b.type == "ipco") {
                if (walk_boxes(b.payload, b.size, [&](const Box& p) {
                        if (properties.size() >= kMaxItems) return false;
                        properties.push_back(p);
                        return true;
                    }) == Walk::Malformed) iprp_bad = true;
            } else if (b.type == "ipma") {
                ipma = b.payload; ipma_size = b.size;
            }
            return true;
        });
        if (iprp_bad) {
            file.status = Status::Malformed;
            file.error = "a box inside iprp does not fit";
            return file;
        }
    }

    // --- iloc: item id -> the bytes, cut out of the file --------------------
    struct Location { uint16_t id; std::vector<std::pair<uint64_t, uint64_t>> extents; };
    std::vector<Location> locations;
    if (iloc_size < 4) {
        file.status = Status::Malformed;
        file.error = "the iloc box is too short";
        return file;
    }
    {
        const uint8_t version = static_cast<uint8_t>(iloc[0]);
        if (version > 2) {
            file.status = Status::Unsupported;
            file.error = "iloc version " + std::to_string(version) + " is not read here";
            return file;
        }
        size_t off = 4;
        auto need = [&](size_t n) { return off + n <= iloc_size; };
        if (!need(2)) {
            file.status = Status::Malformed;
            file.error = "the iloc box is too short";
            return file;
        }
        const uint8_t sizes = iloc[off];
        const int offset_size = sizes >> 4;
        const int length_size = sizes & 0x0f;
        const int base_size = iloc[off + 1] >> 4;
        const int index_size = (version >= 1) ? (iloc[off + 1] & 0x0f) : 0;
        off += 2;
        uint64_t item_count = 0;
        if (version < 2) {
            if (!need(2)) {
                file.status = Status::Malformed;
                file.error = "the iloc box is too short";
                return file;
            }
            item_count = be16(iloc + off);
            off += 2;
        } else {
            if (!need(4)) {
                file.status = Status::Malformed;
                file.error = "the iloc box is too short";
                return file;
            }
            item_count = be32(iloc + off);
            off += 4;
        }
        if (item_count > kMaxItems) {
            file.status = Status::Malformed;
            file.error = "iloc declares more items than a file can hold";
            return file;
        }
        for (uint64_t i = 0; i < item_count; ++i) {
            Location loc{};
            uint64_t id = 0, method = 0, base = 0, extent_count = 0;
            if (version < 2) {
                if (!need(2)) { file.status = Status::Malformed; file.error = "iloc runs out of bytes"; return file; }
                id = be16(iloc + off); off += 2;
            } else {
                if (!need(4)) { file.status = Status::Malformed; file.error = "iloc runs out of bytes"; return file; }
                id = be32(iloc + off); off += 4;
            }
            loc.id = static_cast<uint16_t>(id);
            if (version >= 1) {
                // construction_method: 0 is in this file, 1 is in an idat box,
                // 2 is an item offset. Only 0 is read here.
                if (!need(2)) { file.status = Status::Malformed; file.error = "iloc runs out of bytes"; return file; }
                method = be16(iloc + off) & 0x000f;
                off += 2;
            }
            if (!need(2)) { file.status = Status::Malformed; file.error = "iloc runs out of bytes"; return file; }
            off += 2;                                    // data_reference_index
            if (!read_sized(iloc + off, iloc_size - off, base_size, base)) {
                file.status = Status::Malformed; file.error = "the iloc box is too short"; return file;
            }
            off += static_cast<size_t>(base_size);
            if (!need(2)) { file.status = Status::Malformed; file.error = "iloc runs out of bytes"; return file; }
            extent_count = be16(iloc + off);
            off += 2;
            if (extent_count > kMaxItems) {
                file.status = Status::Malformed;
                file.error = "an item declares more extents than a file can hold";
                return file;
            }
            if (method != 0) {
                file.status = Status::Unsupported;
                file.error = "an item is stored in an idat box, which is not read here";
                return file;
            }
            for (uint64_t e = 0; e < extent_count; ++e) {
                uint64_t extent_index = 0, extent_offset = 0, extent_length = 0;
                if (index_size && !read_sized(iloc + off, iloc_size - off, index_size, extent_index)) {
                    file.status = Status::Malformed; file.error = "the iloc box is too short"; return file;
                }
                off += static_cast<size_t>(index_size);
                if (!read_sized(iloc + off, iloc_size - off, offset_size, extent_offset)) {
                    file.status = Status::Malformed; file.error = "the iloc box is too short"; return file;
                }
                off += static_cast<size_t>(offset_size);
                if (!read_sized(iloc + off, iloc_size - off, length_size, extent_length)) {
                    file.status = Status::Malformed; file.error = "the iloc box is too short"; return file;
                }
                off += static_cast<size_t>(length_size);
                loc.extents.emplace_back(base + extent_offset, extent_length);
            }
            locations.push_back(std::move(loc));
        }
    }

    // --- ipma: item id -> the properties it has -----------------------------
    std::vector<std::pair<uint16_t, std::vector<uint32_t>>> associations;
    if (ipma) {
        if (ipma_size < 8) {
            file.status = Status::Malformed;
            file.error = "the ipma box is too short";
            return file;
        }
        const uint8_t version = static_cast<uint8_t>(ipma[0]);
        const uint8_t flags = static_cast<uint8_t>(ipma[1] & 0xff);   // low 8 of 24; only bit 0 is read
        const bool wide_index = (flags & 1) != 0;
        size_t off = 4;
        auto need = [&](size_t n) { return off + n <= ipma_size; };
        if (!need(4)) {
            file.status = Status::Malformed;
            file.error = "the ipma box is too short";
            return file;
        }
        const uint32_t entries = be32(ipma + off);
        off += 4;
        if (entries > kMaxItems) {
            file.status = Status::Malformed;
            file.error = "ipma declares more entries than a file can hold";
            return file;
        }
        for (uint32_t i = 0; i < entries; ++i) {
            uint16_t id = 0;
            if (version < 1) {
                if (!need(2)) { file.status = Status::Malformed; file.error = "ipma runs out of bytes"; return file; }
                id = be16(ipma + off);
                off += 2;
            } else {
                if (!need(4)) { file.status = Status::Malformed; file.error = "ipma runs out of bytes"; return file; }
                id = static_cast<uint16_t>(be32(ipma + off));
                off += 4;
            }
            if (!need(1)) { file.status = Status::Malformed; file.error = "ipma runs out of bytes"; return file; }
            const uint8_t count = ipma[off++];
            std::vector<uint32_t> indices;
            for (uint8_t a = 0; a < count; ++a) {
                if (!need(wide_index ? 2u : 1u)) {
                    file.status = Status::Malformed;
                    file.error = "ipma runs out of bytes";
                    return file;
                }
                uint32_t index = 0;
                if (wide_index) {
                    index = static_cast<uint32_t>(be16(ipma + off) & 0x7fff);
                    off += 2;
                } else {
                    index = static_cast<uint32_t>(ipma[off] & 0x7f);
                    off += 1;
                }
                indices.push_back(index);          // one-based, see below
            }
            associations.emplace_back(id, std::move(indices));
        }
    }

    // --- assemble the items -------------------------------------------------
    if (locations.size() > kMaxItems) {
        file.status = Status::Malformed;
        file.error = "more items than a file can hold";
        return file;
    }
    for (const auto& loc : locations) {
        Item item;
        item.id = loc.id;
        for (const auto& t : item_types) {
            if (t.first == loc.id) item.type = t.second;
        }
        // The properties, in the order ipma lists them. An index is one-based
        // into ipco, so 0 means "none" and is skipped.
        for (const auto& assoc : associations) {
            if (assoc.first != loc.id) continue;
            for (uint32_t index : assoc.second) {
                if (index == 0 || index > properties.size()) continue;
                const Box& prop = properties[index - 1];
                if (prop.type == "ispe") {
                    item.has_ispe = read_ispe(prop, item.width, item.height);
                } else if (prop.type == "pixi") {
                    item.has_pixi = read_pixi(prop, item.bit_depth);
                } else if (prop.type == "av1C") {
                    item.has_av1c = read_av1c(prop, item.av1c);
                } else if (prop.type == "colr") {
                    read_colr(prop, item.colour);
                }
            }
        }
        // The bytes. Each extent must lie inside the file, and together they
        // must not be more than the ceiling the image decoders use.
        size_t total = 0;
        for (const auto& ext : loc.extents) {
            if (ext.first > size || ext.second > size - ext.first) {
                file.status = Status::Malformed;
                file.error = "an item's bytes lie outside the file";
                return file;
            }
            total += static_cast<size_t>(ext.second);
            if (total > kMaxItemPayload) {
                file.status = Status::Malformed;
                file.error = "an item is larger than the decoder's ceiling";
                return file;
            }
        }
        item.payload.reserve(total);
        for (const auto& ext : loc.extents) {
            item.payload.insert(item.payload.end(), data + ext.first, data + ext.first + ext.second);
        }
        file.items.push_back(std::move(item));
    }

    // --- the primary item ---------------------------------------------------
    file.primary_id = primary_id;
    for (const auto& item : file.items) {
        if (item.id == primary_id) file.primary = item;
    }
    if (file.primary.id != primary_id) {
        file.status = Status::Malformed;
        file.error = "the primary item is not in iloc, so its bytes are nowhere";
        return file;
    }
    if (file.primary.type != "av01") {
        if (file.primary.type == "grid") {
            file.status = Status::Unsupported;
            file.error = "the primary item is a grid: a tiled image whose tiles are their own items";
        } else {
            file.status = Status::Unsupported;
            file.error = "the primary item is '" + file.primary.type + "', not an AV1 image";
        }
        return file;
    }
    file.status = Status::Ok;
    return file;
}

} // namespace litebird
