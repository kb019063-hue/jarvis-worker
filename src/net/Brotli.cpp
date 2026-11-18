#include "litebird/net/Brotli.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace litebird {
namespace {

constexpr int kMaxHuffmanBits = 15;
constexpr int kCodeLengthAlphabetSize = 18;
constexpr int kNumBlockLengthCodes = 26;
constexpr int kNumCommandCodes = 704;
constexpr std::size_t kMaxDistanceAlphabetSize = 520;

constexpr std::array<int, kCodeLengthAlphabetSize> kCodeLengthCodeOrder = {
    1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

constexpr std::array<uint8_t, 16> kCodeLengthPrefixLength = {
    2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4
};

constexpr std::array<uint8_t, 16> kCodeLengthPrefixValue = {
    0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5
};

constexpr std::array<uint32_t, kNumBlockLengthCodes> kBlockLenBase = {
    1, 5, 9, 13, 17, 25, 33, 41,
    49, 65, 81, 97, 113, 145, 177, 209,
    241, 305, 369, 497, 753, 1265, 2289, 4337,
    8433, 16625
};

constexpr std::array<uint8_t, kNumBlockLengthCodes> kBlockLenExtraBits = {
    2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5,
    6, 6, 7, 8, 9, 10, 11, 12,
    13, 24
};

constexpr std::array<uint32_t, 24> kInsertLenBase = {
    0, 1, 2, 3, 4, 5, 6, 8,
    10, 14, 18, 26, 34, 50, 66, 98,
    130, 194, 322, 578, 1090, 2114, 6210, 22594
};

constexpr std::array<uint8_t, 24> kInsertLenExtraBits = {
    0, 0, 0, 0, 0, 0, 1, 1,
    2, 2, 3, 3, 4, 4, 5, 5,
    6, 7, 8, 9, 10, 12, 14, 24
};

constexpr std::array<uint32_t, 24> kCopyLenBase = {
    2, 3, 4, 5, 6, 7, 8, 9,
    10, 12, 14, 18, 22, 30, 38, 54,
    70, 102, 134, 198, 326, 582, 1094, 2118
};

constexpr std::array<uint8_t, 24> kCopyLenExtraBits = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 2, 2, 3, 3, 4, 4,
    5, 5, 6, 7, 8, 9, 10, 24
};

constexpr std::array<uint8_t, 11> kCmdInsertOffset = {
    0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16
};

constexpr std::array<uint8_t, 11> kCmdCopyOffset = {
    0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16
};

constexpr std::array<int8_t, 16> kDistShortBaseIndex = {
    0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1
};

constexpr std::array<int8_t, 16> kDistShortDelta = {
    0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
};

struct BitReader {
    const uint8_t* data{nullptr};
    std::size_t size{0};
    std::size_t byte_pos{0};
    uint64_t bit_buf{0};
    int bit_count{0};

    bool ensure_bits(int n) {
        while (bit_count < n) {
            if (byte_pos >= size) return false;
            bit_buf |= static_cast<uint64_t>(data[byte_pos++]) << bit_count;
            bit_count += 8;
        }
        return true;
    }

    bool read_bits(int n, uint32_t& out) {
        if (n == 0) {
            out = 0;
            return true;
        }
        if (!ensure_bits(n)) return false;
        out = static_cast<uint32_t>(bit_buf & ((1ULL << n) - 1ULL));
        bit_buf >>= n;
        bit_count -= n;
        return true;
    }

    std::size_t current_byte_offset() const {
        return byte_pos - static_cast<std::size_t>(bit_count / 8);
    }

    bool align_to_byte_zero() {
        const int discard = bit_count & 7;
        if (discard > 0) {
            const uint64_t mask = (1ULL << discard) - 1ULL;
            if ((bit_buf & mask) != 0) return false;
            bit_buf >>= discard;
            bit_count -= discard;
        }
        return true;
    }

