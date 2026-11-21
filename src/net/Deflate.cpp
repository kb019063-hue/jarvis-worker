#include "litebird/net/Deflate.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace litebird {
namespace {

constexpr int kMaxBits = 15;
constexpr int kMaxLitLenSymbols = 288;
constexpr int kMaxDistSymbols = 32;

constexpr std::array<int, 29> kLenBase = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
constexpr std::array<int, 29> kLenExtra = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
constexpr std::array<int, 30> kDistBase = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
constexpr std::array<int, 30> kDistExtra = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
constexpr std::array<int, 19> kCodeLenOrder = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

struct BitReader {
    const uint8_t* data{nullptr};
    std::size_t size{0};
    const uint8_t* tail{nullptr};
    std::size_t tail_size{0};
    std::size_t byte_pos{0};
    uint64_t bit_buf{0};
    int bit_count{0};

    std::size_t total_size() const { return size + tail_size; }

    uint8_t byte_at(std::size_t pos) const {
        return pos < size ? data[pos] : tail[pos - size];
    }

    bool ensure_bits(int n) {
        const std::size_t total = total_size();
        while (bit_count < n) {
            if (byte_pos >= total) return false;
            bit_buf |= static_cast<uint64_t>(byte_at(byte_pos++)) << bit_count;
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

    void align_to_byte() {
        const int discard = bit_count & 7;
        bit_buf >>= discard;
        bit_count -= discard;
    }

    std::size_t current_byte_offset() const {
        return byte_pos - static_cast<std::size_t>(bit_count / 8);
    }

    bool at_end() const {
        return byte_pos == total_size() && bit_count == 0;
    }

    bool copy_aligned_bytes(std::size_t count, uint8_t* dst) {
        align_to_byte();
        const std::size_t cur = current_byte_offset();
        const std::size_t total = total_size();
        if (count > total - cur) return false;
        if (cur + count <= size) {
            if (count > 0) std::memcpy(dst, data + cur, count);
        } else if (cur >= size) {
            if (count > 0) std::memcpy(dst, tail + (cur - size), count);
        } else {
            const std::size_t first = size - cur;
            std::memcpy(dst, data + cur, first);
            std::memcpy(dst + first, tail, count - first);
        }
        byte_pos = cur + count;
        bit_buf = 0;
        bit_count = 0;
        return true;
    }
};

struct BitWriter {
    std::vector<uint8_t>& out;
    uint64_t bit_buf{0};
    int bit_count{0};

    void write_bits(uint32_t value, int count) {
        bit_buf |= static_cast<uint64_t>(value) << bit_count;
        bit_count += count;
        while (bit_count >= 8) {
            out.push_back(static_cast<uint8_t>(bit_buf & 0xFFu));
            bit_buf >>= 8;
            bit_count -= 8;
        }
    }

    void write_huffman(uint32_t msb_code, int bit_len) {
        uint32_t rev = 0;
        for (int i = 0; i < bit_len; ++i) {
            rev = (rev << 1) | ((msb_code >> i) & 1u);
        }
        write_bits(rev, bit_len);
    }

    void write_fixed_litlen(int sym) {
        if (sym <= 143) {
            write_huffman(static_cast<uint32_t>(sym + 48), 8);
        } else if (sym <= 255) {
            write_huffman(static_cast<uint32_t>((sym - 144) + 400), 9);
        } else if (sym <= 279) {
            write_huffman(static_cast<uint32_t>(sym - 256), 7);
        } else {
            write_huffman(static_cast<uint32_t>((sym - 280) + 192), 8);
        }
    }

    void flush_to_byte() {
        if (bit_count > 0) {
            out.push_back(static_cast<uint8_t>(bit_buf & 0xFFu));
            bit_buf = 0;
            bit_count = 0;
        }
    }
};

int clamp_window_bits(int bits) {
    if (bits < 8) return 8;
    if (bits > 15) return 15;
    return bits;
}

void append_history(std::vector<uint8_t>& history,
                    std::size_t window_size,
                    const uint8_t* data,
                    std::size_t size) {
    if (size == 0 || !data) return;
    if (size >= window_size) {
        history.assign(data + (size - window_size), data + size);
        return;
    }
    const std::size_t total = history.size() + size;
    if (total > window_size) {
        const std::size_t drop = total - window_size;
        history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    history.insert(history.end(), data, data + size);
}

struct HuffmanTable {
    std::array<int, kMaxBits + 1> counts{};
    std::array<int, kMaxLitLenSymbols> symbols{};
    bool empty_tree{false};

    bool build(const int* lengths, int num_symbols) {
        counts.fill(0);
        symbols.fill(0);
        empty_tree = false;

        int non_zero = 0;
        for (int i = 0; i < num_symbols; ++i) {
            if (lengths[i] < 0 || lengths[i] > kMaxBits) return false;
            if (lengths[i] > 0) ++non_zero;
            ++counts[lengths[i]];
        }
        counts[0] = 0;

        if (non_zero == 0) {
            empty_tree = true;
            return true;
        }

        // Check oversubscription across code lengths.
        int left = 1;
        for (int bits = 1; bits <= kMaxBits; ++bits) {
            left <<= 1;
            left -= counts[bits];
            if (left < 0) return false;
        }
        // RFC 1951 requires a complete prefix code unless there is only a
        // single symbol of length 1 (common in degenerate distance trees).
        if (left > 0 && !(non_zero == 1 && counts[1] == 1)) {
            return false;
        }

        std::array<int, kMaxBits + 1> offsets{};
        int sum = 0;
        for (int bits = 1; bits <= kMaxBits; ++bits) {
            offsets[bits] = sum;
            sum += counts[bits];
        }
        for (int sym = 0; sym < num_symbols; ++sym) {
            const int len = lengths[sym];
            if (len != 0) {
                symbols[offsets[len]++] = sym;
            }
        }
        return true;
    }

    bool decode(BitReader& br, int& out_symbol) const {
        if (empty_tree) return false;
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            uint32_t bit = 0;
            if (!br.read_bits(1, bit)) return false;
            code |= static_cast<int>(bit);
            const int count = counts[len];
            if (code - count < first) {
                out_symbol = symbols[index + (code - first)];
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

uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

uint32_t Deflate::crc32(const uint8_t* data, std::size_t size) {
    static const std::array<uint32_t, 256> table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

uint32_t Deflate::adler32(const uint8_t* data, std::size_t size) {
    constexpr uint32_t kModAdler = 65521u;
    uint32_t s1 = 1u;
    uint32_t s2 = 0u;
    std::size_t pos = 0;
    while (pos < size) {
        const std::size_t chunk = std::min<std::size_t>(size - pos, 5552u);
        for (std::size_t i = 0; i < chunk; ++i) {
            s1 += data[pos + i];
            s2 += s1;
        }
        s1 %= kModAdler;
        s2 %= kModAdler;
        pos += chunk;
    }
    return (s2 << 16) | s1;
}

namespace {

Deflate::InflateStatus inflate_blocks(
    BitReader& br,
    const std::vector<uint8_t>* history,
    std::size_t max_window_size,
    bool rfc7692_stream,
    std::vector<uint8_t>& out,
    std::size_t max_output_bytes,
    bool* saw_bfinal_out
) {
    const std::size_t out_start = out.size();
    const std::size_t hist_size = history ? history->size() : 0u;
    bool final_block = false;

    while (true) {
        if (rfc7692_stream) {
            if (final_block) {
                br.align_to_byte();
                const std::size_t cur = br.current_byte_offset();
                // Valid if all sender payload bytes were consumed (leaving either
                // nothing or only the appended 4-byte sync-flush tail).
                if (cur == br.size || cur == br.total_size()) {
                    if (saw_bfinal_out) *saw_bfinal_out = true;
                    return Deflate::InflateStatus::Ok;
                }
                return Deflate::InflateStatus::CorruptData;
            }
            if (br.at_end()) {
                if (saw_bfinal_out) *saw_bfinal_out = false;
                return Deflate::InflateStatus::Ok;
            }
        } else if (final_block) {
            if (saw_bfinal_out) *saw_bfinal_out = true;
            return Deflate::InflateStatus::Ok;
        }

        uint32_t bfinal = 0;
        uint32_t btype = 0;
        if (!br.read_bits(1, bfinal) || !br.read_bits(2, btype)) {
            return Deflate::InflateStatus::CorruptData;
        }
        final_block = (bfinal != 0);

        if (btype == 3) {
            return Deflate::InflateStatus::CorruptData;
        }

        if (btype == 0) {
            uint8_t hdr[4]{};
            if (!br.copy_aligned_bytes(4, hdr)) {
                return Deflate::InflateStatus::CorruptData;
            }
            const uint16_t len = static_cast<uint16_t>(hdr[0]) | (static_cast<uint16_t>(hdr[1]) << 8);
            const uint16_t nlen = static_cast<uint16_t>(hdr[2]) | (static_cast<uint16_t>(hdr[3]) << 8);
            if (static_cast<uint16_t>(len ^ 0xFFFFu) != nlen) {
                return Deflate::InflateStatus::CorruptData;
            }
            const std::size_t produced = rfc7692_stream ? (out.size() - out_start) : out.size();
            if (len > max_output_bytes || produced > max_output_bytes - len) {
                return Deflate::InflateStatus::OutputLimitExceeded;
            }
            if (len > 0) {
                const std::size_t old_size = out.size();
                out.resize(old_size + len);
                if (!br.copy_aligned_bytes(len, out.data() + old_size)) {
                    out.resize(old_size);
                    return Deflate::InflateStatus::CorruptData;
                }
            }
            continue;
        }

        HuffmanTable lit_len_table;
        HuffmanTable dist_table;

        if (btype == 1) {
            std::array<int, kMaxLitLenSymbols> lit_lengths{};
            for (int i = 0; i <= 143; ++i) lit_lengths[i] = 8;
            for (int i = 144; i <= 255; ++i) lit_lengths[i] = 9;
            for (int i = 256; i <= 279; ++i) lit_lengths[i] = 7;
            for (int i = 280; i <= 287; ++i) lit_lengths[i] = 8;

            std::array<int, kMaxDistSymbols> dist_lengths{};
            dist_lengths.fill(5);

            if (!lit_len_table.build(lit_lengths.data(), kMaxLitLenSymbols) ||
                !dist_table.build(dist_lengths.data(), kMaxDistSymbols)) {
                return Deflate::InflateStatus::CorruptData;
            }
        } else {
            uint32_t hlit_raw = 0, hdist_raw = 0, hclen_raw = 0;
            if (!br.read_bits(5, hlit_raw) || !br.read_bits(5, hdist_raw) || !br.read_bits(4, hclen_raw)) {
                return Deflate::InflateStatus::CorruptData;
            }
            const int hlit = static_cast<int>(hlit_raw) + 257;
            const int hdist = static_cast<int>(hdist_raw) + 1;
            const int hclen = static_cast<int>(hclen_raw) + 4;
            if (hlit > 286 || hdist > 30) {
                return Deflate::InflateStatus::CorruptData;
            }

            std::array<int, 19> code_lengths{};
            for (int i = 0; i < hclen; ++i) {
                uint32_t cl = 0;
                if (!br.read_bits(3, cl)) {
                    return Deflate::InflateStatus::CorruptData;
                }
                code_lengths[kCodeLenOrder[i]] = static_cast<int>(cl);
            }

            HuffmanTable code_len_table;
            if (!code_len_table.build(code_lengths.data(), 19)) {
                return Deflate::InflateStatus::CorruptData;
            }

            std::array<int, kMaxLitLenSymbols + kMaxDistSymbols> combined{};
            const int total_codes = hlit + hdist;
            int idx = 0;
            while (idx < total_codes) {
                int sym = 0;
                if (!code_len_table.decode(br, sym)) {
                    return Deflate::InflateStatus::CorruptData;
                }
                if (sym < 16) {
                    combined[idx++] = sym;
                } else if (sym == 16) {
                    if (idx == 0) return Deflate::InflateStatus::CorruptData;
                    uint32_t extra = 0;
                    if (!br.read_bits(2, extra)) return Deflate::InflateStatus::CorruptData;
                    const int repeat = 3 + static_cast<int>(extra);
                    if (idx + repeat > total_codes) return Deflate::InflateStatus::CorruptData;
                    const int prev = combined[idx - 1];
                    for (int r = 0; r < repeat; ++r) combined[idx++] = prev;
                } else if (sym == 17) {
                    uint32_t extra = 0;
                    if (!br.read_bits(3, extra)) return Deflate::InflateStatus::CorruptData;
                    const int repeat = 3 + static_cast<int>(extra);
                    if (idx + repeat > total_codes) return Deflate::InflateStatus::CorruptData;
                    for (int r = 0; r < repeat; ++r) combined[idx++] = 0;
                } else if (sym == 18) {
                    uint32_t extra = 0;
                    if (!br.read_bits(7, extra)) return Deflate::InflateStatus::CorruptData;
                    const int repeat = 11 + static_cast<int>(extra);
                    if (idx + repeat > total_codes) return Deflate::InflateStatus::CorruptData;
                    for (int r = 0; r < repeat; ++r) combined[idx++] = 0;
                } else {
                    return Deflate::InflateStatus::CorruptData;
                }
            }

            // End-of-block symbol 256 must have a non-zero code length.
            if (combined[256] == 0) return Deflate::InflateStatus::CorruptData;

            if (!lit_len_table.build(combined.data(), hlit) ||
                !dist_table.build(combined.data() + hlit, hdist)) {
                return Deflate::InflateStatus::CorruptData;
            }
        }

        while (true) {
            int sym = 0;
            if (!lit_len_table.decode(br, sym)) {
                return Deflate::InflateStatus::CorruptData;
            }
            if (sym < 256) {
                const std::size_t produced = rfc7692_stream ? (out.size() - out_start) : out.size();
                if (produced >= max_output_bytes) {
                    return Deflate::InflateStatus::OutputLimitExceeded;
                }
                out.push_back(static_cast<uint8_t>(sym));
            } else if (sym == 256) {
                break;
            } else {
                const int len_idx = sym - 257;
                if (len_idx < 0 || len_idx >= 29) {
                    return Deflate::InflateStatus::CorruptData;
                }
                uint32_t len_extra = 0;
                if (!br.read_bits(kLenExtra[len_idx], len_extra)) {
                    return Deflate::InflateStatus::CorruptData;
                }
                const std::size_t match_len =
                    static_cast<std::size_t>(kLenBase[len_idx] + static_cast<int>(len_extra));

                int dist_sym = 0;
                if (!dist_table.decode(br, dist_sym)) {
                    return Deflate::InflateStatus::CorruptData;
                }
                if (dist_sym < 0 || dist_sym >= 30) {
                    return Deflate::InflateStatus::CorruptData;
                }
                uint32_t dist_extra = 0;
                if (!br.read_bits(kDistExtra[dist_sym], dist_extra)) {
                    return Deflate::InflateStatus::CorruptData;
                }
                const std::size_t match_dist =
                    static_cast<std::size_t>(kDistBase[dist_sym] + static_cast<int>(dist_extra));

                const std::size_t produced = rfc7692_stream ? (out.size() - out_start) : out.size();
                if (match_dist == 0 || match_dist > max_window_size ||
                    match_dist > hist_size + produced) {
                    return Deflate::InflateStatus::CorruptData;
                }
                if (match_len > max_output_bytes || produced > max_output_bytes - match_len) {
                    return Deflate::InflateStatus::OutputLimitExceeded;
                }

                const std::size_t old_size = out.size();
                out.resize(old_size + match_len);
                if (!history || match_dist <= produced) {
                    const std::size_t start = old_size - match_dist;
                    for (std::size_t i = 0; i < match_len; ++i) {
                        out[old_size + i] = out[start + i];
                    }
                } else {
                    const std::size_t from_hist = match_dist - produced;
                    const std::size_t hist_pos = hist_size - from_hist;
                    for (std::size_t i = 0; i < match_len; ++i) {
                        if (i < from_hist) {
                            out[old_size + i] = (*history)[hist_pos + i];
                        } else {
                            out[old_size + i] = out[out_start + (i - from_hist)];
                        }
                    }
                }
            }
        }
    }
}

void encode_length_and_distance(BitWriter& bw, std::size_t match_len, std::size_t match_dist) {
    int len_idx = 0;
    if (match_len == 258) {
        len_idx = 28;
    } else {
        while (len_idx + 1 < 28 && static_cast<int>(match_len) >= kLenBase[len_idx + 1]) {
            ++len_idx;
        }
    }
    bw.write_fixed_litlen(257 + len_idx);
    if (kLenExtra[len_idx] > 0) {
        bw.write_bits(static_cast<uint32_t>(match_len - static_cast<std::size_t>(kLenBase[len_idx])),
                      kLenExtra[len_idx]);
    }

    int dist_idx = 0;
    while (dist_idx + 1 < 30 && static_cast<int>(match_dist) >= kDistBase[dist_idx + 1]) {
        ++dist_idx;
    }
    bw.write_huffman(static_cast<uint32_t>(dist_idx), 5);
    if (kDistExtra[dist_idx] > 0) {
        bw.write_bits(static_cast<uint32_t>(match_dist - static_cast<std::size_t>(kDistBase[dist_idx])),
                      kDistExtra[dist_idx]);
    }
}

} // namespace

Deflate::InflateStream::InflateStream(int window_bits)
    : m_window_bits(clamp_window_bits(window_bits)),
      m_window_size(static_cast<std::size_t>(1u) << m_window_bits) {}

void Deflate::InflateStream::reset() {
    m_history.clear();
}

Deflate::InflateStatus Deflate::InflateStream::inflate_message(
    const uint8_t* data,
    std::size_t size,
    std::vector<uint8_t>& out,
    std::size_t max_output_bytes
) {
    if (!data || size == 0) {
        return InflateStatus::CorruptData;
    }
    static constexpr uint8_t kSyncTail[4] = {0x00u, 0x00u, 0xFFu, 0xFFu};
    BitReader br{data, size, kSyncTail, sizeof(kSyncTail), 0, 0, 0};

    const std::size_t out_start = out.size();
    bool saw_bfinal = false;
    InflateStatus status = inflate_blocks(
        br, &m_history, m_window_size, true, out, max_output_bytes, &saw_bfinal);
    if (status != InflateStatus::Ok) {
        out.resize(out_start);
        return status;
    }

    const std::size_t produced = out.size() - out_start;
    if (saw_bfinal) {
        // A BFINAL=1 block terminates the RFC 1951 stream; the next message
        // begins a fresh deflate stream with empty history.
        m_history.clear();
    } else if (produced > 0) {
        append_history(m_history, m_window_size, out.data() + out_start, produced);
    }
    return InflateStatus::Ok;
}

Deflate::DeflateStream::DeflateStream(int window_bits)
    : m_window_bits(clamp_window_bits(window_bits)),
      m_window_size(static_cast<std::size_t>(1u) << m_window_bits) {}

void Deflate::DeflateStream::reset() {
    m_history.clear();
}

void Deflate::DeflateStream::compress_message(
    const uint8_t* data,
    std::size_t size,
    std::vector<uint8_t>& out
) {
    if (size == 0 || !data) {
        // RFC 7692 §7.2.3.4 / Z_SYNC_FLUSH empty payload: empty non-final
        // stored block (0x00 0x00 0x00 0xFF 0xFF) with trailing 4 bytes removed.
        out.push_back(0x00u);
        return;
    }

    const std::size_t hist_len = m_history.size();
    std::vector<uint8_t> buf;
    buf.reserve(hist_len + size);
    buf.insert(buf.end(), m_history.begin(), m_history.end());
    buf.insert(buf.end(), data, data + size);

    constexpr int kHashBits = 12;
    constexpr std::size_t kHashSize = static_cast<std::size_t>(1u) << kHashBits;
    constexpr int kMaxChain = 32;
    const std::size_t window_mask = m_window_size - 1u;

    auto hash3 = [&](std::size_t p) -> std::size_t {
        uint32_t v = (static_cast<uint32_t>(buf[p]) << 16) ^
                     (static_cast<uint32_t>(buf[p + 1]) << 8) ^
                     static_cast<uint32_t>(buf[p + 2]);
        return ((v * 2654435761u) >> (32 - kHashBits)) & (kHashSize - 1u);
    };

    std::array<int32_t, kHashSize> head;
    head.fill(-1);
    std::vector<int32_t> prev(m_window_size, -1);

    const std::size_t total_len = buf.size();
    // Seed hash chains from retained sliding-window history for context takeover.
    if (hist_len >= 3) {
        const std::size_t seed_limit = hist_len - 2;
        for (std::size_t p = 0; p < seed_limit; ++p) {
            const std::size_t h = hash3(p);
            prev[p & window_mask] = head[h];
            head[h] = static_cast<int32_t>(p);
        }
    }
    // Also seed the 2 positions straddling the history/message boundary.
    for (std::size_t p = (hist_len >= 2 ? hist_len - 2 : 0);
         p < hist_len && p + 2 < total_len; ++p) {
        const std::size_t h = hash3(p);
        prev[p & window_mask] = head[h];
        head[h] = static_cast<int32_t>(p);
    }

    std::vector<uint8_t> compressed;
    compressed.reserve(size / 2 + 16);
    BitWriter bw{compressed, 0, 0};

    // Emit one non-final Fixed Huffman block (BFINAL=0, BTYPE=01).
    bw.write_bits(0u, 1);
    bw.write_bits(1u, 2);

    std::size_t pos = hist_len;
    while (pos < total_len) {
        std::size_t best_len = 0;
        std::size_t best_dist = 0;

        if (pos + 3 <= total_len) {
            const std::size_t h = hash3(pos);
            int32_t cand = head[h];
            const std::size_t max_len = std::min<std::size_t>(258u, total_len - pos);

            int steps = 0;
            while (cand >= 0 && steps < kMaxChain) {
                const std::size_t ucand = static_cast<std::size_t>(cand);
                if (ucand >= pos) break;
                const std::size_t dist = pos - ucand;
                if (dist > m_window_size) break;

                if (buf[ucand] == buf[pos] &&
                    buf[ucand + 1] == buf[pos + 1] &&
                    buf[ucand + 2] == buf[pos + 2]) {
                    std::size_t mlen = 3;
                    while (mlen < max_len && buf[ucand + mlen] == buf[pos + mlen]) {
                        ++mlen;
                    }
                    if (mlen > best_len) {
                        best_len = mlen;
                        best_dist = dist;
                        if (best_len == max_len) break;
                    }
                }
                const int32_t next_cand = prev[ucand & window_mask];
                if (next_cand >= cand) break;
                cand = next_cand;
                ++steps;
            }

            prev[pos & window_mask] = head[h];
            head[h] = static_cast<int32_t>(pos);
        }

        if (best_len >= 3) {
            encode_length_and_distance(bw, best_len, best_dist);
            const std::size_t end_match = pos + best_len;
            for (std::size_t q = pos + 1; q < end_match && q + 3 <= total_len; ++q) {
                const std::size_t h = hash3(q);
                prev[q & window_mask] = head[h];
                head[h] = static_cast<int32_t>(q);
            }
            pos = end_match;
        } else {
            bw.write_fixed_litlen(buf[pos]);
            ++pos;
        }
    }

    // End-of-block (256), then empty stored block header (BFINAL=0, BTYPE=00),
    // padded with zero bits to a byte boundary. The trailing 0x00 0x00 0xFF 0xFF
    // is omitted per RFC 7692 §7.2.1.
    bw.write_fixed_litlen(256);
    bw.write_bits(0u, 3);
    bw.flush_to_byte();

    const std::size_t num_stored_blocks = (size + 65534u) / 65535u;
    const std::size_t stored_wire_size = size + 5u * num_stored_blocks + 1u;

    if (compressed.size() <= stored_wire_size) {
        out.insert(out.end(), compressed.begin(), compressed.end());
    } else {
        std::size_t offset = 0;
        while (offset < size) {
            const std::size_t chunk = std::min<std::size_t>(size - offset, 65535u);
            out.push_back(0x00u); // BFINAL=0, BTYPE=00
            const uint16_t len = static_cast<uint16_t>(chunk);
            const uint16_t nlen = static_cast<uint16_t>(len ^ 0xFFFFu);
            out.push_back(static_cast<uint8_t>(len & 0xFFu));
            out.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
            out.push_back(static_cast<uint8_t>(nlen & 0xFFu));
            out.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFFu));
            out.insert(out.end(), data + offset, data + offset + chunk);
            offset += chunk;
        }
        // Final empty stored block header (BFINAL=0, BTYPE=00); trailing 00 00 ff ff stripped.
        out.push_back(0x00u);
    }

    append_history(m_history, m_window_size, data, size);
}

bool Deflate::inflate_raw(
    const uint8_t* data,
    std::size_t size,
    std::vector<uint8_t>& out,
    std::size_t max_output_bytes,
    std::size_t* bytes_consumed
) {
    if (!data && size > 0) return false;

    BitReader br{data, size, nullptr, 0, 0, 0, 0};
    const std::size_t out_start = out.size();
    InflateStatus status = inflate_blocks(
        br, nullptr, 32768u, false, out, max_output_bytes, nullptr);
    if (status != InflateStatus::Ok) {
        out.resize(out_start);
        return false;
    }
    if (bytes_consumed) {
        br.align_to_byte();
        *bytes_consumed = br.current_byte_offset();
    }
    return true;
}

bool Deflate::inflate_zlib(
    const uint8_t* data,
    std::size_t size,
    std::vector<uint8_t>& out,
    std::size_t max_output_bytes
) {
    if (!data || size < 6) return false;
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8u) return false;
    if ((cmf >> 4) > 7u) return false;
    if (((static_cast<unsigned>(cmf) << 8) | static_cast<unsigned>(flg)) % 31u != 0u) return false;
    if ((flg & 0x20u) != 0u) return false; // Preset dictionary not used on web/PNG

    const std::size_t initial_out_size = out.size();
    std::size_t consumed = 0;
    if (!inflate_raw(data + 2, size - 2, out, max_output_bytes, &consumed)) {
        return false;
    }
    if (2 + consumed + 4 > size) return false;

    const uint32_t expected_adler = read_u32_be(data + 2 + consumed);
    const uint32_t actual_adler = adler32(out.data() + initial_out_size, out.size() - initial_out_size);
    return expected_adler == actual_adler;
}

bool Deflate::inflate_gzip(
    const uint8_t* data,
    std::size_t size,
    std::vector<uint8_t>& out,
    std::size_t max_output_bytes
) {
    if (!data || size < 18) return false;

    std::size_t offset = 0;
    while (offset < size) {
        if (size - offset < 18) return false;
        if (data[offset] != 0x1Fu || data[offset + 1] != 0x8Bu || data[offset + 2] != 0x08u) {
            return false;
        }
        const uint8_t flg = data[offset + 3];
        if ((flg & 0xE0u) != 0u) return false; // Reserved bits must be zero

        std::size_t cursor = offset + 10;
        // FEXTRA
        if (flg & 0x04u) {
            if (size - cursor < 2) return false;
            const std::size_t xlen = static_cast<std::size_t>(data[cursor]) |
                                     (static_cast<std::size_t>(data[cursor + 1]) << 8);
            cursor += 2;
            if (xlen > size - cursor) return false;
            cursor += xlen;
        }
        // FNAME
        if (flg & 0x08u) {
            while (cursor < size && data[cursor] != 0) ++cursor;
            if (cursor >= size) return false;
            ++cursor;
        }
        // FCOMMENT
        if (flg & 0x10u) {
            while (cursor < size && data[cursor] != 0) ++cursor;
            if (cursor >= size) return false;
            ++cursor;
        }
        // FHCRC
        if (flg & 0x02u) {
            if (size - cursor < 2) return false;
            const uint16_t expected_hcrc = static_cast<uint16_t>(data[cursor]) |
                                           (static_cast<uint16_t>(data[cursor + 1]) << 8);
            const uint16_t actual_hcrc = static_cast<uint16_t>(crc32(data + offset, cursor - offset) & 0xFFFFu);
            if (expected_hcrc != actual_hcrc) return false;
            cursor += 2;
        }

        const std::size_t member_start = out.size();
        std::size_t consumed = 0;
        if (!inflate_raw(data + cursor, size - cursor, out, max_output_bytes, &consumed)) {
            return false;
        }
        cursor += consumed;
        if (size - cursor < 8) return false;

        const uint32_t expected_crc = read_u32_le(data + cursor);
        const uint32_t expected_isize = read_u32_le(data + cursor + 4);
        const std::size_t member_len = out.size() - member_start;
        const uint32_t actual_crc = crc32(out.data() + member_start, member_len);
        if (expected_crc != actual_crc) return false;
        if (expected_isize != static_cast<uint32_t>(member_len & 0xFFFFFFFFu)) return false;

        offset = cursor + 8;
    }

    return true;
}

void Deflate::compress_zlib_stored(
    const uint8_t* data,
    std::size_t size,
    std::vector<uint8_t>& out
) {
    // CMF = 0x78 (deflate, 32K window), FLG = 0x01 (0x7801 % 31 == 0)
    out.push_back(0x78u);
    out.push_back(0x01u);

    std::size_t offset = 0;
    do {
        const std::size_t chunk = std::min<std::size_t>(size - offset, 65535u);
        const bool is_final = (offset + chunk >= size);
        out.push_back(is_final ? 0x01u : 0x00u); // BFINAL=1/0, BTYPE=00
        const uint16_t len = static_cast<uint16_t>(chunk);
        const uint16_t nlen = static_cast<uint16_t>(len ^ 0xFFFFu);
        out.push_back(static_cast<uint8_t>(len & 0xFFu));
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        out.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFFu));
        if (chunk > 0 && data) {
            out.insert(out.end(), data + offset, data + offset + chunk);
        }
        offset += chunk;
    } while (offset < size);

    const uint32_t adler = adler32(data, size);
    out.push_back(static_cast<uint8_t>((adler >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((adler >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((adler >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(adler & 0xFFu));
}

} // namespace litebird
