#include "litebird/render/JpegDecoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace litebird {

namespace {

static constexpr uint8_t kZigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

struct IdctTable {
    double c[8][8]{};
    IdctTable() {
        constexpr double kPi = 3.14159265358979323846;
        const double inv_sqrt8 = 1.0 / std::sqrt(8.0);
        for (int u = 0; u < 8; ++u) {
            const double alpha = (u == 0) ? inv_sqrt8 : 0.5;
            for (int x = 0; x < 8; ++x) {
                c[u][x] = alpha * std::cos(((2.0 * x + 1.0) * u * kPi) / 16.0);
            }
        }
    }
};

const IdctTable& idct_table() {
    static const IdctTable table;
    return table;
}

inline uint8_t clamp_u8(double v) {
    if (v <= 0.0) return 0;
    if (v >= 255.0) return 255;
    return static_cast<uint8_t>(v + 0.5);
}

inline uint8_t clamp_i32_u8(int32_t v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<uint8_t>(v);
}

inline uint16_t read_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

bool validate_dimensions(int64_t width, int64_t height) {
    if (width <= 0 || height <= 0) return false;
    if (width > JpegDecoder::kMaxDecodedPixels / height) return false;
    return (width * height) <= JpegDecoder::kMaxDecodedPixels;
}

struct HuffmanTable {
    static constexpr int kFastBits = 9;
    static constexpr int kFastSize = 1 << kFastBits;

    bool valid{false};
    std::array<uint8_t, kFastSize> fast_sym{};
    std::array<uint8_t, kFastSize> fast_len{};
    std::array<int32_t, 17> min_code{};
    std::array<int32_t, 17> max_code{};
    std::array<int32_t, 17> val_offset{};
    std::array<uint8_t, 256> values{};
    int total_values{0};

    bool build(const uint8_t counts[16], const uint8_t* vals, int count_sum) {
        if (count_sum < 0 || count_sum > 256) return false;
        fast_sym.fill(0);
        fast_len.fill(0);
        min_code.fill(0);
        max_code.fill(-1);
        val_offset.fill(0);
        total_values = count_sum;
        for (int i = 0; i < count_sum; ++i) {
            values[i] = vals[i];
        }

        int32_t code = 0;
        int idx = 0;
        for (int len = 1; len <= 16; ++len) {
            const int cnt = counts[len - 1];
            if (cnt > 0) {
                min_code[len] = code;
                val_offset[len] = idx;
                max_code[len] = code + cnt - 1;
                if (max_code[len] >= (1 << len)) {
                    return false; // Over-subscribed Huffman tree
                }
                if (len <= kFastBits) {
                    const int shift = kFastBits - len;
                    const int span = 1 << shift;
                    for (int k = 0; k < cnt; ++k) {
                        const int base = (code + k) << shift;
                        const uint8_t sym = values[idx + k];
                        for (int j = 0; j < span; ++j) {
                            fast_sym[base + j] = sym;
                            fast_len[base + j] = static_cast<uint8_t>(len);
                        }
                    }
                }
                code += cnt;
                idx += cnt;
            }
            code <<= 1;
        }
        valid = true;
        return true;
    }
};

struct BitReader {
    const uint8_t* data{nullptr};
    size_t size{0};
    size_t pos{0};

    uint64_t bit_buf{0};
    int valid_bits{0};
    uint8_t pending_marker{0};
    bool hit_eof{false};

    BitReader(const uint8_t* d, size_t s, size_t start_pos)
        : data(d), size(s), pos(start_pos) {}

    void reset_after_restart() {
        bit_buf = 0;
        valid_bits = 0;
        pending_marker = 0;
    }

    // Reads one entropy byte from the stream (unescaping FF 00 -> FF).
    // Stops without consuming past a non-zero marker and records it in pending_marker.
    bool read_entropy_byte(uint8_t& out) {
        if (pending_marker != 0 || hit_eof) return false;
        if (pos >= size) {
            hit_eof = true;
            return false;
        }
        uint8_t b = data[pos++];
        if (b != 0xFF) {
            out = b;
            return true;
        }
        // 0xFF inside entropy data
        if (pos >= size) {
            hit_eof = true;
            return false;
        }
        uint8_t next = data[pos++];
        if (next == 0x00) {
            out = 0xFF;
            return true;
        }
        // Skip repeated 0xFF fill bytes before the actual marker byte
        while (next == 0xFF) {
            if (pos >= size) {
                hit_eof = true;
                return false;
            }
            next = data[pos++];
        }
        if (next == 0x00) {
            out = 0xFF;
            return true;
        }
        pending_marker = next;
        return false;
    }

    void fill(int need) {
        while (valid_bits < need) {
            uint8_t b = 0;
            if (!read_entropy_byte(b)) break;
            bit_buf = (bit_buf << 8) | static_cast<uint64_t>(b);
            valid_bits += 8;
        }
    }

    bool read_bits(int n, uint32_t& out) {
        if (n == 0) {
            out = 0;
            return true;
        }
        if (n < 0 || n > 24) return false;
        fill(n);
        if (valid_bits < n) return false;
        out = static_cast<uint32_t>((bit_buf >> (valid_bits - n)) & ((1ull << n) - 1ull));
        valid_bits -= n;
        return true;
    }

