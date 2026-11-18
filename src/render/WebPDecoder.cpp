#include "litebird/render/WebPDecoder.hpp"


#include "litebird/render/Vp8Decoder.hpp"

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
    // An ALPH chunk's lossless payload is a VP8L image stream without the
    // signature and dimension header: the container supplies them, and the
    // alpha value travels in the green channel.
    bool decode_alpha_stream(int width, int height, std::vector<uint8_t>& plane, std::string& error);

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

bool Vp8lDecoder::decode_alpha_stream(int width, int height, std::vector<uint8_t>& plane, std::string& error) {
    std::vector<uint32_t> decoded;
    if (!decode_stream(width, height, true, decoded)) {
        error = error_;
        return false;
    }
    if (decoded.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        error = "the ALPH image does not match the picture size";
        return false;
    }
    plane.resize(decoded.size());
    for (size_t i = 0; i < decoded.size(); ++i) plane[i] = static_cast<uint8_t>((decoded[i] >> 8) & 0xff);
    return true;
}

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


// ---------------------------------------------------------------------------
// The alpha plane of a lossy image (the `ALPH` chunk).
//
// The plane is stored at full resolution, either raw or as a VP8L stream whose
// green channel is the alpha value, and prefixed by a one-byte header naming
// the compression, a spatial predictor to undo and an optional level-reduction
// step. Only the "no preprocessing" case is lossless to reverse, so the other
// is refused by name rather than silently returning flatter alpha.
// ---------------------------------------------------------------------------
bool decode_alpha_plane(
    const Chunk& chunk,
    int width,
    int height,
    std::vector<uint8_t>& plane,
    std::string& error
) {
    if (chunk.size < 1) {
        error = "the ALPH chunk is empty";
        return false;
    }
    const uint8_t header = chunk.data[0];
    const int compression = header & 0x03;
    const int filter = (header >> 2) & 0x03;
    const int preprocessing = (header >> 4) & 0x03;
    if (((header >> 6) & 0x03) != 0) {
        error = "the ALPH header has reserved bits set";
        return false;
    }
    if (preprocessing != 0) {
        error = "the ALPH chunk asks for level-reduction preprocessing";
        return false;
    }
    const uint8_t* payload = chunk.data + 1;
    const size_t payload_size = chunk.size - 1;
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> raw;

    if (compression == 0) {
        if (payload_size != pixel_count) {
            error = "the uncompressed ALPH plane is the wrong size";
            return false;
        }
        raw.assign(payload, payload + payload_size);
    } else if (compression == 1) {
        Vp8lDecoder decoder(payload, payload_size);
        if (!decoder.decode_alpha_stream(width, height, raw, error)) return false;
    } else {
        error = "the ALPH chunk uses an unknown compression method";
        return false;
    }

    plane.assign(pixel_count, 0);
    // The three spatial filters invert the encoder's predictors. All of them fall
    // back to the horizontal filter on the first row, which has no row above it.
    const auto unfilter_horizontal = [&](const uint8_t* in, uint8_t* out, const uint8_t* prev) {
        int pred = prev != nullptr ? prev[0] : 0;
        for (int x = 0; x < width; ++x) {
            out[x] = static_cast<uint8_t>(pred + in[x]);
            pred = out[x];
        }
    };
    for (int y = 0; y < height; ++y) {
        const uint8_t* in = raw.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        uint8_t* out = plane.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        const uint8_t* prev = y > 0 ? out - width : nullptr;
        switch (filter) {
            case 0:  // none: the plane is stored as it is
                std::memcpy(out, in, static_cast<size_t>(width));
                break;
            case 1:  // horizontal: each pixel predicts from the one to its left
                unfilter_horizontal(in, out, prev);
                break;
            case 2:  // vertical: each row predicts from the row above
                if (prev == nullptr) {
                    unfilter_horizontal(in, out, nullptr);
                    break;
                }
                for (int x = 0; x < width; ++x) out[x] = static_cast<uint8_t>(prev[x] + in[x]);
                break;
            default: {  // gradient: left + above - above-left, modulo 256
                if (prev == nullptr) {
                    unfilter_horizontal(in, out, nullptr);
                    break;
                }
                int top = prev[0];
                int top_left = top;
                int left = top;
                for (int x = 0; x < width; ++x) {
                    top = prev[x];
                    const int predicted = left + top - top_left;
                    left = static_cast<uint8_t>(in[x] + predicted);
                    top_left = top;
                    out[x] = static_cast<uint8_t>(left);
                }
                break;
            }
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

namespace {

/// The chunks of a byte range: an `ANMF` payload holds its image's chunks with
/// no RIFF header around them.
bool read_nested_chunks(const uint8_t* data, size_t size, std::vector<Chunk>& chunks, std::string& error) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        Chunk chunk;
        std::memcpy(chunk.fourcc, data + pos, 4);
        chunk.size = read_u32_le(data + pos + 4);
        const size_t payload = pos + 8;
        if (payload + chunk.size > size) {
            error = "truncated WebP frame: chunk " + std::string(chunk.fourcc) + " runs past its ANMF";
            return false;
        }
        chunk.data = data + payload;
        chunks.push_back(chunk);
        pos = payload + chunk.size + (chunk.size & 1u);
    }
    return true;
}

/// One still image, from the chunks that carry it: `VP8L`, or `VP8 ` with the
/// `ALPH` beside it. `canvas_width`/`canvas_height` bound it when they are not
/// zero.
struct StillImage {
    std::shared_ptr<ImageData> image;
    bool lossy{false};
    WebPDecoder::Status status{WebPDecoder::Status::Ok};
    std::string error;
};

StillImage decode_still(const std::vector<Chunk>& chunks, int canvas_width, int canvas_height) {
    StillImage still;
    const Chunk* lossless = nullptr;
    const Chunk* lossy = nullptr;
    const Chunk* alpha_chunk = nullptr;
    for (const Chunk& chunk : chunks) {
        if (std::memcmp(chunk.fourcc, "VP8L", 4) == 0) {
            if (lossless == nullptr) lossless = &chunk;
        } else if (std::memcmp(chunk.fourcc, "VP8 ", 4) == 0) {
            if (lossy == nullptr) lossy = &chunk;
        } else if (std::memcmp(chunk.fourcc, "ALPH", 4) == 0) {
            if (alpha_chunk == nullptr) alpha_chunk = &chunk;
        }
    }

    if (lossless == nullptr && lossy != nullptr) {
        // A lossy image: decode the VP8 key frame, then fold in the alpha plane
        // if the file carries one.
        Vp8Decoder::Frame frame;
        std::string vp8_error;
        const Vp8Decoder::Status vp8_status =
            Vp8Decoder::decode(lossy->data, lossy->size, frame, vp8_error);
        if (vp8_status != Vp8Decoder::Status::Ok) {
            switch (vp8_status) {
                case Vp8Decoder::Status::Truncated: still.status = WebPDecoder::Status::Truncated; break;
                case Vp8Decoder::Status::TooLarge: still.status = WebPDecoder::Status::TooLarge; break;
                case Vp8Decoder::Status::NotKeyFrame:
                case Vp8Decoder::Status::UnsupportedProfile:
                case Vp8Decoder::Status::Corrupt:
                default: still.status = WebPDecoder::Status::Corrupt; break;
            }
            still.error = "lossy VP8: " + vp8_error;
            return still;
        }
        if (canvas_width > 0 && canvas_height > 0 &&
            (frame.width > canvas_width || frame.height > canvas_height)) {
            still.status = WebPDecoder::Status::Corrupt;
            still.error = "image is larger than its VP8X canvas";
            return still;
        }
        std::vector<uint8_t> alpha;
        const uint8_t* alpha_pixels = nullptr;
        if (alpha_chunk != nullptr) {
            std::string alpha_error;
            if (!decode_alpha_plane(*alpha_chunk, frame.width, frame.height, alpha, alpha_error)) {
                still.status = alpha_error.find("level-reduction") != std::string::npos
                                   ? WebPDecoder::Status::UnsupportedAlpha
                                   : WebPDecoder::Status::Corrupt;
                still.error = alpha_error;
                return still;
            }
            alpha_pixels = alpha.data();
        }
        std::vector<uint32_t> pixels;
        Vp8Decoder::to_argb(frame, alpha_pixels, frame.width, pixels);
        if (pixels.size() != static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height)) {
            still.status = WebPDecoder::Status::Corrupt;
            still.error = "the lossy decode produced the wrong number of pixels";
            return still;
        }
        auto image = std::make_shared<ImageData>();
        image->width = frame.width;
        image->height = frame.height;
        image->pixels = std::move(pixels);
        still.image = std::move(image);
        still.lossy = true;
        return still;
    }
    if (lossless == nullptr) {
        still.status = WebPDecoder::Status::Corrupt;
        still.error = "no VP8L or VP8 image chunk";
        return still;
    }

    Vp8lDecoder decoder(lossless->data, lossless->size);
    std::vector<uint32_t> pixels;
    int width = 0;
    int height = 0;
    if (!decoder.decode_image(pixels, width, height, still.error)) {
        if (still.error.find("truncated") != std::string::npos) {
            still.status = WebPDecoder::Status::Truncated;
        } else if (still.error.find("budget") != std::string::npos ||
                   still.error.find("dimension") != std::string::npos) {
            still.status = WebPDecoder::Status::TooLarge;
        } else {
            still.status = WebPDecoder::Status::Corrupt;
        }
        return still;
    }
    if (width <= 0 || height <= 0) {
        still.status = WebPDecoder::Status::Corrupt;
        still.error = "decoded image has no pixels";
        return still;
    }
    if (canvas_width > 0 && canvas_height > 0 && (width > canvas_width || height > canvas_height)) {
        still.status = WebPDecoder::Status::Corrupt;
        still.error = "image is larger than its VP8X canvas";
        return still;
    }
    if (pixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height)) {
        still.status = WebPDecoder::Status::Corrupt;
        still.error = "decoded pixel buffer is short";
        return still;
    }

    auto image = std::make_shared<ImageData>();
    image->width = width;
    image->height = height;
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    image->pixels = std::move(pixels);
    still.image = std::move(image);
    return still;
}