    bool read_aligned_bytes(std::size_t count, const uint8_t*& ptr) {
        if (!align_to_byte_zero()) return false;
        const std::size_t cur = current_byte_offset();
        if (count > size - cur) return false;
        ptr = data + cur;
        byte_pos = cur + count;
        bit_buf = 0;
        bit_count = 0;
        return true;
    }
};

struct PrefixTree {
    bool is_single{false};
    uint16_t single_sym{0};
    std::array<int, kMaxHuffmanBits + 1> counts{};
    std::vector<uint16_t> symbols;
    std::array<uint16_t, 256> fast{};

    bool build(const uint8_t* lengths, std::size_t num_symbols, bool allow_single_nonzero_incomplete) {
        counts.fill(0);
        fast.fill(0);
        is_single = false;
        single_sym = 0;

        std::size_t non_zero = 0;
        uint16_t only_sym = 0;
        for (std::size_t i = 0; i < num_symbols; ++i) {
            const int len = lengths[i];
            if (len < 0 || len > kMaxHuffmanBits) return false;
            if (len > 0) {
                ++non_zero;
                only_sym = static_cast<uint16_t>(i);
                ++counts[len];
            }
        }
        counts[0] = 0;

        if (non_zero == 0) return false;
        if (non_zero == 1) {
            if (!allow_single_nonzero_incomplete) return false;
            is_single = true;
            single_sym = only_sym;
            return true;
        }

        int left = 1;
        for (int bits = 1; bits <= kMaxHuffmanBits; ++bits) {
            left <<= 1;
            left -= counts[bits];
            if (left < 0) return false;
        }
        if (left != 0) return false;

        symbols.resize(non_zero);
        std::array<std::size_t, kMaxHuffmanBits + 1> offsets{};
        std::size_t sum = 0;
        for (int bits = 1; bits <= kMaxHuffmanBits; ++bits) {
            offsets[bits] = sum;
            sum += static_cast<std::size_t>(counts[bits]);
        }
        for (std::size_t sym = 0; sym < num_symbols; ++sym) {
            const int len = lengths[sym];
            if (len != 0) {
                symbols[offsets[len]++] = static_cast<uint16_t>(sym);
            }
        }

        // Populate an 8-bit LSB lookup cache for codes of length 1..8.
        int code = 0;
        std::size_t sym_pos = 0;
        for (int len = 1; len <= 8; ++len) {
            const int count = counts[len];
            for (int c = 0; c < count; ++c) {
                const uint16_t sym = symbols[sym_pos++];
                uint32_t rev = 0;
                for (int b = 0; b < len; ++b) {
                    rev = (rev << 1) | ((static_cast<uint32_t>(code) >> b) & 1U);
                }
                const uint32_t step = 1U << len;
                const uint16_t packed = static_cast<uint16_t>((static_cast<uint32_t>(len) << 12) | sym);
                for (uint32_t idx = rev; idx < 256; idx += step) {
                    fast[idx] = packed;
                }
                ++code;
            }
            code <<= 1;
        }
        return true;
    }