    int decode_huffman(const HuffmanTable& h) {
        if (!h.valid) return -1;
        fill(HuffmanTable::kFastBits);
        if (valid_bits <= 0) return -1;

        if (valid_bits >= HuffmanTable::kFastBits) {
            const uint32_t peek = static_cast<uint32_t>(
                (bit_buf >> (valid_bits - HuffmanTable::kFastBits)) &
                ((1u << HuffmanTable::kFastBits) - 1u)
            );
            const uint8_t len = h.fast_len[peek];
            if (len > 0) {
                valid_bits -= len;
                return h.fast_sym[peek];
            }
        } else {
            // Fewer than kFastBits left before a marker/EOF: pad low bits with 1s
            // (valid JPEG codes are never all-1s prefixes) and accept only if len <= valid_bits.
            const int pad = HuffmanTable::kFastBits - valid_bits;
            const uint32_t mask = (1u << valid_bits) - 1u;
            const uint32_t top = static_cast<uint32_t>(bit_buf & mask);
            const uint32_t peek = (top << pad) | ((1u << pad) - 1u);
            const uint8_t len = h.fast_len[peek];
            if (len > 0 && len <= valid_bits) {
                valid_bits -= len;
                return h.fast_sym[peek];
            }
        }

        // Slow path: lengths 1..16 bit-by-bit
        int32_t code = 0;
        for (int len = 1; len <= 16; ++len) {
            uint32_t bit = 0;
            if (!read_bits(1, bit)) return -1;
            code = (code << 1) | static_cast<int32_t>(bit);
            if (h.max_code[len] >= 0 && code <= h.max_code[len]) {
                const int idx = h.val_offset[len] + (code - h.min_code[len]);
                if (idx >= 0 && idx < h.total_values) {
                    return h.values[idx];
                }
                return -1;
            }
        }
        return -1;
    }

    // Consumes an expected restart marker RST0..RST7 between restart intervals.
    bool consume_restart_marker(int expected_rst) {
        const uint8_t expected = static_cast<uint8_t>(0xD0 + (expected_rst & 7));

        // Drop remaining bits in the current partial byte.
        // If a pending marker wasn't already hit, peek ahead in the stream.
        if (pending_marker == 0) {
            // Discard unused pad bits in the final byte; if there are none or < 8, read marker.
            if (valid_bits >= 8) {
                // Shouldn't happen in valid files unless extra bytes precede RST; discard pad bits.
                valid_bits = 0;
                bit_buf = 0;
            }
            while (pos < size) {
                uint8_t b = data[pos++];
                if (b == 0xFF) {
                    while (pos < size && data[pos] == 0xFF) ++pos;
                    if (pos >= size) {
                        hit_eof = true;
                        return false;
                    }
                    uint8_t m = data[pos++];
                    if (m == 0x00) continue; // Stuffed byte
                    pending_marker = m;
                    break;
                }
            }
        }

        if (pending_marker == expected || (pending_marker >= 0xD0 && pending_marker <= 0xD7)) {
            reset_after_restart();
            return true;
        }
        return false;
    }
};

// ITU-T T.81 Figure F.12 EXTEND(V, T)
inline int32_t jpeg_extend(uint32_t v, int t) {
    if (t <= 0) return 0;
    const uint32_t vt = 1u << (t - 1);
    if (v < vt) {
        return static_cast<int32_t>(v) + static_cast<int32_t>((~0u << t) + 1u);
    }
    return static_cast<int32_t>(v);
}

// Safely shift a signed coefficient left by `al` bits without signed-shift UB.
inline int16_t safe_shift_left_i16(int32_t val, int al) {
    if (al <= 0) {
        return static_cast<int16_t>(std::clamp(val, -32768, 32767));
    }
    const int64_t scaled = static_cast<int64_t>(val) * (int64_t{1} << al);
    return static_cast<int16_t>(std::clamp<int64_t>(scaled, -32768, 32767));
}

// Exact double-precision separable 8x8 IDCT with DC-only row/col fast paths.
void idct_8x8(const int16_t coef[64], const uint16_t quant[64], uint8_t* out, size_t stride) {
    const auto& tbl = idct_table();
    double tmp[64];

    // Row pass (1D IDCT on each row)
    for (int r = 0; r < 8; ++r) {
        const int base = r * 8;
        const double d0 = static_cast<double>(coef[base + 0]) * quant[base + 0];
        const double d1 = static_cast<double>(coef[base + 1]) * quant[base + 1];
        const double d2 = static_cast<double>(coef[base + 2]) * quant[base + 2];
        const double d3 = static_cast<double>(coef[base + 3]) * quant[base + 3];
        const double d4 = static_cast<double>(coef[base + 4]) * quant[base + 4];
        const double d5 = static_cast<double>(coef[base + 5]) * quant[base + 5];
        const double d6 = static_cast<double>(coef[base + 6]) * quant[base + 6];
        const double d7 = static_cast<double>(coef[base + 7]) * quant[base + 7];

        if (d1 == 0.0 && d2 == 0.0 && d3 == 0.0 &&
            d4 == 0.0 && d5 == 0.0 && d6 == 0.0 && d7 == 0.0) {
            const double dc = d0 * tbl.c[0][0];
            for (int x = 0; x < 8; ++x) tmp[base + x] = dc;
        } else {
            for (int x = 0; x < 8; ++x) {
                tmp[base + x] = d0 * tbl.c[0][x] + d1 * tbl.c[1][x] +
                                d2 * tbl.c[2][x] + d3 * tbl.c[3][x] +
                                d4 * tbl.c[4][x] + d5 * tbl.c[5][x] +
                                d6 * tbl.c[6][x] + d7 * tbl.c[7][x];
            }
        }
    }

    // Column pass (1D IDCT on each column)
    for (int x = 0; x < 8; ++x) {
        const double t0 = tmp[0 * 8 + x];
        const double t1 = tmp[1 * 8 + x];
        const double t2 = tmp[2 * 8 + x];
        const double t3 = tmp[3 * 8 + x];
        const double t4 = tmp[4 * 8 + x];
        const double t5 = tmp[5 * 8 + x];
        const double t6 = tmp[6 * 8 + x];
        const double t7 = tmp[7 * 8 + x];

        if (t1 == 0.0 && t2 == 0.0 && t3 == 0.0 &&
            t4 == 0.0 && t5 == 0.0 && t6 == 0.0 && t7 == 0.0) {
            const uint8_t val = clamp_u8(t0 * tbl.c[0][0] + 128.0);
            for (int y = 0; y < 8; ++y) {
                out[static_cast<size_t>(y) * stride + static_cast<size_t>(x)] = val;
            }
        } else {
            for (int y = 0; y < 8; ++y) {
                const double sum = t0 * tbl.c[0][y] + t1 * tbl.c[1][y] +
                                   t2 * tbl.c[2][y] + t3 * tbl.c[3][y] +
                                   t4 * tbl.c[4][y] + t5 * tbl.c[5][y] +
                                   t6 * tbl.c[6][y] + t7 * tbl.c[7][y];
                out[static_cast<size_t>(y) * stride + static_cast<size_t>(x)] = clamp_u8(sum + 128.0);
            }
        }
    }
}

struct Component {
    uint8_t id{0};
    int h{1};
    int v{1};
    uint8_t qt_id{0};
    int blocks_x{0};
    int blocks_y{0};
    int stride{0};
    int height_px{0};
    int32_t dc_pred{0};
    // Sequential scans decode directly to pixels; progressive scans accumulate DCT coefs.
    std::vector<uint8_t> plane;
    std::vector<int16_t> coefs;
    bool has_data{false};
};

// Triangle (libjpeg fancy) 2x horizontal/vertical upsampling, with general fallback.
uint8_t sample_component(const Component& c, int x, int y, int max_h, int max_v, int img_w, int img_h) {
    if (c.plane.empty() || c.stride <= 0 || c.height_px <= 0) return 128;

    const int comp_w = (img_w * c.h + max_h - 1) / max_h;
    const int comp_h = (img_h * c.v + max_v - 1) / max_v;

    if (c.h == max_h && c.v == max_v) {
        const int sx = std::clamp(x, 0, c.stride - 1);
        const int sy = std::clamp(y, 0, c.height_px - 1);
        return c.plane[static_cast<size_t>(sy) * static_cast<size_t>(c.stride) + static_cast<size_t>(sx)];
    }

    // 2x horizontal and/or 2x vertical: standard centered [3/4, 1/4] triangle filter
    const bool h2 = (c.h * 2 == max_h);
    const bool v2 = (c.v * 2 == max_v);

    auto at = [&](int sx, int sy) -> int {
        sx = std::clamp(sx, 0, std::max(0, comp_w - 1));
        sy = std::clamp(sy, 0, std::max(0, comp_h - 1));
        sx = std::min(sx, c.stride - 1);
        sy = std::min(sy, c.height_px - 1);
        return c.plane[static_cast<size_t>(sy) * static_cast<size_t>(c.stride) + static_cast<size_t>(sx)];
    };

    if (h2 && c.v == max_v) {
        const int cx = x >> 1;
        const int nx = (x & 1) ? (cx + 1) : (cx - 1);
        return static_cast<uint8_t>((3 * at(cx, y) + at(nx, y) + 2) >> 2);
    }
    if (c.h == max_h && v2) {
        const int cy = y >> 1;
        const int ny = (y & 1) ? (cy + 1) : (cy - 1);
        return static_cast<uint8_t>((3 * at(x, cy) + at(x, ny) + 2) >> 2);
    }
    if (h2 && v2) {
        const int cx = x >> 1;
        const int cy = y >> 1;
        const int nx = (x & 1) ? (cx + 1) : (cx - 1);
        const int ny = (y & 1) ? (cy + 1) : (cy - 1);
        const int sum = 9 * at(cx, cy) + 3 * at(nx, cy) + 3 * at(cx, ny) + at(nx, ny);
        return static_cast<uint8_t>((sum + 8) >> 4);
    }

    // Arbitrary sampling factor ratio: nearest mapped sample
    const int sx = (x * c.h) / max_h;
    const int sy = (y * c.v) / max_v;
    return static_cast<uint8_t>(at(sx, sy));
}

struct DecoderState {
    int width{0};
    int height{0};
    int num_components{0};
    int max_h{1};
    int max_v{1};
    int mcu_cols{0};
    int mcu_rows{0};
    bool has_sof{false};
    bool progressive{false};
    bool has_jfif{false};
    bool has_adobe{false};
    uint8_t adobe_transform{0};
    int restart_interval{0};
    int scan_count{0};