/// Whether a frame's own image data carries transparency: an `ALPH` chunk, or a
/// `VP8L` image whose alpha bit is set.
bool frame_has_alpha(const std::vector<Chunk>& chunks) {
    for (const Chunk& chunk : chunks) {
        if (std::memcmp(chunk.fourcc, "ALPH", 4) == 0) return true;
        if (std::memcmp(chunk.fourcc, "VP8L", 4) == 0 && chunk.size >= 5) {
            const uint32_t bits = read_u32_le(chunk.data + 1);
            if (((bits >> 28) & 1u) != 0) return true;
        }
    }
    return false;
}

/// One source pixel over one canvas pixel, as libwebp's animation decoder does
/// it ("non-premultiplied" mode, src/demux/anim_decode.c): a pixel that is
/// opaque replaces the canvas pixel, a fully transparent one leaves it, and
/// anything between is blended with the same integer arithmetic — the
/// destination's alpha scaled by 256 - src_a, and one reciprocal for the
/// result's alpha — so that two decoders drawing the same frames give the same
/// bytes.
uint32_t blend_over(uint32_t src, uint32_t dst) {
    const uint32_t src_a = (src >> 24) & 0xffu;
    if (src_a == 0xffu) return src;
    if (src_a == 0) return dst;
    const uint32_t dst_a = (dst >> 24) & 0xffu;
    const uint32_t dst_factor_a = (dst_a * (256u - src_a)) >> 8;
    const uint32_t blend_a = src_a + dst_factor_a;
    const uint32_t scale = (1u << 24) / blend_a;
    uint32_t out = blend_a << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t src_channel = (src >> shift) & 0xffu;
        const uint32_t dst_channel = (dst >> shift) & 0xffu;
        const uint32_t blended = (src_channel * src_a + dst_channel * dst_factor_a) * scale;
        out |= ((blended >> 24) & 0xffu) << shift;
    }
    return out;
}

} // namespace

