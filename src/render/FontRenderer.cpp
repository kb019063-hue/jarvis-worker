#include "litebird/render/FontRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace litebird {

namespace {

// Embedded 8x8 fallback font for environments without system `.ttf` files.
static const uint8_t FALLBACK_FONT_8X8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00},
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, {0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00},
    {0x18,0x7e,0xc0,0x7c,0x06,0xfc,0x18,0x00}, {0x00,0xc6,0xcc,0x18,0x30,0x66,0xc6,0x00},
    {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00}, {0x30,0x30,0x10,0x20,0x00,0x00,0x00,0x00},
    {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00}, {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00},
    {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00}, {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, {0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00},
    {0x7c,0xc6,0xce,0xd6,0xe6,0xc6,0x7c,0x00}, {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00},
    {0x7c,0xc6,0x06,0x1c,0x30,0x60,0xfe,0x00}, {0x7c,0xc6,0x06,0x3c,0x06,0xc6,0x7c,0x00},
    {0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00}, {0xfe,0xc0,0xfc,0x06,0x06,0xc6,0x7c,0x00},
    {0x38,0x60,0xc0,0xfc,0xc6,0xc6,0x7c,0x00}, {0xfe,0xc6,0x0c,0x18,0x30,0x30,0x30,0x00},
    {0x7c,0xc6,0xc6,0x7c,0xc6,0xc6,0x7c,0x00}, {0x7c,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00},
    {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00}, {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00},
    {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00}, {0x7c,0xc6,0x0c,0x18,0x18,0x00,0x18,0x00},
    {0x7c,0xc6,0xde,0xde,0xdc,0xc0,0x7c,0x00}, {0x38,0x6c,0xc6,0xfe,0xc6,0xc6,0xc6,0x00},
    {0xfc,0x66,0x66,0x7c,0x66,0x66,0xfc,0x00}, {0x3c,0x66,0xc0,0xc0,0xc0,0x66,0x3c,0x00},
    {0xf8,0x6c,0x66,0x66,0x66,0x6c,0xf8,0x00}, {0xfe,0x62,0x68,0x78,0x68,0x62,0xfe,0x00},
    {0xfe,0x62,0x68,0x78,0x68,0x60,0xf0,0x00}, {0x3c,0x66,0xc0,0xc0,0xce,0x66,0x3e,0x00},
    {0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0x00}, {0x7e,0x18,0x18,0x18,0x18,0x18,0x7e,0x00},
    {0x1e,0x0c,0x0c,0x0c,0xcc,0xcc,0x78,0x00}, {0xe6,0x66,0x6c,0x78,0x6c,0x66,0xe6,0x00},
    {0xf0,0x60,0x60,0x60,0x62,0x66,0xfe,0x00}, {0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0x00},
    {0xc6,0xe6,0xf6,0xfe,0xde,0xce,0xc6,0x00}, {0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00},
    {0xfc,0x66,0x66,0x7c,0x60,0x60,0xf0,0x00}, {0x7c,0xc6,0xc6,0xc6,0xd6,0x7c,0x0e,0x00},
    {0xfc,0x66,0x66,0x7c,0x6c,0x66,0xe6,0x00}, {0x7c,0xc6,0x60,0x38,0x0c,0xc6,0x7c,0x00},
    {0x7e,0x7e,0x5a,0x18,0x18,0x18,0x3c,0x00}, {0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00},
    {0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x10,0x00}, {0xc6,0xc6,0xd6,0xfe,0xfe,0xee,0xc6,0x00},
    {0xc6,0x6c,0x38,0x38,0x6c,0xc6,0xc6,0x00}, {0x66,0x66,0x66,0x3c,0x18,0x18,0x3c,0x00},
    {0xfe,0xc6,0x8c,0x18,0x32,0x66,0xfe,0x00}, {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00},
    {0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00}, {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00},
    {0x10,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00},
    {0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x78,0x0c,0x7c,0xcc,0x76,0x00},
    {0xe0,0x60,0x7c,0x66,0x66,0x66,0xdc,0x00}, {0x00,0x00,0x7c,0xc6,0xc0,0xc6,0x7c,0x00},
    {0x1c,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0x00}, {0x00,0x00,0x7c,0xc6,0xfe,0xc0,0x7c,0x00},
    {0x1c,0x36,0x30,0x78,0x30,0x30,0x78,0x00}, {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0xf8},
    {0xe0,0x60,0x6c,0x76,0x66,0x66,0xe6,0x00}, {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00},
    {0x06,0x00,0x0e,0x06,0x06,0x66,0x3c,0x00}, {0xe0,0x60,0x66,0x6c,0x78,0x6c,0xe6,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, {0x00,0x00,0xec,0xfe,0xd6,0xd6,0xd6,0x00},
    {0x00,0x00,0xdc,0x66,0x66,0x66,0x66,0x00}, {0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7c,0x00},
    {0x00,0x00,0xdc,0x66,0x66,0x7c,0x60,0xf0}, {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0x1e},
    {0x00,0x00,0xdc,0x76,0x60,0x60,0xf0,0x00}, {0x00,0x00,0x7c,0xc0,0x7c,0x06,0xfc,0x00},
    {0x30,0x30,0xfc,0x30,0x30,0x34,0x18,0x00}, {0x00,0x00,0xcc,0xcc,0xcc,0xcc,0x76,0x00},
    {0x00,0x00,0xc6,0xc6,0x6c,0x38,0x10,0x00}, {0x00,0x00,0xc6,0xd6,0xfe,0xee,0xc6,0x00},
    {0x00,0x00,0xc6,0x6c,0x38,0x6c,0xc6,0x00}, {0x00,0x00,0xc6,0xc6,0x7e,0x06,0x0c,0xf8},
    {0x00,0x00,0xfe,0xcc,0x18,0x32,0xfe,0x00}, {0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, {0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0x00},
    {0x76,0xdc,0x00,0x00,0x00,0x00,0x00,0x00}
};

uint16_t read_u16_be(const std::vector<uint8_t>& d, std::size_t off) {
    if (off + 2 > d.size()) return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(d[off]) << 8) | d[off + 1]);
}