    std::array<std::array<uint16_t, 64>, 4> dqt{};
    std::array<bool, 4> dqt_valid{};
    std::array<HuffmanTable, 4> dc_tables{};
    std::array<HuffmanTable, 4> ac_tables{};
    std::array<Component, 4> comps{};

    bool parse_dqt(const uint8_t* seg, size_t len, std::string& err) {
        size_t p = 0;
        while (p < len) {
            const uint8_t pq_tq = seg[p++];
            const int pq = (pq_tq >> 4) & 0x0F;
            const int tq = pq_tq & 0x0F;
            if (tq > 3 || (pq != 0 && pq != 1)) {
                err = "Invalid DQT table header";
                return false;
            }
            const size_t need = (pq == 0) ? 64u : 128u;
            if (p + need > len) {
                err = "Truncated DQT segment";
                return false;
            }
            for (int k = 0; k < 64; ++k) {
                uint16_t qval = 0;
                if (pq == 0) {
                    qval = seg[p++];
                } else {
                    qval = read_be16(seg + p);
                    p += 2;
                }
                dqt[tq][kZigzag[k]] = qval;
            }
            dqt_valid[tq] = true;
        }
        return true;
    }

    bool parse_dht(const uint8_t* seg, size_t len, std::string& err) {
        size_t p = 0;
        while (p < len) {
            const uint8_t tc_th = seg[p++];
            const int tc = (tc_th >> 4) & 0x0F;
            const int th = tc_th & 0x0F;
            if ((tc != 0 && tc != 1) || th > 3) {
                err = "Invalid DHT class or table ID";
                return false;
            }
            if (p + 16 > len) {
                err = "Truncated DHT counts";
                return false;
            }
            uint8_t counts[16];
            int total = 0;
            for (int i = 0; i < 16; ++i) {
                counts[i] = seg[p++];
                total += counts[i];
            }
            if (total > 256 || p + static_cast<size_t>(total) > len) {
                err = "Invalid or truncated DHT values";
                return false;
            }
            HuffmanTable& target = (tc == 0) ? dc_tables[th] : ac_tables[th];
            if (!target.build(counts, seg + p, total)) {
                err = "Malformed Huffman code tree";
                return false;
            }
            p += static_cast<size_t>(total);
        }
        return true;
    }