    bool decode(BitReader& br, uint16_t& out_sym) const {
        if (is_single) {
            out_sym = single_sym;
            return true;
        }
        if (br.bit_count >= 8 || br.ensure_bits(8)) {
            const uint16_t packed = fast[br.bit_buf & 0xffU];
            if (packed != 0) {
                const int len = packed >> 12;
                br.bit_buf >>= len;
                br.bit_count -= len;
                out_sym = static_cast<uint16_t>(packed & 0x0fffU);
                return true;
            }
        }

        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= kMaxHuffmanBits; ++len) {
            if (!br.ensure_bits(1)) return false;
            code |= static_cast<int>(br.bit_buf & 1ULL);
            br.bit_buf >>= 1;
            --br.bit_count;
            const int count = counts[len];
            if (code - count < first) {
                out_sym = symbols[static_cast<std::size_t>(index + (code - first))];
                return true;
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }
};

int bit_width_u32(std::size_t value) {
    int bits = 0;
    while (value > 0) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

bool read_window_bits(BitReader& br, int& wbits_out) {
    uint32_t b0 = 0;
    if (!br.read_bits(1, b0)) return false;
    if (b0 == 0) {
        wbits_out = 16;
        return true;
    }
    uint32_t n = 0;
    if (!br.read_bits(3, n)) return false;
    if (n != 0) {
        wbits_out = 17 + static_cast<int>(n);
        return true;
    }
    uint32_t m = 0;
    if (!br.read_bits(3, m)) return false;
    if (m == 0) {
        wbits_out = 17;
        return true;
    }
    if (m == 1) return false;
    wbits_out = 8 + static_cast<int>(m);
    return true;
}

bool read_var_block_count(BitReader& br, std::size_t& count_out) {
    uint32_t present = 0;
    if (!br.read_bits(1, present)) return false;
    if (present == 0) {
        count_out = 1;
        return true;
    }
    uint32_t nbits = 0;
    if (!br.read_bits(3, nbits)) return false;
    if (nbits == 0) {
        count_out = 2;
        return true;
    }
    uint32_t extra = 0;
    if (!br.read_bits(static_cast<int>(nbits), extra)) return false;
    count_out = (1ULL << nbits) + static_cast<std::size_t>(extra) + 1ULL;
    return true;
}

bool read_prefix_tree(BitReader& br, std::size_t alphabet_size, PrefixTree& tree_out) {
    if (alphabet_size < 2) return false;
    uint32_t hskip = 0;
    if (!br.read_bits(2, hskip)) return false;

    if (hskip == 1) {
        uint32_t nsym_m1 = 0;
        if (!br.read_bits(2, nsym_m1)) return false;
        const std::size_t nsym = static_cast<std::size_t>(nsym_m1) + 1;
        const int abits = bit_width_u32(alphabet_size - 1);

        std::array<uint16_t, 4> syms{};
        for (std::size_t i = 0; i < nsym; ++i) {
            uint32_t sym = 0;
            if (!br.read_bits(abits, sym)) return false;
            if (sym >= alphabet_size) return false;
            for (std::size_t j = 0; j < i; ++j) {
                if (syms[j] == sym) return false;
            }
            syms[i] = static_cast<uint16_t>(sym);
        }

        if (nsym == 1) {
            std::vector<uint8_t> lengths(alphabet_size, 0);
            lengths[syms[0]] = 1;
            return tree_out.build(lengths.data(), alphabet_size, /*allow_single=*/true);
        }

        std::vector<uint8_t> lengths(alphabet_size, 0);
        if (nsym == 2) {
            lengths[syms[0]] = 1;
            lengths[syms[1]] = 1;
        } else if (nsym == 3) {
            lengths[syms[0]] = 1;
            lengths[syms[1]] = 2;
            lengths[syms[2]] = 2;
        } else {
            uint32_t tree_select = 0;
            if (!br.read_bits(1, tree_select)) return false;
            if (tree_select == 0) {
                lengths[syms[0]] = 2;
                lengths[syms[1]] = 2;
                lengths[syms[2]] = 2;
                lengths[syms[3]] = 2;
            } else {
                lengths[syms[0]] = 1;
                lengths[syms[1]] = 2;
                lengths[syms[2]] = 3;
                lengths[syms[3]] = 3;
            }
        }
        return tree_out.build(lengths.data(), alphabet_size, /*allow_single=*/false);
    }

    // Complex prefix code: first decode the 18-symbol code-length tree.
    std::array<uint8_t, kCodeLengthAlphabetSize> cl_lengths{};
    int cl_space = 32;
    std::size_t cl_nonzero = 0;
    for (std::size_t i = hskip; i < kCodeLengthAlphabetSize && cl_space > 0; ++i) {
        if (!br.ensure_bits(1)) return false;
        // Peek up to 4 bits without requiring 4 bytes remaining when a 2-bit code suffices.
        if (br.bit_count < 4) {
            while (br.bit_count < 4 && br.byte_pos < br.size) {
                br.bit_buf |= static_cast<uint64_t>(br.data[br.byte_pos++]) << br.bit_count;
                br.bit_count += 8;
            }
        }
        const uint32_t peek = static_cast<uint32_t>(br.bit_buf & 0x0fU);
        const int need = kCodeLengthPrefixLength[peek];
        if (br.bit_count < need) return false;
        br.bit_buf >>= need;
        br.bit_count -= need;
        const uint8_t v = kCodeLengthPrefixValue[peek];
        cl_lengths[kCodeLengthCodeOrder[i]] = v;
        if (v != 0) {
            cl_space -= (32 >> v);
            ++cl_nonzero;
        }
    }

    if (cl_nonzero == 0) return false;
    if (cl_nonzero >= 2 && cl_space != 0) return false;

    PrefixTree cl_tree;
    if (!cl_tree.build(cl_lengths.data(), kCodeLengthAlphabetSize, /*allow_single=*/true)) {
        return false;
    }

    std::vector<uint8_t> lengths(alphabet_size, 0);
    int space = 32768;
    std::size_t sym_idx = 0;
    uint8_t prev_code_len = 8;
    uint16_t prev_sym = 0;
    std::size_t repeat = 0;

    while (sym_idx < alphabet_size && space > 0) {
        uint16_t code = 0;
        if (!cl_tree.decode(br, code)) return false;

        if (code < 16) {
            repeat = 0;
            prev_sym = code;
            const uint8_t len = static_cast<uint8_t>(code);
            lengths[sym_idx++] = len;
            if (len != 0) {
                const int weight = 32768 >> len;
                if (weight > space) return false;
                space -= weight;
                prev_code_len = len;
            }
        } else if (code == 16) {
            if (prev_sym != 16) repeat = 0;
            prev_sym = 16;
            if (repeat > alphabet_size + 2) return false;
            uint32_t extra = 0;
            if (!br.read_bits(2, extra)) return false;
            const std::size_t new_repeat = (repeat > 0 ? ((repeat - 2) << 2) : 0) + 3 + extra;
            const std::size_t add = new_repeat - repeat;
            repeat = new_repeat;
            if (add > alphabet_size - sym_idx) return false;
            const int weight = 32768 >> prev_code_len;
            if (add > static_cast<std::size_t>(space / weight)) return false;
            for (std::size_t r = 0; r < add; ++r) {
                lengths[sym_idx++] = prev_code_len;
            }
            space -= static_cast<int>(add) * weight;
        } else if (code == 17) {
            if (prev_sym != 17) repeat = 0;
            prev_sym = 17;
            if (repeat > alphabet_size + 2) return false;
            uint32_t extra = 0;
            if (!br.read_bits(3, extra)) return false;
            const std::size_t new_repeat = (repeat > 0 ? ((repeat - 2) << 3) : 0) + 3 + extra;
            const std::size_t add = new_repeat - repeat;
            repeat = new_repeat;
            if (add > alphabet_size - sym_idx) return false;
            sym_idx += add; // zeros already initialized
        } else {
            return false;
        }
    }

    if (space != 0) return false;
    return tree_out.build(lengths.data(), alphabet_size, /*allow_single=*/false);
}

bool read_context_map(
    BitReader& br,
    std::size_t map_size,
    std::size_t num_trees,
    std::vector<uint8_t>& map_out
) {
    map_out.assign(map_size, 0);
    if (num_trees < 2) {
        return true;
    }

    uint32_t use_rle = 0;
    if (!br.read_bits(1, use_rle)) return false;
    std::size_t rle_max = 0;
    if (use_rle != 0) {
        uint32_t rle_bits = 0;
        if (!br.read_bits(4, rle_bits)) return false;
        rle_max = static_cast<std::size_t>(rle_bits) + 1;
    }

    PrefixTree cmap_tree;
    if (!read_prefix_tree(br, num_trees + rle_max, cmap_tree)) return false;

    std::size_t i = 0;
    while (i < map_size) {
        uint16_t sym = 0;
        if (!cmap_tree.decode(br, sym)) return false;
        if (sym == 0) {
            map_out[i++] = 0;
        } else if (sym <= rle_max) {
            uint32_t extra = 0;
            if (!br.read_bits(sym, extra)) return false;
            const std::size_t reps = (1ULL << sym) + static_cast<std::size_t>(extra);
            if (reps > map_size - i) return false;
            i += reps; // already zero
        } else {
            const std::size_t tree_id = static_cast<std::size_t>(sym) - rle_max;
            if (tree_id >= num_trees) return false;
            map_out[i++] = static_cast<uint8_t>(tree_id);
        }
    }

    uint32_t imtf = 0;
    if (!br.read_bits(1, imtf)) return false;
    if (imtf != 0) {
        std::array<uint8_t, 256> mtf{};
        for (std::size_t k = 0; k < 256; ++k) mtf[k] = static_cast<uint8_t>(k);
        for (std::size_t k = 0; k < map_size; ++k) {
            const uint8_t idx = map_out[k];
            const uint8_t val = mtf[idx];
            map_out[k] = val;
            if (idx != 0) {
                std::memmove(mtf.data() + 1, mtf.data(), idx);
                mtf[0] = val;
            }
        }
    }
    return true;
}

std::size_t apply_dictionary_transform(
    uint8_t* dst,
    const uint8_t* word,
    std::size_t word_len,
    std::size_t transform_id
) {
    const detail::BrotliTransform& tr = detail::kBrotliTransforms[transform_id];
    std::size_t out_len = 0;

    const uint8_t pref_len = detail::kBrotliPrefixSuffixLen[tr.prefix_id];
    if (pref_len > 0) {
        std::memcpy(dst, detail::kBrotliPrefixSuffix[tr.prefix_id], pref_len);
        out_len += pref_len;
    }

    std::size_t skip_front = 0;
    std::size_t body_len = word_len;
    if (tr.type >= 1 && tr.type <= 9) {
        const std::size_t omit = tr.type;
        body_len = (body_len > omit) ? (body_len - omit) : 0;
    } else if (tr.type >= 12 && tr.type <= 20) {
        const std::size_t omit = static_cast<std::size_t>(tr.type) - 11;
        if (body_len > omit) {
            skip_front = omit;
            body_len -= omit;
        } else {
            body_len = 0;
        }
    }

    uint8_t* body_dst = dst + out_len;
    if (body_len > 0) {
        std::memcpy(body_dst, word + skip_front, body_len);
        out_len += body_len;

        auto to_upper_step = [](uint8_t* p, std::size_t rem) -> std::size_t {
            if (p[0] < 192) {
                if (p[0] >= 'a' && p[0] <= 'z') p[0] ^= 32;
                return 1;
            }
            if (p[0] < 224) {
                if (rem >= 2) p[1] ^= 32;
                return 2;
            }
            if (rem >= 3) p[2] ^= 5;
            return 3;
        };

        if (tr.type == 10) { // UPPERCASE_FIRST
            (void)to_upper_step(body_dst, body_len);
        } else if (tr.type == 11) { // UPPERCASE_ALL
            std::size_t pos = 0;
            while (pos < body_len) {
                pos += to_upper_step(body_dst + pos, body_len - pos);
            }
        }
    }

    const uint8_t suff_len = detail::kBrotliPrefixSuffixLen[tr.suffix_id];
    if (suff_len > 0) {
        std::memcpy(dst + out_len, detail::kBrotliPrefixSuffix[tr.suffix_id], suff_len);
        out_len += suff_len;
    }
    return out_len;
}

} // namespace

bool Brotli::decompress(
    const uint8_t* data,
    std::size_t size,
    std::vector<uint8_t>& out,
    std::size_t max_output_bytes,
    std::size_t* bytes_consumed
) {
    out.clear();
    if (bytes_consumed) *bytes_consumed = 0;
    if (!data || size == 0) return false;

    BitReader br;
    br.data = data;
    br.size = size;

    int wbits = 0;
    if (!read_window_bits(br, wbits)) return false;
    const std::size_t window_size = (1ULL << wbits) - 16ULL;

    std::array<int, 4> dist_rb = {16, 15, 11, 4};
    std::size_t dist_rb_idx = 3;

    bool is_last = false;
    while (!is_last) {
        uint32_t last_bit = 0;
        if (!br.read_bits(1, last_bit)) return false;
        is_last = (last_bit != 0);

        if (is_last) {
            uint32_t last_empty = 0;
            if (!br.read_bits(1, last_empty)) return false;
            if (last_empty != 0) {
                if (!br.align_to_byte_zero()) return false;
                break;
            }
        }

        uint32_t mnibbles_code = 0;
        if (!br.read_bits(2, mnibbles_code)) return false;
        if (mnibbles_code == 3) {
            // Metadata block
            uint32_t reserved = 0;
            if (!br.read_bits(1, reserved) || reserved != 0) return false;
            uint32_t mskipbytes = 0;
            if (!br.read_bits(2, mskipbytes)) return false;
            if (mskipbytes == 0) {
                if (!br.align_to_byte_zero()) return false;
                continue;
            }
            uint32_t raw_skip = 0;
            if (!br.read_bits(static_cast<int>(mskipbytes) * 8, raw_skip)) return false;
            if (mskipbytes > 1 && (raw_skip >> (8 * (mskipbytes - 1))) == 0) return false;
            const std::size_t skip_len = static_cast<std::size_t>(raw_skip) + 1;
            const uint8_t* ignored = nullptr;
            if (!br.read_aligned_bytes(skip_len, ignored)) return false;
            continue;
        }

        const int mnibbles = 4 + static_cast<int>(mnibbles_code);
        uint32_t mlen_m1 = 0;
        if (!br.read_bits(mnibbles * 4, mlen_m1)) return false;
        if (mnibbles > 4 && (mlen_m1 >> (4 * (mnibbles - 1))) == 0) return false;
        const std::size_t mlen = static_cast<std::size_t>(mlen_m1) + 1;

        if (mlen > max_output_bytes - out.size()) return false;

        if (!is_last) {
            uint32_t is_uncompressed = 0;
            if (!br.read_bits(1, is_uncompressed)) return false;
            if (is_uncompressed != 0) {
                const uint8_t* raw_ptr = nullptr;
                if (!br.read_aligned_bytes(mlen, raw_ptr)) return false;
                out.insert(out.end(), raw_ptr, raw_ptr + mlen);
                continue;
            }
        }

        // Compressed meta-block header
        std::array<std::size_t, 3> nbltypes = {1, 1, 1};
        std::array<PrefixTree, 3> bt_trees;
        std::array<PrefixTree, 3> bl_trees;
        std::array<std::size_t, 3> btype = {0, 0, 0};
        std::array<std::size_t, 3> prev_btype = {1, 1, 1};
        std::array<std::size_t, 3> blen = {1ULL << 24, 1ULL << 24, 1ULL << 24};

        auto decode_block_length = [&](int cat, std::size_t& out_len) -> bool {
            uint16_t sym = 0;
            if (!bl_trees[cat].decode(br, sym)) return false;
            if (sym >= kNumBlockLengthCodes) return false;
            uint32_t extra = 0;
            if (!br.read_bits(kBlockLenExtraBits[sym], extra)) return false;
            out_len = kBlockLenBase[sym] + extra;
            return true;
        };

        for (int cat = 0; cat < 3; ++cat) {
            if (!read_var_block_count(br, nbltypes[cat])) return false;
            if (nbltypes[cat] >= 2) {
                if (!read_prefix_tree(br, nbltypes[cat] + 2, bt_trees[cat])) return false;
                if (!read_prefix_tree(br, kNumBlockLengthCodes, bl_trees[cat])) return false;
                if (!decode_block_length(cat, blen[cat])) return false;
            }
        }

        uint32_t npostfix = 0;
        uint32_t ndirect_raw = 0;
        if (!br.read_bits(2, npostfix)) return false;
        if (!br.read_bits(4, ndirect_raw)) return false;
        const std::size_t ndirect = static_cast<std::size_t>(ndirect_raw) << npostfix;

        std::vector<uint8_t> lit_ctx_modes(nbltypes[0], 0);
        for (std::size_t i = 0; i < nbltypes[0]; ++i) {
            uint32_t mode = 0;
            if (!br.read_bits(2, mode)) return false;
            lit_ctx_modes[i] = static_cast<uint8_t>(mode);
        }

        std::size_t ntrees_lit = 1;
        std::size_t ntrees_dist = 1;
        std::vector<uint8_t> cmap_lit;
        std::vector<uint8_t> cmap_dist;

        if (!read_var_block_count(br, ntrees_lit)) return false;
        if (!read_context_map(br, nbltypes[0] * 64, ntrees_lit, cmap_lit)) return false;

        if (!read_var_block_count(br, ntrees_dist)) return false;
        if (!read_context_map(br, nbltypes[2] * 4, ntrees_dist, cmap_dist)) return false;

        std::vector<PrefixTree> lit_trees(ntrees_lit);
        for (std::size_t i = 0; i < ntrees_lit; ++i) {
            if (!read_prefix_tree(br, 256, lit_trees[i])) return false;
        }

        std::vector<PrefixTree> iac_trees(nbltypes[1]);
        for (std::size_t i = 0; i < nbltypes[1]; ++i) {
            if (!read_prefix_tree(br, kNumCommandCodes, iac_trees[i])) return false;
        }

        const std::size_t dist_alphabet = 16 + ndirect + (48ULL << npostfix);
        if (dist_alphabet > kMaxDistanceAlphabetSize) return false;
        std::vector<PrefixTree> dist_trees(ntrees_dist);
        for (std::size_t i = 0; i < ntrees_dist; ++i) {
            if (!read_prefix_tree(br, dist_alphabet, dist_trees[i])) return false;
        }

        auto maybe_switch_block = [&](int cat) -> bool {
            if (blen[cat] > 0 || nbltypes[cat] < 2) return true;
            uint16_t bcode = 0;
            if (!bt_trees[cat].decode(br, bcode)) return false;
            std::size_t new_type = 0;
            if (bcode == 0) {
                new_type = prev_btype[cat];
            } else if (bcode == 1) {
                new_type = (btype[cat] + 1) % nbltypes[cat];
            } else {
                new_type = static_cast<std::size_t>(bcode - 2);
                if (new_type >= nbltypes[cat]) return false;
            }
            prev_btype[cat] = btype[cat];
            btype[cat] = new_type;
            return decode_block_length(cat, blen[cat]);
        };

        std::size_t meta_remaining = mlen;
        const std::size_t hint_reserve = std::min<std::size_t>(
            out.size() + mlen,
            out.size() + std::max<std::size_t>(size * 8, 4096)
        );
        if (out.capacity() < hint_reserve) out.reserve(hint_reserve);

        while (meta_remaining > 0) {
            if (!maybe_switch_block(1)) return false;
            --blen[1];
            uint16_t cmd = 0;
            if (!iac_trees[btype[1]].decode(br, cmd)) return false;

            const std::size_t block_idx = cmd >> 6;
            if (block_idx >= 11) return false;
            const bool implicit_dist_zero = (cmd < 128);
            const std::size_t insert_code = kCmdInsertOffset[block_idx] + ((cmd >> 3) & 7U);
            const std::size_t copy_code = kCmdCopyOffset[block_idx] + (cmd & 7U);

            uint32_t insert_extra = 0;
            if (!br.read_bits(kInsertLenExtraBits[insert_code], insert_extra)) return false;
            const std::size_t insert_len = kInsertLenBase[insert_code] + insert_extra;

            uint32_t copy_extra = 0;
            if (!br.read_bits(kCopyLenExtraBits[copy_code], copy_extra)) return false;
            const std::size_t copy_len = kCopyLenBase[copy_code] + copy_extra;

            if (insert_len > meta_remaining) return false;
            if (insert_len > max_output_bytes - out.size()) return false;

            for (std::size_t i = 0; i < insert_len; ++i) {
                if (!maybe_switch_block(0)) return false;
                --blen[0];
                const uint8_t p1 = out.empty() ? 0 : out.back();
                const uint8_t p2 = (out.size() >= 2) ? out[out.size() - 2] : 0;
                const std::size_t ctx_base = static_cast<std::size_t>(lit_ctx_modes[btype[0]]) * 512ULL;
                const std::size_t ctx =
                    detail::kBrotliContextLookup[ctx_base + p1] |
                    detail::kBrotliContextLookup[ctx_base + 256 + p2];
                const std::size_t tree_id = cmap_lit[btype[0] * 64 + ctx];
                uint16_t lit = 0;
                if (!lit_trees[tree_id].decode(br, lit) || lit >= 256) return false;
                out.push_back(static_cast<uint8_t>(lit));
            }
            meta_remaining -= insert_len;
            if (meta_remaining == 0) break;

            uint64_t distance = 0;
            uint16_t dist_code = 0;
            if (implicit_dist_zero) {
                distance = static_cast<uint64_t>(dist_rb[dist_rb_idx]);
            } else {
                if (!maybe_switch_block(2)) return false;
                --blen[2];
                const std::size_t copy_ctx = std::min<std::size_t>(copy_len >= 2 ? copy_len - 2 : 0, 3);
                const std::size_t dist_tree_id = cmap_dist[btype[2] * 4 + copy_ctx];
                if (!dist_trees[dist_tree_id].decode(br, dist_code)) return false;

                if (dist_code < 16) {
                    const int base_idx = kDistShortBaseIndex[dist_code];
                    const int rb_val = dist_rb[(dist_rb_idx - static_cast<std::size_t>(base_idx)) & 3U];
                    const int signed_dist = rb_val + kDistShortDelta[dist_code];
                    if (signed_dist <= 0) return false;
                    distance = static_cast<uint64_t>(signed_dist);
                } else if (dist_code < 16 + ndirect) {
                    distance = static_cast<uint64_t>(dist_code - 15);
                } else {
                    const std::size_t dcode = static_cast<std::size_t>(dist_code) - 16 - ndirect;
                    const std::size_t hcode = dcode >> npostfix;
                    const std::size_t lcode = dcode & ((1ULL << npostfix) - 1ULL);
                    const int nbits = 1 + static_cast<int>(hcode >> 1);
                    if (nbits > 24) return false;
                    const uint64_t offset = ((2ULL + (hcode & 1ULL)) << nbits) - 4ULL;
                    uint32_t extra = 0;
                    if (!br.read_bits(nbits, extra)) return false;
                    distance = ((offset + extra) << npostfix) + lcode + ndirect + 1ULL;
                }
            }

            if (distance == 0) return false;
            const std::size_t max_backward = std::min<std::size_t>(out.size(), window_size);
            if (distance <= max_backward) {
                if (copy_len > meta_remaining) return false;
                if (copy_len > max_output_bytes - out.size()) return false;
                const std::size_t src_base = out.size() - static_cast<std::size_t>(distance);
                for (std::size_t i = 0; i < copy_len; ++i) {
                    out.push_back(out[src_base + i]);
                }
                meta_remaining -= copy_len;
                if (!implicit_dist_zero && dist_code != 0) {
                    dist_rb_idx = (dist_rb_idx + 1) & 3U;
                    dist_rb[dist_rb_idx] = static_cast<int>(distance);
                }
            } else {
                if (copy_len < 4 || copy_len > detail::kBrotliMaxWordLength) return false;
                const uint8_t ndbits = detail::kBrotliDictSizeBitsByLength[copy_len];
                if (ndbits == 0) return false;
                const uint64_t word_id = distance - max_backward - 1ULL;
                const uint64_t word_index = word_id & ((1ULL << ndbits) - 1ULL);
                const uint64_t transform_id = word_id >> ndbits;
                if (transform_id >= detail::kBrotliNumTransforms) return false;

                const std::size_t dict_offset =
                    static_cast<std::size_t>(detail::kBrotliDictOffsetsByLength[copy_len]) +
                    static_cast<std::size_t>(word_index) * copy_len;
                if (dict_offset + copy_len > detail::kBrotliDictionarySize) return false;

                uint8_t transformed[96];
                const std::size_t out_len = apply_dictionary_transform(
                    transformed,
                    detail::kBrotliDictionaryData + dict_offset,
                    copy_len,
                    static_cast<std::size_t>(transform_id)
                );
                if (out_len > meta_remaining) return false;
                if (out_len > max_output_bytes - out.size()) return false;
                out.insert(out.end(), transformed, transformed + out_len);
                meta_remaining -= out_len;
            }
        }
    }

    if (!br.align_to_byte_zero()) return false;
    if (bytes_consumed) *bytes_consumed = br.current_byte_offset();
    return true;
}

} // namespace litebird
