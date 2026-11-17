#include "litebird/render/WebPDecoder.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace litebird {
namespace {

// ---------------------------------------------------------------------------
// Constants from the WebP lossless bitstream specification.
// ---------------------------------------------------------------------------

constexpr int kNumLiteralCodes = 256;
constexpr int kNumLengthCodes = 24;
constexpr int kNumDistanceCodes = 40;
constexpr int kNumCodeLengthCodes = 19;
constexpr int kMaxCacheBits = 11;
constexpr int kCodeToPlaneCodes = 120;
constexpr int kNumTransforms = 4;
constexpr int kMaxPrefixGroups = 1000;
constexpr int kMaxHuffmanBits = 15;
constexpr int kDefaultCodeLength = 8;
constexpr uint32_t kArgbBlack = 0xff000000u;

enum TransformType {
    kPredictorTransform = 0,
    kColorTransform = 1,
    kSubtractGreenTransform = 2,
    kColorIndexingTransform = 3,
};

const int kCodeLengthCodeOrder[kNumCodeLengthCodes] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

// Distance code -> packed (dy << 4) | (8 - dx) for the 120 near-neighbour codes.
const uint8_t kCodeToPlane[kCodeToPlaneCodes] = {
    0x18, 0x07, 0x17, 0x19, 0x28, 0x06, 0x27, 0x29, 0x16, 0x1a,
    0x26, 0x2a, 0x38, 0x05, 0x37, 0x39, 0x15, 0x1b, 0x36, 0x3a,
    0x25, 0x2b, 0x48, 0x04, 0x47, 0x49, 0x14, 0x1c, 0x35, 0x3b,
    0x46, 0x4a, 0x24, 0x2c, 0x58, 0x45, 0x4b, 0x34, 0x3c, 0x03,
    0x57, 0x59, 0x13, 0x1d, 0x56, 0x5a, 0x23, 0x2d, 0x44, 0x4c,
    0x55, 0x5b, 0x33, 0x3d, 0x68, 0x02, 0x67, 0x69, 0x12, 0x1e,
    0x66, 0x6a, 0x22, 0x2e, 0x54, 0x5c, 0x43, 0x4d, 0x65, 0x6b,
    0x32, 0x3e, 0x78, 0x01, 0x77, 0x79, 0x53, 0x5d, 0x11, 0x1f,
    0x64, 0x6c, 0x42, 0x4e, 0x76, 0x7a, 0x21, 0x2f, 0x75, 0x7b,
    0x31, 0x3f, 0x63, 0x6d, 0x52, 0x5e, 0x00, 0x74, 0x7c, 0x41,
    0x4f, 0x10, 0x20, 0x62, 0x6e, 0x30, 0x73, 0x7d, 0x51, 0x5f,
    0x40, 0x72, 0x7e, 0x61, 0x6f, 0x50, 0x71, 0x7f, 0x60, 0x70
};

/// ceil(size / 2^bits), the dimension of a sub-sampled image.
int subsample_size(int size, int bits) {
    return (size + (1 << bits) - 1) >> bits;
}

// ---------------------------------------------------------------------------
// Bit reader: bytes in stream order, bits least-significant-first.
// ---------------------------------------------------------------------------

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    /// Reads `n` bits (0 <= n <= 24) as an unsigned integer, LSB-first. On
    /// end-of-stream the returned value is 0 and `eos()` becomes true; callers
    /// stop at the next check.
    uint32_t read(int n) {
        if (n <= 0) return 0;
        const size_t total_bits = size_ * 8;
        if (bit_pos_ + static_cast<size_t>(n) > total_bits) {
            eos_ = true;
            bit_pos_ = total_bits;
            return 0;
        }
        const size_t byte = bit_pos_ >> 3;
        const int shift = static_cast<int>(bit_pos_ & 7);
        uint64_t window = 0;
        for (int i = 0; i < 5; ++i) {
            if (byte + static_cast<size_t>(i) < size_) {
                window |= static_cast<uint64_t>(data_[byte + static_cast<size_t>(i)]) << (8 * i);
            }
        }
        const uint32_t mask = (n >= 32) ? 0xffffffffu : ((1u << n) - 1u);
        const uint32_t value = static_cast<uint32_t>((window >> shift) & mask);
        bit_pos_ += static_cast<size_t>(n);
        return value;
    }