    bool parse_sof(uint8_t marker, const uint8_t* seg, size_t len, std::string& err) {
        if (has_sof) {
            err = "Duplicate SOF marker";
            return false;
        }
        if (len < 6) {
            err = "Truncated SOF segment";
            return false;
        }
        const uint8_t precision = seg[0];
        if (precision != 8) {
            err = "Unsupported JPEG sample precision (only 8-bit is supported)";
            return false;
        }
        const int h = read_be16(seg + 1);
        const int w = read_be16(seg + 3);
        const int nf = seg[5];
        if (!validate_dimensions(w, h)) {
            err = "Invalid or oversized JPEG dimensions";
            return false;
        }
        if (nf != 1 && nf != 3 && nf != 4) {
            err = "Unsupported JPEG component count";
            return false;
        }
        if (len != static_cast<size_t>(6 + nf * 3)) {
            err = "Invalid SOF segment length";
            return false;
        }

        width = w;
        height = h;
        num_components = nf;
        progressive = (marker == 0xC2);
        max_h = 1;
        max_v = 1;

        for (int i = 0; i < nf; ++i) {
            const uint8_t cid = seg[6 + i * 3];
            const uint8_t hv = seg[6 + i * 3 + 1];
            const uint8_t tq = seg[6 + i * 3 + 2];
            const int ch = (hv >> 4) & 0x0F;
            const int cv = hv & 0x0F;
            if (ch < 1 || ch > 4 || cv < 1 || cv > 4 || tq > 3) {
                err = "Invalid component sampling factor or quant table ID";
                return false;
            }
            for (int prev = 0; prev < i; ++prev) {
                if (comps[prev].id == cid) {
                    err = "Duplicate component ID in SOF";
                    return false;
                }
            }
            comps[i].id = cid;
            comps[i].h = ch;
            comps[i].v = cv;
            comps[i].qt_id = tq;
            max_h = std::max(max_h, ch);
            max_v = std::max(max_v, cv);
        }

        // Single-component images always have effective sampling 1x1
        if (nf == 1) {
            comps[0].h = 1;
            comps[0].v = 1;
            max_h = 1;
            max_v = 1;
        }

        const int mcu_w = max_h * 8;
        const int mcu_h = max_v * 8;
        mcu_cols = (width + mcu_w - 1) / mcu_w;
        mcu_rows = (height + mcu_h - 1) / mcu_h;

        size_t total_progressive_bytes = 0;
        for (int i = 0; i < nf; ++i) {
            comps[i].blocks_x = mcu_cols * comps[i].h;
            comps[i].blocks_y = mcu_rows * comps[i].v;
            comps[i].stride = comps[i].blocks_x * 8;
            comps[i].height_px = comps[i].blocks_y * 8;

            const uint64_t plane_pixels = static_cast<uint64_t>(comps[i].stride) *
                                          static_cast<uint64_t>(comps[i].height_px);
            if (plane_pixels > static_cast<uint64_t>(JpegDecoder::kMaxDecodedPixels) * 2ull) {
                err = "Component buffer exceeds safety cap";
                return false;
            }

            if (progressive) {
                const uint64_t num_blocks = static_cast<uint64_t>(comps[i].blocks_x) *
                                            static_cast<uint64_t>(comps[i].blocks_y);
                const uint64_t bytes = num_blocks * 64ull * sizeof(int16_t);
                if (bytes > JpegDecoder::kMaxProgressiveBytes ||
                    total_progressive_bytes > JpegDecoder::kMaxProgressiveBytes - bytes) {
                    err = "Progressive DCT buffer exceeds memory budget";
                    return false;
                }
                total_progressive_bytes += static_cast<size_t>(bytes);
                comps[i].coefs.assign(static_cast<size_t>(num_blocks * 64ull), 0);
            } else {
                comps[i].plane.assign(static_cast<size_t>(plane_pixels), 128);
            }
        }

        has_sof = true;
        return true;
    }

    bool decode_sequential_block(
        BitReader& br,
        int comp_idx,
        const HuffmanTable& dc_tab,
        const HuffmanTable& ac_tab,
        int16_t out_coef[64]
    ) {
        std::memset(out_coef, 0, 64 * sizeof(int16_t));

        // DC coefficient
        const int t = br.decode_huffman(dc_tab);
        if (t < 0 || t > 15) return false;
        uint32_t bits = 0;
        if (!br.read_bits(t, bits)) return false;
        comps[comp_idx].dc_pred += jpeg_extend(bits, t);
        out_coef[0] = static_cast<int16_t>(std::clamp<int32_t>(comps[comp_idx].dc_pred, -32768, 32767));

        // AC coefficients
        int k = 1;
        while (k < 64) {
            const int rs = br.decode_huffman(ac_tab);
            if (rs < 0) return false;
            const int r = (rs >> 4) & 0x0F;
            const int s = rs & 0x0F;
            if (s == 0) {
                if (r == 15) {
                    k += 16;
                } else if (r == 0) {
                    break; // EOB
                } else {
                    return false;
                }
            } else {
                if (s > 14) return false;
                k += r;
                if (k >= 64) return false;
                uint32_t ac_bits = 0;
                if (!br.read_bits(s, ac_bits)) return false;
                out_coef[kZigzag[k]] = static_cast<int16_t>(jpeg_extend(ac_bits, s));
                ++k;
            }
        }
        return true;
    }

