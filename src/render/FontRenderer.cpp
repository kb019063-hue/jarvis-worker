#include "litebird/render/FontRenderer.hpp"

#include <algorithm>
#include <map>
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

/// How far a synthesised oblique leans: 0.25 px of horizontal run per px of
/// height above the baseline, which is tan(14 degrees) -- the angle CSS Fonts 4
/// gives an `oblique` that names none, and the one Chrome skews by.
constexpr float kSyntheticObliqueSkew = 0.25f;

/// Leans a rasterized glyph as if it had come from an italic face.
///
/// A host with no italic face is the normal case, not the exception -- the
/// machine this was written on has six DejaVu files and not one of them is
/// slanted -- and CSS says a browser that cannot find a slanted face skews an
/// upright one rather than leave the text upright. So the shear happens on the
/// bitmap, per row: every row is the glyph at one height above the baseline,
/// which is exactly the line a shear moves along.
///
/// Rows above the baseline move right and rows below it -- a descender -- move
/// left, so the result is given room on both sides and its bearing moves with
/// it. Getting that wrong is not cosmetic: clip the descender's overhang and
/// the tail of every `g` is cut off.
void apply_synthetic_oblique(GlyphBitmap& bmp) {
    if (bmp.width <= 0 || bmp.height <= 0) return;
    if (bmp.coverage.size() < static_cast<std::size_t>(bmp.width) * static_cast<std::size_t>(bmp.height)) {
        return;
    }

    const auto shift_of_row = [&bmp](int row) {
        // `bearing_y` is how far the top row is above the baseline, so row `r`
        // is `bearing_y - r` px above it, and leans right by that much times
        // the skew.
        const float above_baseline = static_cast<float>(bmp.bearing_y - row);
        return static_cast<int>(std::lround(kSyntheticObliqueSkew * above_baseline));
    };

    int min_shift = 0;
    int max_shift = 0;
    for (int r = 0; r < bmp.height; ++r) {
        const int shift = shift_of_row(r);
        min_shift = std::min(min_shift, shift);
        max_shift = std::max(max_shift, shift);
    }

    const int new_width = bmp.width + (max_shift - min_shift);
    std::vector<uint8_t> sheared(static_cast<std::size_t>(new_width) * static_cast<std::size_t>(bmp.height), 0);
    for (int r = 0; r < bmp.height; ++r) {
        const int offset = shift_of_row(r) - min_shift;
        for (int c = 0; c < bmp.width; ++c) {
            const uint8_t coverage = bmp.coverage[static_cast<std::size_t>(r) * static_cast<std::size_t>(bmp.width) +
                                                  static_cast<std::size_t>(c)];
            if (coverage != 0) {
                sheared[static_cast<std::size_t>(r) * static_cast<std::size_t>(new_width) +
                        static_cast<std::size_t>(c + offset)] = coverage;
            }
        }
    }

    bmp.bearing_x += min_shift;
    bmp.width = new_width;
    bmp.coverage = std::move(sheared);
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

// --- the `name` table -------------------------------------------------------
//
// A CSS `font-family` list names families in English words ("DejaVu Serif",
// "monospace"). The only place a font file says which of those it is is its
// `name` table, so family selection has to read it — and to read it for every
// `.ttf` on the host without loading a single one of them, because the whole
// point of asking is to decide what to load.
//
// The table is: uint16 format, uint16 count, uint16 stringOffset, then `count`
// records of {platformID, encodingID, languageID, nameID, length, offset}, with
// the strings themselves at `stringOffset` from the start of the table.
//
// Name ids: 1 family, 2 subfamily, 16 typographic family, 17 typographic
// subfamily. Where 16/17 exist they are the better answer: a family of eight
// weights is one typographic family and four legacy families.

namespace {

constexpr uint16_t kNameFamily = 1;
constexpr uint16_t kNameSubfamily = 2;
constexpr uint16_t kNameTypoFamily = 16;
constexpr uint16_t kNameTypoSubfamily = 17;

/// How much a `name` record is preferred, higher first. Windows UCS-2 is what
/// essentially every font on the platforms this runs on has; Unicode-platform
/// records are the same encoding and equally good; Mac Roman is a last resort,
/// because decoding it properly needs a table nobody here can test.
int name_record_rank(uint16_t platform, uint16_t encoding, bool is_english) {
    if ((platform == 3 && (encoding == 1 || encoding == 0)) || platform == 0) {
        return is_english ? 30 : 20;
    }
    if (platform == 1 && encoding == 0) {
        return is_english ? 10 : 5;
    }
    return 0;                     // an encoding this cannot decode is not an answer
}

/// The bytes of a `name` record as UTF-8.
///
/// Windows and Unicode-platform strings are UTF-16BE ( UCS-4 for the two
/// encodings nobody ships, which are left out rather than guessed at). Mac
/// Roman is taken byte by byte, and a byte outside ASCII is dropped: a mangled
/// name would match the wrong family, and a short one merely fails to match,
/// which falls through to the next candidate. That is the right way to be
/// wrong.
std::string name_record_string(const std::vector<uint8_t>& table,
                               std::size_t strings, uint16_t length,
                               uint16_t offset, uint16_t platform) {
    const std::size_t start = strings + offset;
    if (offset + length > table.size() || start + length > table.size()) return {};

    if (platform == 1) {
        std::string out;
        for (uint16_t i = 0; i < length; ++i) {
            const uint8_t b = table[start + i];
            if (b < 0x80 && b != 0) out.push_back(static_cast<char>(b));
        }
        return out;
    }

    std::string out;
    for (uint16_t i = 0; i + 1 < length; i += 2) {
        const uint32_t cp = (static_cast<uint32_t>(table[start + i]) << 8) |
                            static_cast<uint32_t>(table[start + i + 1]);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

} // namespace

bool TrueTypeFont::read_names(const std::string& path, Names& out) {
    out = Names{};
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    uint8_t header[12];
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
    const uint32_t sfnt = (static_cast<uint32_t>(header[0]) << 24) |
                          (static_cast<uint32_t>(header[1]) << 16) |
                          (static_cast<uint32_t>(header[2]) << 8) |
                          static_cast<uint32_t>(header[3]);
    if (sfnt != 0x00010000u && sfnt != 0x74727565u && sfnt != 0x4F54544Fu) return false;

    const uint16_t num_tables = (static_cast<uint16_t>(header[4]) << 8) | header[5];
    // Three tables, one pass over the directory: the name (to be asked for by
    // name), the weight and slant bits (`OS/2`), and the slant bit older fonts
    // put in `head` instead. The directory entries are read once and the tables
    // afterwards, because a file is a stream and this is two seeks cheaper than
    // walking it twice.
    uint32_t name_off = 0, name_len = 0;
    uint32_t os2_off = 0, os2_len = 0;
    uint32_t head_off = 0, head_len = 0;
    for (uint16_t i = 0; i < num_tables; ++i) {
        uint8_t rec[16];
        if (!file.read(reinterpret_cast<char*>(rec), sizeof(rec))) return false;
        const uint32_t off = (static_cast<uint32_t>(rec[8]) << 24) |
                             (static_cast<uint32_t>(rec[9]) << 16) |
                             (static_cast<uint32_t>(rec[10]) << 8) | rec[11];
        const uint32_t len = (static_cast<uint32_t>(rec[12]) << 24) |
                             (static_cast<uint32_t>(rec[13]) << 16) |
                             (static_cast<uint32_t>(rec[14]) << 8) | rec[15];
        if (std::memcmp(rec, "name", 4) == 0) {
            name_off = off;
            name_len = len;
        } else if (std::memcmp(rec, "OS/2", 4) == 0) {
            os2_off = off;
            os2_len = len;
        } else if (std::memcmp(rec, "head", 4) == 0) {
            head_off = off;
            head_len = len;
        }
    }
    if (name_len < 6 || name_len > 4u * 1024u * 1024u) return false;

    std::vector<uint8_t> table(name_len);
    file.clear();
    file.seekg(static_cast<std::streamoff>(name_off), std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(name_len))) {
        return false;
    }

    const uint16_t count = read_u16_be(table, 2);
    const uint16_t strings = read_u16_be(table, 4);
    if (6u + static_cast<std::size_t>(count) * 12u > table.size()) return false;

    // Best record seen so far for each of the four ids.
    int best_rank[18] = {0};
    std::string best[18];
    for (uint16_t i = 0; i < count; ++i) {
        const std::size_t rec = 6u + static_cast<std::size_t>(i) * 12u;
        const uint16_t platform = read_u16_be(table, rec);
        const uint16_t encoding = read_u16_be(table, rec + 2);
        const uint16_t language = read_u16_be(table, rec + 4);
        const uint16_t name_id = read_u16_be(table, rec + 6);
        const uint16_t length = read_u16_be(table, rec + 8);
        const uint16_t offset = read_u16_be(table, rec + 10);
        if (name_id >= 18) continue;

        const int rank = name_record_rank(platform, encoding, language == 0x0409);
        if (rank <= best_rank[name_id]) continue;
        std::string s = name_record_string(table, strings, length, offset, platform);
        if (s.empty()) continue;
        best_rank[name_id] = rank;
        best[name_id] = std::move(s);
    }

    out.family = best[kNameTypoFamily].empty() ? best[kNameFamily] : best[kNameTypoFamily];
    out.subfamily = best[kNameTypoSubfamily].empty() ? best[kNameSubfamily] : best[kNameTypoSubfamily];

    // The two bits that say what the face *is*, which its name only claims.
    // `usWeightClass` is at 4 and `fsSelection` at 62 in every version of the
    // table (0 through 5), and the shortest table is 78 bytes, so a 64-byte read
    // is enough for both and short files are refused rather than guessed at.
    const auto read_table = [&file](uint32_t off, uint32_t len, std::size_t want) {
        std::vector<uint8_t> bytes;
        if (len < want) return bytes;
        bytes.resize(want);
        file.clear();
        file.seekg(static_cast<std::streamoff>(off), std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(want))) {
            bytes.clear();
        }
        return bytes;
    };
    if (const std::vector<uint8_t> os2 = read_table(os2_off, os2_len, 64); os2.size() == 64) {
        const uint16_t weight = read_u16_be(os2, 4);
        // 1..1000 is the scale; anything else is a table this cannot trust.
        if (weight >= 1 && weight <= 1000) out.weight = weight;
        if ((read_u16_be(os2, 62) & 0x0001u) != 0) out.italic = true;
    }
    if (const std::vector<uint8_t> head = read_table(head_off, head_len, 46); head.size() == 46) {
        if ((read_u16_be(head, 44) & 0x0002u) != 0) out.italic = true;
    }

    return !out.family.empty();
}

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

    // `kern` is optional and its absence is not a failure: a font that does not
    // say how its pairs close up is a font whose pairs do not close up, and
    // that is a normal state of affairs. Version 0 is the Windows table; the
    // Apple version-1 table starts with 0x00010000 as a 32-bit number, is a
    // different format inside, and is left out rather than guessed at.
    m_ligature_sets.clear();
    uint32_t gsub_off = 0, gsub_len = 0;
    if (find_table("GSUB", gsub_off, gsub_len)) load_ligatures(gsub_off, gsub_len);

    m_kern_pairs_offset = 0;
    m_kern_pairs = 0;
    uint32_t kern_off = 0, kern_len = 0;
    // header: version u16, nTables u16; each subtable: version, length,
    // coverage, then the format-0 pair list's own search header.
    if (find_table("kern", kern_off, kern_len) && kern_len >= 18 && read_u16_be(m_data, kern_off) == 0) {
        const uint16_t subtables = read_u16_be(m_data, kern_off + 2);
        uint32_t pos = kern_off + 4;
        for (uint16_t i = 0; i < subtables; ++i) {
            if (pos + 14 > kern_off + kern_len) break;
            const uint16_t sub_length = read_u16_be(m_data, pos + 2);
            const uint16_t coverage = read_u16_be(m_data, pos + 4);
            // Format 0 is the pair list; bit 0 is "this affects the
            // horizontal advance", which is the only direction the pen has
            // here, and bit 2 would mean a cross-stream table whose values are
            // not advances at all.
            const bool format0 = (coverage >> 8) == 0;
            const bool horizontal = (coverage & 0x1u) != 0;
            const bool crosses = (coverage & 0x4u) != 0;
            if (format0 && horizontal && !crosses && sub_length >= 14) {
                const uint16_t pairs = read_u16_be(m_data, pos + 6);
                const std::size_t pairs_end = pos + 14u + static_cast<std::size_t>(pairs) * 6u;
                if (pairs > 0 && pairs_end <= static_cast<std::size_t>(kern_off) + kern_len &&
                    pairs_end <= m_data.size()) {
                    m_kern_pairs_offset = pos + 14;
                    m_kern_pairs = pairs;
                }
                break;
            }
            if (sub_length < 6) break;
            pos += sub_length;
        }
    }

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

bool TrueTypeFont::load_ligatures(uint32_t gsub_offset, uint32_t gsub_length) {
    m_ligature_sets.clear();
    const std::size_t table_end = static_cast<std::size_t>(gsub_offset) + gsub_length;
    if (gsub_length < 10 || table_end > m_data.size()) return false;
    const uint32_t version = read_u32_be(m_data, gsub_offset);
    // 1.0 and 1.1 have the same three lists at the same offsets; 1.1 adds a
    // feature-variations table after them, which nothing here asks for yet.
    if (version != 0x00010000u && version != 0x00010001u) return false;

    const auto within = [table_end](std::size_t pos, std::size_t need) {
        return pos + need <= table_end;
    };
    const uint32_t script_list = gsub_offset + read_u16_be(m_data, gsub_offset + 4);
    const uint32_t feature_list = gsub_offset + read_u16_be(m_data, gsub_offset + 6);
    const uint32_t lookup_list = gsub_offset + read_u16_be(m_data, gsub_offset + 8);
    if (!within(script_list, 2) || !within(feature_list, 2) || !within(lookup_list, 2)) return false;

    // Which script: `latn` when there is one, else the default. A page of Latin
    // text is what this engine lays out, and `DFLT` is the table's own answer
    // for a font that does not name Latin separately.
    std::size_t lang_sys = 0;
    {
        std::size_t latn_script = 0, dflt_script = 0;
        const uint16_t script_count = read_u16_be(m_data, script_list);
        for (uint16_t i = 0; i < script_count; ++i) {
            const std::size_t record = script_list + 2u + 6u * i;
            if (!within(record, 6)) return false;
            const uint32_t tag = read_u32_be(m_data, record);
            const uint32_t script = script_list + read_u16_be(m_data, record + 4);
            if (tag == 0x6C61746Eu && !latn_script) latn_script = script;   // 'latn'
            if (tag == 0x44464C54u && !dflt_script) dflt_script = script;   // 'DFLT'
        }
        const std::size_t script = latn_script ? latn_script : dflt_script;
        if (!script || !within(script, 4)) return false;
        const uint16_t default_lang_sys = read_u16_be(m_data, script);
        if (default_lang_sys == 0 || !within(script + default_lang_sys, 6)) return false;
        lang_sys = script + default_lang_sys;
    }

    // The features this language system turns on, filtered to the two that mean
    // "join these letters": `liga` (common) and `rlig` (required).
    std::vector<uint16_t> lookup_indices;
    const auto add_feature = [&](uint16_t feature_index) {
        const uint16_t feature_count = read_u16_be(m_data, feature_list);
        if (feature_index >= feature_count) return;
        const std::size_t record = feature_list + 2u + 6u * feature_index;
        if (!within(record, 6)) return;
        const uint32_t tag = read_u32_be(m_data, record);
        if (tag != 0x6C696761u && tag != 0x726C6967u) return;   // 'liga', 'rlig'
        const std::size_t feature = feature_list + read_u16_be(m_data, record + 4);
        if (!within(feature, 4)) return;
        const uint16_t lookup_count = read_u16_be(m_data, feature + 2);
        for (uint16_t i = 0; i < lookup_count; ++i) {
            if (!within(feature + 4u + 2u * i, 2)) return;
            lookup_indices.push_back(read_u16_be(m_data, feature + 4u + 2u * i));
        }
    };
    const uint16_t feature_count = read_u16_be(m_data, lang_sys + 4);
    for (uint16_t i = 0; i < feature_count; ++i) {
        if (!within(lang_sys + 6u + 2u * i, 2)) return false;
        add_feature(read_u16_be(m_data, lang_sys + 6u + 2u * i));
    }
    const uint16_t required_feature = read_u16_be(m_data, lang_sys + 2);
    if (required_feature != 0xFFFF) add_feature(required_feature);
    if (lookup_indices.empty()) return false;

    // Each lookup, as a set of type-4 (ligature) subtables. A type-7 lookup is
    // a wrapper that points at another type, which is how a font with more than
    // 64k of lookups keeps its offsets small; follow it rather than skip it.
    std::map<uint16_t, std::vector<Ligature>> by_first;
    for (uint16_t lookup_index : lookup_indices) {
        const uint16_t lookup_count = read_u16_be(m_data, lookup_list);
        if (lookup_index >= lookup_count) continue;
        const std::size_t lookup_record = lookup_list + 2u + 2u * lookup_index;
        if (!within(lookup_record, 2)) continue;
        const std::size_t lookup = lookup_list + read_u16_be(m_data, lookup_record);
        if (!within(lookup, 6)) continue;
        const uint16_t subtable_count = read_u16_be(m_data, lookup + 4);
        for (uint16_t s = 0; s < subtable_count; ++s) {
            if (!within(lookup + 6u + 2u * s, 2)) break;
            std::size_t subtable = lookup + read_u16_be(m_data, lookup + 6u + 2u * s);
            if (!within(subtable, 6)) continue;
            uint16_t lookup_type = read_u16_be(m_data, lookup);
            if (lookup_type == 7) {   // extension
                if (read_u16_be(m_data, subtable) != 1) continue;
                lookup_type = read_u16_be(m_data, subtable + 2);
                subtable += read_u32_be(m_data, subtable + 4);
                if (!within(subtable, 6)) continue;
            }
            if (lookup_type != 4) continue;   // only ligature substitution
            if (read_u16_be(m_data, subtable) != 1) continue;   // format 1 only

            // Coverage: which glyphs may start a ligature, and where each one's
            // ligature set is. Format 2 is ranges; both are read because a font
            // picks whichever is smaller.
            const std::size_t coverage = subtable + read_u16_be(m_data, subtable + 2);
            if (!within(coverage, 4)) continue;
            const uint16_t set_count = read_u16_be(m_data, subtable + 4);
            std::vector<std::pair<uint16_t, uint16_t>> starts;   // glyph, set index
            const uint16_t coverage_format = read_u16_be(m_data, coverage);
            const uint16_t covered = read_u16_be(m_data, coverage + 2);
            if (coverage_format == 1) {
                for (uint16_t i = 0; i < covered && i < set_count; ++i) {
                    if (!within(coverage + 4u + 2u * i, 2)) break;
                    starts.emplace_back(read_u16_be(m_data, coverage + 4u + 2u * i), i);
                }
            } else if (coverage_format == 2) {
                for (uint16_t i = 0; i < covered; ++i) {
                    if (!within(coverage + 4u + 6u * i, 6)) break;
                    const uint16_t first_glyph = read_u16_be(m_data, coverage + 4u + 6u * i);
                    const uint16_t last_glyph = read_u16_be(m_data, coverage + 6u + 6u * i);
                    const uint16_t first_set = read_u16_be(m_data, coverage + 8u + 6u * i);
                    for (uint32_t g = first_glyph; g <= last_glyph; ++g) {
                        starts.emplace_back(static_cast<uint16_t>(g),
                                            static_cast<uint16_t>(first_set + (g - first_glyph)));
                    }
                }
            }

            for (const auto& [first_glyph, set_index] : starts) {
                if (set_index >= set_count) continue;
                const std::size_t set_pos = subtable + read_u16_be(m_data, subtable + 6u + 2u * set_index);
                if (!within(set_pos, 2)) continue;
                const uint16_t ligature_count = read_u16_be(m_data, set_pos);
                for (uint16_t l = 0; l < ligature_count; ++l) {
                    if (!within(set_pos + 2u + 2u * l, 2)) break;
                    const std::size_t ligature = set_pos + read_u16_be(m_data, set_pos + 2u + 2u * l);
                    if (!within(ligature, 4)) break;
                    const uint16_t produced = read_u16_be(m_data, ligature);
                    const uint16_t components = read_u16_be(m_data, ligature + 2);
                    if (components < 2 || components > 8) continue;
                    if (!within(ligature + 4u, 2u * (components - 1))) break;
                    Ligature lig;
                    lig.glyph = produced;
                    lig.rest.reserve(components - 1);
                    for (uint16_t c = 1; c < components; ++c) {
                        lig.rest.push_back(read_u16_be(m_data, ligature + 2u + 2u * c));
                    }
                    by_first[first_glyph].push_back(std::move(lig));
                }
            }
        }
    }
    if (by_first.empty()) return false;

    m_ligature_sets.reserve(by_first.size());
    for (auto& [first_glyph, ligatures] : by_first) {
        LigatureSet set;
        set.first = first_glyph;
        // Longest first, so `ffi` wins over `ff` at the same position: the
        // table's own order is not a promise about which match to prefer.
        std::stable_sort(ligatures.begin(), ligatures.end(),
                         [](const Ligature& a, const Ligature& b) {
                             return a.rest.size() > b.rest.size();
                         });
        set.ligatures = std::move(ligatures);
        m_ligature_sets.push_back(std::move(set));
    }
    std::sort(m_ligature_sets.begin(), m_ligature_sets.end(),
              [](const LigatureSet& a, const LigatureSet& b) { return a.first < b.first; });
    return true;
}

void TrueTypeFont::apply_ligatures(std::vector<uint16_t>& glyphs) const {
    if (m_ligature_sets.empty() || glyphs.size() < 2) return;
    std::vector<uint16_t> out;
    out.reserve(glyphs.size());
    std::size_t i = 0;
    while (i < glyphs.size()) {
        std::size_t lo = 0, hi = m_ligature_sets.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (m_ligature_sets[mid].first < glyphs[i]) lo = mid + 1;
            else hi = mid;
        }
        bool matched = false;
        if (lo < m_ligature_sets.size() && m_ligature_sets[lo].first == glyphs[i]) {
            for (const Ligature& lig : m_ligature_sets[lo].ligatures) {
                if (i + 1u + lig.rest.size() > glyphs.size()) continue;
                bool same = true;
                for (std::size_t k = 0; k < lig.rest.size(); ++k) {
                    if (glyphs[i + 1u + k] != lig.rest[k]) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    out.push_back(lig.glyph);
                    i += 1u + lig.rest.size();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            out.push_back(glyphs[i]);
            ++i;
        }
    }
    glyphs.swap(out);
}

int16_t TrueTypeFont::kerning_units(uint32_t left, uint32_t right) const {
    return kerning_units_for_glyphs(glyph_index(left), glyph_index(right));
}

int16_t TrueTypeFont::kerning_units_for_glyphs(uint16_t left_glyph, uint16_t right_glyph) const {
    if (m_kern_pairs == 0) return 0;
    // A `.notdef` box does not kern against anything: the font has no opinion
    // about a glyph it does not have.
    if (left_glyph == 0 || right_glyph == 0) return 0;

    const uint32_t wanted = (static_cast<uint32_t>(left_glyph) << 16) | right_glyph;
    // The pairs are sorted by that key, so this is a binary search over the
    // file rather than a table of thousands of entries built at load time.
    int low = 0;
    int high = static_cast<int>(m_kern_pairs) - 1;
    while (low <= high) {
        const int mid = low + (high - low) / 2;
        const std::size_t record = m_kern_pairs_offset + static_cast<std::size_t>(mid) * 6u;
        if (record + 6u > m_data.size()) return 0;
        const uint32_t key = (static_cast<uint32_t>(read_u16_be(m_data, record)) << 16) |
                             read_u16_be(m_data, record + 2);
        if (key == wanted) return read_i16_be(m_data, record + 4);
        if (key < wanted) low = mid + 1;
        else high = mid - 1;
    }
    return 0;
}

float TrueTypeFont::glyph_advance_by_index(uint16_t glyph, float font_size_px) const {
    if (m_advance_widths.empty() || m_units_per_em == 0) return font_size_px * 0.5f;
    const uint16_t adv = glyph < m_advance_widths.size() ? m_advance_widths[glyph] : m_advance_widths[0];
    return static_cast<float>(adv) * font_size_px / static_cast<float>(m_units_per_em);
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
    return rasterize_glyph_index((codepoint < 128) ? m_ascii_glyphs[codepoint] : glyph_index(codepoint),
                                 px);
}

GlyphBitmap TrueTypeFont::rasterize_glyph_index(uint16_t gid, int pixel_size) const {
    const int px = std::clamp(pixel_size, 6, 160);
    if (!m_loaded) {
        return rasterize_fallback_8x8(gid, px, false);
    }

    GlyphBitmap bmp;
    const float scale = static_cast<float>(px) / static_cast<float>(m_units_per_em);
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

/// Every `.ttf` under the system font directories, in a stable order.
///
/// `limit` bounds the walk, not the answer: a host can hold a thousand faces
/// and the walk is a `stat` per file. Nothing here loads or parses anything —
/// the names are read later, and only for the files that are kept.
std::vector<std::string> discover_font_files(std::size_t limit) {
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
            out.push_back(it->path().string());
            if (out.size() >= limit) return out;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// The discovered files minus the two the primary faces came from — a face is
/// no use as a fallback for itself.
std::vector<std::string> discover_system_fonts(const std::string& exclude_a,
                                               const std::string& exclude_b) {
    std::vector<std::string> out;
    for (std::string& path : discover_font_files(64)) {
        if (path == exclude_a || path == exclude_b) continue;
        out.push_back(std::move(path));
        if (out.size() >= FontRenderer::kMaxFallbackFaces) break;
    }
    return out;
}

/// "DejaVu Sans Mono" -> "dejavu sans mono". What CSS matching compares: the
/// spec says family names are matched case-insensitively, and a name written
/// with two spaces matches one written with one.
std::string family_key(std::string_view name) {
    std::string out;
    bool pending_space = false;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isspace(u)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(static_cast<char>(std::tolower(u)));
    }
    return out;
}

/// What a font file's subfamily says about its weight and its slant.
///
/// This is the *fallback*. A font file answers the weight question properly in
/// `OS/2.usWeightClass`, which is read; a file with no `OS/2` table answers
/// with a word in its `name` table, and this reads the words CSS itself uses,
/// in the order CSS lists them. Anything else is 400 — a font whose subfamily
/// says nothing usable is a regular face, which is what it almost always is.
/// A foundry that writes "Halbfett" or "Gras" gets 400 from here and the
/// right answer from `OS/2`.
void weight_and_slant(const std::string& subfamily, int& weight_out, bool& italic_out) {
    std::string s;
    for (const char c : subfamily) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    italic_out = s.find("italic") != std::string::npos || s.find("oblique") != std::string::npos;
    if (s.find("black") != std::string::npos || s.find("heavy") != std::string::npos) weight_out = 900;
    else if (s.find("extrabold") != std::string::npos || s.find("ultrabold") != std::string::npos) weight_out = 800;
    else if (s.find("bold") != std::string::npos) weight_out = 700;
    else if (s.find("semibold") != std::string::npos || s.find("demibold") != std::string::npos) weight_out = 600;
    else if (s.find("medium") != std::string::npos) weight_out = 500;
    else if (s.find("extralight") != std::string::npos || s.find("ultralight") != std::string::npos) weight_out = 200;
    else if (s.find("light") != std::string::npos) weight_out = 300;
    else weight_out = 400;
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

    // Every face on the host is in the registry, the primary ones included: a
    // page that asks for `font-family: DejaVu Sans` by name is entitled to the
    // same file the default gets, and one that asks for another family is not
    // served it by accident.
    for (const std::string& path : discover_font_files(64)) {
        register_font_file(path);
    }
    // The primary faces, whether or not discovery walked past them. A host can
    // keep its fonts where the search does not look -- the paths above are
    // guessed at precisely because there is no fontconfig to ask -- and a
    // primary that is not in the registry leaves `default_family_id()` with no
    // answer, which is the question "is the default monospace" needs asked.
    register_font_file(m_regular_path);
    register_font_file(m_bold_path);
}

// --- the family registry ---------------------------------------------------

FontRenderer::FaceEntry FontRenderer::describe(const std::string& path) {
    TrueTypeFont::Names names;
    if (!TrueTypeFont::read_names(path, names)) return {};
    if (names.family.empty()) return {};
    FaceEntry e;
    e.path = path;
    e.family = names.family;
    e.key = family_key(names.family);
    e.subfamily = names.subfamily;
    int named_weight = 400;
    bool named_slant = false;
    weight_and_slant(names.subfamily, named_weight, named_slant);
    // The font's own table wins where it has one. `usWeightClass` is the same
    // 1..1000 scale CSS asks in, so there is no translation to get wrong; the
    // subfamily name is a word, and a word has to be guessed at. The guess
    // stays as the fallback because a font with no `OS/2` table is old, and old
    // is not the same as unnameable.
    const int weight = names.weight > 0 ? names.weight : named_weight;
    e.weight_min = weight;
    e.weight_max = weight;
    e.italic = names.italic || named_slant;
    return e;
}

int FontRenderer::generic_family_id(std::string_view generic) const {
    const std::string key = family_key(generic);
    if (key == "serif") return m_generic_serif;
    if (key == "sans-serif") return m_generic_sans;
    if (key == "monospace" || key == "ui-monospace") return m_generic_mono;
    if (key == "system-ui" || key == "-apple-system") return m_generic_sans;
    return kNoFamily;
}

int FontRenderer::register_font_file(const std::string& path) {
    if (path.empty()) return kNoFamily;
    for (const FaceEntry& have : m_face_entries) {
        if (have.path == path) {                // registered already, by discovery or by hand
            const auto it = m_family_by_key.find(have.key);
            return it == m_family_by_key.end() ? kNoFamily : it->second;
        }
    }
    FaceEntry entry = describe(path);
    if (entry.key.empty()) return kNoFamily;    // a font that cannot name itself cannot be asked for

    const std::size_t index = m_face_entries.size();
    m_face_entries.push_back(std::move(entry));

    auto known = m_family_by_key.find(m_face_entries[index].key);
    if (known == m_family_by_key.end()) {
        const int id = static_cast<int>(m_families.size());
        Family f;
        f.name = m_face_entries[index].family;
        f.key = m_face_entries[index].key;
        f.faces.push_back(index);
        m_families.push_back(std::move(f));
        m_family_names.push_back(m_families.back().name);
        m_family_by_key.emplace(m_families.back().key, id);
    } else {
        m_families[known->second].faces.push_back(index);
    }

    // Which family each generic name means is a judgement about this host, so
    // it is made once, here, from the names the host's fonts declare. It is
    // not fontconfig: there is no fontconfig, and no dependency is being
    // added to get one.
    // The three are mutually exclusive and mono is asked first: "DejaVu Sans
    // Mono" has "sans" in its name and is not a sans-serif face.
    const std::string& key = m_face_entries[index].key;
    const int id_of_key = [this, &key] {
        const auto it = m_family_by_key.find(key);
        return it == m_family_by_key.end() ? kNoFamily : it->second;
    }();
    if (id_of_key != kNoFamily) {
    if (key.find("mono") != std::string::npos) {
        if (m_generic_mono == kNoFamily) m_generic_mono = id_of_key;
    } else if (key.find("sans") != std::string::npos) {
        if (m_generic_sans == kNoFamily) m_generic_sans = id_of_key;
    } else if (key.find("serif") != std::string::npos) {
        if (m_generic_serif == kNoFamily) m_generic_serif = id_of_key;
    }
    }

    // A family that has appeared, or a generic that now maps somewhere else,
    // changes what a list resolves to.
    m_resolved.clear();
    const auto it = m_family_by_key.find(m_face_entries[index].key);
    const int id = it == m_family_by_key.end() ? kNoFamily : it->second;
    // What `kDefaultFamily` names, for a caller that has to say it: "the
    // default" is whichever face this host had, and on a host whose only face
    // is monospace the default *is* monospace.
    if (path == m_regular_path) m_default_family = id;
    return id;
}

int FontRenderer::register_webfont(std::string family, std::vector<WebfontFetcher> sources,
                                   int weight_min, int weight_max, bool italic) {
    const std::string key = family_key(family);
    if (key.empty() || sources.empty()) return kNoFamily;

    if (weight_min > weight_max) std::swap(weight_min, weight_max);

    FaceEntry entry;
    // No path: the bytes are not on disk, they are wherever the URL points.
    // The family is the one the page declared, so the name inside the file is
    // never asked for -- a page may serve "Body Text" from a file that calls
    // itself something else entirely, and the page's name is the one CSS means.
    entry.family = std::move(family);
    entry.key = key;
    entry.weight_min = weight_min;
    entry.weight_max = weight_max;
    entry.italic = italic;
    entry.webfont = true;
    entry.sources = std::move(sources);

    const std::size_t index = m_face_entries.size();
    m_face_entries.push_back(std::move(entry));

    auto known = m_family_by_key.find(key);
    if (known == m_family_by_key.end()) {
        const int id = static_cast<int>(m_families.size());
        Family f;
        f.name = m_face_entries[index].family;
        f.key = key;
        f.faces.push_back(index);
        m_family_names.push_back(f.name);   // read before `f` is moved from
        m_families.push_back(std::move(f));
        m_family_by_key.emplace(key, id);
    } else {
        // Put the webfont ahead of whatever this host already had under the
        // name: a `@font-face` family is the answer for that name, and a
        // system family of the same name is only what it falls back to.
        // `pick_face` would otherwise be free to choose either.
        m_families[known->second].faces.insert(m_families[known->second].faces.begin(), index);
    }

    // A new family, or a family that gained a face, changes what every list
    // mentioning it resolves to.
    m_resolved.clear();
    return m_family_by_key[key];
}

std::vector<FontRenderer::FaceFacts> FontRenderer::family_face_facts(
    std::string_view css_family_list) const {
    std::vector<FaceFacts> facts;
    const int id = family_id_for(css_family_list);
    if (id < 0 || static_cast<std::size_t>(id) >= m_families.size()) return facts;
    for (const std::size_t index : m_families[static_cast<std::size_t>(id)].faces) {
        const FaceEntry& e = m_face_entries[index];
        facts.push_back(FaceFacts{e.weight_min, e.weight_max, e.italic, e.webfont});
    }
    return facts;
}

std::string FontRenderer::face_path(std::string_view css_family_list) const {
    const int id = family_id_for(css_family_list);
    if (id < 0 || static_cast<std::size_t>(id) >= m_families.size()) return {};
    const Family& family = m_families[static_cast<std::size_t>(id)];
    // Upright, regular: `local()` names a family, and the question `@font-face`
    // asks of it is "is this font on the host", not "is this the slant the
    // rule declared". A rule that declares italic and is answered from
    // `local()` gets the upright file, which is then sheared like any other
    // upright that has to stand in for an italic.
    const std::size_t index = pick_face(family, 400, false, /*italic=*/false);
    if (index == static_cast<std::size_t>(-1)) return {};
    // A webfont has no path: its bytes were never on this disk. `local()` is
    // asking for a font the host already has, so a webfont is the one thing it
    // cannot be answered with.
    const FaceEntry& e = m_face_entries[index];
    return e.webfont ? std::string() : e.path;
}

void FontRenderer::clear_webfonts() {
    // Whether there is anything to do, asked *before* anything is moved.
    // "Nothing to do" is the common case -- most documents declare no webfonts
    // -- and a check made after the moves below would be too late: the entries
    // are moved into `kept`, and a `kept` that is then discarded on an early
    // return leaves every face on the host with an empty path. The registry
    // still looked whole, and could no longer load a single system font.
    std::size_t survivors = 0;
    for (const FaceEntry& e : m_face_entries) {
        if (!e.webfont) ++survivors;
    }
    if (survivors == m_face_entries.size()) return;

    // Which face indices survive, and where they end up.
    std::vector<std::size_t> remap(m_face_entries.size(), static_cast<std::size_t>(-1));
    std::vector<FaceEntry> kept;
    kept.reserve(survivors);
    for (std::size_t i = 0; i < m_face_entries.size(); ++i) {
        if (m_face_entries[i].webfont) continue;
        remap[i] = kept.size();
        kept.push_back(std::move(m_face_entries[i]));
    }

    for (Family& f : m_families) {
        std::vector<std::size_t> faces;
        faces.reserve(f.faces.size());
        for (const std::size_t index : f.faces) {
            if (remap[index] != static_cast<std::size_t>(-1)) faces.push_back(remap[index]);
        }
        f.faces = std::move(faces);
    }

    // Faces already parsed are held by index into `m_family_faces`, and a
    // webfont's bytes live only in the face that parsed them -- so a loaded
    // webfont has to be dropped, and with it the index that finds it. The
    // glyph cache is deliberately left alone: a `GlyphBitmap` is a value, not
    // a view into the face that made it.
    m_family_faces.clear();
    m_family_face_index.clear();
    m_face_entries = std::move(kept);
    m_resolved.clear();
}

std::size_t FontRenderer::pick_face(const Family& family, int weight, bool bold, bool italic) const {
    if (family.faces.empty()) return static_cast<std::size_t>(-1);

    // Whether this family has any slanted face at all, decided once for the
    // family rather than once per face: it is the question that decides
    // whether an upright face may answer a slanted request. A family with no
    // italic is not a dead end -- a browser slants what it has rather than
    // leave the text upright -- so the answer changes eligibility, not success.
    bool family_has_slant = false;
    for (const std::size_t index : family.faces) {
        if (m_face_entries[index].italic) {
            family_has_slant = true;
            break;
        }
    }

    const int wanted = bold ? std::max(weight, 600) : weight;
    // CSS Fonts 4 §5.2.1 does not ask for the *nearest* weight, it asks for a
    // search in a declared order, and the difference is not academic: asking a
    // family of 500 and 900 for 600 is a tie by distance and an exact answer by
    // the spec, which looks at 600 and above first. Getting this wrong is a
    // page whose semibold headings come out medium.
    const auto rank_in_search_order = [wanted](int candidate) {
        if (wanted >= 400 && wanted <= 500) {
            if (candidate >= wanted && candidate <= 500) return std::pair{0, candidate - wanted};
            if (candidate < wanted) return std::pair{1, wanted - candidate};
            return std::pair{2, candidate - 500};
        }
        if (wanted < 400) {
            if (candidate <= wanted) return std::pair{0, wanted - candidate};
            return std::pair{1, candidate - wanted};
        }
        if (candidate >= wanted) return std::pair{0, candidate - wanted};
        return std::pair{1, wanted - candidate};
    };

    std::size_t best = static_cast<std::size_t>(-1);
    std::pair<int, int> best_rank{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    bool best_is_webfont = false;
    for (const std::size_t index : family.faces) {
        const FaceEntry& e = m_face_entries[index];
        // A slanted face answers only a slanted request: `font-style: normal`
        // inside an italic paragraph must not land on the italic face. And an
        // upright face answers a slanted request only when the family has no
        // slanted one to offer -- then the caller shears it.
        if (e.italic && !italic) continue;
        if (!e.italic && italic && family_has_slant) continue;
        // A face that declares a range containing the weight asked for is an
        // exact answer, whatever the range is: a variable font serving
        // `100 900` answers 600 exactly. Otherwise the nearest end of its range
        // stands for the face, because a static face is one weight.
        const int candidate = wanted < e.weight_min   ? e.weight_min
                              : wanted > e.weight_max ? e.weight_max
                                                      : wanted;
        const std::pair<int, int> rank = rank_in_search_order(candidate);
        // A webfont outranks a host family it shares a name with, so it wins
        // ties outright -- and, having won, it is not displaced by a host face
        // that happens to be nearer the weight asked for.
        const bool better = rank < best_rank || (rank == best_rank && e.webfont && !best_is_webfont);
        if (better) {
            best_rank = rank;
            best = index;
            best_is_webfont = e.webfont;
        }
    }
    // Nothing answered the slant. That happens when the family's only faces
    // are slanted and upright was asked for -- a family with one italic file
    // and no regular one, which real pages do ship. The family the element
    // named is still the right family, so its first face is a better answer
    // than dropping to the default: the wrong slant is a smaller error than
    // the wrong typeface.
    if (best == static_cast<std::size_t>(-1)) best = family.faces.front();
    return best;
}

const TrueTypeFont* FontRenderer::family_face(int family_id, int weight, bool bold, bool italic,
                                              bool* synthetic_oblique) const {
    if (synthetic_oblique) *synthetic_oblique = false;
    if (family_id < 0 || static_cast<std::size_t>(family_id) >= m_families.size()) {
        return nullptr;
    }
    const Family& family = m_families[static_cast<std::size_t>(family_id)];
    const std::size_t index = pick_face(family, weight, bold, italic);
    if (index == static_cast<std::size_t>(-1)) return nullptr;

    // The slant was asked for and the face that answered does not have one of
    // its own: the caller is to shear it. Reported only on the paths that
    // return a face, so a caller cannot mistake "nothing loaded" for "shear
    // nothing".
    const bool answered_upright = italic && !m_face_entries[index].italic;

    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(family_id)) << 32) |
                         static_cast<uint64_t>(static_cast<uint32_t>(index));
    const auto seen = m_family_face_index.find(key);
    if (seen != m_family_face_index.end()) {
        if (synthetic_oblique) *synthetic_oblique = answered_upright;
        return &m_family_faces[seen->second];
    }
    if (m_family_faces.size() >= kMaxLoadedFamilyFaces) {
        // Drop the lot and start again. A cache that keeps one face and evicts
        // another would thrash on a page that alternates between three. The
        // glyph cache is deliberately left alone: a `GlyphBitmap` is a value,
        // not a view into the face it was rasterized from, so it stays correct
        // after the face that made it is gone.
        m_family_faces.clear();
        m_family_face_index.clear();
    }
    TrueTypeFont face;
    const FaceEntry& chosen = m_face_entries[index];
    if (chosen.webfont) {
        // Try the rule's sources in order and keep the first that parses. A
        // source that returns nothing, or bytes that are not a font this can
        // read, is not the end -- it is what moves on to the next one, which
        // is the entire point of `src: url(a.woff), url(a.ttf)`.
        bool loaded = false;
        for (const WebfontFetcher& fetch : chosen.sources) {
            if (!fetch) continue;
            std::vector<std::uint8_t> bytes = fetch();
            if (bytes.empty()) continue;
            if (face.load_memory(std::move(bytes))) {
                loaded = true;
                break;
            }
        }
        if (!loaded) return nullptr;
    } else if (!face.load_file(chosen.path)) {
        return nullptr;
    }
    m_family_faces.push_back(std::move(face));
    const std::size_t slot = m_family_faces.size() - 1;
    m_family_face_index.emplace(key, slot);
    if (synthetic_oblique) *synthetic_oblique = answered_upright;
    return &m_family_faces[slot];
}

int FontRenderer::family_id_for(std::string_view css_family_list) const {
    if (css_family_list.empty()) return kDefaultFamily;

    const auto cached = m_resolved.find(std::string(css_family_list));
    if (cached != m_resolved.end()) return cached->second;

    // A list is split on commas that are not inside quotes, so that
    // `font-family: "Font, Inc", sans-serif` is two entries and not three.
    std::string entry;
    bool quoted = false;
    int result = kDefaultFamily;
    // Whether an entry has been matched, which is not the same as whether
    // `result` has moved off `kDefaultFamily`: the family a generic names can
    // perfectly well *be* the default one, and a list must stop there rather
    // than go on to the next entry and take something else.
    bool matched = false;

    const auto consider = [&](std::string candidate) {
        if (matched) return;
        const std::string key = family_key(candidate);
        if (key.empty()) return;

        int found = kNoFamily;
        if (key == "serif") {
            found = m_generic_serif;
        } else if (key == "sans-serif") {
            found = m_generic_sans;
        } else if (key == "monospace" || key == "ui-monospace") {
            found = m_generic_mono;
        } else if (key == "system-ui" || key == "-apple-system") {
            found = m_generic_sans;
        } else {
            const auto it = m_family_by_key.find(key);
            if (it != m_family_by_key.end()) found = it->second;
        }
        // A family that has no face behind it is not a family a page can have,
        // however well its name matched. `clear_webfonts` leaves such an entry
        // behind -- it drops the faces and keeps the name, because renumbering
        // families would invalidate every id already handed out -- so the
        // question is asked here, where the answer is cheap.
        if (found != kNoFamily && static_cast<std::size_t>(found) < m_families.size() &&
            m_families[static_cast<std::size_t>(found)].faces.empty()) {
            found = kNoFamily;
        }
        // `cursive` and `fantasy` fall through to here: this host has nothing
        // by those names, and the default is a better answer than a guess,
        // because a page is better served by a real face than by one picked
        // for the sound of its name.
        if (found == kNoFamily) return;
        result = found;
        matched = true;
    };

    for (const char c : css_family_list) {
        if (c == '"' || c == '\'') { quoted = !quoted; continue; }
        if (c == ',' && !quoted) {
            consider(entry);
            entry.clear();
            continue;
        }
        entry.push_back(c);
        if (matched) break;
    }
    consider(entry);

    m_resolved.emplace(std::string(css_family_list), result);
    return result;
}

void FontRenderer::set_fallbacks(std::vector<std::string> paths) {
    m_fallback_paths = std::move(paths);
    m_fallbacks.clear();
    m_next_fallback = 0;
    // A cached glyph may have come from a face that is no longer in the set.
    m_cache.clear();
}

const TrueTypeFont* FontRenderer::face_for(uint32_t codepoint, int font_weight, int family_id,
                                           bool italic, bool* synthetic_oblique) const {
    if (synthetic_oblique) *synthetic_oblique = false;
    const int weight = std::clamp(font_weight, 1, 1000);
    const bool bold = weight >= 600;
    const TrueTypeFont* primary = (bold && m_bold.is_loaded())
                                      ? &m_bold
                                      : (m_regular.is_loaded() ? &m_regular : nullptr);

    // The element named a family, and the family has the glyph. This is the
    // whole of `font-family`: before it, the primary answered for every element
    // on the page and a `<pre>` was drawn in the same face as a heading.
    bool named_upright = false;
    if (const TrueTypeFont* named = family_face(family_id, weight, bold, italic, &named_upright)) {
        if (named->has_glyph(codepoint)) {
            if (synthetic_oblique) *synthetic_oblique = named_upright;
            return named;
        }
    }

    // Past here the faces are the host's own regular and bold primaries and
    // whatever discovery found; none of them is the element's italic, so a
    // slant that was asked for is one this has to make. One slanted word in
    // the middle of an italic sentence reads as a mistake, so the whole run is
    // sheared rather than only the part the family covered.
    if (primary && primary->has_glyph(codepoint)) {
        if (synthetic_oblique) *synthetic_oblique = italic;
        return primary;
    }

    // Already loaded as a result of an earlier miss.
    for (const TrueTypeFont& face : m_fallbacks) {
        if (face.has_glyph(codepoint)) {
            if (synthetic_oblique) *synthetic_oblique = italic;
            return &face;
        }
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
        // A shaped glyph's cache entry names the face by its address, and this
        // vector reallocates: every entry that named a face by address is stale
        // the moment one is added. Rare enough to pay for by emptying it.
        m_cache.clear();
        m_fallbacks.push_back(std::move(face));
        if (m_fallbacks.back().has_glyph(codepoint)) {
            if (synthetic_oblique) *synthetic_oblique = italic;
            return &m_fallbacks.back();
        }
    }

    // No face covers it. The primary draws its `.notdef` (or nothing), which
    // is what happened to every such codepoint before fallback existed. A
    // `.notdef` box is as slanted as the rest of the line has to be.
    if (synthetic_oblique) *synthetic_oblique = italic;
    return primary;
}

std::pair<float, float> FontRenderer::em_ascent_descent(float font_size, int font_weight,
                                                        std::string_view css_family,
                                                        bool italic) const {
    if (!(font_size > 0.0f)) return {0.0f, 0.0f};
    const bool want_bold = font_weight >= 600;
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    const TrueTypeFont* font = family_face(family_id, font_weight, want_bold, italic);
    if (!font) {
        font = (want_bold && m_bold.is_loaded())
                   ? &m_bold
                   : (m_regular.is_loaded() ? &m_regular : nullptr);
    }
    if (!font) return {font_size * 0.8f, font_size * 0.2f};
    const float scale = font_size / static_cast<float>(font->units_per_em());
    const float asc = static_cast<float>(font->ascent()) * scale;
    const float desc = -static_cast<float>(font->descent()) * scale;
    return {asc, desc};
}

float FontRenderer::glyph_advance_px(uint32_t codepoint, float font_size, int font_weight,
                                     std::string_view css_family, bool italic) const {
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    return advance_in_family(codepoint, font_size, font_weight, family_id, italic);
}

float FontRenderer::kerning_px(uint32_t left, uint32_t right, float font_size, int font_weight,
                               std::string_view css_family, bool italic) const {
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    return kerning_in_family(left, right, font_size, font_weight, family_id, italic);
}

float FontRenderer::kerning_in_family(uint32_t left, uint32_t right, float font_size, int font_weight,
                                      int family_id, bool italic) const {
    if (!(font_size > 0.0f)) return 0.0f;
    // Both glyphs have to come from the same face: a pair split across a
    // family and a fallback was never a pair the font had an opinion about.
    const TrueTypeFont* left_face = face_for(left, font_weight, family_id, italic);
    if (!left_face || !left_face->has_kerning()) return 0.0f;
    if (!left_face->has_glyph(right)) return 0.0f;
    const float units = static_cast<float>(left_face->kerning_units(left, right));
    if (units == 0.0f) return 0.0f;
    return units * font_size / static_cast<float>(left_face->units_per_em());
}

bool FontRenderer::font_kerning(std::string_view css_family, int font_weight, bool italic) const {
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    // `'M'` stands for "some codepoint this face covers": the question is
    // whether the face has a table, not what it says about one pair.
    const TrueTypeFont* face = face_for('M', font_weight, family_id, italic);
    return face && face->has_kerning();
}

std::vector<FontRenderer::ShapedGlyph> FontRenderer::shape_text(std::string_view utf8_text,
                                                                float font_size, int font_weight,
                                                                int family_id, bool italic) const {
    std::vector<ShapedGlyph> out;
    if (utf8_text.empty()) return out;

    // A run is the stretch of text one face draws. Ligatures are applied inside
    // a run and never across one, which is also true of the fonts: `latn`'s
    // table has an opinion about `f` followed by `i`, not about whichever face
    // happens to supply the `i`.
    std::vector<uint32_t> run;
    const TrueTypeFont* run_face = nullptr;
    bool run_shear = false;

    const auto flush = [&]() {
        if (run.empty()) return;
        std::vector<uint16_t> glyphs;
        glyphs.reserve(run.size());
        for (uint32_t cp : run) {
            glyphs.push_back(run_face ? run_face->glyph_index(cp) : 0);
        }
        if (run_face) run_face->apply_ligatures(glyphs);
        for (uint16_t glyph : glyphs) {
            ShapedGlyph shaped;
            shaped.glyph = glyph;
            shaped.face = run_face;
            shaped.shear = run_shear;
            shaped.advance = run_face ? run_face->glyph_advance_by_index(glyph, font_size)
                                      : std::max(1.0f, font_size * 0.55f);
            out.push_back(shaped);
        }
        run.clear();
    };

    std::size_t pos = 0;
    while (pos < utf8_text.size()) {
        const uint32_t cp = next_utf8_codepoint(utf8_text, pos);
        bool shear = false;
        const TrueTypeFont* face = face_for(cp, font_weight, family_id, italic, &shear);
        if (face != run_face && !run.empty()) flush();
        run_face = face;
        run_shear = shear;
        run.push_back(cp);
    }
    flush();
    return out;
}

std::vector<FontRenderer::ShapedGlyph> FontRenderer::shape_text(std::string_view utf8_text,
                                                               float font_size, int font_weight,
                                                               std::string_view css_family,
                                                               bool italic) const {
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    return shape_text(utf8_text, font_size, font_weight, family_id, italic);
}

float FontRenderer::kerning_px_shaped(const ShapedGlyph& left, const ShapedGlyph& right,
                                      float font_size) const {
    if (!(font_size > 0.0f)) return 0.0f;
    if (!left.face || left.face != right.face) return 0.0f;
    if (!left.face->has_kerning()) return 0.0f;
    const float units = static_cast<float>(left.face->kerning_units_for_glyphs(left.glyph, right.glyph));
    if (units == 0.0f) return 0.0f;
    return units * font_size / static_cast<float>(left.face->units_per_em());
}

const GlyphBitmap& FontRenderer::get_glyph_shaped(const ShapedGlyph& glyph, int pixel_size,
                                                  int font_weight) {
    const int px = std::clamp(pixel_size, 6, 160);
    const int weight = std::clamp(font_weight, 1, 1000);
    const bool bold = weight >= 600;
    // The key names the glyph and the face, not the codepoint: a ligature has
    // no codepoint, and the face that has the glyph need not be the family's
    // own (a fallback run). The face is folded to 16 bits of pointer; the cache
    // is cleared whenever a face is added or replaced, so two faces cannot
    // trade an entry.
    const uint64_t face_bits = (static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(glyph.face)) >> 4) & 0xFFFFu;
    const uint64_t key = (1ull << 63) | static_cast<uint64_t>(glyph.glyph) << 32 |
                         static_cast<uint64_t>(static_cast<uint32_t>(px)) << 24 |
                         (static_cast<uint64_t>(glyph.shear ? 1u : 0u) << 23) | face_bits;
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;
    if (m_cache.size() >= kMaxCachedGlyphs) m_cache.clear();

    GlyphBitmap bmp;
    if (glyph.face) {
        bmp = glyph.face->rasterize_glyph_index(glyph.glyph, px);
    } else {
        bmp = rasterize_fallback_8x8(glyph.glyph, px, bold);
    }
    if (glyph.shear) apply_synthetic_oblique(bmp);

    auto [ins, _] = m_cache.emplace(key, std::move(bmp));
    return ins->second;
}

float FontRenderer::advance_in_family(uint32_t codepoint, float font_size, int font_weight,
                                      int family_id, bool italic) const {
    // The slant reaches `face_for` because it can change which face answers,
    // and an italic face has advances of its own. A synthesised oblique does
    // not: shear moves the ink, not the pen.
    const TrueTypeFont* font = face_for(codepoint, font_weight, family_id, italic);
    if (font) return font->glyph_advance(codepoint, font_size);
    return std::max(1.0f, font_size * 0.55f);
}

float FontRenderer::measure_text(std::string_view utf8_text, float font_size, int font_weight,
                                 std::string_view css_family, bool italic) {
    if (utf8_text.empty() || font_size <= 0.0f) return 0.0f;
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    const std::vector<ShapedGlyph> shaped = shape_text(utf8_text, font_size, font_weight, family_id, italic);
    float total = 0.0f;
    for (std::size_t i = 0; i < shaped.size(); ++i) {
        total += shaped[i].advance;
        // Kerning is a property of a pair, so the first glyph has nothing to
        // kern against. It is asked of the shaped glyphs and not of the
        // codepoints: after a ligature, `ff` is one glyph, and the `kern` table
        // has an opinion about that glyph only if it names it.
        if (i > 0) total += kerning_px_shaped(shaped[i - 1], shaped[i], font_size);
    }
    return total;
}

const GlyphBitmap& FontRenderer::get_glyph(uint32_t codepoint, int pixel_size, int font_weight,
                                           int family_id, bool italic) {
    const int px = std::clamp(pixel_size, 6, 160);
    const int weight = std::clamp(font_weight, 1, 1000);
    const bool bold = weight >= 600;
    // The family is part of the key. Without it the cache would answer with a
    // glyph rasterized from one family for an element that asked for another,
    // which is the bug that makes `font-family` look like it works in the
    // first line of a paragraph and not in the rest. The slant is part of it
    // for the same reason: a sheared bitmap and an upright one are two
    // pictures of one glyph, and whichever was drawn first would otherwise
    // answer for both.
    // The weight is in the key and not a "bold" bit: a family with a 300 face
    // and a 500 face draws the same glyph two ways, and one flag cannot hold
    // that. Bits: codepoint (32), pixel size (8, clamped to 6..160), weight
    // (10, clamped to 1..1000), slant (1), family (13).
    const uint64_t key = static_cast<uint64_t>(codepoint) |
                         (static_cast<uint64_t>(static_cast<uint32_t>(px)) << 32) |
                         (static_cast<uint64_t>(static_cast<uint32_t>(weight)) << 40) |
                         (static_cast<uint64_t>(italic ? 1u : 0u) << 50) |
                         (static_cast<uint64_t>(static_cast<uint32_t>(family_id) & 0x1FFFu) << 51);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    if (m_cache.size() >= kMaxCachedGlyphs) {
        m_cache.clear();
    }

    GlyphBitmap bmp;
    bool shear = false;
    const TrueTypeFont* face = face_for(codepoint, weight, family_id, italic, &shear);
    if (face) {
        bmp = face->rasterize_glyph(codepoint, px);
    } else {
        bmp = rasterize_fallback_8x8(codepoint, px, bold);
    }
    if (shear) apply_synthetic_oblique(bmp);

    auto [ins, _] = m_cache.emplace(key, std::move(bmp));
    return ins->second;
}

void FontRenderer::draw_text(
    float top_x,
    float top_y,
    std::string_view utf8_text,
    float font_size,
    int font_weight,
    const std::function<void(int px, int py, uint8_t coverage)>& emit_pixel,
    std::string_view css_family,
    bool italic
) {
    if (utf8_text.empty() || font_size <= 0.0f) return;
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    const int px_size = std::clamp(static_cast<int>(std::lround(font_size)), 6, 160);
    const bool bold = font_weight >= 600;
    const bool synthetic_bold = bold && !m_bold.is_loaded() && m_regular.is_loaded();

    // The baseline comes from the face the element asked for, which is the
    // face the line builder measured with. It used to come from the primary
    // whatever the family was, so text in a named family sat a little higher
    // or lower than the box that had been laid out around it.
    const TrueTypeFont* font = family_face(family_id, font_weight, bold, italic);
    if (!font) {
        font = (bold && m_bold.is_loaded())
                   ? &m_bold
                   : (m_regular.is_loaded() ? &m_regular : nullptr);
    }
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

    // The same list `measure_text` summed, glyph for glyph: painting walks what
    // shaping produced, so a ligature is drawn as the glyph it is and the pen
    // moves by the advance that was counted.
    const std::vector<ShapedGlyph> shaped = shape_text(utf8_text, font_size, font_weight, family_id, italic);
    for (std::size_t i = 0; i < shaped.size(); ++i) {
        // The pair closes up *before* its second glyph is placed: kerning is
        // the gap between two glyphs, so adding it after drawing the second one
        // would move the gap and leave the glyph where it was.
        if (i > 0) pen_x += kerning_px_shaped(shaped[i - 1], shaped[i], font_size);
        const GlyphBitmap& g = get_glyph_shaped(shaped[i], px_size, font_weight);
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
        // The pen advances by the advance shaping resolved for this glyph --
        // the same number layout measured, from the same face. Advancing with
        // the primary is how a `<pre>` measured monospace and drew
        // proportional: every glyph in the right order, packed to the width of
        // a font nobody asked for.
        pen_x += shaped[i].advance;
    }
}

} // namespace litebird