    bool eos() const { return eos_; }
    size_t bit_position() const { return bit_pos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t bit_pos_{0};
    bool eos_{false};
};

// ---------------------------------------------------------------------------
// Canonical prefix code, decoded one bit at a time (the puff approach). The
// streams are small next to the decoder's other work, and bit-at-a-time
// decoding keeps the validity checks next to the codes that fail them.
// ---------------------------------------------------------------------------

class HuffmanTree {
public:
    /// Builds the canonical code from `lengths`. Returns false when the lengths
    /// are invalid: a length beyond 15 bits, an over-subscribed code, or an
    /// empty code (the format always codes a single symbol instead).
    bool build(const std::vector<uint8_t>& lengths) {
        counts_.assign(kMaxHuffmanBits + 1, 0);
        int used = 0;
        for (uint8_t length : lengths) {
            if (length > kMaxHuffmanBits) return false;
            if (length > 0) {
                counts_[length] += 1;
                ++used;
            }
        }
        if (used == 0) return false;
        int left = 1;
        for (int length = 1; length <= kMaxHuffmanBits; ++length) {
            left <<= 1;
            left -= counts_[length];
            if (left < 0) return false; // over-subscribed
        }
        if (used == 1) {
            // A code with a single symbol costs no bits at all: the symbol is
            // implied and the bitstream carries nothing for it. libwebp builds
            // such a table with `bits == 0`, and encoders rely on it (a literal
            // whose red, blue and alpha trees are all single-symbol costs one
            // bit, for the green channel, and nothing else).
            for (size_t symbol = 0; symbol < lengths.size(); ++symbol) {
                if (lengths[symbol] > 0) {
                    single_symbol_ = static_cast<int>(symbol);
                    break;
                }
            }
            empty_ = false;
            return true;
        }
        max_length_ = kMaxHuffmanBits;
        while (max_length_ > 0 && counts_[max_length_] == 0) --max_length_;

        std::vector<int> offsets(kMaxHuffmanBits + 2, 0);
        for (int length = 1; length <= kMaxHuffmanBits; ++length) {
            offsets[length + 1] = offsets[length] + counts_[length];
        }
        symbols_.assign(static_cast<size_t>(used), 0);
        std::vector<int> next(offsets);
        for (size_t symbol = 0; symbol < lengths.size(); ++symbol) {
            const int length = lengths[symbol];
            if (length > 0) symbols_[static_cast<size_t>(next[length]++)] = static_cast<uint16_t>(symbol);
        }
        empty_ = false;
        return true;
    }

    bool empty() const { return empty_; }