    bool decode_progressive_dc_first(
        BitReader& br,
        int comp_idx,
        const HuffmanTable& dc_tab,
        int al,
        int16_t* block_coef
    ) {
        const int t = br.decode_huffman(dc_tab);
        if (t < 0 || t > 15) return false;
        uint32_t bits = 0;
        if (!br.read_bits(t, bits)) return false;
        comps[comp_idx].dc_pred += jpeg_extend(bits, t);
        block_coef[0] = safe_shift_left_i16(comps[comp_idx].dc_pred, al);
        return true;
    }

    bool decode_progressive_dc_refine(BitReader& br, int al, int16_t* block_coef) {
        uint32_t bit = 0;
        if (!br.read_bits(1, bit)) return false;
        if (bit) {
            const int32_t delta = 1 << al;
            block_coef[0] = static_cast<int16_t>(
                std::clamp<int32_t>(static_cast<int32_t>(block_coef[0]) + delta, -32768, 32767)
            );
        }
        return true;
    }

    bool decode_progressive_ac_first(
        BitReader& br,
        const HuffmanTable& ac_tab,
        int ss,
        int se,
        int al,
        int32_t& eobrun,
        int16_t* block_coef
    ) {
        if (eobrun > 0) {
            --eobrun;
            return true;
        }
        int k = ss;
        while (k <= se) {
            const int rs = br.decode_huffman(ac_tab);
            if (rs < 0) return false;
            const int r = (rs >> 4) & 0x0F;
            const int s = rs & 0x0F;
            if (s == 0) {
                if (r == 15) {
                    k += 16;
                } else {
                    uint32_t extra = 0;
                    if (r > 0 && !br.read_bits(r, extra)) return false;
                    eobrun = (1 << r) + static_cast<int32_t>(extra) - 1;
                    break;
                }
            } else {
                if (s > 14) return false;
                k += r;
                if (k > se) return false;
                uint32_t bits = 0;
                if (!br.read_bits(s, bits)) return false;
                block_coef[kZigzag[k]] = safe_shift_left_i16(jpeg_extend(bits, s), al);
                ++k;
            }
        }
        return true;
    }

    bool refine_nonzero_coef(BitReader& br, int16_t& c, int32_t p1, int32_t m1) {
        uint32_t bit = 0;
        if (!br.read_bits(1, bit)) return false;
        if (bit) {
            if (c > 0) {
                c = static_cast<int16_t>(std::min<int32_t>(32767, static_cast<int32_t>(c) + p1));
            } else {
                c = static_cast<int16_t>(std::max<int32_t>(-32768, static_cast<int32_t>(c) + m1));
            }
        }
        return true;
    }

    bool decode_progressive_ac_refine(
        BitReader& br,
        const HuffmanTable& ac_tab,
        int ss,
        int se,
        int al,
        int32_t& eobrun,
        int16_t* block_coef
    ) {
        const int32_t p1 = 1 << al;
        const int32_t m1 = -p1;
        int k = ss;

        if (eobrun == 0) {
            while (k <= se) {
                const int rs = br.decode_huffman(ac_tab);
                if (rs < 0) return false;
                int r = (rs >> 4) & 0x0F;
                const int s = rs & 0x0F;
                int32_t new_val = 0;

                if (s == 1) {
                    uint32_t sign_bit = 0;
                    if (!br.read_bits(1, sign_bit)) return false;
                    new_val = sign_bit ? p1 : m1;
                } else if (s == 0) {
                    if (r < 15) {
                        uint32_t extra = 0;
                        if (r > 0 && !br.read_bits(r, extra)) return false;
                        eobrun = (1 << r) + static_cast<int32_t>(extra);
                        break;
                    }
                    // r == 15, s == 0: ZRL (skip 16 zero-valued coefficients while refining non-zero ones)
                } else {
                    return false;
                }

                // Advance through k..se, refining non-zero entries and counting `r` zero entries.
                while (k <= se) {
                    int16_t& c = block_coef[kZigzag[k]];
                    if (c != 0) {
                        if (!refine_nonzero_coef(br, c, p1, m1)) return false;
                    } else {
                        if (r == 0) {
                            break;
                        }
                        --r;
                    }
                    ++k;
                }

                if (s == 1) {
                    if (k > se) return false;
                    block_coef[kZigzag[k]] = static_cast<int16_t>(new_val);
                    ++k;
                } else {
                    // ZRL consumed 16 zero slots (r reached 0 on the 16th zero at k)
                    if (k <= se) ++k;
                }
            }
        }

        if (eobrun > 0) {
            for (; k <= se; ++k) {
                int16_t& c = block_coef[kZigzag[k]];
                if (c != 0) {
                    if (!refine_nonzero_coef(br, c, p1, m1)) return false;
                }
            }
            --eobrun;
        }
        return true;
    }