int16_t read_i16_be(const std::vector<uint8_t>& d, std::size_t off) {
    return static_cast<int16_t>(read_u16_be(d, off));
}

uint32_t read_u32_be(const std::vector<uint8_t>& d, std::size_t off) {
    if (off + 4 > d.size()) return 0;
    return (static_cast<uint32_t>(d[off]) << 24) |
           (static_cast<uint32_t>(d[off + 1]) << 16) |
           (static_cast<uint32_t>(d[off + 2]) << 8) |
           static_cast<uint32_t>(d[off + 3]);
}

// Decodes one UTF-8 scalar value and advances `pos`. Invalid sequences emit U+FFFD.
uint32_t next_utf8_codepoint(std::string_view s, std::size_t& pos) {
    if (pos >= s.size()) return 0;
    const uint8_t b0 = static_cast<uint8_t>(s[pos++]);
    if (b0 < 0x80) return b0;
    if ((b0 & 0xE0) == 0xC0) {
        if (pos >= s.size()) return 0xFFFD;
        const uint8_t b1 = static_cast<uint8_t>(s[pos]);
        if ((b1 & 0xC0) != 0x80) return 0xFFFD;
        ++pos;
        const uint32_t cp = ((static_cast<uint32_t>(b0 & 0x1F) << 6) | (b1 & 0x3F));
        return (cp >= 0x80) ? cp : 0xFFFD;
    }
    if ((b0 & 0xF0) == 0xE0) {
        if (pos + 1 >= s.size()) { pos = s.size(); return 0xFFFD; }
        const uint8_t b1 = static_cast<uint8_t>(s[pos]);
        const uint8_t b2 = static_cast<uint8_t>(s[pos + 1]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { ++pos; return 0xFFFD; }
        pos += 2;
        const uint32_t cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) |
                            (static_cast<uint32_t>(b1 & 0x3F) << 6) |
                            (b2 & 0x3F);
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return 0xFFFD;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0) {
        if (pos + 2 >= s.size()) { pos = s.size(); return 0xFFFD; }
        const uint8_t b1 = static_cast<uint8_t>(s[pos]);
        const uint8_t b2 = static_cast<uint8_t>(s[pos + 1]);
        const uint8_t b3 = static_cast<uint8_t>(s[pos + 2]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) { ++pos; return 0xFFFD; }
        pos += 3;
        const uint32_t cp = (static_cast<uint32_t>(b0 & 0x07) << 18) |
                            (static_cast<uint32_t>(b1 & 0x3F) << 12) |
                            (static_cast<uint32_t>(b2 & 0x3F) << 6) |
                            (b3 & 0x3F);
        return (cp >= 0x10000 && cp <= 0x10FFFF) ? cp : 0xFFFD;
    }
    return 0xFFFD;
}

void flatten_quad_bezier(
    float x0, float y0,
    float cx, float cy,
    float x1, float y1,
    int depth,
    std::vector<std::pair<float, float>>& pts
) {
    const float mx = 0.5f * (x0 + x1);
    const float my = 0.5f * (y0 + y1);
    const float dx = cx - mx;
    const float dy = cy - my;
    if (depth >= 6 || (dx * dx + dy * dy) <= 0.03f) {
        pts.emplace_back(x1, y1);
        return;
    }
    const float q0x = 0.5f * (x0 + cx);
    const float q0y = 0.5f * (y0 + cy);
    const float q1x = 0.5f * (cx + x1);
    const float q1y = 0.5f * (cy + y1);
    const float midx = 0.5f * (q0x + q1x);
    const float midy = 0.5f * (q0y + q1y);
    flatten_quad_bezier(x0, y0, q0x, q0y, midx, midy, depth + 1, pts);
    flatten_quad_bezier(midx, midy, q1x, q1y, x1, y1, depth + 1, pts);
}