    /// Reads one symbol. Returns false when the bits do not name a code (an
    /// incomplete code, or the end of the stream).
    bool read_symbol(BitReader& br, int& value) const {
        if (empty_) return false;
        if (single_symbol_ >= 0) {
            value = single_symbol_;
            return true;
        }
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= max_length_; ++length) {
            if (br.eos()) return false;
            code |= static_cast<int>(br.read(1));
            const int count = counts_[length];
            if (code - first < count) {
                const size_t slot = static_cast<size_t>(index) + static_cast<size_t>(code - first);
                if (slot >= symbols_.size()) return false;
                value = symbols_[slot];
                return true;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return false;
    }

private:
    std::vector<int> counts_;
    std::vector<uint16_t> symbols_;
    int max_length_{0};
    int single_symbol_{-1};
    bool empty_{true};
};

/// The five prefix codes used together for a block of the image: green (with
/// length prefixes and colour-cache indices), red, blue, alpha and distance.
struct PrefixGroup {
    HuffmanTree trees[5];
};

struct Transform {
    int type{0};
    int bits{0};
    int xsize{0}; // image width before the transform was applied
    int ysize{0};
    std::vector<uint32_t> data; // transform data image (mode / colour codes / palette)
};

// ---------------------------------------------------------------------------
// Pixel helpers. All arithmetic is per 8-bit channel, modulo 256.
// ---------------------------------------------------------------------------

uint32_t add_pixels(uint32_t a, uint32_t b) {
    return (((a & 0xff00ff00u) + (b & 0xff00ff00u)) & 0xff00ff00u) |
           (((a & 0x00ff00ffu) + (b & 0x00ff00ffu)) & 0x00ff00ffu);
}

uint32_t average2(uint32_t a, uint32_t b) {
    return (((a ^ b) & 0xfefefefeu) >> 1) + (a & b);
}

int clip255(int a) { return (a < 0) ? 0 : ((a > 255) ? 255 : a); }

int channel(uint32_t argb, int shift) { return static_cast<int>((argb >> shift) & 0xff); }

int add_subtract_component_full(int a, int b, int c) { return clip255(a + b - c); }

int add_subtract_component_half(int a, int b) { return clip255(a + (a - b) / 2); }

uint32_t clamped_add_subtract_full(uint32_t c0, uint32_t c1, uint32_t c2) {
    const uint32_t a = static_cast<uint32_t>(add_subtract_component_full(channel(c0, 24), channel(c1, 24), channel(c2, 24)));
    const uint32_t r = static_cast<uint32_t>(add_subtract_component_full(channel(c0, 16), channel(c1, 16), channel(c2, 16)));
    const uint32_t g = static_cast<uint32_t>(add_subtract_component_full(channel(c0, 8), channel(c1, 8), channel(c2, 8)));
    const uint32_t b = static_cast<uint32_t>(add_subtract_component_full(channel(c0, 0), channel(c1, 0), channel(c2, 0)));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t clamped_add_subtract_half(uint32_t c0, uint32_t c1, uint32_t c2) {
    const uint32_t ave = average2(c0, c1);
    const uint32_t a = static_cast<uint32_t>(add_subtract_component_half(channel(ave, 24), channel(c2, 24)));
    const uint32_t r = static_cast<uint32_t>(add_subtract_component_half(channel(ave, 16), channel(c2, 16)));
    const uint32_t g = static_cast<uint32_t>(add_subtract_component_half(channel(ave, 8), channel(c2, 8)));
    const uint32_t b = static_cast<uint32_t>(add_subtract_component_half(channel(ave, 0), channel(c2, 0)));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/// Select(L, T, TL): the neighbour closer to L + T - TL, by Manhattan distance.
uint32_t select_predictor(uint32_t left, uint32_t top, uint32_t top_left) {
    auto sub3 = [&](int shift) {
        const int a = channel(top, shift);
        const int b = channel(left, shift);
        const int c = channel(top_left, shift);
        return std::abs(b - c) - std::abs(a - c);
    };
    const int top_minus_left = sub3(24) + sub3(16) + sub3(8) + sub3(0);
    return (top_minus_left <= 0) ? top : left;
}

uint32_t predict(int mode, uint32_t left, uint32_t top, uint32_t top_left, uint32_t top_right) {
    switch (mode) {
        case 0: return kArgbBlack;
        case 1: return left;
        case 2: return top;
        case 3: return top_right;
        case 4: return top_left;
        case 5: return average2(average2(left, top_right), top);
        case 6: return average2(left, top_left);
        case 7: return average2(left, top);
        case 8: return average2(top_left, top);
        case 9: return average2(top, top_right);
        case 10: return average2(average2(left, top_left), average2(top, top_right));
        case 11: return select_predictor(left, top, top_left);
        case 12: return clamped_add_subtract_full(left, top, top_left);
        case 13: return clamped_add_subtract_half(left, top, top_left);
        default: return kArgbBlack; // modes 14 and 15 do not exist
    }
}

/// (t * c) >> 5 with t and c taken as signed 8-bit values, as the colour
/// transform's 3.5-fixed-point multiplier requires.
int color_transform_delta(uint8_t t_byte, uint8_t c_byte) {
    const int8_t t = static_cast<int8_t>(t_byte);
    const int8_t c = static_cast<int8_t>(c_byte);
    return (static_cast<int>(t) * static_cast<int>(c)) >> 5;
}

// ---------------------------------------------------------------------------
// Container.
// ---------------------------------------------------------------------------

struct Chunk {
    char fourcc[5]{0, 0, 0, 0, 0};
    const uint8_t* data{nullptr};
    size_t size{0};
};

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t read_u24_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}

/// Walks the RIFF chunk list. Returns false with `error` set when the container
/// is not readable at all, or when a chunk runs past the buffer.
bool read_chunks(
    const uint8_t* data,
    size_t size,
    std::vector<Chunk>& chunks,
    std::string& error
) {
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WEBP", 4) != 0) {
        error = "not a WebP file: missing RIFF/WEBP header";
        return false;
    }
    // The RIFF size counts everything after the size field. Encoders do get it
    // wrong, so a size that does not fit the buffer is clamped rather than
    // rejected; a chunk that then runs off the end still fails below.
    const uint32_t declared = read_u32_le(data + 4);
    const uint64_t declared_end = static_cast<uint64_t>(declared) + 8u;
    const size_t end = (declared_end >= 12u && declared_end < size) ? static_cast<size_t>(declared_end) : size;

    size_t pos = 12;
    while (pos + 8 <= end) {
        Chunk chunk;
        std::memcpy(chunk.fourcc, data + pos, 4);
        chunk.size = read_u32_le(data + pos + 4);
        const size_t payload = pos + 8;
        if (payload + chunk.size > end) {
            error = "truncated WebP: chunk " + std::string(chunk.fourcc) + " runs past the file";
            return false;
        }
        chunk.data = data + payload;
        chunks.push_back(chunk);
        pos = payload + chunk.size + (chunk.size & 1u);
    }
    if (chunks.empty()) {
        error = "WebP container holds no chunks";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// VP8L image stream.
// ---------------------------------------------------------------------------

class Vp8lDecoder {
public:
    Vp8lDecoder(const uint8_t* data, size_t size) : br_(data, size) {}

    bool decode_image(std::vector<uint32_t>& pixels, int& width, int& height, std::string& error);

private:
    bool fail(const char* message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    bool read_image_header(int& width, int& height);
    bool decode_stream(int xsize, int ysize, bool is_level0, std::vector<uint32_t>& pixels);
    bool read_transform(
        int xsize,
        int ysize,
        int& transform_xsize,
        std::vector<Transform>& transforms,
        unsigned& transforms_seen
    );
    bool decode_pixels(
        uint32_t* data,
        int width,
        int height,
        const std::vector<PrefixGroup>& groups,
        const std::vector<uint32_t>& huffman_image,
        int huffman_bits,
        int huffman_xsize,
        int color_cache_bits
    );
    bool read_prefix_code(int alphabet_size, HuffmanTree& tree);
    bool read_prefix_code_lengths(int alphabet_size, std::vector<uint8_t>& lengths);
    int read_copy_value(int prefix_code, bool& ok);

    bool inverse_transform(const Transform& transform, const std::vector<uint32_t>& src, std::vector<uint32_t>& dst);
    bool inverse_predictor(const Transform& transform, std::vector<uint32_t>& pixels);
    void inverse_color(const Transform& transform, const std::vector<uint32_t>& src, std::vector<uint32_t>& dst);
    void inverse_subtract_green(const std::vector<uint32_t>& src, std::vector<uint32_t>& dst);
    void inverse_color_indexing(const Transform& transform, const std::vector<uint32_t>& src, std::vector<uint32_t>& dst);

    BitReader br_;
    std::string error_;
};

bool Vp8lDecoder::read_image_header(int& width, int& height) {
    if (br_.read(8) != 0x2f) return fail("bad VP8L signature");
    const int w = static_cast<int>(br_.read(14)) + 1;
    const int h = static_cast<int>(br_.read(14)) + 1;
    (void)br_.read(1); // alpha_is_used hint, informational for lossless
    if (br_.read(3) != 0) return fail("unsupported VP8L version");
    if (br_.eos()) return fail("truncated VP8L header");
    if (w > WebPDecoder::kMaxDimension || h > WebPDecoder::kMaxDimension) {
        return fail("VP8L image dimension out of range");
    }
    if (static_cast<int64_t>(w) * static_cast<int64_t>(h) > WebPDecoder::kMaxDecodedPixels) {
        return fail("VP8L image exceeds the decoded pixel budget");
    }
    width = w;
    height = h;
    return true;
}

bool Vp8lDecoder::decode_image(std::vector<uint32_t>& pixels, int& width, int& height, std::string& error) {
    int w = 0;
    int h = 0;
    if (!read_image_header(w, h)) {
        error = error_;
        return false;
    }
    std::vector<uint32_t> decoded;
    if (!decode_stream(w, h, true, decoded)) {
        error = error_;
        return false;
    }
    pixels = std::move(decoded);
    width = w;
    height = h;
    return true;
}

int Vp8lDecoder::read_copy_value(int prefix_code, bool& ok) {
    if (prefix_code < 0) {
        ok = false;
        return 0;
    }
    if (prefix_code < 4) return prefix_code + 1;
    const int extra_bits = (prefix_code - 2) >> 1;
    if (extra_bits > 18) {
        ok = false;
        return 0;
    }
    const int offset = (2 + (prefix_code & 1)) << extra_bits;
    ok = !br_.eos();
    return offset + static_cast<int>(br_.read(extra_bits)) + 1;
}

bool Vp8lDecoder::decode_stream(int xsize, int ysize, bool is_level0, std::vector<uint32_t>& pixels) {
    if (xsize <= 0 || ysize <= 0) return fail("empty image stream");
    if (static_cast<int64_t>(xsize) * static_cast<int64_t>(ysize) > WebPDecoder::kMaxDecodedPixels) {
        return fail("image stream exceeds the decoded pixel budget");
    }

    std::vector<Transform> transforms;
    int transform_xsize = xsize;
    if (is_level0) {
        unsigned transforms_seen = 0;
        for (;;) {
            const int present = static_cast<int>(br_.read(1));
            if (br_.eos()) return fail("truncated transform list");
            if (present == 0) break;
            if (!read_transform(xsize, ysize, transform_xsize, transforms, transforms_seen)) return false;
        }
    }

    int color_cache_bits = 0;
    if (br_.read(1)) {
        color_cache_bits = static_cast<int>(br_.read(4));
        if (color_cache_bits < 1 || color_cache_bits > kMaxCacheBits) {
            return fail("colour cache size out of range");
        }
    }
    if (br_.eos()) return fail("truncated image stream header");

    std::vector<PrefixGroup> groups;
    std::vector<uint32_t> huffman_image;
    int huffman_bits = 0;
    int huffman_xsize = 0;
    int num_groups = 1;

    if (is_level0 && br_.read(1)) {
        huffman_bits = static_cast<int>(br_.read(3)) + 2;
        const int hx = subsample_size(transform_xsize, huffman_bits);
        const int hy = subsample_size(ysize, huffman_bits);
        std::vector<uint32_t> entropy_image;
        if (!decode_stream(hx, hy, false, entropy_image)) return false;
        if (entropy_image.size() != static_cast<size_t>(hx) * static_cast<size_t>(hy)) {
            return fail("entropy image has the wrong size");
        }
        int max_group = 0;
        huffman_image.resize(entropy_image.size());
        for (size_t i = 0; i < entropy_image.size(); ++i) {
            const int group = static_cast<int>((entropy_image[i] >> 8) & 0xffff);
            huffman_image[i] = static_cast<uint32_t>(group);
            if (group > max_group) max_group = group;
        }
        if (max_group + 1 > kMaxPrefixGroups) return fail("too many prefix code groups");
        num_groups = max_group + 1;
        huffman_xsize = hx;
    }
    if (br_.eos()) return fail("truncated prefix code header");

    groups.resize(static_cast<size_t>(num_groups));
    const int green_alphabet = kNumLiteralCodes + kNumLengthCodes +
                               ((color_cache_bits > 0) ? (1 << color_cache_bits) : 0);
    for (int group = 0; group < num_groups; ++group) {
        for (int tree = 0; tree < 5; ++tree) {
            const int alphabet = (tree == 0) ? green_alphabet
                                              : ((tree == 4) ? kNumDistanceCodes : kNumLiteralCodes);
            if (!read_prefix_code(alphabet, groups[static_cast<size_t>(group)].trees[tree])) return false;
        }
    }
    if (br_.eos()) return fail("truncated prefix codes");

    const size_t total = static_cast<size_t>(transform_xsize) * static_cast<size_t>(ysize);
    pixels.assign(total, 0);
    if (!decode_pixels(
            pixels.data(),
            transform_xsize,
            ysize,
            groups,
            huffman_image,
            huffman_bits,
            huffman_xsize,
            color_cache_bits)) {
        return false;
    }

    // Undo the transforms in reverse order of the stream.
    std::vector<uint32_t> current = std::move(pixels);
    int current_width = transform_xsize;
    int current_height = ysize;
    for (size_t i = transforms.size(); i-- > 0;) {
        std::vector<uint32_t> restored;
        if (!inverse_transform(transforms[i], current, restored)) return false;
        current = std::move(restored);
        current_width = transforms[i].xsize;
        current_height = transforms[i].ysize;
    }
    if (current.size() != static_cast<size_t>(current_width) * static_cast<size_t>(current_height)) {
        return fail("transformed image has the wrong size");
    }
    pixels = std::move(current);
    return true;
}

bool Vp8lDecoder::read_transform(
    int /*xsize*/,
    int ysize,
    int& transform_xsize,
    std::vector<Transform>& transforms,
    unsigned& transforms_seen
) {
    const int type = static_cast<int>(br_.read(2));
    if (type < 0 || type >= kNumTransforms) return fail("unknown transform type");
    if ((transforms_seen & (1u << type)) != 0) return fail("duplicate transform");
    transforms_seen |= (1u << type);

    Transform transform;
    transform.type = type;
    transform.xsize = transform_xsize;
    transform.ysize = ysize;
    switch (type) {
        case kPredictorTransform:
        case kColorTransform: {
            transform.bits = static_cast<int>(br_.read(3)) + 2;
            if (br_.eos()) return fail("truncated transform");
            const int bx = subsample_size(transform_xsize, transform.bits);
            const int by = subsample_size(ysize, transform.bits);
            const int expected = bx * by;
            std::vector<uint32_t> data;
            if (!decode_stream(bx, by, false, data)) return false;
            if (static_cast<int>(data.size()) != expected) return fail("transform data has the wrong size");
            transform.data = std::move(data);
            break;
        }
        case kColorIndexingTransform: {
            const int num_colors = static_cast<int>(br_.read(8)) + 1;
            if (br_.eos()) return fail("truncated colour table");
            const int bits = (num_colors > 16) ? 0 : (num_colors > 4) ? 1 : (num_colors > 2) ? 2 : 3;
            transform.bits = bits;
            transform_xsize = subsample_size(transform.xsize, bits);
            std::vector<uint32_t> palette;
            if (!decode_stream(num_colors, 1, false, palette)) return false;
            if (palette.size() != static_cast<size_t>(num_colors)) {
                return fail("colour table has the wrong size");
            }
            // The palette is stored delta-coded, per channel, and padded with
            // transparent black to the next power of two.
            const int final_colors = 1 << (8 >> bits);
            std::vector<uint32_t> expanded(static_cast<size_t>(final_colors), 0);
            expanded[0] = palette[0];
            uint8_t* out = reinterpret_cast<uint8_t*>(expanded.data());
            const uint8_t* in = reinterpret_cast<const uint8_t*>(palette.data());
            for (int i = 4; i < 4 * final_colors; ++i) {
                out[i] = (i < 4 * num_colors) ? static_cast<uint8_t>(in[i] + out[i - 4]) : 0;
            }
            transform.data = std::move(expanded);
            break;
        }
        case kSubtractGreenTransform:
        default:
            break;
    }
    transforms.push_back(std::move(transform));
    return true;
}

bool Vp8lDecoder::read_prefix_code_lengths(int alphabet_size, std::vector<uint8_t>& lengths) {
    lengths.assign(static_cast<size_t>(alphabet_size), 0);

    const int simple_code = static_cast<int>(br_.read(1));
    if (simple_code) {
        const int num_symbols = static_cast<int>(br_.read(1)) + 1;
        const int first_is_8bits = static_cast<int>(br_.read(1));
        int symbol = static_cast<int>(br_.read(first_is_8bits ? 8 : 1));
        if (br_.eos() || symbol >= alphabet_size) return fail("simple prefix code symbol out of range");
        lengths[static_cast<size_t>(symbol)] = 1;
        if (num_symbols == 2) {
            symbol = static_cast<int>(br_.read(8));
            if (br_.eos() || symbol >= alphabet_size) return fail("simple prefix code symbol out of range");
            lengths[static_cast<size_t>(symbol)] = 1;
        }
        return true;
    }

    int code_length_code_lengths[kNumCodeLengthCodes] = {0};
    const int num_codes = static_cast<int>(br_.read(4)) + 4;
    if (num_codes > kNumCodeLengthCodes) return fail("too many code-length codes");
    for (int i = 0; i < num_codes; ++i) {
        code_length_code_lengths[kCodeLengthCodeOrder[i]] = static_cast<int>(br_.read(3));
    }
    if (br_.eos()) return fail("truncated code-length code");

    std::vector<uint8_t> code_length_lengths(kNumCodeLengthCodes, 0);
    for (int i = 0; i < kNumCodeLengthCodes; ++i) {
        code_length_lengths[static_cast<size_t>(i)] = static_cast<uint8_t>(code_length_code_lengths[i]);
    }
    HuffmanTree length_tree;
    if (!length_tree.build(code_length_lengths)) return fail("invalid code-length prefix code");

    int max_symbol = alphabet_size;
    if (br_.read(1)) {
        const int length_nbits = 2 + 2 * static_cast<int>(br_.read(3));
        max_symbol = 2 + static_cast<int>(br_.read(length_nbits));
        if (max_symbol > alphabet_size) return fail("code-length count out of range");
    }

    int symbol = 0;
    int previous = kDefaultCodeLength;
    while (symbol < alphabet_size) {
        if (max_symbol-- == 0) break;
        int code_length = 0;
        if (!length_tree.read_symbol(br_, code_length)) return fail("corrupt code-length prefix code");
        if (code_length < 16) {
            lengths[static_cast<size_t>(symbol++)] = static_cast<uint8_t>(code_length);
            if (code_length != 0) previous = code_length;
        } else {
            const int extra_bits = (code_length == 16) ? 2 : ((code_length == 17) ? 3 : 7);
            const int repeat_offset = (code_length == 16) ? 3 : ((code_length == 17) ? 3 : 11);
            const int repeat = static_cast<int>(br_.read(extra_bits)) + repeat_offset;
            if (symbol + repeat > alphabet_size) return fail("code-length repeat out of range");
            const uint8_t value = (code_length == 16) ? static_cast<uint8_t>(previous) : 0;
            for (int i = 0; i < repeat; ++i) lengths[static_cast<size_t>(symbol++)] = value;
        }
        if (br_.eos()) return fail("truncated code lengths");
    }
    return true;
}

bool Vp8lDecoder::read_prefix_code(int alphabet_size, HuffmanTree& tree) {
    std::vector<uint8_t> lengths;
    if (!read_prefix_code_lengths(alphabet_size, lengths)) return false;
    if (!tree.build(lengths)) return fail("invalid prefix code");
    return true;
}

bool Vp8lDecoder::decode_pixels(
    uint32_t* data,
    int width,
    int height,
    const std::vector<PrefixGroup>& groups,
    const std::vector<uint32_t>& huffman_image,
    int huffman_bits,
    int huffman_xsize,
    int color_cache_bits
) {
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    const int len_code_limit = kNumLiteralCodes + kNumLengthCodes;

    std::vector<uint32_t> color_cache;
    if (color_cache_bits > 0) color_cache.assign(static_cast<size_t>(1) << color_cache_bits, 0);
    const int cache_limit = len_code_limit + static_cast<int>(color_cache.size());
    const uint32_t cache_shift = 32u - static_cast<uint32_t>(color_cache_bits);

    auto cache_insert = [&](uint32_t color) {
        if (color_cache.empty()) return;
        const uint32_t key = (0x1e35a7bdu * color) >> cache_shift;
        color_cache[key] = color;
    };

    const PrefixGroup* group = &groups[0];
    size_t src = 0;
    int col = 0;
    int row = 0;
    while (src < total) {
        if (!huffman_image.empty()) {
            const size_t pos = static_cast<size_t>(row >> huffman_bits) * static_cast<size_t>(huffman_xsize) +
                               static_cast<size_t>(col >> huffman_bits);
            if (pos >= huffman_image.size()) return fail("prefix code group index out of range");
            const size_t group_index = huffman_image[pos];
            if (group_index >= groups.size()) return fail("prefix code group out of range");
            group = &groups[group_index];
        }
        int code = 0;
        if (!group->trees[0].read_symbol(br_, code)) return fail("corrupt prefix code in pixel data");

        if (code < kNumLiteralCodes) {
            int red = 0;
            int blue = 0;
            int alpha = 0;
            if (!group->trees[1].read_symbol(br_, red)) return fail("corrupt red prefix code");
            if (!group->trees[2].read_symbol(br_, blue)) return fail("corrupt blue prefix code");
            if (!group->trees[3].read_symbol(br_, alpha)) return fail("corrupt alpha prefix code");
            const uint32_t pixel = (static_cast<uint32_t>(alpha) << 24) | (static_cast<uint32_t>(red) << 16) |
                                   (static_cast<uint32_t>(code) << 8) | static_cast<uint32_t>(blue);
            data[src++] = pixel;
            cache_insert(pixel);
            if (++col >= width) {
                col = 0;
                ++row;
            }
        } else if (code < len_code_limit) {
            bool ok = true;
            const int length = read_copy_value(code - kNumLiteralCodes, ok);
            if (!ok) return fail("truncated backward reference");
            int distance_symbol = 0;
            if (!group->trees[4].read_symbol(br_, distance_symbol)) return fail("corrupt distance prefix code");
            const int distance_code = read_copy_value(distance_symbol, ok);
            if (!ok) return fail("truncated backward reference");
            int distance = 0;
            if (distance_code > kCodeToPlaneCodes) {
                distance = distance_code - kCodeToPlaneCodes;
            } else {
                const int plane = kCodeToPlane[distance_code - 1];
                const int y_offset = plane >> 4;
                const int x_offset = 8 - (plane & 0xf);
                distance = y_offset * width + x_offset;
                if (distance < 1) distance = 1;
            }
            if (src < static_cast<size_t>(distance) || total - src < static_cast<size_t>(length)) {
                return fail("backward reference out of range");
            }
            for (int i = 0; i < length; ++i) {
                const uint32_t pixel = data[src - static_cast<size_t>(distance)];
                data[src++] = pixel;
                cache_insert(pixel);
            }
            col += length;
            while (col >= width) {
                col -= width;
                ++row;
            }
        } else if (code < cache_limit) {
            const size_t key = static_cast<size_t>(code - len_code_limit);
            const uint32_t pixel = color_cache[key];
            data[src++] = pixel;
            cache_insert(pixel);
            if (++col >= width) {
                col = 0;
                ++row;
            }
        } else {
            return fail("invalid prefix code in pixel data");
        }
        if (br_.eos()) return fail("truncated pixel data");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Inverse transforms.
// ---------------------------------------------------------------------------

bool Vp8lDecoder::inverse_predictor(const Transform& transform, std::vector<uint32_t>& pixels) {
    const int width = transform.xsize;
    const int height = transform.ysize;
    const int tile_bits = transform.bits;
    const int tiles_per_row = subsample_size(width, tile_bits);

    for (int y = 0; y < height; ++y) {
        uint32_t* const row_pixels = pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        const uint32_t* const mode_row = transform.data.data() +
                                         static_cast<size_t>(y >> tile_bits) * static_cast<size_t>(tiles_per_row);
        for (int x = 0; x < width; ++x) {
            uint32_t predicted = 0;
            if (y == 0) {
                predicted = (x == 0) ? kArgbBlack : row_pixels[x - 1];
            } else if (x == 0) {
                predicted = row_pixels[x - width];
            } else {
                const int mode = static_cast<int>((mode_row[x >> tile_bits] >> 8) & 0xff);
                if (mode > 13) return fail("unknown predictor mode");
                const uint32_t left = row_pixels[x - 1];
                const uint32_t top = row_pixels[x - width];
                const uint32_t top_left = row_pixels[x - width - 1];
                // On the rightmost column the top-right neighbour is the first
                // pixel of the row being reconstructed (spec, [AMENDED2]).
                const uint32_t top_right = (x + 1 < width) ? row_pixels[x - width + 1] : row_pixels[0];
                predicted = predict(mode, left, top, top_left, top_right);
            }
            row_pixels[x] = add_pixels(row_pixels[x], predicted);
        }
    }
    return true;
}

void Vp8lDecoder::inverse_color(const Transform& transform, const std::vector<uint32_t>& src, std::vector<uint32_t>& dst) {
    const int width = transform.xsize;
    const int height = transform.ysize;
    const int tile_bits = transform.bits;
    const int tiles_per_row = subsample_size(width, tile_bits);
    for (int y = 0; y < height; ++y) {
        const uint32_t* const code_row = transform.data.data() +
                                         static_cast<size_t>(y >> tile_bits) * static_cast<size_t>(tiles_per_row);
        const uint32_t* const src_row = src.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        uint32_t* const dst_row = dst.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            const uint32_t code = code_row[x >> tile_bits];
            const uint8_t green_to_red = static_cast<uint8_t>(code & 0xff);
            const uint8_t green_to_blue = static_cast<uint8_t>((code >> 8) & 0xff);
            const uint8_t red_to_blue = static_cast<uint8_t>((code >> 16) & 0xff);
            const uint32_t argb = src_row[x];
            const uint8_t green = static_cast<uint8_t>((argb >> 8) & 0xff);
            int new_red = channel(argb, 16) + color_transform_delta(green_to_red, green);
            new_red &= 0xff;
            int new_blue = channel(argb, 0) + color_transform_delta(green_to_blue, green);
            new_blue += color_transform_delta(red_to_blue, static_cast<uint8_t>(new_red & 0xff));
            new_blue &= 0xff;
            dst_row[x] = (argb & 0xff00ff00u) | (static_cast<uint32_t>(new_red) << 16) |
                         static_cast<uint32_t>(new_blue);
        }
    }
}

void Vp8lDecoder::inverse_subtract_green(const std::vector<uint32_t>& src, std::vector<uint32_t>& dst) {
    for (size_t i = 0; i < src.size(); ++i) {
        const uint32_t argb = src[i];
        const uint32_t green = (argb >> 8) & 0xffu;
        uint32_t red_blue = argb & 0x00ff00ffu;
        red_blue += (green << 16) | green;
        red_blue &= 0x00ff00ffu;
        dst[i] = (argb & 0xff00ff00u) | red_blue;
    }
}

void Vp8lDecoder::inverse_color_indexing(const Transform& transform, const std::vector<uint32_t>& src, std::vector<uint32_t>& dst) {
    const int width = transform.xsize;
    const int height = transform.ysize;
    const int bits = transform.bits;
    const int bits_per_pixel = 8 >> bits;
    const int in_width = subsample_size(width, bits);
    const uint32_t* const color_map = transform.data.data();
    const int map_size = static_cast<int>(transform.data.size());

    if (bits_per_pixel >= 8) {
        for (int y = 0; y < height; ++y) {
            const uint32_t* const src_row = src.data() + static_cast<size_t>(y) * static_cast<size_t>(in_width);
            uint32_t* const dst_row = dst.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
            for (int x = 0; x < width; ++x) {
                const int index = static_cast<int>((src_row[x] >> 8) & 0xff);
                dst_row[x] = (index >= 0 && index < map_size) ? color_map[index] : 0u;
            }
        }
        return;
    }

    const int pixels_per_byte = 1 << bits;
    const int count_mask = pixels_per_byte - 1;
    const uint32_t bit_mask = (1u << bits_per_pixel) - 1u;
    for (int y = 0; y < height; ++y) {
        const uint32_t* const src_row = src.data() + static_cast<size_t>(y) * static_cast<size_t>(in_width);
        uint32_t* const dst_row = dst.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        uint32_t packed = 0;
        for (int x = 0; x < width; ++x) {
            if ((x & count_mask) == 0) packed = (src_row[x >> bits] >> 8) & 0xffu;
            const int index = static_cast<int>(packed & bit_mask);
            dst_row[x] = (index < map_size) ? color_map[index] : 0u;
            packed >>= bits_per_pixel;
        }
    }
}

bool Vp8lDecoder::inverse_transform(
    const Transform& transform,
    const std::vector<uint32_t>& src,
    std::vector<uint32_t>& dst
) {
    const size_t out_size = static_cast<size_t>(transform.xsize) * static_cast<size_t>(transform.ysize);
    switch (transform.type) {
        case kPredictorTransform: {
            if (src.size() != out_size) return fail("predictor input has the wrong size");
            dst = src;
            if (!inverse_predictor(transform, dst)) return false;
            break;
        }
        case kColorTransform: {
            if (src.size() != out_size) return fail("colour transform input has the wrong size");
            dst.assign(out_size, 0);
            inverse_color(transform, src, dst);
            break;
        }
        case kSubtractGreenTransform: {
            if (src.size() != out_size) return fail("subtract-green input has the wrong size");
            dst.assign(out_size, 0);
            inverse_subtract_green(src, dst);
            break;
        }
        case kColorIndexingTransform:
        default: {
            const size_t in_size = static_cast<size_t>(subsample_size(transform.xsize, transform.bits)) *
                                   static_cast<size_t>(transform.ysize);
            if (src.size() != in_size) return fail("colour-indexing input has the wrong size");
            dst.assign(out_size, 0);
            inverse_color_indexing(transform, src, dst);
            break;
        }
    }
    if (dst.size() != out_size) return fail("inverse transform produced the wrong size");
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

bool WebPDecoder::is_webp(const uint8_t* data, size_t size) {
    return data != nullptr && size >= 12 && std::memcmp(data, "RIFF", 4) == 0 &&
           std::memcmp(data + 8, "WEBP", 4) == 0;
}

const char* WebPDecoder::status_string(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotWebP: return "not a WebP file";
        case Status::UnsupportedLossy: return "lossy VP8 is not supported";
        case Status::UnsupportedAnimation: return "animated WebP is not supported";
        case Status::TooLarge: return "image is too large";
        case Status::Truncated: return "truncated WebP file";
        case Status::Corrupt: return "corrupt WebP bitstream";
    }
    return "unknown";
}

WebPDecoder::Status WebPDecoder::probe(const uint8_t* data, size_t size) {
    if (!is_webp(data, size)) return Status::NotWebP;
    std::vector<Chunk> chunks;
    std::string error;
    const bool complete = read_chunks(data, size, chunks, error);
    bool has_lossless = false;
    bool has_lossy = false;
    bool has_animation = false;
    for (const Chunk& chunk : chunks) {
        if (std::memcmp(chunk.fourcc, "VP8L", 4) == 0) {
            has_lossless = true;
        } else if (std::memcmp(chunk.fourcc, "VP8 ", 4) == 0) {
            has_lossy = true;
        } else if (std::memcmp(chunk.fourcc, "ANIM", 4) == 0 || std::memcmp(chunk.fourcc, "ANMF", 4) == 0) {
            has_animation = true;
        } else if (std::memcmp(chunk.fourcc, "VP8X", 4) == 0 && chunk.size >= 1 && (chunk.data[0] & 0x02u) != 0) {
            has_animation = true;
        }
    }
    if (has_animation) return Status::UnsupportedAnimation;
    if (!complete) return Status::Truncated;
    if (has_lossless) return Status::Ok;
    if (has_lossy) return Status::UnsupportedLossy;
    return Status::Corrupt;
}

WebPDecoder::DecodeResult WebPDecoder::decode_detailed(const uint8_t* data, size_t size) {
    DecodeResult result;
    if (!is_webp(data, size)) {
        result.status = Status::NotWebP;
        result.error = "not a WebP file";
        return result;
    }

    std::vector<Chunk> chunks;
    std::string error;
    if (!read_chunks(data, size, chunks, error)) {
        result.status = (error.find("truncated") != std::string::npos) ? Status::Truncated : Status::Corrupt;
        result.error = error;
        return result;
    }

    bool animation = false;
    const Chunk* lossless = nullptr;
    bool lossy = false;
    int canvas_width = 0;
    int canvas_height = 0;
    for (const Chunk& chunk : chunks) {
        if (std::memcmp(chunk.fourcc, "VP8X", 4) == 0) {
            if (chunk.size < 10) {
                result.status = Status::Truncated;
                result.error = "truncated VP8X header";
                return result;
            }
            if ((chunk.data[0] & 0x02u) != 0) animation = true;
            canvas_width = static_cast<int>(read_u24_le(chunk.data + 4)) + 1;
            canvas_height = static_cast<int>(read_u24_le(chunk.data + 7)) + 1;
        } else if (std::memcmp(chunk.fourcc, "ANIM", 4) == 0 || std::memcmp(chunk.fourcc, "ANMF", 4) == 0) {
            animation = true;
        } else if (std::memcmp(chunk.fourcc, "VP8L", 4) == 0) {
            if (lossless == nullptr) lossless = &chunk;
        } else if (std::memcmp(chunk.fourcc, "VP8 ", 4) == 0) {
            lossy = true;
        }
    }

    if (animation) {
        result.status = Status::UnsupportedAnimation;
        result.error = "animated WebP: only still images are decoded";
        return result;
    }
    if (lossless == nullptr) {
        if (lossy) {
            result.status = Status::UnsupportedLossy;
            result.error = "lossy VP8 image: the lossy codec is not implemented";
        } else {
            result.status = Status::Corrupt;
            result.error = "no VP8L or VP8 image chunk";
        }
        return result;
    }

    Vp8lDecoder decoder(lossless->data, lossless->size);
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
    if (!decoder.decode_image(pixels, width, height, error)) {
        if (error.find("truncated") != std::string::npos) {
            result.status = Status::Truncated;
        } else if (error.find("budget") != std::string::npos || error.find("dimension") != std::string::npos) {
            result.status = Status::TooLarge;
        } else {
            result.status = Status::Corrupt;
        }
        result.error = error;
        return result;
    }
    if (width <= 0 || height <= 0) {
        result.status = Status::Corrupt;
        result.error = "decoded image has no pixels";
        return result;
    }
    if (canvas_width > 0 && canvas_height > 0 && (width > canvas_width || height > canvas_height)) {
        result.status = Status::Corrupt;
        result.error = "image is larger than its VP8X canvas";
        return result;
    }
    if (pixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height)) {
        result.status = Status::Corrupt;
        result.error = "decoded pixel buffer is short";
        return result;
    }

    auto image = std::make_shared<ImageData>();
    image->width = width;
    image->height = height;
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    image->pixels = std::move(pixels);
    result.image = std::move(image);
    result.status = Status::Ok;
    return result;
}

std::shared_ptr<ImageData> WebPDecoder::decode(const uint8_t* data, size_t size) {
    DecodeResult result = decode_detailed(data, size);
    return result.ok() ? result.image : nullptr;
}

std::shared_ptr<ImageData> WebPDecoder::decode(const std::vector<uint8_t>& data) {
    return decode(data.data(), data.size());
}

} // namespace litebird
