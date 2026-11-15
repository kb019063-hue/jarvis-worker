#include "litebird/render/GifDecoder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace litebird {

namespace {

inline uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

bool validate_dimensions(int64_t width, int64_t height) {
    if (width <= 0 || height <= 0) return false;
    if (width > GifDecoder::kMaxDecodedPixels / height) return false;
    return (width * height) <= GifDecoder::kMaxDecodedPixels;
}

bool skip_sub_blocks(const uint8_t* data, size_t size, size_t& pos) {
    while (pos < size) {
        const uint8_t block_len = data[pos++];
        if (block_len == 0) return true;
        if (pos + block_len > size) {
            pos = size;
            return false;
        }
        pos += block_len;
    }
    return false;
}

struct SubBlockBitReader {
    const uint8_t* data{nullptr};
    size_t size{0};
    size_t& pos;

    uint32_t bit_buf{0};
    int valid_bits{0};
    size_t block_rem{0};
    bool ended{false};

    SubBlockBitReader(const uint8_t* d, size_t s, size_t& p)
        : data(d), size(s), pos(p) {}

    bool read_byte(uint8_t& out) {
        if (ended) return false;
        while (block_rem == 0) {
            if (pos >= size) {
                ended = true;
                return false;
            }
            const uint8_t len = data[pos++];
            if (len == 0) {
                ended = true;
                return false;
            }
            if (pos + len > size) {
                pos = size;
                ended = true;
                return false;
            }
            block_rem = len;
        }
        out = data[pos++];
        --block_rem;
        return true;
    }

    bool read_code(int code_size, int& out_code) {
        while (valid_bits < code_size) {
            uint8_t b = 0;
            if (!read_byte(b)) return false;
            bit_buf |= static_cast<uint32_t>(b) << valid_bits;
            valid_bits += 8;
        }
        out_code = static_cast<int>(bit_buf & ((1u << code_size) - 1u));
        bit_buf >>= code_size;
        valid_bits -= code_size;
        return true;
    }

    void drain_remaining_sub_blocks() {
        if (block_rem > 0) {
            pos += block_rem;
            block_rem = 0;
        }
        if (!ended) {
            skip_sub_blocks(data, size, pos);
            ended = true;
        }
    }
};

bool decode_lzw(
    const uint8_t* data,
    size_t size,
    size_t& pos,
    size_t expected_pixels,
    std::vector<uint8_t>& out_indices,
    std::string& err
) {
    if (pos >= size) {
        err = "Truncated GIF LZW minimum code size";
        return false;
    }
    const uint8_t min_code_size = data[pos++];
    if (min_code_size < 2 || min_code_size > 8) {
        err = "Invalid GIF LZW minimum code size";
        return false;
    }

    const int clear_code = 1 << min_code_size;
    const int eoi_code = clear_code + 1;

    std::array<uint16_t, 4096> prefix{};
    std::array<uint8_t, 4096> suffix{};
    std::array<uint8_t, 4096> first_char{};
    std::array<uint8_t, 4096> stack{};

    for (int i = 0; i < clear_code; ++i) {
        suffix[i] = static_cast<uint8_t>(i);
        first_char[i] = static_cast<uint8_t>(i);
    }

    int code_size = min_code_size + 1;
    int next_code = eoi_code + 1;
    int prev_code = -1;

    out_indices.assign(expected_pixels, 0);
    size_t out_pos = 0;

    SubBlockBitReader br(data, size, pos);

    while (true) {
        int code = 0;
        if (!br.read_code(code_size, code)) {
            // Tolerate premature end of sub-blocks if pixels were already decoded
            break;
        }

        if (code == clear_code) {
            code_size = min_code_size + 1;
            next_code = eoi_code + 1;
            prev_code = -1;
            continue;
        }

        if (code == eoi_code) {
            break;
        }

        if (prev_code < 0) {
            if (code >= clear_code) {
                br.drain_remaining_sub_blocks();
                err = "Invalid initial GIF LZW code";
                return false;
            }
            if (out_pos < expected_pixels) {
                out_indices[out_pos++] = static_cast<uint8_t>(code);
            }
            prev_code = code;
            continue;
        }

        if (code > next_code || code >= 4096) {
            br.drain_remaining_sub_blocks();
            err = "Out-of-range GIF LZW code";
            return false;
        }

        int sp = 0;
        int cur = code;
        if (cur == next_code) {
            stack[sp++] = first_char[prev_code];
            cur = prev_code;
        }

        while (cur >= clear_code) {
            if (sp >= 4096) {
                br.drain_remaining_sub_blocks();
                err = "Cycle detected in GIF LZW dictionary";
                return false;
            }
            stack[sp++] = suffix[cur];
            cur = prefix[cur];
        }

        const uint8_t first = static_cast<uint8_t>(cur);
        stack[sp++] = first;

        for (int i = sp - 1; i >= 0; --i) {
            if (out_pos < expected_pixels) {
                out_indices[out_pos++] = stack[i];
            }
        }

        if (next_code < 4096) {
            prefix[next_code] = static_cast<uint16_t>(prev_code);
            suffix[next_code] = first;
            first_char[next_code] = first_char[prev_code];
            ++next_code;
            if (next_code == (1 << code_size) && code_size < 12) {
                ++code_size;
            }
        }

        prev_code = code;
    }

    br.drain_remaining_sub_blocks();
    return true;
}

struct GraphicControl {
    GifDisposal disposal{GifDisposal::Unspecified};
    bool has_transparency{false};
    uint8_t transparent_index{0};
    int delay_ms{0};