GlyphBitmap rasterize_fallback_8x8(uint32_t cp, int pixel_size, bool bold) {
    GlyphBitmap bmp;
    const int px = std::clamp(pixel_size, 6, 128);
    bmp.advance = std::max(1.0f, px * 0.55f);
    if (cp == ' ' || cp == '\t') return bmp;
    char ch = (cp >= 32 && cp <= 126) ? static_cast<char>(cp) : '?';
    const uint8_t* glyph = FALLBACK_FONT_8X8[static_cast<uint8_t>(ch) - 32];
    const int w = std::max(4, static_cast<int>(std::lround(px * 0.55f)) + (bold ? 1 : 0));
    const int h = px;
    bmp.width = w;
    bmp.height = h;
    bmp.bearing_x = 0;
    bmp.bearing_y = static_cast<int>(std::lround(px * 0.8f));
    bmp.coverage.assign(static_cast<std::size_t>(w * h), 0);
    for (int y = 0; y < h; ++y) {
        const int sy = std::clamp(y * 8 / h, 0, 7);
        for (int x = 0; x < w; ++x) {
            const int sx = std::clamp(x * 8 / std::max(1, w - (bold ? 1 : 0)), 0, 7);
            bool set = (glyph[sy] & (0x80u >> sx)) != 0;
            if (!set && bold && x > 0) {
                const int sx_prev = std::clamp((x - 1) * 8 / std::max(1, w - 1), 0, 7);
                set = (glyph[sy] & (0x80u >> sx_prev)) != 0;
            }
            if (set) bmp.coverage[static_cast<std::size_t>(y * w + x)] = 255;
        }
    }
    return bmp;
}

} // namespace

bool TrueTypeFont::find_table(const char tag[4], uint32_t& offset_out, uint32_t& length_out) const {
    offset_out = 0;
    length_out = 0;
    if (m_data.size() < 12) return false;
    const uint16_t num_tables = read_u16_be(m_data, 4);
    if (12u + static_cast<std::size_t>(num_tables) * 16u > m_data.size()) return false;
    for (uint16_t i = 0; i < num_tables; ++i) {
        const std::size_t rec = 12u + static_cast<std::size_t>(i) * 16u;
        if (std::memcmp(&m_data[rec], tag, 4) == 0) {
            const uint32_t off = read_u32_be(m_data, rec + 8);
            const uint32_t len = read_u32_be(m_data, rec + 12);
            if (static_cast<std::size_t>(off) + static_cast<std::size_t>(len) <= m_data.size()) {
                offset_out = off;
                length_out = len;
                return true;
            }
            return false;
        }
    }
    return false;
}

bool TrueTypeFont::load_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto sz = file.tellg();
    if (sz <= 0 || sz > 32 * 1024 * 1024) return false;
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(sz));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), sz)) return false;
    return load_memory(std::move(bytes));
}

bool TrueTypeFont::load_memory(std::vector<uint8_t> data) {
    m_data = std::move(data);
    m_loaded = false;
    if (m_data.size() < 32) return false;

    const uint32_t sfnt = read_u32_be(m_data, 0);
    if (sfnt != 0x00010000u && sfnt != 0x74727565u) return false; // TrueType outlines required

    uint32_t head_off = 0, head_len = 0;
    uint32_t hhea_off = 0, hhea_len = 0;
    uint32_t maxp_off = 0, maxp_len = 0;
    uint32_t hmtx_off = 0, hmtx_len = 0;
    uint32_t cmap_off = 0, cmap_len = 0;
    if (!find_table("head", head_off, head_len) || head_len < 54) return false;
    if (!find_table("hhea", hhea_off, hhea_len) || hhea_len < 36) return false;
    if (!find_table("maxp", maxp_off, maxp_len) || maxp_len < 6) return false;
    if (!find_table("hmtx", hmtx_off, hmtx_len)) return false;
    if (!find_table("cmap", cmap_off, cmap_len) || cmap_len < 4) return false;
    if (!find_table("loca", m_loca_offset, m_loca_length)) return false;
    if (!find_table("glyf", m_glyf_offset, m_glyf_length)) return false;

    m_units_per_em = read_u16_be(m_data, head_off + 18);
    if (m_units_per_em == 0) return false;
    m_loc_format = read_i16_be(m_data, head_off + 50);
    if (m_loc_format != 0 && m_loc_format != 1) return false;

    m_ascent = read_i16_be(m_data, hhea_off + 4);
    m_descent = read_i16_be(m_data, hhea_off + 6);
    m_num_hmetrics = read_u16_be(m_data, hhea_off + 34);
    m_num_glyphs = read_u16_be(m_data, maxp_off + 4);
    if (m_num_glyphs == 0 || m_num_hmetrics == 0) return false;
    if (static_cast<std::size_t>(m_num_hmetrics) * 4u > hmtx_len) return false;

    m_advance_widths.resize(m_num_glyphs);
    uint16_t last_adv = 0;
    for (uint16_t i = 0; i < m_num_glyphs; ++i) {
        if (i < m_num_hmetrics) {
            last_adv = read_u16_be(m_data, hmtx_off + static_cast<std::size_t>(i) * 4u);
        }
        m_advance_widths[i] = last_adv;
    }

    // Select Unicode cmap subtable (prefer format 12 full Unicode, else format 4 BMP).
    const uint16_t num_cmap_tables = read_u16_be(m_data, cmap_off + 2);
    uint32_t best_sub = 0;
    uint16_t best_fmt = 0;
    for (uint16_t i = 0; i < num_cmap_tables; ++i) {
        const std::size_t rec = cmap_off + 4u + static_cast<std::size_t>(i) * 8u;
        if (rec + 8 > m_data.size()) break;
        const uint16_t pid = read_u16_be(m_data, rec);
        const uint16_t eid = read_u16_be(m_data, rec + 2);
        const uint32_t rel = read_u32_be(m_data, rec + 4);
        if (rel >= cmap_len) continue;
        const uint32_t sub = cmap_off + rel;
        if (sub + 4 > m_data.size()) continue;
        const uint16_t fmt = read_u16_be(m_data, sub);

        const bool is_unicode = (pid == 0) || (pid == 3 && (eid == 1 || eid == 10));
        if (!is_unicode) continue;
        if (fmt == 12 && sub + 16 <= m_data.size()) {
            best_sub = sub;
            best_fmt = 12;
            break;
        }
        if (fmt == 4 && best_fmt == 0 && sub + 14 <= m_data.size()) {
            best_sub = sub;
            best_fmt = 4;
        }
    }
    if (best_fmt == 0) return false;
    m_cmap_subtable = best_sub;
    m_cmap_format = best_fmt;
    m_loaded = true;

    for (uint32_t cp = 0; cp < 128; ++cp) {
        m_ascii_glyphs[cp] = glyph_index(cp);
    }
    return true;
}