    bool parse_and_decode_sos(
        const uint8_t* data,
        size_t size,
        size_t seg_start,
        size_t seg_len,
        size_t& out_pos,
        uint8_t& out_next_marker,
        std::string& err
    ) {
        if (!has_sof) {
            err = "SOS encountered before SOF";
            return false;
        }
        if (++scan_count > JpegDecoder::kMaxScans) {
            err = "Too many SOS scans";
            return false;
        }
        if (seg_len < 6) {
            err = "Truncated SOS segment";
            return false;
        }
        const uint8_t* seg = data + seg_start;
        const int ns = seg[0];
        if (ns < 1 || ns > num_components || seg_len != static_cast<size_t>(4 + 2 * ns)) {
            err = "Invalid SOS component count or length";
            return false;
        }

        struct ScanComp {
            int comp_idx{0};
            int td{0};
            int ta{0};
        };
        std::array<ScanComp, 4> scan_comps{};
        for (int i = 0; i < ns; ++i) {
            const uint8_t cs = seg[1 + i * 2];
            const uint8_t td_ta = seg[1 + i * 2 + 1];
            const int td = (td_ta >> 4) & 0x0F;
            const int ta = td_ta & 0x0F;
            if (td > 3 || ta > 3) {
                err = "Invalid SOS Huffman table selector";
                return false;
            }
            int found = -1;
            for (int c = 0; c < num_components; ++c) {
                if (comps[c].id == cs) {
                    found = c;
                    break;
                }
            }
            if (found < 0) {
                err = "SOS references unknown component ID";
                return false;
            }
            for (int prev = 0; prev < i; ++prev) {
                if (scan_comps[prev].comp_idx == found) {
                    err = "Duplicate component in SOS";
                    return false;
                }
            }
            scan_comps[i] = {found, td, ta};
            comps[found].has_data = true;
            if (!dqt_valid[comps[found].qt_id]) {
                err = "Component references missing quantization table";
                return false;
            }
        }

        const int ss = seg[1 + 2 * ns];
        const int se = seg[2 + 2 * ns];
        const uint8_t ah_al = seg[3 + 2 * ns];
        const int ah = (ah_al >> 4) & 0x0F;
        const int al = ah_al & 0x0F;

        if (ss > se || se > 63 || ah > 13 || al > 13) {
            err = "Invalid SOS spectral selection or successive approximation";
            return false;
        }
        if (!progressive) {
            if (ss != 0 || se != 63 || ah != 0 || al != 0) {
                err = "Sequential SOS has non-sequential spectral parameters";
                return false;
            }
        } else {
            if (ss == 0 && se != 0) {
                err = "Progressive DC scan cannot include AC coefficients";
                return false;
            }
            if (ss > 0 && ns != 1) {
                err = "Progressive AC scan must contain a single component";
                return false;
            }
            if (ah != 0 && ah != al + 1) {
                err = "Invalid progressive successive approximation step";
                return false;
            }
        }

        // Validate required Huffman tables exist before decoding
        for (int i = 0; i < ns; ++i) {
            if (!progressive) {
                if (!dc_tables[scan_comps[i].td].valid || !ac_tables[scan_comps[i].ta].valid) {
                    err = "Missing Huffman table for sequential scan";
                    return false;
                }
            } else if (ss == 0 && ah == 0) {
                if (!dc_tables[scan_comps[i].td].valid) {
                    err = "Missing DC Huffman table for progressive scan";
                    return false;
                }
            } else if (ss > 0) {
                if (!ac_tables[scan_comps[i].ta].valid) {
                    err = "Missing AC Huffman table for progressive AC scan";
                    return false;
                }
            }
        }

        for (int i = 0; i < num_components; ++i) {
            comps[i].dc_pred = 0;
        }

        BitReader br(data, size, seg_start + seg_len);
        int32_t eobrun = 0;
        int mcus_until_restart = restart_interval;
        int next_rst = 0;

        auto handle_restart_if_needed = [&](bool has_more_mcus) -> bool {
            if (restart_interval <= 0 || !has_more_mcus) return true;
            if (--mcus_until_restart == 0) {
                if (!br.consume_restart_marker(next_rst)) {
                    // Be tolerant if stream is already at EOI/marker
                    return false;
                }
                next_rst = (next_rst + 1) & 7;
                mcus_until_restart = restart_interval;
                for (int i = 0; i < num_components; ++i) comps[i].dc_pred = 0;
                eobrun = 0;
            }
            return true;
        };

        if (ns == 1) {
            // Non-interleaved scan (1 block per MCU over the component's active block grid)
            const int ci = scan_comps[0].comp_idx;
            Component& comp = comps[ci];
            const int active_bx = (width * comp.h + max_h - 1) / max_h;
            const int active_by = (height * comp.v + max_v - 1) / max_v;
            const int scan_bx = (active_bx + 7) / 8;
            const int scan_by = (active_by + 7) / 8;

            const int total_blocks = scan_bx * scan_by;
            int block_num = 0;
            for (int by = 0; by < scan_by; ++by) {
                for (int bx = 0; bx < scan_bx; ++bx) {
                    ++block_num;
                    if (!progressive) {
                        int16_t coef[64];
                        if (!decode_sequential_block(br, ci, dc_tables[scan_comps[0].td],
                                                     ac_tables[scan_comps[0].ta], coef)) {
                            err = "Corrupt sequential entropy stream";
                            return false;
                        }
                        uint8_t* dst = comp.plane.data() +
                                       static_cast<size_t>(by * 8) * static_cast<size_t>(comp.stride) +
                                       static_cast<size_t>(bx * 8);
                        idct_8x8(coef, dqt[comp.qt_id].data(), dst, static_cast<size_t>(comp.stride));
                    } else {
                        const size_t blk_offset = (static_cast<size_t>(by) * static_cast<size_t>(comp.blocks_x) +
                                                   static_cast<size_t>(bx)) * 64u;
                        int16_t* blk = comp.coefs.data() + blk_offset;
                        bool ok = false;
                        if (ss == 0 && ah == 0) {
                            ok = decode_progressive_dc_first(br, ci, dc_tables[scan_comps[0].td], al, blk);
                        } else if (ss == 0) {
                            ok = decode_progressive_dc_refine(br, al, blk);
                        } else if (ah == 0) {
                            ok = decode_progressive_ac_first(br, ac_tables[scan_comps[0].ta], ss, se, al, eobrun, blk);
                        } else {
                            ok = decode_progressive_ac_refine(br, ac_tables[scan_comps[0].ta], ss, se, al, eobrun, blk);
                        }
                        if (!ok) {
                            err = "Corrupt progressive entropy stream";
                            return false;
                        }
                    }

                    if (!handle_restart_if_needed(block_num < total_blocks)) {
                        err = "Missing or corrupt JPEG restart marker";
                        return false;
                    }
                }
            }
        } else {
            // Interleaved scan (MCU grid of mcu_cols x mcu_rows)
            const int total_mcus = mcu_cols * mcu_rows;
            int mcu_num = 0;
            for (int my = 0; my < mcu_rows; ++my) {
                for (int mx = 0; mx < mcu_cols; ++mx) {
                    ++mcu_num;
                    for (int si = 0; si < ns; ++si) {
                        const int ci = scan_comps[si].comp_idx;
                        Component& comp = comps[ci];
                        for (int bv = 0; bv < comp.v; ++bv) {
                            for (int bh = 0; bh < comp.h; ++bh) {
                                const int bx = mx * comp.h + bh;
                                const int by = my * comp.v + bv;
                                if (!progressive) {
                                    int16_t coef[64];
                                    if (!decode_sequential_block(br, ci, dc_tables[scan_comps[si].td],
                                                                 ac_tables[scan_comps[si].ta], coef)) {
                                        err = "Corrupt interleaved sequential entropy stream";
                                        return false;
                                    }
                                    uint8_t* dst = comp.plane.data() +
                                                   static_cast<size_t>(by * 8) * static_cast<size_t>(comp.stride) +
                                                   static_cast<size_t>(bx * 8);
                                    idct_8x8(coef, dqt[comp.qt_id].data(), dst, static_cast<size_t>(comp.stride));
                                } else {
                                    const size_t blk_offset = (static_cast<size_t>(by) * static_cast<size_t>(comp.blocks_x) +
                                                               static_cast<size_t>(bx)) * 64u;
                                    int16_t* blk = comp.coefs.data() + blk_offset;
                                    bool ok = false;
                                    if (ah == 0) {
                                        ok = decode_progressive_dc_first(br, ci, dc_tables[scan_comps[si].td], al, blk);
                                    } else {
                                        ok = decode_progressive_dc_refine(br, al, blk);
                                    }
                                    if (!ok) {
                                        err = "Corrupt interleaved progressive DC stream";
                                        return false;
                                    }
                                }
                            }
                        }
                    }

                    if (!handle_restart_if_needed(mcu_num < total_mcus)) {
                        err = "Missing or corrupt JPEG restart marker";
                        return false;
                    }
                }
            }
        }

        out_pos = br.pos;
        out_next_marker = br.pending_marker;
        return true;
    }