bool WebPDecoder::is_webp(const uint8_t* data, size_t size) {
    return data != nullptr && size >= 12 && std::memcmp(data, "RIFF", 4) == 0 &&
           std::memcmp(data + 8, "WEBP", 4) == 0;
}

const char* WebPDecoder::status_string(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotWebP: return "not a WebP file";
        case Status::UnsupportedAlpha:
            return "lossy WebP alpha with level-reduction preprocessing is not supported";
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
    if (!complete) return Status::Truncated;
    if (has_animation || has_lossless || has_lossy) return Status::Ok;
    return Status::Corrupt;
}

/// The walk every entry point goes through: fills `animation`, says what went
/// wrong in its `error`, and returns the status `decode_detailed()` reports.
WebPDecoder::Status decode_animation(const uint8_t* data, size_t size, size_t max_frames,
                                     WebPDecoder::Animation& animation, bool& first_is_lossy);

WebPDecoder::DecodeResult WebPDecoder::decode_detailed(const uint8_t* data, size_t size) {
    DecodeResult result;
    Animation animation;
    bool first_is_lossy = false;
    result.status = decode_animation(data, size, 1, animation, first_is_lossy);
    if (result.status != Status::Ok) {
        result.error = animation.error;
        return result;
    }
    result.image = animation.frames.front().image;
    result.lossy = first_is_lossy;
    return result;
}