uint16_t TrueTypeFont::glyph_index(uint32_t codepoint) const {
    if (!m_loaded) return 0;
    if (m_cmap_format == 12) {
        const uint32_t num_groups = read_u32_be(m_data, m_cmap_subtable + 12);
        const std::size_t groups_base = m_cmap_subtable + 16u;
        if (groups_base + static_cast<std::size_t>(num_groups) * 12u > m_data.size()) return 0;
        uint32_t lo = 0, hi = num_groups;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const std::size_t g = groups_base + static_cast<std::size_t>(mid) * 12u;
            const uint32_t start_cp = read_u32_be(m_data, g);
            const uint32_t end_cp = read_u32_be(m_data, g + 4);
            if (codepoint < start_cp) {
                hi = mid;
            } else if (codepoint > end_cp) {
                lo = mid + 1;
            } else {
                const uint32_t start_gid = read_u32_be(m_data, g + 8);
                const uint32_t gid = start_gid + (codepoint - start_cp);
                return (gid < m_num_glyphs) ? static_cast<uint16_t>(gid) : 0;
            }
        }
        return 0;
    }

    if (m_cmap_format == 4 && codepoint <= 0xFFFFu) {
        const uint16_t seg_count_x2 = read_u16_be(m_data, m_cmap_subtable + 6);
        const uint16_t seg_count = seg_count_x2 / 2;
        const std::size_t end_codes = m_cmap_subtable + 14u;
        const std::size_t start_codes = end_codes + seg_count_x2 + 2u;
        const std::size_t id_deltas = start_codes + seg_count_x2;
        const std::size_t id_range_offsets = id_deltas + seg_count_x2;
        if (id_range_offsets + seg_count_x2 > m_data.size()) return 0;

        uint16_t lo = 0, hi = seg_count;
        while (lo < hi) {
            const uint16_t mid = static_cast<uint16_t>(lo + (hi - lo) / 2);
            const uint16_t end_c = read_u16_be(m_data, end_codes + mid * 2u);
            if (codepoint > end_c) {
                lo = static_cast<uint16_t>(mid + 1);
            } else {
                hi = mid;
            }
        }
        if (lo >= seg_count) return 0;
        const uint16_t start_c = read_u16_be(m_data, start_codes + lo * 2u);
        if (codepoint < start_c) return 0;
        const int16_t id_delta = read_i16_be(m_data, id_deltas + lo * 2u);
        const std::size_t ro_pos = id_range_offsets + lo * 2u;
        const uint16_t id_range_offset = read_u16_be(m_data, ro_pos);
        if (id_range_offset == 0) {
            const uint16_t gid = static_cast<uint16_t>((static_cast<int32_t>(codepoint) + id_delta) & 0xFFFF);
            return (gid < m_num_glyphs) ? gid : 0;
        }
        const std::size_t glyph_addr = ro_pos + id_range_offset + (codepoint - start_c) * 2u;
        if (glyph_addr + 2 > m_data.size()) return 0;
        uint16_t gid = read_u16_be(m_data, glyph_addr);
        if (gid == 0) return 0;
        gid = static_cast<uint16_t>((static_cast<int32_t>(gid) + id_delta) & 0xFFFF);
        return (gid < m_num_glyphs) ? gid : 0;
    }
    return 0;
}

float TrueTypeFont::glyph_advance(uint32_t codepoint, float font_size_px) const {
    if (!m_loaded || m_units_per_em == 0) return font_size_px * 0.55f;
    const uint16_t gid = (codepoint < 128) ? m_ascii_glyphs[codepoint] : glyph_index(codepoint);
    const uint16_t adv = (gid < m_advance_widths.size()) ? m_advance_widths[gid] : m_advance_widths[0];
    return static_cast<float>(adv) * (font_size_px / static_cast<float>(m_units_per_em));
}

bool TrueTypeFont::glyph_data_range(uint16_t glyph_id, uint32_t& start_out, uint32_t& end_out) const {
    if (!m_loaded || glyph_id >= m_num_glyphs) return false;
    if (m_loc_format == 0) {
        const std::size_t pos = m_loca_offset + static_cast<std::size_t>(glyph_id) * 2u;
        if (pos + 4 > m_loca_offset + m_loca_length || pos + 4 > m_data.size()) return false;
        start_out = static_cast<uint32_t>(read_u16_be(m_data, pos)) * 2u;
        end_out = static_cast<uint32_t>(read_u16_be(m_data, pos + 2)) * 2u;
    } else {
        const std::size_t pos = m_loca_offset + static_cast<std::size_t>(glyph_id) * 4u;
        if (pos + 8 > m_loca_offset + m_loca_length || pos + 8 > m_data.size()) return false;
        start_out = read_u32_be(m_data, pos);
        end_out = read_u32_be(m_data, pos + 4);
    }
    if (end_out < start_out || end_out > m_glyf_length) return false;
    start_out += m_glyf_offset;
    end_out += m_glyf_offset;
    return end_out <= m_data.size();
}