    void reset() {
        disposal = GifDisposal::Unspecified;
        has_transparency = false;
        transparent_index = 0;
        delay_ms = 0;
    }
};

} // namespace

bool GifDecoder::is_gif(const uint8_t* data, size_t size) {
    if (!data || size < 6) return false;
    return std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0;
}

GifDecodeResult GifDecoder::decode_frames(
    const uint8_t* data,
    size_t size,
    size_t max_frames
) {
    GifDecodeResult result;
    if (!is_gif(data, size)) {
        result.error = "Not a GIF stream (invalid header signature)";
        return result;
    }
    if (size < 13) {
        result.error = "Truncated GIF logical screen descriptor";
        return result;
    }

    const int canvas_w = read_le16(data + 6);
    const int canvas_h = read_le16(data + 8);
    if (!validate_dimensions(canvas_w, canvas_h)) {
        result.error = "Invalid or oversized GIF canvas dimensions";
        return result;
    }

    result.width = canvas_w;
    result.height = canvas_h;

    const uint8_t lsd_packed = data[10];
    const bool has_gct = (lsd_packed & 0x80) != 0;
    const size_t gct_entries = has_gct ? (size_t{1} << ((lsd_packed & 0x07) + 1)) : 0u;
    size_t pos = 13;

    std::vector<uint32_t> gct;
    if (has_gct) {
        const size_t gct_bytes = gct_entries * 3u;
        if (pos + gct_bytes > size) {
            result.error = "Truncated GIF global color table";
            return result;
        }
        gct.resize(gct_entries);
        for (size_t i = 0; i < gct_entries; ++i) {
            const uint8_t r = data[pos + i * 3 + 0];
            const uint8_t g = data[pos + i * 3 + 1];
            const uint8_t b = data[pos + i * 3 + 2];
            gct[i] = Color(r, g, b, 255).to_argb();
        }
        pos += gct_bytes;
    }

    const size_t canvas_pixels = static_cast<size_t>(canvas_w) * static_cast<size_t>(canvas_h);
    std::vector<uint32_t> canvas(canvas_pixels, 0u);
    std::vector<uint32_t> previous_rect;

    GraphicControl gce;
    const size_t frame_cap = std::min(max_frames, kMaxFrames);
    int64_t total_frame_pixels = 0;

    while (pos < size) {
        const uint8_t introducer = data[pos++];

        if (introducer == 0x3B) {
            // GIF Trailer
            break;
        }

        if (introducer == 0x00) {
            // Stray padding byte between blocks
            continue;
        }

        if (introducer == 0x21) {
            // Extension block
            if (pos >= size) {
                if (result.frames.empty()) result.error = "Truncated GIF extension label";
                break;
            }
            const uint8_t label = data[pos++];
            if (label == 0xF9) {
                // Graphic Control Extension
                if (pos >= size) {
                    if (result.frames.empty()) result.error = "Truncated GIF Graphic Control Extension";
                    break;
                }
                const uint8_t blk_size = data[pos++];
                if (pos + blk_size > size) {
                    if (result.frames.empty()) result.error = "Truncated GIF Graphic Control Extension body";
                    break;
                }
                if (blk_size >= 4) {
                    const uint8_t packed = data[pos];
                    const uint8_t disp_raw = (packed >> 2) & 0x07;
                    gce.disposal = (disp_raw <= 3)
                        ? static_cast<GifDisposal>(disp_raw)
                        : GifDisposal::Unspecified;
                    gce.has_transparency = (packed & 0x01) != 0;
                    gce.delay_ms = static_cast<int>(read_le16(data + pos + 1)) * 10;
                    gce.transparent_index = data[pos + 3];
                }
                pos += blk_size;
                skip_sub_blocks(data, size, pos);
            } else if (label == 0xFF) {
                // Application Extension (check for NETSCAPE2.0 / ANIMEXTS1.0 loop count)
                if (pos < size) {
                    const uint8_t app_len = data[pos++];
                    if (pos + app_len <= size) {
                        const bool is_netscape = (app_len == 11) && (
                            std::memcmp(data + pos, "NETSCAPE2.0", 11) == 0 ||
                            std::memcmp(data + pos, "ANIMEXTS1.0", 11) == 0
                        );
                        pos += app_len;
                        if (is_netscape && pos < size) {
                            const uint8_t sub_len = data[pos];
                            if (sub_len >= 3 && pos + 1 + sub_len <= size && data[pos + 1] == 1) {
                                result.loop_count = read_le16(data + pos + 2);
                                result.has_loop_extension = true;
                            }
                        }
                        skip_sub_blocks(data, size, pos);
                    } else {
                        pos = size;
                    }
                }
            } else {
                // Comment (0xFE), Plain Text (0x01), or unknown extension
                skip_sub_blocks(data, size, pos);
            }
            continue;
        }

        if (introducer == 0x2C) {
            // Image Descriptor (9 bytes)
            if (pos + 9 > size) {
                if (result.frames.empty()) result.error = "Truncated GIF image descriptor";
                break;
            }
            const int img_left = read_le16(data + pos + 0);
            const int img_top = read_le16(data + pos + 2);
            const int img_w = read_le16(data + pos + 4);
            const int img_h = read_le16(data + pos + 6);
            const uint8_t img_packed = data[pos + 8];
            pos += 9;

            if (!validate_dimensions(img_w, img_h)) {
                result.error = "Invalid or oversized GIF frame dimensions";
                return result;
            }

            const bool has_lct = (img_packed & 0x80) != 0;
            const bool interlaced = (img_packed & 0x40) != 0;
            const size_t lct_entries = has_lct ? (size_t{1} << ((img_packed & 0x07) + 1)) : 0u;

            std::vector<uint32_t> lct;
            if (has_lct) {
                const size_t lct_bytes = lct_entries * 3u;
                if (pos + lct_bytes > size) {
                    result.error = "Truncated GIF local color table";
                    return result;
                }
                lct.resize(lct_entries);
                for (size_t i = 0; i < lct_entries; ++i) {
                    const uint8_t r = data[pos + i * 3 + 0];
                    const uint8_t g = data[pos + i * 3 + 1];
                    const uint8_t b = data[pos + i * 3 + 2];
                    lct[i] = Color(r, g, b, 255).to_argb();
                }
                pos += lct_bytes;
            }

            const std::vector<uint32_t>& palette = has_lct ? lct : gct;
            if (palette.empty()) {
                result.error = "GIF frame has neither local nor global color table";
                return result;
            }

            const size_t frame_pixels = static_cast<size_t>(img_w) * static_cast<size_t>(img_h);
            std::vector<uint8_t> indices;
            if (!decode_lzw(data, size, pos, frame_pixels, indices, result.error)) {
                if (result.frames.empty()) return result;
                result.error.clear();
                break;
            }

            // Compute clipped rectangle on canvas
            const int x0 = std::max(0, img_left);
            const int y0 = std::max(0, img_top);
            const int x1 = std::min(canvas_w, img_left + img_w);
            const int y1 = std::min(canvas_h, img_top + img_h);

            // Save pre-frame canvas region if disposal == RestoreToPrevious
            if (gce.disposal == GifDisposal::RestoreToPrevious && x0 < x1 && y0 < y1) {
                const size_t rw = static_cast<size_t>(x1 - x0);
                const size_t rh = static_cast<size_t>(y1 - y0);
                previous_rect.resize(rw * rh);
                for (int cy = y0; cy < y1; ++cy) {
                    std::memcpy(
                        previous_rect.data() + static_cast<size_t>(cy - y0) * rw,
                        canvas.data() + static_cast<size_t>(cy) * static_cast<size_t>(canvas_w) + static_cast<size_t>(x0),
                        rw * sizeof(uint32_t)
                    );
                }
            }

            // Paint decoded indices onto canvas (handling 4-pass interlacing if set)
            auto paint_row = [&](int src_row, int dst_local_y) {
                const int cy = img_top + dst_local_y;
                if (cy < 0 || cy >= canvas_h) return;
                const uint8_t* src = indices.data() + static_cast<size_t>(src_row) * static_cast<size_t>(img_w);
                uint32_t* dst_row = canvas.data() + static_cast<size_t>(cy) * static_cast<size_t>(canvas_w);
                for (int lx = 0; lx < img_w; ++lx) {
                    const int cx = img_left + lx;
                    if (cx < 0 || cx >= canvas_w) continue;
                    const uint8_t idx = src[lx];
                    if (gce.has_transparency && idx == gce.transparent_index) continue;
                    if (idx < palette.size()) {
                        dst_row[cx] = palette[idx];
                    }
                }
            };

            if (!interlaced) {
                for (int y = 0; y < img_h; ++y) {
                    paint_row(y, y);
                }
            } else {
                static constexpr int kStarts[4] = {0, 4, 2, 1};
                static constexpr int kSteps[4] = {8, 8, 4, 2};
                int src_row = 0;
                for (int pass = 0; pass < 4; ++pass) {
                    for (int y = kStarts[pass]; y < img_h; y += kSteps[pass]) {
                        if (src_row < img_h) {
                            paint_row(src_row++, y);
                        }
                    }
                }
            }

            // Snapshot composited canvas as an emitted frame
            auto frame_img = std::make_shared<ImageData>();
            frame_img->width = canvas_w;
            frame_img->height = canvas_h;
            frame_img->pixels = canvas;

            GifFrame frame;
            frame.image = std::move(frame_img);
            frame.delay_ms = gce.delay_ms;
            frame.disposal = gce.disposal;
            frame.left = img_left;
            frame.top = img_top;
            frame.width = img_w;
            frame.height = img_h;
            result.frames.push_back(std::move(frame));

            // Apply post-frame disposal
            if (x0 < x1 && y0 < y1) {
                if (gce.disposal == GifDisposal::RestoreToBackground) {
                    const size_t rw = static_cast<size_t>(x1 - x0);
                    for (int cy = y0; cy < y1; ++cy) {
                        std::memset(
                            canvas.data() + static_cast<size_t>(cy) * static_cast<size_t>(canvas_w) + static_cast<size_t>(x0),
                            0,
                            rw * sizeof(uint32_t)
                        );
                    }
                } else if (gce.disposal == GifDisposal::RestoreToPrevious) {
                    const size_t rw = static_cast<size_t>(x1 - x0);
                    for (int cy = y0; cy < y1; ++cy) {
                        std::memcpy(
                            canvas.data() + static_cast<size_t>(cy) * static_cast<size_t>(canvas_w) + static_cast<size_t>(x0),
                            previous_rect.data() + static_cast<size_t>(cy - y0) * rw,
                            rw * sizeof(uint32_t)
                        );
                    }
                }
            }

            gce.reset();

            total_frame_pixels += static_cast<int64_t>(canvas_pixels);
            if (result.frames.size() >= frame_cap ||
                total_frame_pixels + static_cast<int64_t>(canvas_pixels) > kMaxAnimationPixels) {
                break;
            }
            continue;
        }

        // Unexpected block introducer
        if (result.frames.empty()) {
            result.error = "Unexpected GIF block introducer";
            return result;
        }
        break;
    }

    if (result.frames.empty() && result.error.empty()) {
        result.error = "GIF contained no image frames";
    }
    return result;
}

std::shared_ptr<ImageData> GifDecoder::decode(const uint8_t* data, size_t size) {
    auto res = decode_frames(data, size, 1);
    return res.ok() ? res.frames[0].image : nullptr;
}

std::shared_ptr<ImageData> GifDecoder::decode(const std::vector<uint8_t>& data) {
    return decode(data.data(), data.size());
}

} // namespace litebird