WebPDecoder::Animation WebPDecoder::decode_frames(const uint8_t* data, size_t size, size_t max_frames) {
    Animation animation;
    bool first_is_lossy = false;
    const Status status = decode_animation(data, size, max_frames, animation, first_is_lossy);
    animation.first_is_lossy = first_is_lossy;
    // A walk that stops part way leaves the frames it did decode in place, as
    // GifDecoder does: the error says the file is broken, and `ok()` is false,
    // but what was drawn before the break is there for a caller that wants it.
    (void)status;
    return animation;
}

WebPDecoder::Status decode_animation(const uint8_t* data, size_t size, size_t max_frames,
                                     WebPDecoder::Animation& animation, bool& first_is_lossy) {
    using Status = WebPDecoder::Status;
    using AnimationFrame = WebPDecoder::AnimationFrame;
    if (!WebPDecoder::is_webp(data, size)) {
        animation.error = "not a WebP file";
        return Status::NotWebP;
    }

    std::vector<Chunk> chunks;
    if (!read_chunks(data, size, chunks, animation.error)) {
        // A chunk that runs past the end is a truncation; a container with no
        // chunks at all is not, and probe() has always called that one corrupt.
        return animation.error.find("truncated") != std::string::npos ? Status::Truncated : Status::Corrupt;
    }

    int canvas_width = 0;
    int canvas_height = 0;
    bool animation_flag = false;
    const Chunk* anim = nullptr;
    std::vector<const Chunk*> frame_chunks;
    for (const Chunk& chunk : chunks) {
        if (std::memcmp(chunk.fourcc, "VP8X", 4) == 0) {
            if (chunk.size < 10) {
                animation.error = "truncated VP8X header";
                return Status::Truncated;
            }
            animation_flag = (chunk.data[0] & 0x02u) != 0;
            canvas_width = static_cast<int>(read_u24_le(chunk.data + 4)) + 1;
            canvas_height = static_cast<int>(read_u24_le(chunk.data + 7)) + 1;
        } else if (std::memcmp(chunk.fourcc, "ANIM", 4) == 0) {
            if (anim == nullptr) anim = &chunk;
        } else if (std::memcmp(chunk.fourcc, "ANMF", 4) == 0) {
            frame_chunks.push_back(&chunk);
        }
    }

    if (frame_chunks.empty()) {
        // A still image: one frame, with no duration and the whole image as the
        // canvas. `probe()` says so too.
        StillImage still = decode_still(chunks, canvas_width, canvas_height);
        if (!still.image) {
            animation.error = still.error.empty() ? "no VP8L or VP8 image chunk" : still.error;
            return still.status != Status::Ok ? still.status : Status::Corrupt;
        }
        AnimationFrame frame;
        frame.image = still.image;
        frame.width = still.image->width;
        frame.height = still.image->height;
        animation.canvas_width = still.image->width;
        animation.canvas_height = still.image->height;
        first_is_lossy = still.lossy;
        animation.frames.push_back(std::move(frame));
        return Status::Ok;
    }

    // An animation: the canvas is `VP8X`'s, and `ANIM` carries the background
    // colour and the loop count (both optional in practice: a file that says
    // nothing starts transparent and plays once).
    if (!animation_flag || canvas_width <= 0 || canvas_height <= 0) {
        animation.error = "an animated WebP without a VP8X canvas";
        return Status::Corrupt;
    }
    if (canvas_width > WebPDecoder::kMaxDimension || canvas_height > WebPDecoder::kMaxDimension ||
        static_cast<int64_t>(canvas_width) * canvas_height > WebPDecoder::kMaxDecodedPixels) {
        animation.error = "canvas is too large";
        return Status::TooLarge;
    }
    animation.canvas_width = canvas_width;
    animation.canvas_height = canvas_height;
    animation.animated = true;
    if (anim != nullptr) {
        if (anim->size < 6) {
            animation.error = "truncated ANIM header";
            return Status::Truncated;
        }
        // Stored blue, green, red, alpha.
        const uint32_t blue = anim->data[0];
        const uint32_t green = anim->data[1];
        const uint32_t red = anim->data[2];
        const uint32_t alpha = anim->data[3];
        animation.background = (alpha << 24) | (red << 16) | (green << 8) | blue;
        animation.loop_count = static_cast<int>(anim->data[4]) | (static_cast<int>(anim->data[5]) << 8);
    }

    const size_t canvas_pixels = static_cast<size_t>(canvas_width) * static_cast<size_t>(canvas_height);
    // Two canvases, as libwebp keeps them: `canvas` is the frame being built and
    // what a viewer shows, `previous` is what the last frame left behind once
    // its disposal method was applied.
    std::vector<uint32_t> canvas(canvas_pixels, 0u);
    std::vector<uint32_t> previous(canvas_pixels, 0u);
    // The previous frame's rectangle, and whether it was cleared.
    int previous_x = 0;
    int previous_y = 0;
    int previous_width = 0;
    int previous_height = 0;
    bool previous_disposed = false;
    bool previous_was_key_frame = false;
    size_t frame_index = 0;
    int64_t total_frame_pixels = 0;
    for (const Chunk* chunk : frame_chunks) {
        if (animation.frames.size() >= max_frames) break;
        if (chunk->size < 16) {
            animation.error = "truncated ANMF header";
            return Status::Truncated;
        }
        const uint8_t* header = chunk->data;
        // Every geometry field is in units of two pixels, but the width and the
        // height, which are stored minus one.
        const int x = static_cast<int>(read_u24_le(header)) * 2;
        const int y = static_cast<int>(read_u24_le(header + 3)) * 2;
        const int width = static_cast<int>(read_u24_le(header + 6)) + 1;
        const int height = static_cast<int>(read_u24_le(header + 9)) + 1;
        const int duration_ms = static_cast<int>(read_u24_le(header + 12));
        const uint8_t flags = header[15];
        // Bit 1 is the blending method (0 = alpha blend), bit 0 the disposal
        // method (0 = none, 1 = clear the rectangle).
        const bool blend = (flags & 0x02u) == 0;
        const bool dispose_to_background = (flags & 0x01u) != 0;
        if (width <= 0 || height <= 0 || x + width > canvas_width || y + height > canvas_height ||
            static_cast<int64_t>(width) * height > WebPDecoder::kMaxDecodedPixels) {
            animation.error = "a frame rectangle is not inside the canvas";
            return Status::Corrupt;
        }

        std::vector<Chunk> frame_content;
        if (!read_nested_chunks(header + 16, chunk->size - 16, frame_content, animation.error)) {
            return Status::Truncated;
        }
        StillImage still = decode_still(frame_content, 0, 0);
        if (!still.image) {
            animation.error = still.error.empty() ? "a frame has no image chunk" : still.error;
            return still.status != Status::Ok ? still.status : Status::Corrupt;
        }
        if (still.image->width != width || still.image->height != height) {
            animation.error = "a frame's image is not the size of its rectangle";
            return Status::Corrupt;
        }

        // A key frame is drawn on a cleared canvas and is never blended: the
        // first frame, a full-canvas frame that has no transparency or is not
        // blended, and a frame that follows one which cleared the whole canvas.
        // libwebp decides this the same way
        // (IsKeyFrame(), src/demux/anim_decode.c), and it matters: blending a
        // frame over transparent pixels is not quite the identity, because the
        // blend rounds, so an animation that libwebp draws exactly would come
        // out a shade different here.
        // The last clause is about the previous frame, not this one: when it was
        // the whole canvas and cleared itself, everything underneath is
        // transparent, so this frame can be drawn without blending too.
        const bool full_canvas = width == canvas_width && height == canvas_height;
        const bool previous_was_full_canvas = previous_width == canvas_width && previous_height == canvas_height;
        const bool key_frame =
            frame_index == 0 || ((!frame_has_alpha(frame_content) || !blend) && full_canvas) ||
            (previous_disposed && (previous_was_full_canvas || previous_was_key_frame));
        if (key_frame) {
            std::fill(canvas.begin(), canvas.end(), 0u);
        } else {
            canvas = previous;
        }

        // Draw the frame's pixels where its rectangle is, transparency included.
        for (int row = 0; row < height; ++row) {
            uint32_t* dst = canvas.data() + static_cast<size_t>(y + row) * canvas_width + x;
            const uint32_t* src = still.image->pixels.data() + static_cast<size_t>(row) * width;
            std::memcpy(dst, src, static_cast<size_t>(width) * sizeof(uint32_t));
        }

        // Blend what the frame left translucent over what was underneath, the
        // way libwebp does it: only over pixels that came from an earlier
        // frame, so nowhere that the previous frame cleared, and only outside
        // the previous frame's rectangle when it did clear itself.
        if (frame_index > 0 && blend && !key_frame) {
            for (int row = 0; row < height; ++row) {
                const int canvas_row = y + row;
                uint32_t* dst = canvas.data() + static_cast<size_t>(canvas_row) * canvas_width;
                const uint32_t* underneath = previous.data() + static_cast<size_t>(canvas_row) * canvas_width;
                int left1 = x;
                int width1 = width;
                int left2 = 0;
                int width2 = 0;
                if (previous_disposed) {
                    // Rows above and below the cleared rectangle are whole;
                    // rows through it blend in the parts either side of it.
                    const int previous_max_x = previous_x + previous_width;
                    const int previous_max_y = previous_y + previous_height;
                    width1 = 0;
                    left1 = -1;
                    if (canvas_row < previous_y || canvas_row >= previous_max_y || x >= previous_max_x ||
                        x + width <= previous_x) {
                        // The row misses the cleared rectangle, or this frame's
                        // rectangle does: the whole row is blended.
                        left1 = x;
                        width1 = width;
                    } else {
                        if (x < previous_x) {
                            left1 = x;
                            width1 = previous_x - x;
                        }
                        if (x + width > previous_max_x) {
                            left2 = previous_max_x;
                            width2 = x + width - previous_max_x;
                        }
                    }
                }
                for (int i = 0; i < width1; ++i) {
                    const int column = left1 + i;
                    dst[column] = blend_over(dst[column], underneath[column]);
                }
                for (int i = 0; i < width2; ++i) {
                    const int column = left2 + i;
                    dst[column] = blend_over(dst[column], underneath[column]);
                }
            }
        }

        auto composited = std::make_shared<ImageData>();
        composited->width = canvas_width;
        composited->height = canvas_height;
        composited->pixels = canvas;

        AnimationFrame frame;
        frame.image = std::move(composited);
        frame.duration_ms = duration_ms;
        frame.x = x;
        frame.y = y;
        frame.width = width;
        frame.height = height;
        frame.blend = blend;
        frame.dispose_to_background = dispose_to_background;
        animation.frames.push_back(std::move(frame));

        // What this frame leaves behind: the canvas, with the rectangle cleared
        // when the frame asks for that.
        previous = canvas;
        if (dispose_to_background) {
            for (int row = 0; row < height; ++row) {
                std::memset(previous.data() + static_cast<size_t>(y + row) * canvas_width + x, 0,
                            static_cast<size_t>(width) * sizeof(uint32_t));
            }
        }
        previous_x = x;
        previous_y = y;
        previous_width = width;
        previous_height = height;
        previous_disposed = dispose_to_background;
        previous_was_key_frame = key_frame;
        ++frame_index;

        total_frame_pixels += static_cast<int64_t>(canvas_pixels);
        if (animation.frames.size() >= max_frames ||
            total_frame_pixels + static_cast<int64_t>(canvas_pixels) > WebPDecoder::kMaxAnimationPixels) {
            break;
        }
    }
    if (animation.frames.empty()) animation.error = "the animation holds no frames";
    return animation.frames.empty() ? Status::Corrupt : Status::Ok;
}

std::shared_ptr<ImageData> WebPDecoder::first_frame(const uint8_t* data, size_t size) {
    Animation animation = decode_frames(data, size, 1);
    return animation.frames.empty() ? nullptr : animation.frames.front().image;
}

std::shared_ptr<ImageData> WebPDecoder::decode(const uint8_t* data, size_t size) {
    DecodeResult result = decode_detailed(data, size);
    return result.ok() ? result.image : nullptr;
}

std::shared_ptr<ImageData> WebPDecoder::decode(const std::vector<uint8_t>& data) {
    return decode(data.data(), data.size());
}

} // namespace litebird