void TrueTypeFont::append_glyph_edges(
    uint16_t glyph_id,
    float scale,
    float baseline_y,
    float a, float b, float c, float d,
    float tx_units, float ty_units,
    int depth,
    std::vector<Edge>& edges_out
) const {
    if (depth > 8) return;
    uint32_t g_start = 0, g_end = 0;
    if (!glyph_data_range(glyph_id, g_start, g_end) || g_start == g_end) return;
    if (g_start + 10 > g_end) return;

    const int16_t num_contours = read_i16_be(m_data, g_start);
    if (num_contours == 0) return;

    if (num_contours > 0) {
        const std::size_t contours_pos = g_start + 10u;
        const std::size_t ins_len_pos = contours_pos + static_cast<std::size_t>(num_contours) * 2u;
        if (ins_len_pos + 2 > g_end) return;
        const uint16_t num_pts = static_cast<uint16_t>(read_u16_be(m_data, ins_len_pos - 2) + 1u);
        if (num_pts == 0 || num_pts > 4096) return;

        const uint16_t ins_len = read_u16_be(m_data, ins_len_pos);
        std::size_t cur = ins_len_pos + 2u + ins_len;
        if (cur > g_end) return;

        std::vector<uint8_t> flags(num_pts);
        for (uint16_t i = 0; i < num_pts; ) {
            if (cur >= g_end) return;
            const uint8_t f = m_data[cur++];
            flags[i++] = f;
            if (f & 0x08u) {
                if (cur >= g_end) return;
                uint8_t repeat = m_data[cur++];
                while (repeat-- > 0 && i < num_pts) {
                    flags[i++] = f;
                }
            }
        }

        std::vector<int32_t> xs(num_pts, 0);
        std::vector<int32_t> ys(num_pts, 0);
        int32_t acc_x = 0;
        for (uint16_t i = 0; i < num_pts; ++i) {
            const uint8_t f = flags[i];
            if (f & 0x02u) {
                if (cur >= g_end) return;
                const int32_t dx = m_data[cur++];
                acc_x += (f & 0x10u) ? dx : -dx;
            } else if (!(f & 0x10u)) {
                if (cur + 2 > g_end) return;
                acc_x += read_i16_be(m_data, cur);
                cur += 2;
            }
            xs[i] = acc_x;
        }

        int32_t acc_y = 0;
        for (uint16_t i = 0; i < num_pts; ++i) {
            const uint8_t f = flags[i];
            if (f & 0x04u) {
                if (cur >= g_end) return;
                const int32_t dy = m_data[cur++];
                acc_y += (f & 0x20u) ? dy : -dy;
            } else if (!(f & 0x20u)) {
                if (cur + 2 > g_end) return;
                acc_y += read_i16_be(m_data, cur);
                cur += 2;
            }
            ys[i] = acc_y;
        }

        auto map_pt = [&](int32_t rx, int32_t ry) -> Vec2 {
            const float ux = a * static_cast<float>(rx) + c * static_cast<float>(ry) + tx_units;
            const float uy = b * static_cast<float>(rx) + d * static_cast<float>(ry) + ty_units;
            return Vec2{ux * scale, baseline_y - uy * scale};
        };

        uint16_t c_start = 0;
        std::vector<std::pair<float, float>> poly;
        for (int16_t ci = 0; ci < num_contours; ++ci) {
            const uint16_t c_end = read_u16_be(m_data, contours_pos + static_cast<std::size_t>(ci) * 2u);
            if (c_end >= num_pts || c_end < c_start) return;
            const uint16_t count = static_cast<uint16_t>(c_end - c_start + 1u);
            if (count < 2) {
                c_start = static_cast<uint16_t>(c_end + 1u);
                continue;
            }

            // Expand implied on-curve midpoints between consecutive off-curve points.
            struct CurvePt { Vec2 p; bool on; };
            std::vector<CurvePt> cpts;
            cpts.reserve(count * 2u);
            for (uint16_t k = 0; k < count; ++k) {
                const uint16_t idx = static_cast<uint16_t>(c_start + k);
                const Vec2 p = map_pt(xs[idx], ys[idx]);
                const bool on = (flags[idx] & 0x01u) != 0;
                if (!cpts.empty() && !cpts.back().on && !on) {
                    const Vec2 mid{0.5f * (cpts.back().p.x + p.x), 0.5f * (cpts.back().p.y + p.y)};
                    cpts.push_back(CurvePt{mid, true});
                }
                cpts.push_back(CurvePt{p, on});
            }
            if (!cpts.front().on && !cpts.back().on) {
                const Vec2 mid{0.5f * (cpts.back().p.x + cpts.front().p.x),
                               0.5f * (cpts.back().p.y + cpts.front().p.y)};
                cpts.insert(cpts.begin(), CurvePt{mid, true});
            } else if (!cpts.front().on && cpts.back().on) {
                CurvePt last = cpts.back();
                cpts.pop_back();
                cpts.insert(cpts.begin(), last);
            }

            poly.clear();
            if (!cpts.empty()) {
                poly.emplace_back(cpts[0].p.x, cpts[0].p.y);
                std::size_t k = 1;
                while (k < cpts.size()) {
                    if (cpts[k].on) {
                        poly.emplace_back(cpts[k].p.x, cpts[k].p.y);
                        ++k;
                    } else {
                        const Vec2 ctrl = cpts[k].p;
                        const Vec2 next_on = (k + 1 < cpts.size()) ? cpts[k + 1].p : cpts[0].p;
                        const auto [x0, y0] = poly.back();
                        flatten_quad_bezier(x0, y0, ctrl.x, ctrl.y, next_on.x, next_on.y, 0, poly);
                        k += 2;
                    }
                }
                if (poly.front() != poly.back()) {
                    poly.push_back(poly.front());
                }
                for (std::size_t e = 0; e + 1 < poly.size(); ++e) {
                    const auto [x0, y0] = poly[e];
                    const auto [x1, y1] = poly[e + 1];
                    if (std::fabs(y1 - y0) > 1e-5f) {
                        edges_out.push_back(Edge{x0, y0, x1, y1});
                    }
                }
            }

            c_start = static_cast<uint16_t>(c_end + 1u);
        }
        return;
    }

    // Compound glyph (`num_contours < 0`): compose transformed component glyphs.
    std::size_t cur = g_start + 10u;
    uint16_t comp_flags = 0x0020u;
    while ((comp_flags & 0x0020u) && cur + 4 <= g_end) {
        comp_flags = read_u16_be(m_data, cur);
        const uint16_t sub_gid = read_u16_be(m_data, cur + 2);
        cur += 4;

        float dx = 0.0f, dy = 0.0f;
        if (comp_flags & 0x0001u) {
            if (cur + 4 > g_end) return;
            if (comp_flags & 0x0002u) {
                dx = static_cast<float>(read_i16_be(m_data, cur));
                dy = static_cast<float>(read_i16_be(m_data, cur + 2));
            }
            cur += 4;
        } else {
            if (cur + 2 > g_end) return;
            if (comp_flags & 0x0002u) {
                dx = static_cast<float>(static_cast<int8_t>(m_data[cur]));
                dy = static_cast<float>(static_cast<int8_t>(m_data[cur + 1]));
            }
            cur += 2;
        }

        float la = 1.0f, lb = 0.0f, lc = 0.0f, ld = 1.0f;
        if (comp_flags & 0x0008u) {
            if (cur + 2 > g_end) return;
            la = ld = static_cast<float>(read_i16_be(m_data, cur)) / 16384.0f;
            cur += 2;
        } else if (comp_flags & 0x0040u) {
            if (cur + 4 > g_end) return;
            la = static_cast<float>(read_i16_be(m_data, cur)) / 16384.0f;
            ld = static_cast<float>(read_i16_be(m_data, cur + 2)) / 16384.0f;
            cur += 4;
        } else if (comp_flags & 0x0080u) {
            if (cur + 8 > g_end) return;
            la = static_cast<float>(read_i16_be(m_data, cur)) / 16384.0f;
            lb = static_cast<float>(read_i16_be(m_data, cur + 2)) / 16384.0f;
            lc = static_cast<float>(read_i16_be(m_data, cur + 4)) / 16384.0f;
            ld = static_cast<float>(read_i16_be(m_data, cur + 6)) / 16384.0f;
            cur += 8;
        }

        const float na = a * la + c * lb;
        const float nb = b * la + d * lb;
        const float nc = a * lc + c * ld;
        const float nd = b * lc + d * ld;
        const float ntx = a * dx + c * dy + tx_units;
        const float nty = b * dx + d * dy + ty_units;
        append_glyph_edges(sub_gid, scale, baseline_y, na, nb, nc, nd, ntx, nty, depth + 1, edges_out);
    }
}