    std::shared_ptr<ImageData> finalize_image(std::string& err) {
        if (!has_sof || scan_count == 0) {
            err = "JPEG contained no image scans";
            return nullptr;
        }

        if (progressive) {
            for (int c = 0; c < num_components; ++c) {
                Component& comp = comps[c];
                if (!dqt_valid[comp.qt_id]) {
                    err = "Missing quantization table for progressive component";
                    return nullptr;
                }
                const size_t plane_size = static_cast<size_t>(comp.stride) * static_cast<size_t>(comp.height_px);
                comp.plane.assign(plane_size, 128);
                for (int by = 0; by < comp.blocks_y; ++by) {
                    for (int bx = 0; bx < comp.blocks_x; ++bx) {
                        const size_t blk_offset = (static_cast<size_t>(by) * static_cast<size_t>(comp.blocks_x) +
                                                   static_cast<size_t>(bx)) * 64u;
                        uint8_t* dst = comp.plane.data() +
                                       static_cast<size_t>(by * 8) * static_cast<size_t>(comp.stride) +
                                       static_cast<size_t>(bx * 8);
                        idct_8x8(comp.coefs.data() + blk_offset, dqt[comp.qt_id].data(),
                                 dst, static_cast<size_t>(comp.stride));
                    }
                }
            }
        }

        auto img = std::make_shared<ImageData>();
        img->width = width;
        img->height = height;
        img->pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

        // Determine color space per JFIF / Adobe APP14 conventions
        const bool is_rgb3 = (num_components == 3) && (
            (has_adobe && adobe_transform == 0) ||
            (!has_jfif && comps[0].id == 'R' && comps[1].id == 'G' && comps[2].id == 'B')
        );
        const bool is_ycck4 = (num_components == 4) && has_adobe && (adobe_transform == 2);

        for (int y = 0; y < height; ++y) {
            uint32_t* row = img->pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
            if (num_components == 1) {
                for (int x = 0; x < width; ++x) {
                    const uint8_t g = sample_component(comps[0], x, y, max_h, max_v, width, height);
                    row[x] = Color(g, g, g, 255).to_argb();
                }
            } else if (num_components == 3) {
                for (int x = 0; x < width; ++x) {
                    const uint8_t c0 = sample_component(comps[0], x, y, max_h, max_v, width, height);
                    const uint8_t c1 = sample_component(comps[1], x, y, max_h, max_v, width, height);
                    const uint8_t c2 = sample_component(comps[2], x, y, max_h, max_v, width, height);
                    if (is_rgb3) {
                        row[x] = Color(c0, c1, c2, 255).to_argb();
                    } else {
                        const double Y = static_cast<double>(c0);
                        const double Cb = static_cast<double>(c1) - 128.0;
                        const double Cr = static_cast<double>(c2) - 128.0;
                        const uint8_t r = clamp_u8(Y + 1.402 * Cr);
                        const uint8_t g = clamp_u8(Y - 0.344136 * Cb - 0.714136 * Cr);
                        const uint8_t b = clamp_u8(Y + 1.772 * Cb);
                        row[x] = Color(r, g, b, 255).to_argb();
                    }
                }
            } else {
                // 4 components: CMYK or YCCK (Adobe inverted channels)
                for (int x = 0; x < width; ++x) {
                    const uint8_t c0 = sample_component(comps[0], x, y, max_h, max_v, width, height);
                    const uint8_t c1 = sample_component(comps[1], x, y, max_h, max_v, width, height);
                    const uint8_t c2 = sample_component(comps[2], x, y, max_h, max_v, width, height);
                    const uint8_t k = sample_component(comps[3], x, y, max_h, max_v, width, height);
                    uint8_t c = c0;
                    uint8_t m = c1;
                    uint8_t yc = c2;
                    if (is_ycck4) {
                        const double Y = static_cast<double>(c0);
                        const double Cb = static_cast<double>(c1) - 128.0;
                        const double Cr = static_cast<double>(c2) - 128.0;
                        c = clamp_u8(255.0 - (Y + 1.402 * Cr));
                        m = clamp_u8(255.0 - (Y - 0.344136 * Cb - 0.714136 * Cr));
                        yc = clamp_u8(255.0 - (Y + 1.772 * Cb));
                    }
                    const uint8_t r = clamp_i32_u8((static_cast<int32_t>(c) * k + 127) / 255);
                    const uint8_t g = clamp_i32_u8((static_cast<int32_t>(m) * k + 127) / 255);
                    const uint8_t b = clamp_i32_u8((static_cast<int32_t>(yc) * k + 127) / 255);
                    row[x] = Color(r, g, b, 255).to_argb();
                }
            }
        }

        return img;
    }
};

} // namespace