GlyphBitmap TrueTypeFont::rasterize_glyph(uint32_t codepoint, int pixel_size) const {
    const int px = std::clamp(pixel_size, 6, 160);
    if (!m_loaded) {
        return rasterize_fallback_8x8(codepoint, px, false);
    }

    GlyphBitmap bmp;
    const float scale = static_cast<float>(px) / static_cast<float>(m_units_per_em);
    const uint16_t gid = (codepoint < 128) ? m_ascii_glyphs[codepoint] : glyph_index(codepoint);
    const uint16_t adv = (gid < m_advance_widths.size()) ? m_advance_widths[gid] : m_advance_widths[0];
    bmp.advance = static_cast<float>(adv) * scale;

    std::vector<Edge> edges;
    append_glyph_edges(gid, scale, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0, edges);
    if (edges.empty()) return bmp;

    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    for (const auto& e : edges) {
        min_x = std::min({min_x, e.x0, e.x1});
        min_y = std::min({min_y, e.y0, e.y1});
        max_x = std::max({max_x, e.x0, e.x1});
        max_y = std::max({max_y, e.y0, e.y1});
    }

    const int ix0 = static_cast<int>(std::floor(min_x));
    const int iy0 = static_cast<int>(std::floor(min_y));
    const int ix1 = static_cast<int>(std::ceil(max_x));
    const int iy1 = static_cast<int>(std::ceil(max_y));
    const int w = std::clamp(ix1 - ix0, 0, 256);
    const int h = std::clamp(iy1 - iy0, 0, 256);
    if (w <= 0 || h <= 0) return bmp;

    bmp.width = w;
    bmp.height = h;
    bmp.bearing_x = ix0;
    bmp.bearing_y = -iy0;
    bmp.coverage.assign(static_cast<std::size_t>(w * h), 0);

    // 4-vertical-subscanline x exact analytic horizontal span anti-aliasing.
    std::vector<float> accum(static_cast<std::size_t>(w), 0.0f);
    std::vector<std::pair<float, int>> crossings;
    crossings.reserve(16);

    constexpr int kSubY = 4;
    for (int row = 0; row < h; ++row) {
        std::fill(accum.begin(), accum.end(), 0.0f);
        for (int sy = 0; sy < kSubY; ++sy) {
            const float sample_y = static_cast<float>(iy0 + row) + (static_cast<float>(sy) + 0.5f) / static_cast<float>(kSubY);
            crossings.clear();
            for (const auto& e : edges) {
                if ((e.y0 <= sample_y && e.y1 > sample_y) || (e.y1 <= sample_y && e.y0 > sample_y)) {
                    const float t = (sample_y - e.y0) / (e.y1 - e.y0);
                    const float x = e.x0 + t * (e.x1 - e.x0);
                    const int dir = (e.y1 > e.y0) ? 1 : -1;
                    crossings.emplace_back(x, dir);
                }
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });

            int winding = 0;
            float span_start = 0.0f;
            for (const auto& [cx, dir] : crossings) {
                const int prev = winding;
                winding += dir;
                if (prev == 0 && winding != 0) {
                    span_start = cx;
                } else if (prev != 0 && winding == 0) {
                    const float x_left = span_start - static_cast<float>(ix0);
                    const float x_right = cx - static_cast<float>(ix0);
                    const int c0 = std::max(0, static_cast<int>(std::floor(x_left)));
                    const int c1 = std::min(w, static_cast<int>(std::ceil(x_right)));
                    for (int col = c0; col < c1; ++col) {
                        const float seg_l = std::max(x_left, static_cast<float>(col));
                        const float seg_r = std::min(x_right, static_cast<float>(col + 1));
                        if (seg_r > seg_l) {
                            accum[static_cast<std::size_t>(col)] += (seg_r - seg_l);
                        }
                    }
                }
            }
        }
        for (int col = 0; col < w; ++col) {
            const float cov = std::clamp(accum[static_cast<std::size_t>(col)] / static_cast<float>(kSubY), 0.0f, 1.0f);
            bmp.coverage[static_cast<std::size_t>(row * w + col)] =
                static_cast<uint8_t>(std::lround(cov * 255.0f));
        }
    }

    return bmp;
}

FontRenderer& FontRenderer::instance() {
    static FontRenderer s_instance;
    return s_instance;
}

namespace {

// Where a host keeps TrueType faces. Walked once, at construction, and only
// for the *paths*: a face is parsed when a codepoint needs it, not before, so
// a host with hundreds of fonts costs a directory walk and nothing else.
const char* const kFontDirectories[] = {
    "/usr/share/fonts/truetype",
    "/usr/share/fonts/TTF",
    "/usr/share/fonts",
    "/System/Library/Fonts",
    "/Library/Fonts",
};

/// Every `.ttf` under the system font directories, except the two the primary
/// faces came from — a face is no use as a fallback for itself.
std::vector<std::string> discover_system_fonts(const std::string& exclude_a,
                                               const std::string& exclude_b) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const char* dir : kFontDirectories) {
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
             !ec && it != end;
             it.increment(ec)) {
            if (ec) break;
            const std::string ext = it->path().extension().string();
            if (ext != ".ttf" && ext != ".TTF") continue;
            std::string path = it->path().string();
            if (path == exclude_a || path == exclude_b) continue;
            out.push_back(std::move(path));
            if (out.size() >= FontRenderer::kMaxFallbackFaces) return out;
        }
    }
    return out;
}

}  // namespace

FontRenderer::FontRenderer() {
    static const char* kRegularPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
    };
    static const char* kBoldPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf",
    };

    for (const char* path : kRegularPaths) {
        if (m_regular.load_file(path)) {
            m_regular_path = path;
            break;
        }
    }
    for (const char* path : kBoldPaths) {
        if (m_bold.load_file(path)) {
            m_bold_path = path;
            break;
        }
    }

    m_fallback_paths = discover_system_fonts(m_regular_path, m_bold_path);
}

void FontRenderer::set_fallbacks(std::vector<std::string> paths) {
    m_fallback_paths = std::move(paths);
    m_fallbacks.clear();
    m_next_fallback = 0;
    // A cached glyph may have come from a face that is no longer in the set.
    m_cache.clear();
}

const TrueTypeFont* FontRenderer::face_for(uint32_t codepoint, bool bold) const {
    const TrueTypeFont* primary = (bold && m_bold.is_loaded())
                                      ? &m_bold
                                      : (m_regular.is_loaded() ? &m_regular : nullptr);
    if (primary && primary->has_glyph(codepoint)) return primary;

    // Already loaded as a result of an earlier miss.
    for (const TrueTypeFont& face : m_fallbacks) {
        if (face.has_glyph(codepoint)) return &face;
    }

    // Not covered by anything loaded yet. Load more of what discovery found,
    // one at a time, and stop at the first that covers it: a page that never
    // leaves Latin never pays for the CJK faces on the host.
    std::error_code ec;
    while (m_next_fallback < m_fallback_paths.size() &&
           m_fallbacks.size() < kMaxFallbackFaces) {
        const std::string& path = m_fallback_paths[m_next_fallback++];
        if (!std::filesystem::is_regular_file(path, ec)) continue;
        TrueTypeFont face;
        if (!face.load_file(path)) continue;
        m_fallbacks.push_back(std::move(face));
        if (m_fallbacks.back().has_glyph(codepoint)) return &m_fallbacks.back();
    }

    // No face covers it. The primary draws its `.notdef` (or nothing), which
    // is what happened to every such codepoint before fallback existed.
    return primary;
}