bool JpegDecoder::is_jpeg(const uint8_t* data, size_t size) {
    return data != nullptr && size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

JpegDecodeResult JpegDecoder::decode_detailed(const uint8_t* data, size_t size) {
    JpegDecodeResult res;
    if (!is_jpeg(data, size)) {
        res.error = "Not a JPEG stream (missing SOI marker)";
        return res;
    }

    DecoderState state;
    size_t pos = 2;
    uint8_t pending_marker = 0;

    while (pos < size || pending_marker != 0) {
        uint8_t marker = 0;
        if (pending_marker != 0) {
            marker = pending_marker;
            pending_marker = 0;
        } else {
            // Find next 0xFF marker prefix
            while (pos < size && data[pos] != 0xFF) ++pos;
            if (pos >= size) break;
            while (pos < size && data[pos] == 0xFF) ++pos;
            if (pos >= size) break;
            marker = data[pos++];
            if (marker == 0x00) continue; // Stuffed byte outside scan
        }

        // Standalone markers without length field
        if (marker == 0xD9) {
            // EOI
            break;
        }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }

        // All other markers have a 16-bit big-endian length (including the 2 length bytes)
        if (pos + 2 > size) {
            res.error = "Truncated JPEG marker length";
            return res;
        }
        const uint16_t raw_len = read_be16(data + pos);
        if (raw_len < 2) {
            res.error = "Invalid JPEG marker length (< 2)";
            return res;
        }
        const size_t seg_len = static_cast<size_t>(raw_len) - 2u;
        pos += 2;
        if (pos + seg_len > size) {
            res.error = "Truncated JPEG marker segment";
            return res;
        }
        const size_t seg_start = pos;
        pos += seg_len;

        switch (marker) {
            case 0xDB: // DQT
                if (!state.parse_dqt(data + seg_start, seg_len, res.error)) return res;
                break;
            case 0xC4: // DHT
                if (!state.parse_dht(data + seg_start, seg_len, res.error)) return res;
                break;
            case 0xDD: // DRI
                if (seg_len != 2) {
                    res.error = "Invalid DRI segment length";
                    return res;
                }
                state.restart_interval = read_be16(data + seg_start);
                break;
            case 0xC0: // SOF0 Baseline DCT
            case 0xC1: // SOF1 Extended Sequential DCT
            case 0xC2: // SOF2 Progressive DCT
                if (!state.parse_sof(marker, data + seg_start, seg_len, res.error)) return res;
                break;
            case 0xC3: case 0xC5: case 0xC6: case 0xC7:
            case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
                res.error = "Unsupported JPEG coding mode (lossless/arithmetic/hierarchical)";
                return res;
            case 0xE0: // APP0 (JFIF)
                if (seg_len >= 5 && std::memcmp(data + seg_start, "JFIF\0", 5) == 0) {
                    state.has_jfif = true;
                }
                break;
            case 0xEE: // APP14 (Adobe)
                if (seg_len >= 12 && std::memcmp(data + seg_start, "Adobe", 5) == 0) {
                    state.has_adobe = true;
                    state.adobe_transform = data[seg_start + 11];
                }
                break;
            case 0xDA: { // SOS
                size_t after_scan = pos;
                uint8_t after_marker = 0;
                if (!state.parse_and_decode_sos(data, size, seg_start, seg_len,
                                                after_scan, after_marker, res.error)) {
                    return res;
                }
                pos = after_scan;
                pending_marker = after_marker;
                break;
            }
            default:
                // Skip APPn, COM, DNL, etc.
                break;
        }
    }

    res.image = state.finalize_image(res.error);
    if (res.image) {
        res.width = state.width;
        res.height = state.height;
        res.components = state.num_components;
        res.progressive = state.progressive;
    }
    return res;
}

std::shared_ptr<ImageData> JpegDecoder::decode(const uint8_t* data, size_t size) {
    auto res = decode_detailed(data, size);
    return res.ok() ? res.image : nullptr;
}

std::shared_ptr<ImageData> JpegDecoder::decode(const std::vector<uint8_t>& data) {
    return decode(data.data(), data.size());
}

} // namespace litebird