std::pair<float, float> FontRenderer::em_ascent_descent(float font_size, int font_weight) const {
    if (!(font_size > 0.0f)) return {0.0f, 0.0f};
    const bool want_bold = font_weight >= 600;
    const TrueTypeFont* font = (want_bold && m_bold.is_loaded())
                                   ? &m_bold
                                   : (m_regular.is_loaded() ? &m_regular : nullptr);
    if (!font) return {font_size * 0.8f, font_size * 0.2f};
    const float scale = font_size / static_cast<float>(font->units_per_em());
    const float asc = static_cast<float>(font->ascent()) * scale;
    const float desc = -static_cast<float>(font->descent()) * scale;
    return {asc, desc};
}

float FontRenderer::glyph_advance_px(uint32_t codepoint, float font_size, int font_weight) const {
    const bool want_bold = font_weight >= 600;
    const TrueTypeFont* font = face_for(codepoint, want_bold);
    if (font) return font->glyph_advance(codepoint, font_size);
    return std::max(1.0f, font_size * 0.55f);
}

float FontRenderer::measure_text(std::string_view utf8_text, float font_size, int font_weight) {
    if (utf8_text.empty() || font_size <= 0.0f) return 0.0f;
    float total = 0.0f;
    std::size_t pos = 0;
    while (pos < utf8_text.size()) {
        const uint32_t cp = next_utf8_codepoint(utf8_text, pos);
        total += glyph_advance_px(cp, font_size, font_weight);
    }
    return total;
}

const GlyphBitmap& FontRenderer::get_glyph(uint32_t codepoint, int pixel_size, bool bold) {
    const int px = std::clamp(pixel_size, 6, 160);
    const uint64_t key = (static_cast<uint64_t>(bold ? 1u : 0u) << 40) |
                         (static_cast<uint64_t>(static_cast<uint32_t>(px)) << 32) |
                         static_cast<uint64_t>(codepoint);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    if (m_cache.size() >= kMaxCachedGlyphs) {
        m_cache.clear();
    }

    GlyphBitmap bmp;
    const TrueTypeFont* face = face_for(codepoint, bold);
    if (face) {
        bmp = face->rasterize_glyph(codepoint, px);
    } else {
        bmp = rasterize_fallback_8x8(codepoint, px, bold);
    }

    auto [ins, _] = m_cache.emplace(key, std::move(bmp));
    return ins->second;
}

void FontRenderer::draw_text(
    float top_x,
    float top_y,
    std::string_view utf8_text,
    float font_size,
    int font_weight,
    const std::function<void(int px, int py, uint8_t coverage)>& emit_pixel
) {
    if (utf8_text.empty() || font_size <= 0.0f) return;
    const int px_size = std::clamp(static_cast<int>(std::lround(font_size)), 6, 160);
    const bool bold = font_weight >= 600;
    const bool synthetic_bold = bold && !m_bold.is_loaded() && m_regular.is_loaded();

    const TrueTypeFont* font = (bold && m_bold.is_loaded())
                                   ? &m_bold
                                   : (m_regular.is_loaded() ? &m_regular : nullptr);
    // Position baseline comfortably inside the `1.25 * font_size` CSS line box.
    float baseline_offset = font_size * 0.92f;
    if (font) {
        const float scale = font_size / static_cast<float>(font->units_per_em());
        const float asc = static_cast<float>(font->ascent()) * scale;
        const float desc = static_cast<float>(-font->descent()) * scale;
        const float leading = std::max(0.0f, font_size * 1.25f - (asc + desc));
        baseline_offset = asc + leading * 0.5f;
    }

    float pen_x = top_x;
    const int baseline_y = static_cast<int>(std::lround(top_y + baseline_offset));

    std::size_t pos = 0;
    while (pos < utf8_text.size()) {
        const uint32_t cp = next_utf8_codepoint(utf8_text, pos);
        const GlyphBitmap& g = get_glyph(cp, px_size, bold);
        if (g.width > 0 && g.height > 0) {
            const int gx0 = static_cast<int>(std::lround(pen_x)) + g.bearing_x;
            const int gy0 = baseline_y - g.bearing_y;
            for (int r = 0; r < g.height; ++r) {
                for (int c = 0; c < g.width; ++c) {
                    const uint8_t cov = g.coverage[static_cast<std::size_t>(r * g.width + c)];
                    if (cov > 0) {
                        emit_pixel(gx0 + c, gy0 + r, cov);
                        if (synthetic_bold) {
                            emit_pixel(gx0 + c + 1, gy0 + r, cov);
                        }
                    }
                }
            }
        }
        pen_x += (font ? font->glyph_advance(cp, font_size) : g.advance);
    }
}

} // namespace litebird
