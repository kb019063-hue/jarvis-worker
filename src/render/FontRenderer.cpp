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

    // `GSUB`, `GDEF`, `GPOS`, and `kern` are all optional and their absence is
    // not a failure: a font without them simply lays out plain advances.
    m_ligature_sets.clear();
    m_has_gdef_glyph_class = false;
    m_gdef_glyph_class = OtfClassDef{};
    m_gdef_mark_attach_class = OtfClassDef{};
    m_gdef_mark_sets.clear();
    m_has_contextual_gsub = false;
    m_gsub_singles.clear();
    m_gsub_multiples.clear();
    m_gsub_alternates.clear();
    m_gsub_ligatures.clear();
    m_gsub_contexts.clear();
    m_gsub_lookups.clear();
    m_gsub_scripts.clear();
    m_has_gpos_kern = false;
    m_has_gpos_cursive = false;
    m_has_gpos_marks = false;
    m_has_contextual_gpos = false;
    m_gpos_known_marks.clear();
    m_gpos_singles.clear();
    m_gpos_pair_fmt1.clear();
    m_gpos_pair_fmt2.clear();
    m_gpos_cursives.clear();
    m_gpos_mark_base.clear();
    m_gpos_mark_lig.clear();
    m_gpos_mark_mark.clear();
    m_gpos_contexts.clear();
    m_gpos_lookups.clear();
    m_gpos_scripts.clear();

    uint32_t gdef_off = 0, gdef_len = 0;
    if (find_table("GDEF", gdef_off, gdef_len)) load_gdef(gdef_off, gdef_len);

    uint32_t gsub_off = 0, gsub_len = 0;
    if (find_table("GSUB", gsub_off, gsub_len)) load_ligatures(gsub_off, gsub_len);

    uint32_t gpos_off = 0, gpos_len = 0;
    if (find_table("GPOS", gpos_off, gpos_len)) load_gpos(gpos_off, gpos_len);

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

namespace {

constexpr uint32_t ot_tag(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

constexpr uint32_t kTagCommon = 0u;
constexpr uint32_t kTagInherited = 1u;
constexpr uint32_t kTagDflt = ot_tag('D', 'F', 'L', 'T');
constexpr uint32_t kTagLatn = ot_tag('l', 'a', 't', 'n');
constexpr uint32_t kTagArab = ot_tag('a', 'r', 'a', 'b');
constexpr uint32_t kTagHebr = ot_tag('h', 'e', 'b', 'r');
constexpr uint32_t kTagSyrc = ot_tag('s', 'y', 'r', 'c');
constexpr uint32_t kTagThaa = ot_tag('t', 'h', 'a', 'a');
constexpr uint32_t kTagNko  = ot_tag('n', 'k', 'o', ' ');
constexpr uint32_t kTagCyrl = ot_tag('c', 'y', 'r', 'l');
constexpr uint32_t kTagGrek = ot_tag('g', 'r', 'e', 'k');
constexpr uint32_t kTagThai = ot_tag('t', 'h', 'a', 'i');
constexpr uint32_t kTagLao  = ot_tag('l', 'a', 'o', ' ');
constexpr uint32_t kTagDev2 = ot_tag('d', 'e', 'v', '2');
constexpr uint32_t kTagDeva = ot_tag('d', 'e', 'v', 'a');

bool is_rtl_script_tag(uint32_t tag) {
    return tag == kTagArab || tag == kTagHebr || tag == kTagSyrc ||
           tag == kTagThaa || tag == kTagNko;
}

bool is_digit_codepoint(uint32_t cp) {
    return (cp >= '0' && cp <= '9') ||
           (cp >= 0x0660u && cp <= 0x0669u) ||
           (cp >= 0x06F0u && cp <= 0x06F9u);
}

bool is_join_control(uint32_t cp) {
    return cp == 0x200Cu || cp == 0x200Du;
}

uint32_t classify_codepoint_script(uint32_t cp) {
    if ((cp >= 0x0300u && cp <= 0x036Fu) ||
        (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
        (cp >= 0x1DC0u && cp <= 0x1DFFu) ||
        (cp >= 0x20D0u && cp <= 0x20FFu) ||
        (cp >= 0xFE20u && cp <= 0xFE2Fu) ||
        is_join_control(cp)) {
        return kTagInherited;
    }
    if ((cp >= 0x0600u && cp <= 0x06FFu) ||
        (cp >= 0x0750u && cp <= 0x077Fu) ||
        (cp >= 0x0870u && cp <= 0x08FFu) ||
        (cp >= 0xFB50u && cp <= 0xFDFFu) ||
        (cp >= 0xFE70u && cp <= 0xFEFFu)) {
        if (is_digit_codepoint(cp)) return kTagCommon;
        return kTagArab;
    }
    if ((cp >= 0x0590u && cp <= 0x05FFu) || (cp >= 0xFB1Du && cp <= 0xFB4Fu)) return kTagHebr;
    if (cp >= 0x0700u && cp <= 0x074Fu) return kTagSyrc;
    if (cp >= 0x0780u && cp <= 0x07BFu) return kTagThaa;
    if (cp >= 0x07C0u && cp <= 0x07FFu) return kTagNko;
    if ((cp >= 0x0400u && cp <= 0x052Fu) || (cp >= 0x2DE0u && cp <= 0x2DFFu) ||
        (cp >= 0xA640u && cp <= 0xA69Fu)) {
        return kTagCyrl;
    }
    if ((cp >= 0x0370u && cp <= 0x03FFu) || (cp >= 0x1F00u && cp <= 0x1FFFu)) return kTagGrek;
    if (cp >= 0x0E00u && cp <= 0x0E7Fu) return kTagThai;
    if (cp >= 0x0E80u && cp <= 0x0EFFu) return kTagLao;
    if (cp >= 0x0900u && cp <= 0x097Fu) return kTagDev2;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        cp == 0x00AAu || cp == 0x00BAu ||
        (cp >= 0x00C0u && cp <= 0x00D6u) || (cp >= 0x00D8u && cp <= 0x00F6u) ||
        (cp >= 0x00F8u && cp <= 0x024Fu) || (cp >= 0x1E00u && cp <= 0x1EFFu) ||
        (cp >= 0x2C60u && cp <= 0x2C7Fu) || (cp >= 0xA720u && cp <= 0xA7FFu)) {
        return kTagLatn;
    }
    return kTagCommon;
}

std::vector<uint32_t> itemize_codepoint_scripts(const std::vector<uint32_t>& cps) {
    const std::size_t n = cps.size();
    std::vector<uint32_t> out(n, kTagLatn);
    if (n == 0) return out;

    for (std::size_t i = 0; i < n; ++i) {
        out[i] = classify_codepoint_script(cps[i]);
    }
    // Attach Inherited (combining marks, ZWJ/ZWNJ) to preceding strong/common script.
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i] == kTagInherited) {
            if (i > 0) out[i] = out[i - 1];
            else {
                std::size_t k = i + 1;
                while (k < n && out[k] == kTagInherited) ++k;
                out[i] = (k < n) ? out[k] : kTagLatn;
            }
        }
    }
    // Resolve Common spans: spaces/punctuation bounded by identical strong scripts
    // (without digits) merge into that script so multi-word RTL stays a single run.
    std::size_t i = 0;
    while (i < n) {
        if (out[i] != kTagCommon) {
            ++i;
            continue;
        }
        std::size_t j = i;
        bool has_digit = false;
        while (j < n && out[j] == kTagCommon) {
            if (is_digit_codepoint(cps[j])) has_digit = true;
            ++j;
        }
        const uint32_t prev_strong = (i > 0) ? out[i - 1] : 0u;
        const uint32_t next_strong = (j < n) ? out[j] : 0u;
        uint32_t resolved = kTagLatn;
        if (!has_digit && prev_strong != 0u && prev_strong == next_strong &&
            prev_strong != kTagCommon) {
            resolved = prev_strong;
        }
        for (std::size_t k = i; k < j; ++k) out[k] = resolved;
        i = j;
    }
    return out;
}

enum class ArabicJoinType : uint8_t {
    NonJoining,  // U
    Right,       // R
    Left,        // L
    Dual,        // D
    Causing,     // C (Tatweel, ZWJ)
    Transparent  // T (combining marks)
};

ArabicJoinType classify_arabic_joining(uint32_t cp) {
    if (is_join_control(cp)) {
        return (cp == 0x200Du) ? ArabicJoinType::Causing : ArabicJoinType::NonJoining;
    }
    if ((cp >= 0x0300u && cp <= 0x036Fu) ||
        (cp >= 0x0610u && cp <= 0x061Au) ||
        (cp >= 0x064Bu && cp <= 0x065Fu) ||
        cp == 0x0670u ||
        (cp >= 0x06D6u && cp <= 0x06DCu) ||
        (cp >= 0x06DFu && cp <= 0x06E4u) ||
        (cp >= 0x06E7u && cp <= 0x06E8u) ||
        (cp >= 0x06EAu && cp <= 0x06EDu) ||
        (cp >= 0x07EBu && cp <= 0x07F3u) ||
        cp == 0x07FDu ||
        (cp >= 0x08D3u && cp <= 0x08E1u) ||
        (cp >= 0x08E3u && cp <= 0x08FFu) ||
        (cp >= 0xFE20u && cp <= 0xFE2Fu)) {
        return ArabicJoinType::Transparent;
    }
    if (cp == 0x0640u || cp == 0x07FAu) return ArabicJoinType::Causing;

    // Arabic U+0620..U+06FF
    if ((cp >= 0x0622u && cp <= 0x0625u) || cp == 0x0627u || cp == 0x0629u ||
        (cp >= 0x062Fu && cp <= 0x0632u) || cp == 0x0648u ||
        (cp >= 0x0671u && cp <= 0x0673u) || (cp >= 0x0675u && cp <= 0x0677u) ||
        (cp >= 0x0688u && cp <= 0x0699u) || cp == 0x06C0u ||
        (cp >= 0x06C3u && cp <= 0x06CBu) || cp == 0x06CDu || cp == 0x06CFu ||
        (cp >= 0x06D2u && cp <= 0x06D3u) || cp == 0x06D5u ||
        (cp >= 0x06EEu && cp <= 0x06EFu)) {
        return ArabicJoinType::Right;
    }
    if (cp == 0x0620u || cp == 0x0626u || cp == 0x0628u ||
        (cp >= 0x062Au && cp <= 0x062Eu) ||
        (cp >= 0x0633u && cp <= 0x063Fu) ||
        (cp >= 0x0641u && cp <= 0x0647u) ||
        (cp >= 0x0649u && cp <= 0x064Au) ||
        (cp >= 0x066Eu && cp <= 0x066Fu) ||
        (cp >= 0x0678u && cp <= 0x0687u) ||
        (cp >= 0x069Au && cp <= 0x06BFu) ||
        (cp >= 0x06C1u && cp <= 0x06C2u) ||
        cp == 0x06CCu || cp == 0x06CEu ||
        (cp >= 0x06D0u && cp <= 0x06D1u) ||
        (cp >= 0x06FAu && cp <= 0x06FCu) || cp == 0x06FFu) {
        return ArabicJoinType::Dual;
    }
    // Arabic Supplement U+0750..U+077F
    if ((cp >= 0x0750u && cp <= 0x0758u) || (cp >= 0x075Cu && cp <= 0x076Au) ||
        (cp >= 0x076Du && cp <= 0x0770u) || cp == 0x0772u ||
        (cp >= 0x0775u && cp <= 0x0777u) || (cp >= 0x077Cu && cp <= 0x077Fu)) {
        return ArabicJoinType::Dual;
    }
    if ((cp >= 0x0759u && cp <= 0x075Bu) || (cp >= 0x076Bu && cp <= 0x076Cu) ||
        cp == 0x0771u || (cp >= 0x0773u && cp <= 0x0774u) ||
        (cp >= 0x0778u && cp <= 0x077Bu)) {
        return ArabicJoinType::Right;
    }
    // Arabic Extended-A U+08A0..U+08CE
    if (cp == 0x08AAu || cp == 0x08ABu || cp == 0x08ACu || cp == 0x08AEu ||
        cp == 0x08B1u || cp == 0x08B2u || cp == 0x08B9u) {
        return ArabicJoinType::Right;
    }
    if (cp >= 0x08A0u && cp <= 0x08CEu) {
        return ArabicJoinType::Dual;
    }
    // N'Ko U+07CA..U+07EA
    if (cp >= 0x07CAu && cp <= 0x07EAu) {
        return ArabicJoinType::Dual;
    }
    return ArabicJoinType::NonJoining;
}

std::vector<uint32_t> compute_arabic_feature_masks(const std::vector<uint32_t>& cps,
                                                   std::size_t start,
                                                   std::size_t end) {
    constexpr uint32_t kMaskGlobal = 1u << 0;
    constexpr uint32_t kMaskIsol   = 1u << 1;
    constexpr uint32_t kMaskInit   = 1u << 2;
    constexpr uint32_t kMaskMedi   = 1u << 3;
    constexpr uint32_t kMaskFina   = 1u << 4;

    const std::size_t len = end - start;
    std::vector<uint32_t> masks(len, kMaskGlobal);
    std::vector<ArabicJoinType> jts(len);
    for (std::size_t i = 0; i < len; ++i) {
        jts[i] = classify_arabic_joining(cps[start + i]);
    }
    for (std::size_t i = 0; i < len; ++i) {
        const ArabicJoinType jt = jts[i];
        if (jt == ArabicJoinType::NonJoining || jt == ArabicJoinType::Causing ||
            jt == ArabicJoinType::Transparent) {
            continue;
        }
        int32_t prev = static_cast<int32_t>(i) - 1;
        while (prev >= 0 && jts[static_cast<std::size_t>(prev)] == ArabicJoinType::Transparent) {
            --prev;
        }
        std::size_t next = i + 1;
        while (next < len && jts[next] == ArabicJoinType::Transparent) {
            ++next;
        }
        const bool prev_joins = (prev >= 0) &&
                                (jts[static_cast<std::size_t>(prev)] == ArabicJoinType::Dual ||
                                 jts[static_cast<std::size_t>(prev)] == ArabicJoinType::Left ||
                                 jts[static_cast<std::size_t>(prev)] == ArabicJoinType::Causing);
        const bool next_joins = (next < len) &&
                                (jts[next] == ArabicJoinType::Dual ||
                                 jts[next] == ArabicJoinType::Right ||
                                 jts[next] == ArabicJoinType::Causing);

        uint32_t form = kMaskIsol;
        if (jt == ArabicJoinType::Right) {
            form = prev_joins ? kMaskFina : kMaskIsol;
        } else if (jt == ArabicJoinType::Left) {
            form = next_joins ? kMaskInit : kMaskIsol;
        } else { // Dual
            if (prev_joins && next_joins) form = kMaskMedi;
            else if (prev_joins) form = kMaskFina;
            else if (next_joins) form = kMaskInit;
            else form = kMaskIsol;
        }
        masks[i] |= form;
    }
    return masks;
}

}  // namespace

bool TrueTypeFont::parse_context_subtable(std::size_t sub_off, std::size_t table_end,
                                          bool is_chain, uint16_t expected_type,
                                          OtfContextSubtable& out) const {
    (void)expected_type;
    out = OtfContextSubtable{};
    out.is_chain = is_chain;
    const auto within = [table_end](std::size_t pos, std::size_t need) {
        return pos <= table_end && need <= table_end - pos;
    };
    if (!within(sub_off, 6)) return false;
    const uint16_t fmt = read_u16_be(m_data, sub_off);
    out.format = fmt;

    const auto read_records = [&](std::size_t pos, uint16_t cnt,
                                  std::vector<OtfLookupRecord>& recs) -> bool {
        if (!within(pos, static_cast<std::size_t>(cnt) * 4u)) return false;
        recs.resize(cnt);
        for (uint16_t i = 0; i < cnt; ++i) {
            recs[i].sequence_index = read_u16_be(m_data, pos + 4u * i);
            recs[i].lookup_index = read_u16_be(m_data, pos + 2u + 4u * i);
        }
        return true;
    };

    if (!is_chain) {
        if (fmt == 1) {
            const uint16_t cov_off = read_u16_be(m_data, sub_off + 2);
            const uint16_t set_cnt = read_u16_be(m_data, sub_off + 4);
            if (!parse_coverage(sub_off + cov_off, table_end, out.cov)) return false;
            if (!within(sub_off + 6u, 2u * set_cnt)) return false;
            out.sets.resize(set_cnt);
            for (uint16_t si = 0; si < set_cnt; ++si) {
                const uint16_t rel = read_u16_be(m_data, sub_off + 6u + 2u * si);
                if (rel == 0) continue;
                const std::size_t rset = sub_off + rel;
                if (!within(rset, 2)) continue;
                const uint16_t rcnt = read_u16_be(m_data, rset);
                if (!within(rset + 2u, 2u * rcnt)) continue;
                out.sets[si].reserve(rcnt);
                for (uint16_t ri = 0; ri < rcnt; ++ri) {
                    const std::size_t rpos = rset + read_u16_be(m_data, rset + 2u + 2u * ri);
                    if (!within(rpos, 4)) continue;
                    const uint16_t gcnt = read_u16_be(m_data, rpos);
                    const uint16_t act_cnt = read_u16_be(m_data, rpos + 2);
                    if (gcnt < 1) continue;
                    const std::size_t inp_bytes = static_cast<std::size_t>(gcnt - 1u) * 2u;
                    if (!within(rpos + 4u, inp_bytes)) continue;
                    OtfContextRule rule;
                    rule.input.resize(gcnt - 1u);
                    for (uint16_t k = 0; k + 1 < gcnt; ++k) {
                        rule.input[k] = read_u16_be(m_data, rpos + 4u + 2u * k);
                    }
                    if (!read_records(rpos + 4u + inp_bytes, act_cnt, rule.actions)) continue;
                    out.sets[si].push_back(std::move(rule));
                }
            }
            return true;
        }
        if (fmt == 2) {
            if (!within(sub_off, 8)) return false;
            const uint16_t cov_off = read_u16_be(m_data, sub_off + 2);
            const uint16_t cd_off = read_u16_be(m_data, sub_off + 4);
            const uint16_t set_cnt = read_u16_be(m_data, sub_off + 6);
            if (!parse_coverage(sub_off + cov_off, table_end, out.cov)) return false;
            if (!parse_classdef(sub_off + cd_off, table_end, out.in_cd)) return false;
            if (!within(sub_off + 8u, 2u * set_cnt)) return false;
            out.sets.resize(set_cnt);
            for (uint16_t si = 0; si < set_cnt; ++si) {
                const uint16_t rel = read_u16_be(m_data, sub_off + 8u + 2u * si);
                if (rel == 0) continue;
                const std::size_t rset = sub_off + rel;
                if (!within(rset, 2)) continue;
                const uint16_t rcnt = read_u16_be(m_data, rset);
                if (!within(rset + 2u, 2u * rcnt)) continue;
                out.sets[si].reserve(rcnt);
                for (uint16_t ri = 0; ri < rcnt; ++ri) {
                    const std::size_t rpos = rset + read_u16_be(m_data, rset + 2u + 2u * ri);
                    if (!within(rpos, 4)) continue;
                    const uint16_t gcnt = read_u16_be(m_data, rpos);
                    const uint16_t act_cnt = read_u16_be(m_data, rpos + 2);
                    if (gcnt < 1) continue;
                    const std::size_t inp_bytes = static_cast<std::size_t>(gcnt - 1u) * 2u;
                    if (!within(rpos + 4u, inp_bytes)) continue;
                    OtfContextRule rule;
                    rule.input.resize(gcnt - 1u);
                    for (uint16_t k = 0; k + 1 < gcnt; ++k) {
                        rule.input[k] = read_u16_be(m_data, rpos + 4u + 2u * k);
                    }
                    if (!read_records(rpos + 4u + inp_bytes, act_cnt, rule.actions)) continue;
                    out.sets[si].push_back(std::move(rule));
                }
            }
            return true;
        }
        if (fmt == 3) {
            const uint16_t gcnt = read_u16_be(m_data, sub_off + 2);
            const uint16_t act_cnt = read_u16_be(m_data, sub_off + 4);
            if (gcnt < 1) return false;
            const std::size_t cov_bytes = static_cast<std::size_t>(gcnt) * 2u;
            if (!within(sub_off + 6u, cov_bytes)) return false;
            out.in_covs.resize(gcnt);
            for (uint16_t k = 0; k < gcnt; ++k) {
                const uint16_t rel = read_u16_be(m_data, sub_off + 6u + 2u * k);
                if (!parse_coverage(sub_off + rel, table_end, out.in_covs[k])) return false;
            }
            return read_records(sub_off + 6u + cov_bytes, act_cnt, out.fmt3_actions);
        }
        return false;
    }

    // Chaining Contextual (GSUB Type 6 / GPOS Type 8)
    const auto parse_chain_rule = [&](std::size_t rpos, OtfContextRule& rule) -> bool {
        if (!within(rpos, 2)) return false;
        const uint16_t bcnt = read_u16_be(m_data, rpos);
        std::size_t cur = rpos + 2u;
        if (!within(cur, static_cast<std::size_t>(bcnt) * 2u + 2u)) return false;
        rule.backtrack.resize(bcnt);
        for (uint16_t k = 0; k < bcnt; ++k) {
            rule.backtrack[k] = read_u16_be(m_data, cur + 2u * k);
        }
        cur += static_cast<std::size_t>(bcnt) * 2u;
        const uint16_t icnt = read_u16_be(m_data, cur);
        cur += 2u;
        if (icnt < 1) return false;
        if (!within(cur, static_cast<std::size_t>(icnt - 1u) * 2u + 2u)) return false;
        rule.input.resize(icnt - 1u);
        for (uint16_t k = 0; k + 1 < icnt; ++k) {
            rule.input[k] = read_u16_be(m_data, cur + 2u * k);
        }
        cur += static_cast<std::size_t>(icnt - 1u) * 2u;
        const uint16_t lcnt = read_u16_be(m_data, cur);
        cur += 2u;
        if (!within(cur, static_cast<std::size_t>(lcnt) * 2u + 2u)) return false;
        rule.lookahead.resize(lcnt);
        for (uint16_t k = 0; k < lcnt; ++k) {
            rule.lookahead[k] = read_u16_be(m_data, cur + 2u * k);
        }
        cur += static_cast<std::size_t>(lcnt) * 2u;
        const uint16_t act_cnt = read_u16_be(m_data, cur);
        cur += 2u;
        return read_records(cur, act_cnt, rule.actions);
    };

    if (fmt == 1) {
        const uint16_t cov_off = read_u16_be(m_data, sub_off + 2);
        const uint16_t set_cnt = read_u16_be(m_data, sub_off + 4);
        if (!parse_coverage(sub_off + cov_off, table_end, out.cov)) return false;
        if (!within(sub_off + 6u, 2u * set_cnt)) return false;
        out.sets.resize(set_cnt);
        for (uint16_t si = 0; si < set_cnt; ++si) {
            const uint16_t rel = read_u16_be(m_data, sub_off + 6u + 2u * si);
            if (rel == 0) continue;
            const std::size_t rset = sub_off + rel;
            if (!within(rset, 2)) continue;
            const uint16_t rcnt = read_u16_be(m_data, rset);
            if (!within(rset + 2u, 2u * rcnt)) continue;
            out.sets[si].reserve(rcnt);
            for (uint16_t ri = 0; ri < rcnt; ++ri) {
                const std::size_t rpos = rset + read_u16_be(m_data, rset + 2u + 2u * ri);
                OtfContextRule rule;
                if (parse_chain_rule(rpos, rule)) out.sets[si].push_back(std::move(rule));
            }
        }
        return true;
    }
    if (fmt == 2) {
        if (!within(sub_off, 12)) return false;
        const uint16_t cov_off = read_u16_be(m_data, sub_off + 2);
        const uint16_t bcd_off = read_u16_be(m_data, sub_off + 4);
        const uint16_t icd_off = read_u16_be(m_data, sub_off + 6);
        const uint16_t lcd_off = read_u16_be(m_data, sub_off + 8);
        const uint16_t set_cnt = read_u16_be(m_data, sub_off + 10);
        if (!parse_coverage(sub_off + cov_off, table_end, out.cov)) return false;
        if (bcd_off != 0 && !parse_classdef(sub_off + bcd_off, table_end, out.back_cd)) return false;
        if (icd_off != 0 && !parse_classdef(sub_off + icd_off, table_end, out.in_cd)) return false;
        if (lcd_off != 0 && !parse_classdef(sub_off + lcd_off, table_end, out.look_cd)) return false;
        if (!within(sub_off + 12u, 2u * set_cnt)) return false;
        out.sets.resize(set_cnt);
        for (uint16_t si = 0; si < set_cnt; ++si) {
            const uint16_t rel = read_u16_be(m_data, sub_off + 12u + 2u * si);
            if (rel == 0) continue;
            const std::size_t rset = sub_off + rel;
            if (!within(rset, 2)) continue;
            const uint16_t rcnt = read_u16_be(m_data, rset);
            if (!within(rset + 2u, 2u * rcnt)) continue;
            out.sets[si].reserve(rcnt);
            for (uint16_t ri = 0; ri < rcnt; ++ri) {
                const std::size_t rpos = rset + read_u16_be(m_data, rset + 2u + 2u * ri);
                OtfContextRule rule;
                if (parse_chain_rule(rpos, rule)) out.sets[si].push_back(std::move(rule));
            }
        }
        return true;
    }
    if (fmt == 3) {
        std::size_t cur = sub_off + 2u;
        if (!within(cur, 2)) return false;
        const uint16_t bcnt = read_u16_be(m_data, cur);
        cur += 2u;
        if (!within(cur, static_cast<std::size_t>(bcnt) * 2u + 2u)) return false;
        out.back_covs.resize(bcnt);
        for (uint16_t k = 0; k < bcnt; ++k) {
            if (!parse_coverage(sub_off + read_u16_be(m_data, cur + 2u * k), table_end, out.back_covs[k])) {
                return false;
            }
        }
        cur += static_cast<std::size_t>(bcnt) * 2u;
        const uint16_t icnt = read_u16_be(m_data, cur);
        cur += 2u;
        if (icnt < 1 || !within(cur, static_cast<std::size_t>(icnt) * 2u + 2u)) return false;
        out.in_covs.resize(icnt);
        for (uint16_t k = 0; k < icnt; ++k) {
            if (!parse_coverage(sub_off + read_u16_be(m_data, cur + 2u * k), table_end, out.in_covs[k])) {
                return false;
            }
        }
        cur += static_cast<std::size_t>(icnt) * 2u;
        const uint16_t lcnt = read_u16_be(m_data, cur);
        cur += 2u;
        if (!within(cur, static_cast<std::size_t>(lcnt) * 2u + 2u)) return false;
        out.look_covs.resize(lcnt);
        for (uint16_t k = 0; k < lcnt; ++k) {
            if (!parse_coverage(sub_off + read_u16_be(m_data, cur + 2u * k), table_end, out.look_covs[k])) {
                return false;
            }
        }
        cur += static_cast<std::size_t>(lcnt) * 2u;
        const uint16_t act_cnt = read_u16_be(m_data, cur);
        cur += 2u;
        return read_records(cur, act_cnt, out.fmt3_actions);
    }
    return false;
}

bool TrueTypeFont::load_ligatures(uint32_t gsub_offset, uint32_t gsub_length) {
    m_ligature_sets.clear();
    m_has_contextual_gsub = false;
    m_gsub_singles.clear();
    m_gsub_multiples.clear();
    m_gsub_alternates.clear();
    m_gsub_ligatures.clear();
    m_gsub_contexts.clear();
    m_gsub_lookups.clear();
    m_gsub_scripts.clear();

    const std::size_t table_end = static_cast<std::size_t>(gsub_offset) + gsub_length;
    if (gsub_length < 10 || table_end > m_data.size()) return false;
    const uint32_t version = read_u32_be(m_data, gsub_offset);
    if (version != 0x00010000u && version != 0x00010001u) return false;

    const auto within = [table_end](std::size_t pos, std::size_t need) {
        return pos <= table_end && need <= table_end - pos;
    };
    const uint32_t script_list = gsub_offset + read_u16_be(m_data, gsub_offset + 4);
    const uint32_t feature_list = gsub_offset + read_u16_be(m_data, gsub_offset + 6);
    const uint32_t lookup_list = gsub_offset + read_u16_be(m_data, gsub_offset + 8);
    if (!within(script_list, 2) || !within(feature_list, 2) || !within(lookup_list, 2)) return false;

    // 1. Parse all GSUB lookups (Types 1..7) indexed by LookupList index so
    //    nested contextual lookup records resolve directly.
    const uint16_t lookup_count = read_u16_be(m_data, lookup_list);
    if (!within(lookup_list + 2u, 2u * lookup_count)) return false;
    m_gsub_lookups.resize(lookup_count);

    for (uint16_t li = 0; li < lookup_count; ++li) {
        const std::size_t lpos = lookup_list + read_u16_be(m_data, lookup_list + 2u + 2u * li);
        if (!within(lpos, 6)) continue;
        const uint16_t raw_type = read_u16_be(m_data, lpos);
        const uint16_t flag = read_u16_be(m_data, lpos + 2);
        const uint16_t sub_count = read_u16_be(m_data, lpos + 4);
        if (!within(lpos + 6u, 2u * sub_count)) continue;

        uint16_t mark_filtering_set = 0;
        if ((flag & 0x0010u) != 0) {
            if (!within(lpos + 6u + 2u * sub_count, 2)) continue;
            mark_filtering_set = read_u16_be(m_data, lpos + 6u + 2u * sub_count);
        }

        GsubLookup& lk = m_gsub_lookups[li];
        lk.flag = flag;
        lk.mark_filtering_set = mark_filtering_set;

        for (uint16_t s = 0; s < sub_count; ++s) {
            std::size_t sub = lpos + read_u16_be(m_data, lpos + 6u + 2u * s);
            uint16_t ltype = raw_type;
            if (ltype == 7) { // ExtensionSubst
                if (!within(sub, 8) || read_u16_be(m_data, sub) != 1) continue;
                ltype = read_u16_be(m_data, sub + 2);
                const uint32_t ext_rel = read_u32_be(m_data, sub + 4);
                if (ext_rel > table_end - sub) continue;
                sub += ext_rel;
            }
            if (!within(sub, 4)) continue;
            if (lk.type == 0) lk.type = ltype;

            if (ltype == 1) { // SingleSubst
                const uint16_t fmt = read_u16_be(m_data, sub);
                if (!within(sub, 6)) continue;
                GsubSingleSubtable st;
                st.format = fmt;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.cov)) continue;
                if (fmt == 1) {
                    st.delta = read_i16_be(m_data, sub + 4);
                    const uint32_t idx = static_cast<uint32_t>(m_gsub_singles.size());
                    m_gsub_singles.push_back(std::move(st));
                    lk.subtables.push_back({GsubSubRef::Kind::Single, idx});
                } else if (fmt == 2) {
                    const uint16_t gcnt = read_u16_be(m_data, sub + 4);
                    if (!within(sub + 6u, 2u * gcnt)) continue;
                    st.substitutes.resize(gcnt);
                    for (uint16_t k = 0; k < gcnt; ++k) {
                        st.substitutes[k] = read_u16_be(m_data, sub + 6u + 2u * k);
                    }
                    const uint32_t idx = static_cast<uint32_t>(m_gsub_singles.size());
                    m_gsub_singles.push_back(std::move(st));
                    lk.subtables.push_back({GsubSubRef::Kind::Single, idx});
                }
            } else if (ltype == 2) { // MultipleSubst
                if (read_u16_be(m_data, sub) != 1 || !within(sub, 6)) continue;
                GsubMultipleSubtable st;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.cov)) continue;
                const uint16_t scnt = read_u16_be(m_data, sub + 4);
                if (!within(sub + 6u, 2u * scnt)) continue;
                st.sequences.resize(scnt);
                for (uint16_t k = 0; k < scnt; ++k) {
                    const std::size_t seq_pos = sub + read_u16_be(m_data, sub + 6u + 2u * k);
                    if (!within(seq_pos, 2)) continue;
                    const uint16_t gc = read_u16_be(m_data, seq_pos);
                    if (gc > 64 || !within(seq_pos + 2u, 2u * gc)) continue;
                    st.sequences[k].resize(gc);
                    for (uint16_t j = 0; j < gc; ++j) {
                        st.sequences[k][j] = read_u16_be(m_data, seq_pos + 2u + 2u * j);
                    }
                }
                const uint32_t idx = static_cast<uint32_t>(m_gsub_multiples.size());
                m_gsub_multiples.push_back(std::move(st));
                lk.subtables.push_back({GsubSubRef::Kind::Multiple, idx});
            } else if (ltype == 3) { // AlternateSubst
                if (read_u16_be(m_data, sub) != 1 || !within(sub, 6)) continue;
                GsubAlternateSubtable st;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.cov)) continue;
                const uint16_t acnt = read_u16_be(m_data, sub + 4);
                if (!within(sub + 6u, 2u * acnt)) continue;
                st.alternates.resize(acnt);
                for (uint16_t k = 0; k < acnt; ++k) {
                    const std::size_t aset = sub + read_u16_be(m_data, sub + 6u + 2u * k);
                    if (!within(aset, 2)) continue;
                    const uint16_t gc = read_u16_be(m_data, aset);
                    if (!within(aset + 2u, 2u * gc)) continue;
                    st.alternates[k].resize(gc);
                    for (uint16_t j = 0; j < gc; ++j) {
                        st.alternates[k][j] = read_u16_be(m_data, aset + 2u + 2u * j);
                    }
                }
                const uint32_t idx = static_cast<uint32_t>(m_gsub_alternates.size());
                m_gsub_alternates.push_back(std::move(st));
                lk.subtables.push_back({GsubSubRef::Kind::Alternate, idx});
            } else if (ltype == 4) { // LigatureSubst
                if (read_u16_be(m_data, sub) != 1 || !within(sub, 6)) continue;
                GsubLigatureSubtable st;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.cov)) continue;
                const uint16_t set_count = read_u16_be(m_data, sub + 4);
                if (!within(sub + 6u, 2u * set_count)) continue;
                st.sets.resize(set_count);
                const bool ignore_marks = (flag & 0x0008u) != 0;
                for (uint16_t si = 0; si < set_count; ++si) {
                    const std::size_t set_pos = sub + read_u16_be(m_data, sub + 6u + 2u * si);
                    if (!within(set_pos, 2)) continue;
                    const uint16_t lcnt = read_u16_be(m_data, set_pos);
                    if (!within(set_pos + 2u, 2u * lcnt)) continue;
                    auto& ligs = st.sets[si];
                    ligs.reserve(lcnt);
                    for (uint16_t l = 0; l < lcnt; ++l) {
                        const std::size_t lpos2 = set_pos + read_u16_be(m_data, set_pos + 2u + 2u * l);
                        if (!within(lpos2, 4)) continue;
                        const uint16_t produced = read_u16_be(m_data, lpos2);
                        const uint16_t comps = read_u16_be(m_data, lpos2 + 2);
                        if (comps < 2 || comps > 32) continue;
                        if (!within(lpos2 + 4u, 2u * (comps - 1u))) continue;
                        Ligature lig;
                        lig.glyph = produced;
                        lig.ignore_marks = ignore_marks;
                        lig.rest.reserve(comps - 1u);
                        for (uint16_t c = 1; c < comps; ++c) {
                            lig.rest.push_back(read_u16_be(m_data, lpos2 + 2u + 2u * c));
                        }
                        ligs.push_back(std::move(lig));
                    }
                    std::stable_sort(ligs.begin(), ligs.end(),
                                     [](const Ligature& a, const Ligature& b) {
                                         return a.rest.size() > b.rest.size();
                                     });
                }
                const uint32_t idx = static_cast<uint32_t>(m_gsub_ligatures.size());
                m_gsub_ligatures.push_back(std::move(st));
                lk.subtables.push_back({GsubSubRef::Kind::Ligature, idx});
            } else if (ltype == 5 || ltype == 6) {
                OtfContextSubtable ctx;
                if (parse_context_subtable(sub, table_end, ltype == 6, ltype, ctx)) {
                    const uint32_t idx = static_cast<uint32_t>(m_gsub_contexts.size());
                    m_gsub_contexts.push_back(std::move(ctx));
                    lk.subtables.push_back({GsubSubRef::Kind::Context, idx});
                    m_has_contextual_gsub = true;
                }
            }
        }
    }

    // 2. Collect per-script active GSUB feature plans.
    const uint16_t script_count = read_u16_be(m_data, script_list);
    const uint16_t total_features = read_u16_be(m_data, feature_list);
    std::vector<uint16_t> latn_liga_lookups;
    std::vector<uint16_t> dflt_liga_lookups;

    for (uint16_t si = 0; si < script_count; ++si) {
        const std::size_t rec = script_list + 2u + 6u * si;
        if (!within(rec, 6)) break;
        const uint32_t stag = read_u32_be(m_data, rec);
        const std::size_t spos = script_list + read_u16_be(m_data, rec + 4);
        if (!within(spos, 4)) continue;
        uint16_t ls_off = read_u16_be(m_data, spos);
        if (ls_off == 0) {
            const uint16_t ls_cnt = read_u16_be(m_data, spos + 2);
            if (ls_cnt > 0 && within(spos + 4u, 6)) ls_off = read_u16_be(m_data, spos + 8);
        }
        if (ls_off == 0 || !within(spos + ls_off, 6)) continue;
        const std::size_t lang_sys = spos + ls_off;

        std::map<uint16_t, uint32_t> active;
        const auto add_feat = [&](uint16_t fi, bool is_required) {
            if (fi >= total_features) return;
            const std::size_t frec = feature_list + 2u + 6u * fi;
            if (!within(frec, 6)) return;
            const uint32_t ftag = read_u32_be(m_data, frec);
            uint32_t fmask = kGsubFeatNone;
            if (is_required) fmask |= kGsubFeatGlobal;
            if (ftag == ot_tag('c', 'c', 'm', 'p') ||
                ftag == ot_tag('l', 'o', 'c', 'l') ||
                ftag == ot_tag('r', 'l', 'i', 'g') ||
                ftag == ot_tag('l', 'i', 'g', 'a') ||
                ftag == ot_tag('c', 'a', 'l', 't') ||
                ftag == ot_tag('c', 'l', 'i', 'g') ||
                ftag == ot_tag('r', 'c', 'l', 't')) {
                fmask |= kGsubFeatGlobal;
            } else if (ftag == ot_tag('i', 's', 'o', 'l')) {
                fmask |= kGsubFeatIsol;
            } else if (ftag == ot_tag('i', 'n', 'i', 't')) {
                fmask |= kGsubFeatInit;
            } else if (ftag == ot_tag('m', 'e', 'd', 'i')) {
                fmask |= kGsubFeatMedi;
            } else if (ftag == ot_tag('f', 'i', 'n', 'a')) {
                fmask |= kGsubFeatFina;
            }
            if (fmask == kGsubFeatNone) return;

            const std::size_t fpos = feature_list + read_u16_be(m_data, frec + 4);
            if (!within(fpos, 4)) return;
            const uint16_t lcnt = read_u16_be(m_data, fpos + 2);
            for (uint16_t k = 0; k < lcnt; ++k) {
                if (!within(fpos + 4u + 2u * k, 2)) break;
                const uint16_t li = read_u16_be(m_data, fpos + 4u + 2u * k);
                if (li >= lookup_count) continue;
                active[li] |= fmask;
                if (ftag == ot_tag('l', 'i', 'g', 'a') || ftag == ot_tag('r', 'l', 'i', 'g')) {
                    if (stag == kTagLatn) latn_liga_lookups.push_back(li);
                    else if (stag == kTagDflt) dflt_liga_lookups.push_back(li);
                }
            }
        };

        const uint16_t req = read_u16_be(m_data, lang_sys + 2);
        if (req != 0xFFFFu) add_feat(req, true);
        const uint16_t fc = read_u16_be(m_data, lang_sys + 4);
        for (uint16_t i = 0; i < fc; ++i) {
            if (!within(lang_sys + 6u + 2u * i, 2)) break;
            add_feat(read_u16_be(m_data, lang_sys + 6u + 2u * i), false);
        }

        if (!active.empty()) {
            GsubScriptPlan plan;
            plan.script_tag = stag;
            plan.lookups.assign(active.begin(), active.end());
            m_gsub_scripts.push_back(std::move(plan));
        }
    }

    // 3. Populate `m_ligature_sets` for `latn` (or `DFLT`) `liga`/`rlig` so
    //    `has_ligatures()` and `apply_ligatures()` remain identical to callers.
    const std::vector<uint16_t>& liga_lks =
        !latn_liga_lookups.empty() ? latn_liga_lookups : dflt_liga_lookups;
    if (!liga_lks.empty()) {
        std::map<uint16_t, std::vector<Ligature>> by_first;
        for (uint16_t li : liga_lks) {
            if (li >= m_gsub_lookups.size()) continue;
            const GsubLookup& lk = m_gsub_lookups[li];
            if (lk.type != 4) continue;
            for (const GsubSubRef& ref : lk.subtables) {
                if (ref.kind != GsubSubRef::Kind::Ligature) continue;
                const auto& st = m_gsub_ligatures[ref.index];
                if (st.cov.format == 1) {
                    for (std::size_t idx = 0; idx < st.cov.glyphs.size() && idx < st.sets.size(); ++idx) {
                        auto& dst = by_first[st.cov.glyphs[idx]];
                        dst.insert(dst.end(), st.sets[idx].begin(), st.sets[idx].end());
                    }
                } else if (st.cov.format == 2) {
                    for (const auto& r : st.cov.ranges) {
                        for (uint32_t g = r.start; g <= r.end; ++g) {
                            const std::size_t idx = r.start_index + (g - r.start);
                            if (idx < st.sets.size()) {
                                auto& dst = by_first[static_cast<uint16_t>(g)];
                                dst.insert(dst.end(), st.sets[idx].begin(), st.sets[idx].end());
                            }
                        }
                    }
                }
            }
        }
        m_ligature_sets.reserve(by_first.size());
        for (auto& [first_glyph, ligatures] : by_first) {
            LigatureSet set;
            set.first = first_glyph;
            std::stable_sort(ligatures.begin(), ligatures.end(),
                             [](const Ligature& a, const Ligature& b) {
                                 return a.rest.size() > b.rest.size();
                             });
            set.ligatures = std::move(ligatures);
            m_ligature_sets.push_back(std::move(set));
        }
        std::sort(m_ligature_sets.begin(), m_ligature_sets.end(),
                  [](const LigatureSet& a, const LigatureSet& b) { return a.first < b.first; });
    }
    return !m_gsub_lookups.empty();
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

int TrueTypeFont::OtfCoverage::index(uint16_t glyph) const {
    if (format == 1) {
        if (sorted) {
            auto it = std::lower_bound(glyphs.begin(), glyphs.end(), glyph);
            if (it != glyphs.end() && *it == glyph) {
                return static_cast<int>(it - glyphs.begin());
            }
            return -1;
        }
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            if (glyphs[i] == glyph) return static_cast<int>(i);
        }
        return -1;
    }
    if (format == 2) {
        if (sorted) {
            std::size_t lo = 0, hi = ranges.size();
            while (lo < hi) {
                const std::size_t mid = lo + (hi - lo) / 2;
                if (ranges[mid].start <= glyph) lo = mid + 1;
                else hi = mid;
            }
            if (lo > 0) {
                const Range& r = ranges[lo - 1];
                if (glyph >= r.start && glyph <= r.end) {
                    return static_cast<int>(r.start_index + (glyph - r.start));
                }
            }
            return -1;
        }
        for (const Range& r : ranges) {
            if (glyph >= r.start && glyph <= r.end) {
                return static_cast<int>(r.start_index + (glyph - r.start));
            }
        }
    }
    return -1;
}

uint16_t TrueTypeFont::OtfClassDef::class_of(uint16_t glyph) const {
    if (format == 1) {
        if (glyph < start_glyph) return 0;
        const std::size_t idx = static_cast<std::size_t>(glyph - start_glyph);
        return idx < classes.size() ? classes[idx] : 0;
    }
    if (format == 2) {
        if (sorted) {
            std::size_t lo = 0, hi = ranges.size();
            while (lo < hi) {
                const std::size_t mid = lo + (hi - lo) / 2;
                if (ranges[mid].start <= glyph) lo = mid + 1;
                else hi = mid;
            }
            if (lo > 0) {
                const Range& r = ranges[lo - 1];
                if (glyph >= r.start && glyph <= r.end) return r.cls;
            }
            return 0;
        }
        for (const Range& r : ranges) {
            if (glyph >= r.start && glyph <= r.end) return r.cls;
        }
    }
    return 0;
}

bool TrueTypeFont::parse_coverage(std::size_t cov_off, std::size_t table_end, OtfCoverage& out) const {
    out = OtfCoverage{};
    if (cov_off > table_end || table_end - cov_off < 4) return false;
    const uint16_t fmt = read_u16_be(m_data, cov_off);
    const uint16_t count = read_u16_be(m_data, cov_off + 2);
    if (fmt == 1) {
        if (static_cast<std::size_t>(count) * 2u > table_end - (cov_off + 4u)) return false;
        out.format = 1;
        out.glyphs.resize(count);
        for (uint16_t i = 0; i < count; ++i) {
            const uint16_t g = read_u16_be(m_data, cov_off + 4u + 2u * i);
            out.glyphs[i] = g;
            if (i > 0 && g <= out.glyphs[i - 1]) out.sorted = false;
        }
        return true;
    }
    if (fmt == 2) {
        if (static_cast<std::size_t>(count) * 6u > table_end - (cov_off + 4u)) return false;
        out.format = 2;
        out.ranges.resize(count);
        for (uint16_t i = 0; i < count; ++i) {
            const std::size_t rec = cov_off + 4u + 6u * i;
            OtfCoverage::Range r;
            r.start = read_u16_be(m_data, rec);
            r.end = read_u16_be(m_data, rec + 2);
            r.start_index = read_u16_be(m_data, rec + 4);
            if (r.end < r.start) return false;
            if (i > 0 && r.start <= out.ranges[i - 1].end) out.sorted = false;
            out.ranges[i] = r;
        }
        return true;
    }
    return false;
}

bool TrueTypeFont::parse_classdef(std::size_t cd_off, std::size_t table_end, OtfClassDef& out) const {
    out = OtfClassDef{};
    if (cd_off > table_end || table_end - cd_off < 4) return false;
    const uint16_t fmt = read_u16_be(m_data, cd_off);
    if (fmt == 1) {
        if (table_end - cd_off < 6) return false;
        const uint16_t start_g = read_u16_be(m_data, cd_off + 2);
        const uint16_t count = read_u16_be(m_data, cd_off + 4);
        if (static_cast<std::size_t>(count) * 2u > table_end - (cd_off + 6u)) return false;
        out.format = 1;
        out.start_glyph = start_g;
        out.classes.resize(count);
        for (uint16_t i = 0; i < count; ++i) {
            out.classes[i] = read_u16_be(m_data, cd_off + 6u + 2u * i);
        }
        return true;
    }
    if (fmt == 2) {
        const uint16_t count = read_u16_be(m_data, cd_off + 2);
        if (static_cast<std::size_t>(count) * 6u > table_end - (cd_off + 4u)) return false;
        out.format = 2;
        out.ranges.resize(count);
        for (uint16_t i = 0; i < count; ++i) {
            const std::size_t rec = cd_off + 4u + 6u * i;
            OtfClassDef::Range r;
            r.start = read_u16_be(m_data, rec);
            r.end = read_u16_be(m_data, rec + 2);
            r.cls = read_u16_be(m_data, rec + 4);
            if (r.end < r.start) return false;
            if (i > 0 && r.start <= out.ranges[i - 1].end) out.sorted = false;
            out.ranges[i] = r;
        }
        return true;
    }
    return false;
}

namespace {

std::size_t gpos_value_size(uint16_t vf) {
    std::size_t count = 0;
    for (unsigned b = 0; b < 8; ++b) {
        if ((vf & (1u << b)) != 0) ++count;
    }
    return count * 2u;
}

bool gpos_value_nonzero(const auto& v) {
    return v.x_placement != 0 || v.y_placement != 0 || v.x_advance != 0 || v.y_advance != 0;
}

}  // namespace

bool TrueTypeFont::read_gpos_value(std::size_t pos, std::size_t table_end, uint16_t vf,
                                   GposValue& out) const {
    out = GposValue{};
    if ((vf & 0xFF00u) != 0) return false;
    const std::size_t sz = gpos_value_size(vf);
    if (pos > table_end || sz > table_end - pos) return false;
    std::size_t cur = pos;
    if (vf & 0x0001u) { out.x_placement = read_i16_be(m_data, cur); cur += 2; }
    if (vf & 0x0002u) { out.y_placement = read_i16_be(m_data, cur); cur += 2; }
    if (vf & 0x0004u) { out.x_advance = read_i16_be(m_data, cur); cur += 2; }
    if (vf & 0x0008u) { out.y_advance = read_i16_be(m_data, cur); cur += 2; }
    return true;
}

bool TrueTypeFont::read_gpos_anchor(std::size_t pos, std::size_t table_end, GposAnchor& out) const {
    out = GposAnchor{};
    if (pos > table_end || table_end - pos < 6) return false;
    const uint16_t fmt = read_u16_be(m_data, pos);
    if (fmt != 1 && fmt != 2 && fmt != 3) return false;
    out.present = true;
    out.x = read_i16_be(m_data, pos + 2);
    out.y = read_i16_be(m_data, pos + 4);
    return true;
}

bool TrueTypeFont::is_mark_glyph(uint16_t gid) const {
    if (m_has_gdef_glyph_class) return m_gdef_glyph_class.class_of(gid) == 3;
    return std::binary_search(m_gpos_known_marks.begin(), m_gpos_known_marks.end(), gid);
}

bool TrueTypeFont::is_ignored_by_lookup(uint16_t gid, uint16_t flag,
                                        uint16_t mark_filtering_set) const {
    const uint16_t cls = m_has_gdef_glyph_class
                             ? m_gdef_glyph_class.class_of(gid)
                             : (is_mark_glyph(gid) ? 3u : 0u);
    if ((flag & 0x0002u) && cls == 1) return true; // IgnoreBaseGlyphs
    if ((flag & 0x0004u) && cls == 2) return true; // IgnoreLigatures
    if ((flag & 0x0008u) && cls == 3) return true; // IgnoreMarks
    if (cls == 3) {
        const uint16_t mark_attach_type = static_cast<uint16_t>((flag >> 8) & 0xFFu);
        if (mark_attach_type != 0 && m_gdef_mark_attach_class.class_of(gid) != mark_attach_type) {
            return true;
        }
        if ((flag & 0x0010u) && mark_filtering_set < m_gdef_mark_sets.size() &&
            m_gdef_mark_sets[mark_filtering_set].index(gid) < 0) {
            return true;
        }
    }
    return false;
}

bool TrueTypeFont::load_gdef(uint32_t gdef_offset, uint32_t gdef_length) {
    if (gdef_length < 12) return false;
    const std::size_t table_end = static_cast<std::size_t>(gdef_offset) + gdef_length;
    if (table_end > m_data.size()) return false;
    const uint32_t version = read_u32_be(m_data, gdef_offset);
    if (version != 0x00010000u && version != 0x00010002u && version != 0x00010003u) return false;

    const uint16_t glyph_class_off = read_u16_be(m_data, gdef_offset + 4);
    const uint16_t mark_attach_off = read_u16_be(m_data, gdef_offset + 10);
    if (glyph_class_off != 0) {
        if (parse_classdef(gdef_offset + glyph_class_off, table_end, m_gdef_glyph_class)) {
            m_has_gdef_glyph_class = true;
        }
    }
    if (mark_attach_off != 0) {
        parse_classdef(gdef_offset + mark_attach_off, table_end, m_gdef_mark_attach_class);
    }
    if (version >= 0x00010002u && gdef_length >= 14) {
        const uint16_t mark_sets_off = read_u16_be(m_data, gdef_offset + 12);
        if (mark_sets_off != 0) {
            const std::size_t mgs = gdef_offset + mark_sets_off;
            if (mgs + 4 <= table_end && read_u16_be(m_data, mgs) == 1) {
                const uint16_t cnt = read_u16_be(m_data, mgs + 2);
                if (static_cast<std::size_t>(cnt) * 4u <= table_end - (mgs + 4u)) {
                    m_gdef_mark_sets.resize(cnt);
                    for (uint16_t i = 0; i < cnt; ++i) {
                        const uint32_t rel = read_u32_be(m_data, mgs + 4u + 4u * i);
                        if (rel < table_end - mgs) {
                            parse_coverage(mgs + rel, table_end, m_gdef_mark_sets[i]);
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool TrueTypeFont::load_gpos(uint32_t gpos_offset, uint32_t gpos_length) {
    m_has_gpos_kern = false;
    m_has_gpos_cursive = false;
    m_has_gpos_marks = false;
    m_has_contextual_gpos = false;
    m_gpos_known_marks.clear();
    m_gpos_singles.clear();
    m_gpos_pair_fmt1.clear();
    m_gpos_pair_fmt2.clear();
    m_gpos_cursives.clear();
    m_gpos_mark_base.clear();
    m_gpos_mark_lig.clear();
    m_gpos_mark_mark.clear();
    m_gpos_contexts.clear();
    m_gpos_lookups.clear();
    m_gpos_scripts.clear();

    if (gpos_length < 10) return false;
    const std::size_t table_end = static_cast<std::size_t>(gpos_offset) + gpos_length;
    if (table_end > m_data.size()) return false;
    const uint32_t version = read_u32_be(m_data, gpos_offset);
    if (version != 0x00010000u && version != 0x00010001u) return false;

    const auto within = [table_end](std::size_t pos, std::size_t need) {
        return pos <= table_end && need <= table_end - pos;
    };

    const std::size_t script_list = gpos_offset + read_u16_be(m_data, gpos_offset + 4);
    const std::size_t feature_list = gpos_offset + read_u16_be(m_data, gpos_offset + 6);
    const std::size_t lookup_list = gpos_offset + read_u16_be(m_data, gpos_offset + 8);
    if (!within(script_list, 2) || !within(feature_list, 2) || !within(lookup_list, 2)) return false;

    const auto parse_mark_array = [&](std::size_t ma_pos, uint16_t expect_count,
                                      std::vector<GposMarkRecord>& out) -> bool {
        if (!within(ma_pos, 2)) return false;
        const uint16_t mark_count = read_u16_be(m_data, ma_pos);
        if (mark_count < expect_count) return false;
        if (static_cast<std::size_t>(mark_count) * 4u > table_end - (ma_pos + 2u)) return false;
        out.resize(expect_count);
        for (uint16_t i = 0; i < expect_count; ++i) {
            const uint16_t mcls = read_u16_be(m_data, ma_pos + 2u + 4u * i);
            const uint16_t aoff = read_u16_be(m_data, ma_pos + 4u + 4u * i);
            out[i].mark_class = mcls;
            if (aoff != 0) read_gpos_anchor(ma_pos + aoff, table_end, out[i].anchor);
        }
        return true;
    };

    const auto collect_cov_glyphs = [](const OtfCoverage& cov, std::vector<uint16_t>& dst) {
        if (cov.format == 1) {
            dst.insert(dst.end(), cov.glyphs.begin(), cov.glyphs.end());
        } else if (cov.format == 2) {
            for (const auto& r : cov.ranges) {
                for (uint32_t g = r.start; g <= r.end; ++g) dst.push_back(static_cast<uint16_t>(g));
            }
        }
    };

    // 1. Parse ALL lookups (0..lookup_count-1) so contextual PosLookupRecords
    //    can call nested lookups directly by LookupList index.
    const uint16_t lookup_count = read_u16_be(m_data, lookup_list);
    if (!within(lookup_list + 2u, 2u * lookup_count)) return false;
    m_gpos_lookups.resize(lookup_count);
    std::vector<uint16_t> known_marks;

    for (uint16_t lookup_index = 0; lookup_index < lookup_count; ++lookup_index) {
        const std::size_t lpos = lookup_list + read_u16_be(m_data, lookup_list + 2u + 2u * lookup_index);
        if (!within(lpos, 6)) continue;

        const uint16_t raw_type = read_u16_be(m_data, lpos);
        const uint16_t flag = read_u16_be(m_data, lpos + 2);
        const uint16_t sub_count = read_u16_be(m_data, lpos + 4);
        if (!within(lpos + 6u, 2u * sub_count)) continue;

        uint16_t mark_filtering_set = 0;
        if ((flag & 0x0010u) != 0) {
            if (!within(lpos + 6u + 2u * sub_count, 2)) continue;
            mark_filtering_set = read_u16_be(m_data, lpos + 6u + 2u * sub_count);
        }

        GposLookup& parsed_lookup = m_gpos_lookups[lookup_index];
        parsed_lookup.flag = flag;
        parsed_lookup.mark_filtering_set = mark_filtering_set;
        parsed_lookup.feature = GposLookup::Feature::Other;

        for (uint16_t s = 0; s < sub_count; ++s) {
            std::size_t sub = lpos + read_u16_be(m_data, lpos + 6u + 2u * s);
            uint16_t ltype = raw_type;
            if (ltype == 9) { // Extension Positioning
                if (!within(sub, 8) || read_u16_be(m_data, sub) != 1) continue;
                ltype = read_u16_be(m_data, sub + 2);
                const uint32_t ext_rel = read_u32_be(m_data, sub + 4);
                if (ext_rel > table_end - sub) continue;
                sub += ext_rel;
            }
            if (!within(sub, 4)) continue;
            if (parsed_lookup.type == 0) parsed_lookup.type = ltype;

            if (ltype == 1) { // Single Adjustment
                const uint16_t fmt = read_u16_be(m_data, sub);
                if (!within(sub, 6)) continue;
                const uint16_t cov_off = read_u16_be(m_data, sub + 2);
                const uint16_t vf = read_u16_be(m_data, sub + 4);
                if ((vf & 0xFF00u) != 0) continue;
                GposSingleSubtable st;
                st.format = fmt;
                if (!parse_coverage(sub + cov_off, table_end, st.cov)) continue;
                if (fmt == 1) {
                    if (!read_gpos_value(sub + 6, table_end, vf, st.single_val)) continue;
                    const uint32_t idx = static_cast<uint32_t>(m_gpos_singles.size());
                    m_gpos_singles.push_back(std::move(st));
                    parsed_lookup.subtables.push_back({GposSubRef::Kind::Single, idx});
                } else if (fmt == 2) {
                    if (!within(sub, 8)) continue;
                    const uint16_t vcnt = read_u16_be(m_data, sub + 6);
                    const std::size_t vsz = gpos_value_size(vf);
                    if (static_cast<std::size_t>(vcnt) * vsz > table_end - (sub + 8u)) continue;
                    st.vals.resize(vcnt);
                    bool ok = true;
                    for (uint16_t k = 0; k < vcnt; ++k) {
                        if (!read_gpos_value(sub + 8u + static_cast<std::size_t>(k) * vsz, table_end,
                                             vf, st.vals[k])) {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) continue;
                    const uint32_t idx = static_cast<uint32_t>(m_gpos_singles.size());
                    m_gpos_singles.push_back(std::move(st));
                    parsed_lookup.subtables.push_back({GposSubRef::Kind::Single, idx});
                }
            } else if (ltype == 2) { // Pair Adjustment
                const uint16_t fmt = read_u16_be(m_data, sub);
                if (!within(sub, 10)) continue;
                const uint16_t cov_off = read_u16_be(m_data, sub + 2);
                const uint16_t vf1 = read_u16_be(m_data, sub + 4);
                const uint16_t vf2 = read_u16_be(m_data, sub + 6);
                if ((vf1 & 0xFF00u) != 0 || (vf2 & 0xFF00u) != 0) continue;
                const std::size_t vsz1 = gpos_value_size(vf1);
                const std::size_t vsz2 = gpos_value_size(vf2);
                if (fmt == 1) {
                    const uint16_t pscnt = read_u16_be(m_data, sub + 8);
                    if (!within(sub + 10u, 2u * pscnt)) continue;
                    GposPairFmt1Subtable st;
                    if (!parse_coverage(sub + cov_off, table_end, st.cov)) continue;
                    st.has_v2 = (vf2 != 0);
                    st.pair_sets.resize(pscnt);
                    const std::size_t rec_sz = 2u + vsz1 + vsz2;
                    for (uint16_t pi = 0; pi < pscnt; ++pi) {
                        const std::size_t pset = sub + read_u16_be(m_data, sub + 10u + 2u * pi);
                        if (!within(pset, 2)) continue;
                        const uint16_t pvc = read_u16_be(m_data, pset);
                        if (static_cast<std::size_t>(pvc) * rec_sz > table_end - (pset + 2u)) continue;
                        auto& entries = st.pair_sets[pi];
                        entries.reserve(pvc);
                        for (uint16_t k = 0; k < pvc; ++k) {
                            const std::size_t rec = pset + 2u + static_cast<std::size_t>(k) * rec_sz;
                            GposPairEntry e;
                            e.second = read_u16_be(m_data, rec);
                            read_gpos_value(rec + 2u, table_end, vf1, e.v1);
                            if (vf2 != 0) read_gpos_value(rec + 2u + vsz1, table_end, vf2, e.v2);
                            entries.push_back(e);
                        }
                        std::stable_sort(entries.begin(), entries.end(),
                                         [](const GposPairEntry& a, const GposPairEntry& b) {
                                             return a.second < b.second;
                                         });
                    }
                    const uint32_t idx = static_cast<uint32_t>(m_gpos_pair_fmt1.size());
                    m_gpos_pair_fmt1.push_back(std::move(st));
                    parsed_lookup.subtables.push_back({GposSubRef::Kind::PairFmt1, idx});
                } else if (fmt == 2) {
                    if (!within(sub, 16)) continue;
                    GposPairFmt2Subtable st;
                    if (!parse_coverage(sub + cov_off, table_end, st.cov)) continue;
                    const uint16_t cd1_off = read_u16_be(m_data, sub + 8);
                    const uint16_t cd2_off = read_u16_be(m_data, sub + 10);
                    if (!parse_classdef(sub + cd1_off, table_end, st.class1)) continue;
                    if (!parse_classdef(sub + cd2_off, table_end, st.class2)) continue;
                    st.c1_count = read_u16_be(m_data, sub + 12);
                    st.c2_count = read_u16_be(m_data, sub + 14);
                    st.vf1 = vf1;
                    st.vf2 = vf2;
                    st.matrix_offset = static_cast<uint32_t>(sub + 16u);
                    const std::size_t rec_sz = vsz1 + vsz2;
                    const std::size_t total_cells = static_cast<std::size_t>(st.c1_count) * st.c2_count;
                    if (rec_sz > 0 && st.c1_count > 0 && st.c2_count > 0 &&
                        (total_cells / st.c1_count != st.c2_count ||
                         total_cells > (table_end - (sub + 16u)) / rec_sz)) {
                        continue;
                    }
                    const uint32_t idx = static_cast<uint32_t>(m_gpos_pair_fmt2.size());
                    m_gpos_pair_fmt2.push_back(std::move(st));
                    parsed_lookup.subtables.push_back({GposSubRef::Kind::PairFmt2, idx});
                }
            } else if (ltype == 3) { // Cursive Attachment
                if (!within(sub, 6) || read_u16_be(m_data, sub) != 1) continue;
                GposCursiveSubtable st;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.cov)) continue;
                const uint16_t ee_count = read_u16_be(m_data, sub + 4);
                if (!within(sub + 6u, static_cast<std::size_t>(ee_count) * 4u)) continue;
                st.records.resize(ee_count);
                for (uint16_t k = 0; k < ee_count; ++k) {
                    const uint16_t eoff = read_u16_be(m_data, sub + 6u + 4u * k);
                    const uint16_t xoff = read_u16_be(m_data, sub + 8u + 4u * k);
                    if (eoff != 0) read_gpos_anchor(sub + eoff, table_end, st.records[k].entry);
                    if (xoff != 0) read_gpos_anchor(sub + xoff, table_end, st.records[k].exit);
                }
                const uint32_t idx = static_cast<uint32_t>(m_gpos_cursives.size());
                m_gpos_cursives.push_back(std::move(st));
                parsed_lookup.subtables.push_back({GposSubRef::Kind::Cursive, idx});
                m_has_gpos_cursive = true;
            } else if (ltype == 4) { // MarkToBase
                if (!within(sub, 12) || read_u16_be(m_data, sub) != 1) continue;
                GposMarkBaseSubtable st;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.mark_cov)) continue;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 4), table_end, st.base_cov)) continue;
                st.class_count = read_u16_be(m_data, sub + 6);
                const std::size_t ma_pos = sub + read_u16_be(m_data, sub + 8);
                const std::size_t ba_pos = sub + read_u16_be(m_data, sub + 10);
                if (!within(ba_pos, 2)) continue;
                const uint16_t base_count = read_u16_be(m_data, ba_pos);
                const std::size_t mark_cov_count =
                    st.mark_cov.format == 1 ? st.mark_cov.glyphs.size()
                                            : [&]() {
                                                  std::size_t c = 0;
                                                  for (const auto& r : st.mark_cov.ranges) {
                                                      c = std::max<std::size_t>(c, r.start_index + (r.end - r.start) + 1u);
                                                  }
                                                  return c;
                                              }();
                if (mark_cov_count > 0xFFFFu) continue;
                if (!parse_mark_array(ma_pos, static_cast<uint16_t>(mark_cov_count), st.marks)) continue;
                const std::size_t num_base_anchors =
                    static_cast<std::size_t>(base_count) * st.class_count;
                if (st.class_count > 0 &&
                    (num_base_anchors / st.class_count != base_count ||
                     num_base_anchors > (table_end - (ba_pos + 2u)) / 2u)) {
                    continue;
                }
                st.base_anchors.resize(num_base_anchors);
                for (std::size_t k = 0; k < num_base_anchors; ++k) {
                    const uint16_t aoff = read_u16_be(m_data, ba_pos + 2u + 2u * k);
                    if (aoff != 0) read_gpos_anchor(ba_pos + aoff, table_end, st.base_anchors[k]);
                }
                collect_cov_glyphs(st.mark_cov, known_marks);
                const uint32_t idx = static_cast<uint32_t>(m_gpos_mark_base.size());
                m_gpos_mark_base.push_back(std::move(st));
                parsed_lookup.subtables.push_back({GposSubRef::Kind::MarkBase, idx});
                m_has_gpos_marks = true;
            } else if (ltype == 5) { // MarkToLig
                if (!within(sub, 12) || read_u16_be(m_data, sub) != 1) continue;
                GposMarkLigSubtable st;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.mark_cov)) continue;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 4), table_end, st.lig_cov)) continue;
                st.class_count = read_u16_be(m_data, sub + 6);
                const std::size_t ma_pos = sub + read_u16_be(m_data, sub + 8);
                const std::size_t la_pos = sub + read_u16_be(m_data, sub + 10);
                if (!within(la_pos, 2)) continue;
                const uint16_t lig_count = read_u16_be(m_data, la_pos);
                if (static_cast<std::size_t>(lig_count) * 2u > table_end - (la_pos + 2u)) continue;
                const std::size_t mark_cov_count =
                    st.mark_cov.format == 1 ? st.mark_cov.glyphs.size()
                                            : [&]() {
                                                  std::size_t c = 0;
                                                  for (const auto& r : st.mark_cov.ranges) {
                                                      c = std::max<std::size_t>(c, r.start_index + (r.end - r.start) + 1u);
                                                  }
                                                  return c;
                                              }();
                if (mark_cov_count > 0xFFFFu) continue;
                if (!parse_mark_array(ma_pos, static_cast<uint16_t>(mark_cov_count), st.marks)) continue;
                st.lig_attaches.resize(lig_count);
                for (uint16_t li = 0; li < lig_count; ++li) {
                    const uint16_t lat_off = read_u16_be(m_data, la_pos + 2u + 2u * li);
                    if (lat_off == 0) continue;
                    const std::size_t lat = la_pos + lat_off;
                    if (!within(lat, 2)) continue;
                    const uint16_t comp_cnt = read_u16_be(m_data, lat);
                    const std::size_t tot = static_cast<std::size_t>(comp_cnt) * st.class_count;
                    if (st.class_count > 0 &&
                        (tot / st.class_count != comp_cnt ||
                         tot > (table_end - (lat + 2u)) / 2u)) {
                        continue;
                    }
                    auto& attach = st.lig_attaches[li];
                    attach.component_count = comp_cnt;
                    attach.anchors.resize(tot);
                    for (std::size_t k = 0; k < tot; ++k) {
                        const uint16_t aoff = read_u16_be(m_data, lat + 2u + 2u * k);
                        if (aoff != 0) read_gpos_anchor(lat + aoff, table_end, attach.anchors[k]);
                    }
                }
                collect_cov_glyphs(st.mark_cov, known_marks);
                const uint32_t idx = static_cast<uint32_t>(m_gpos_mark_lig.size());
                m_gpos_mark_lig.push_back(std::move(st));
                parsed_lookup.subtables.push_back({GposSubRef::Kind::MarkLig, idx});
                m_has_gpos_marks = true;
            } else if (ltype == 6) { // MarkToMark
                if (!within(sub, 12) || read_u16_be(m_data, sub) != 1) continue;
                GposMarkMarkSubtable st;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 2), table_end, st.mark1_cov)) continue;
                if (!parse_coverage(sub + read_u16_be(m_data, sub + 4), table_end, st.mark2_cov)) continue;
                st.class_count = read_u16_be(m_data, sub + 6);
                const std::size_t m1_pos = sub + read_u16_be(m_data, sub + 8);
                const std::size_t m2_pos = sub + read_u16_be(m_data, sub + 10);
                if (!within(m2_pos, 2)) continue;
                const uint16_t m2_count = read_u16_be(m_data, m2_pos);
                const std::size_t m1_cov_count =
                    st.mark1_cov.format == 1 ? st.mark1_cov.glyphs.size()
                                             : [&]() {
                                                   std::size_t c = 0;
                                                   for (const auto& r : st.mark1_cov.ranges) {
                                                       c = std::max<std::size_t>(c, r.start_index + (r.end - r.start) + 1u);
                                                   }
                                                   return c;
                                               }();
                if (m1_cov_count > 0xFFFFu) continue;
                if (!parse_mark_array(m1_pos, static_cast<uint16_t>(m1_cov_count), st.mark1s)) continue;
                const std::size_t num_m2 = static_cast<std::size_t>(m2_count) * st.class_count;
                if (st.class_count > 0 &&
                    (num_m2 / st.class_count != m2_count ||
                     num_m2 > (table_end - (m2_pos + 2u)) / 2u)) {
                    continue;
                }
                st.mark2_anchors.resize(num_m2);
                for (std::size_t k = 0; k < num_m2; ++k) {
                    const uint16_t aoff = read_u16_be(m_data, m2_pos + 2u + 2u * k);
                    if (aoff != 0) read_gpos_anchor(m2_pos + aoff, table_end, st.mark2_anchors[k]);
                }
                collect_cov_glyphs(st.mark1_cov, known_marks);
                collect_cov_glyphs(st.mark2_cov, known_marks);
                const uint32_t idx = static_cast<uint32_t>(m_gpos_mark_mark.size());
                m_gpos_mark_mark.push_back(std::move(st));
                parsed_lookup.subtables.push_back({GposSubRef::Kind::MarkMark, idx});
                m_has_gpos_marks = true;
            } else if (ltype == 7 || ltype == 8) { // ContextPos / ChainContextPos
                OtfContextSubtable ctx;
                if (parse_context_subtable(sub, table_end, ltype == 8, ltype, ctx)) {
                    const uint32_t idx = static_cast<uint32_t>(m_gpos_contexts.size());
                    m_gpos_contexts.push_back(std::move(ctx));
                    parsed_lookup.subtables.push_back({GposSubRef::Kind::Context, idx});
                    m_has_contextual_gpos = true;
                }
            }
        }
    }

    // 2. Collect per-script GPOS feature plans across all scripts in ScriptList.
    const uint16_t script_count = read_u16_be(m_data, script_list);
    const uint16_t total_features = read_u16_be(m_data, feature_list);
    bool latn_has_kern = false;
    bool dflt_has_kern = false;

    for (uint16_t si = 0; si < script_count; ++si) {
        const std::size_t rec = script_list + 2u + 6u * si;
        if (!within(rec, 6)) break;
        const uint32_t stag = read_u32_be(m_data, rec);
        const std::size_t spos = script_list + read_u16_be(m_data, rec + 4);
        if (!within(spos, 4)) continue;
        uint16_t ls_off = read_u16_be(m_data, spos);
        if (ls_off == 0) {
            const uint16_t ls_cnt = read_u16_be(m_data, spos + 2);
            if (ls_cnt > 0 && within(spos + 4u, 6)) ls_off = read_u16_be(m_data, spos + 8);
        }
        if (ls_off == 0 || !within(spos + ls_off, 6)) continue;
        const std::size_t lang_sys = spos + ls_off;

        std::map<uint16_t, GposLookup::Feature> active;
        const auto add_feat = [&](uint16_t fi, bool is_required) {
            if (fi >= total_features) return;
            const std::size_t frec = feature_list + 2u + 6u * fi;
            if (!within(frec, 6)) return;
            const uint32_t ftag = read_u32_be(m_data, frec);
            GposLookup::Feature feat = GposLookup::Feature::Other;
            bool match = is_required;
            if (ftag == ot_tag('k', 'e', 'r', 'n')) {
                feat = GposLookup::Feature::Kern;
                match = true;
            } else if (ftag == ot_tag('c', 'u', 'r', 's')) {
                feat = GposLookup::Feature::Cursive;
                match = true;
            } else if (ftag == ot_tag('m', 'a', 'r', 'k')) {
                feat = GposLookup::Feature::Mark;
                match = true;
            } else if (ftag == ot_tag('m', 'k', 'm', 'k')) {
                feat = GposLookup::Feature::Mkmk;
                match = true;
            } else if (ftag == ot_tag('c', 'a', 'l', 't') ||
                       ftag == ot_tag('d', 'i', 's', 't') ||
                       ftag == ot_tag('a', 'b', 'v', 'm') ||
                       ftag == ot_tag('b', 'l', 'w', 'm')) {
                feat = GposLookup::Feature::Other;
                match = true;
            }
            if (!match) return;

            const std::size_t fpos = feature_list + read_u16_be(m_data, frec + 4);
            if (!within(fpos, 4)) return;
            const uint16_t lcnt = read_u16_be(m_data, fpos + 2);
            for (uint16_t k = 0; k < lcnt; ++k) {
                if (!within(fpos + 4u + 2u * k, 2)) break;
                const uint16_t li = read_u16_be(m_data, fpos + 4u + 2u * k);
                if (li >= lookup_count || m_gpos_lookups[li].subtables.empty()) continue;
                active.emplace(li, feat);
                if (feat == GposLookup::Feature::Kern) {
                    if (stag == kTagLatn) latn_has_kern = true;
                    else if (stag == kTagDflt) dflt_has_kern = true;
                    if (m_gpos_lookups[li].feature == GposLookup::Feature::Other) {
                        m_gpos_lookups[li].feature = feat;
                    }
                } else if (m_gpos_lookups[li].feature == GposLookup::Feature::Other) {
                    m_gpos_lookups[li].feature = feat;
                }
            }
        };

        const uint16_t req = read_u16_be(m_data, lang_sys + 2);
        if (req != 0xFFFFu) add_feat(req, true);
        const uint16_t fc = read_u16_be(m_data, lang_sys + 4);
        for (uint16_t i = 0; i < fc; ++i) {
            if (!within(lang_sys + 6u + 2u * i, 2)) break;
            add_feat(read_u16_be(m_data, lang_sys + 6u + 2u * i), false);
        }

        if (!active.empty()) {
            GposScriptPlan plan;
            plan.script_tag = stag;
            plan.lookups.assign(active.begin(), active.end());
            m_gpos_scripts.push_back(std::move(plan));
        }
    }

    m_has_gpos_kern = latn_has_kern || dflt_has_kern;

    if (!known_marks.empty()) {
        std::sort(known_marks.begin(), known_marks.end());
        known_marks.erase(std::unique(known_marks.begin(), known_marks.end()), known_marks.end());
        m_gpos_known_marks = std::move(known_marks);
    }
    return !m_gpos_scripts.empty();
}

const TrueTypeFont::GsubScriptPlan* TrueTypeFont::find_gsub_script(uint32_t script_tag) const {
    const GsubScriptPlan* dflt = nullptr;
    const GsubScriptPlan* deva = nullptr;
    for (const auto& plan : m_gsub_scripts) {
        if (plan.script_tag == script_tag) return &plan;
        if (plan.script_tag == kTagDflt) dflt = &plan;
        if (script_tag == kTagDev2 && plan.script_tag == kTagDeva) deva = &plan;
    }
    if (deva) return deva;
    return dflt;
}

const TrueTypeFont::GposScriptPlan* TrueTypeFont::find_gpos_script(uint32_t script_tag) const {
    const GposScriptPlan* dflt = nullptr;
    const GposScriptPlan* deva = nullptr;
    for (const auto& plan : m_gpos_scripts) {
        if (plan.script_tag == script_tag) return &plan;
        if (plan.script_tag == kTagDflt) dflt = &plan;
        if (script_tag == kTagDev2 && plan.script_tag == kTagDeva) deva = &plan;
    }
    if (deva) return deva;
    return dflt;
}

int32_t TrueTypeFont::next_unignored(const std::vector<ShapingGlyph>& run, std::size_t idx,
                                     uint16_t flag, uint16_t mark_filtering_set) const {
    for (std::size_t k = idx + 1; k < run.size(); ++k) {
        if (!is_ignored_by_lookup(run[k].glyph, flag, mark_filtering_set)) {
            return static_cast<int32_t>(k);
        }
    }
    return -1;
}

int32_t TrueTypeFont::prev_unignored(const std::vector<ShapingGlyph>& run, std::size_t idx,
                                     uint16_t flag, uint16_t mark_filtering_set) const {
    std::size_t k = idx;
    while (k > 0) {
        --k;
        if (!is_ignored_by_lookup(run[k].glyph, flag, mark_filtering_set)) {
            return static_cast<int32_t>(k);
        }
    }
    return -1;
}

bool TrueTypeFont::match_context_subtable(const OtfContextSubtable& ctx,
                                          const std::vector<ShapingGlyph>& run,
                                          std::size_t pos,
                                          uint16_t flag,
                                          uint16_t mark_filtering_set,
                                          std::vector<std::size_t>& match_positions,
                                          const std::vector<OtfLookupRecord>*& out_actions) const {
    if (pos >= run.size()) return false;
    match_positions.clear();

    if (ctx.format == 3) {
        if (ctx.in_covs.empty()) return false;
        if (ctx.in_covs[0].index(run[pos].glyph) < 0) return false;
        match_positions.push_back(pos);
        std::size_t cur = pos;
        for (std::size_t k = 1; k < ctx.in_covs.size(); ++k) {
            const int32_t nx = next_unignored(run, cur, flag, mark_filtering_set);
            if (nx < 0) return false;
            cur = static_cast<std::size_t>(nx);
            if (ctx.in_covs[k].index(run[cur].glyph) < 0) return false;
            match_positions.push_back(cur);
        }
        if (ctx.is_chain) {
            std::size_t bcur = pos;
            for (std::size_t k = 0; k < ctx.back_covs.size(); ++k) {
                const int32_t pv = prev_unignored(run, bcur, flag, mark_filtering_set);
                if (pv < 0) return false;
                bcur = static_cast<std::size_t>(pv);
                if (ctx.back_covs[k].index(run[bcur].glyph) < 0) return false;
            }
            std::size_t lcur = match_positions.back();
            for (std::size_t k = 0; k < ctx.look_covs.size(); ++k) {
                const int32_t nx = next_unignored(run, lcur, flag, mark_filtering_set);
                if (nx < 0) return false;
                lcur = static_cast<std::size_t>(nx);
                if (ctx.look_covs[k].index(run[lcur].glyph) < 0) return false;
            }
        }
        out_actions = &ctx.fmt3_actions;
        return true;
    }

    const int cov_idx = ctx.cov.index(run[pos].glyph);
    if (cov_idx < 0) return false;

    std::size_t set_idx = 0;
    bool by_class = false;
    if (ctx.format == 1) {
        set_idx = static_cast<std::size_t>(cov_idx);
    } else if (ctx.format == 2) {
        set_idx = static_cast<std::size_t>(ctx.in_cd.class_of(run[pos].glyph));
        by_class = true;
    } else {
        return false;
    }
    if (set_idx >= ctx.sets.size()) return false;

    for (const OtfContextRule& rule : ctx.sets[set_idx]) {
        match_positions.clear();
        match_positions.push_back(pos);
        std::size_t cur = pos;
        bool ok = true;
        for (uint16_t expected : rule.input) {
            const int32_t nx = next_unignored(run, cur, flag, mark_filtering_set);
            if (nx < 0) { ok = false; break; }
            cur = static_cast<std::size_t>(nx);
            const uint16_t got = by_class ? ctx.in_cd.class_of(run[cur].glyph) : run[cur].glyph;
            if (got != expected) { ok = false; break; }
            match_positions.push_back(cur);
        }
        if (!ok) continue;

        if (ctx.is_chain) {
            std::size_t bcur = pos;
            for (uint16_t expected : rule.backtrack) {
                const int32_t pv = prev_unignored(run, bcur, flag, mark_filtering_set);
                if (pv < 0) { ok = false; break; }
                bcur = static_cast<std::size_t>(pv);
                const uint16_t got = by_class ? ctx.back_cd.class_of(run[bcur].glyph) : run[bcur].glyph;
                if (got != expected) { ok = false; break; }
            }
            if (!ok) continue;

            std::size_t lcur = match_positions.back();
            for (uint16_t expected : rule.lookahead) {
                const int32_t nx = next_unignored(run, lcur, flag, mark_filtering_set);
                if (nx < 0) { ok = false; break; }
                lcur = static_cast<std::size_t>(nx);
                const uint16_t got = by_class ? ctx.look_cd.class_of(run[lcur].glyph) : run[lcur].glyph;
                if (got != expected) { ok = false; break; }
            }
            if (!ok) continue;
        }

        out_actions = &rule.actions;
        return true;
    }
    return false;
}

bool TrueTypeFont::apply_gsub_lookup_at(uint16_t lookup_index,
                                        std::vector<ShapingGlyph>& run,
                                        std::size_t pos,
                                        uint16_t& next_lig_id,
                                        bool check_feat_mask,
                                        uint32_t feat_mask,
                                        int depth,
                                        std::size_t max_glyphs,
                                        std::size_t& out_next_pos) const {
    out_next_pos = pos + 1u;
    if (pos >= run.size() || lookup_index >= m_gsub_lookups.size()) return false;
    const GsubLookup& lk = m_gsub_lookups[lookup_index];
    if (lk.subtables.empty()) return false;
    if (is_ignored_by_lookup(run[pos].glyph, lk.flag, lk.mark_filtering_set)) return false;
    if (check_feat_mask && (run[pos].feature_mask & feat_mask) == 0u) return false;

    for (const GsubSubRef& ref : lk.subtables) {
        if (ref.kind == GsubSubRef::Kind::Single) {
            const auto& st = m_gsub_singles[ref.index];
            const int ci = st.cov.index(run[pos].glyph);
            if (ci < 0) continue;
            if (st.format == 1) {
                run[pos].glyph = static_cast<uint16_t>(
                    (static_cast<int32_t>(run[pos].glyph) + st.delta) & 0xFFFF);
                return true;
            }
            if (st.format == 2 && static_cast<std::size_t>(ci) < st.substitutes.size()) {
                run[pos].glyph = st.substitutes[static_cast<std::size_t>(ci)];
                return true;
            }
        } else if (ref.kind == GsubSubRef::Kind::Multiple) {
            const auto& st = m_gsub_multiples[ref.index];
            const int ci = st.cov.index(run[pos].glyph);
            if (ci < 0 || static_cast<std::size_t>(ci) >= st.sequences.size()) continue;
            const auto& seq = st.sequences[static_cast<std::size_t>(ci)];
            if (seq.empty()) {
                run.erase(run.begin() + static_cast<std::ptrdiff_t>(pos));
                out_next_pos = pos;
                return true;
            }
            if (run.size() - 1u + seq.size() > max_glyphs) return false;
            const ShapingGlyph base = run[pos];
            run[pos].glyph = seq[0];
            if (seq.size() > 1u) {
                std::vector<ShapingGlyph> extra(seq.size() - 1u, base);
                for (std::size_t k = 1; k < seq.size(); ++k) extra[k - 1u].glyph = seq[k];
                run.insert(run.begin() + static_cast<std::ptrdiff_t>(pos + 1u),
                           extra.begin(), extra.end());
            }
            out_next_pos = pos + seq.size();
            return true;
        } else if (ref.kind == GsubSubRef::Kind::Alternate) {
            const auto& st = m_gsub_alternates[ref.index];
            const int ci = st.cov.index(run[pos].glyph);
            if (ci < 0 || static_cast<std::size_t>(ci) >= st.alternates.size()) continue;
            const auto& alts = st.alternates[static_cast<std::size_t>(ci)];
            if (alts.empty()) continue;
            run[pos].glyph = alts[0];
            return true;
        } else if (ref.kind == GsubSubRef::Kind::Ligature) {
            const auto& st = m_gsub_ligatures[ref.index];
            const int ci = st.cov.index(run[pos].glyph);
            if (ci < 0 || static_cast<std::size_t>(ci) >= st.sets.size()) continue;
            for (const Ligature& lig : st.sets[static_cast<std::size_t>(ci)]) {
                std::vector<std::size_t> comp_idx;
                comp_idx.reserve(1u + lig.rest.size());
                comp_idx.push_back(pos);
                std::size_t cur = pos;
                bool matched = true;
                for (std::size_t k = 0; k < lig.rest.size(); ++k) {
                    const int32_t nx = next_unignored(run, cur, lk.flag, lk.mark_filtering_set);
                    if (nx < 0) { matched = false; break; }
                    cur = static_cast<std::size_t>(nx);
                    if (run[cur].glyph != lig.rest[k]) { matched = false; break; }
                    comp_idx.push_back(cur);
                }
                if (!matched) continue;

                const std::size_t end_pos = comp_idx.back();
                const uint16_t lid = next_lig_id ? next_lig_id++ : 1u;
                if (next_lig_id == 0) next_lig_id = 1u;

                std::vector<ShapingGlyph> preserved;
                std::size_t comp_cursor = 0;
                for (std::size_t p = pos + 1u; p <= end_pos; ++p) {
                    while (comp_cursor + 1u < comp_idx.size() && p >= comp_idx[comp_cursor + 1u]) {
                        ++comp_cursor;
                    }
                    bool is_comp = false;
                    for (std::size_t ci_p : comp_idx) {
                        if (p == ci_p) { is_comp = true; break; }
                    }
                    if (!is_comp) {
                        ShapingGlyph sg = run[p];
                        if (is_mark_glyph(sg.glyph)) {
                            sg.lig_id = lid;
                            sg.lig_comp = static_cast<uint16_t>(
                                comp_cursor > 0 ? comp_cursor - 1u : 0u);
                        }
                        preserved.push_back(sg);
                    }
                }

                run[pos].glyph = lig.glyph;
                run[pos].lig_id = lid;
                run[pos].lig_comp = 0;
                run.erase(run.begin() + static_cast<std::ptrdiff_t>(pos + 1u),
                          run.begin() + static_cast<std::ptrdiff_t>(end_pos + 1u));
                if (!preserved.empty()) {
                    run.insert(run.begin() + static_cast<std::ptrdiff_t>(pos + 1u),
                               preserved.begin(), preserved.end());
                }
                out_next_pos = pos + 1u;
                return true;
            }
        } else if (ref.kind == GsubSubRef::Kind::Context) {
            const auto& ctx = m_gsub_contexts[ref.index];
            std::vector<std::size_t> match_pos;
            const std::vector<OtfLookupRecord>* actions = nullptr;
            if (!match_context_subtable(ctx, run, pos, lk.flag, lk.mark_filtering_set,
                                        match_pos, actions)) {
                continue;
            }
            if (depth < 16 && actions) {
                for (const OtfLookupRecord& act : *actions) {
                    if (act.sequence_index >= match_pos.size()) continue;
                    const std::size_t target = match_pos[act.sequence_index];
                    if (target >= run.size()) continue;
                    const std::size_t before = run.size();
                    std::size_t dummy_next = target + 1u;
                    apply_gsub_lookup_at(act.lookup_index, run, target, next_lig_id,
                                         false, 0u, depth + 1, max_glyphs, dummy_next);
                    const std::ptrdiff_t delta =
                        static_cast<std::ptrdiff_t>(run.size()) - static_cast<std::ptrdiff_t>(before);
                    if (delta != 0) {
                        for (std::size_t m = act.sequence_index + 1u; m < match_pos.size(); ++m) {
                            const std::ptrdiff_t adj = static_cast<std::ptrdiff_t>(match_pos[m]) + delta;
                            match_pos[m] = adj >= 0 ? static_cast<std::size_t>(adj) : 0u;
                        }
                    }
                }
            }
            out_next_pos = match_pos.empty() ? (pos + 1u)
                                             : std::min(run.size(), match_pos.back() + 1u);
            return true;
        }
    }
    return false;
}

void TrueTypeFont::apply_gsub_plan(const GsubScriptPlan* plan,
                                   std::vector<ShapingGlyph>& run,
                                   uint16_t& next_lig_id) const {
    if (!plan || run.empty()) return;
    const std::size_t max_glyphs = std::max<std::size_t>(run.size() * 8u + 64u, 4096u);
    for (const auto& [lookup_idx, feat_mask] : plan->lookups) {
        std::size_t pos = 0;
        while (pos < run.size()) {
            std::size_t next_pos = pos + 1u;
            apply_gsub_lookup_at(lookup_idx, run, pos, next_lig_id,
                                 true, feat_mask, 0, max_glyphs, next_pos);
            pos = (next_pos > pos) ? next_pos : (pos + 1u);
        }
    }
}

bool TrueTypeFont::match_pair_subtable(const GposSubRef& ref, uint16_t left, uint16_t right,
                                       GposValue& v1, GposValue& v2, bool& advance_two) const {
    v1 = GposValue{};
    v2 = GposValue{};
    advance_two = false;
    if (ref.kind == GposSubRef::Kind::PairFmt1) {
        const auto& st = m_gpos_pair_fmt1[ref.index];
        const int cov_idx = st.cov.index(left);
        if (cov_idx < 0 || static_cast<std::size_t>(cov_idx) >= st.pair_sets.size()) return false;
        const auto& entries = st.pair_sets[static_cast<std::size_t>(cov_idx)];
        auto it = std::lower_bound(entries.begin(), entries.end(), right,
                                   [](const GposPairEntry& e, uint16_t g) { return e.second < g; });
        if (it == entries.end() || it->second != right) return false;
        v1 = it->v1;
        v2 = it->v2;
        advance_two = st.has_v2 && gpos_value_nonzero(v2);
        return true;
    }
    if (ref.kind == GposSubRef::Kind::PairFmt2) {
        const auto& st = m_gpos_pair_fmt2[ref.index];
        if (st.cov.index(left) < 0) return false;
        const uint16_t c1 = st.class1.class_of(left);
        const uint16_t c2 = st.class2.class_of(right);
        if (c1 >= st.c1_count || c2 >= st.c2_count) return false;
        const std::size_t vsz1 = gpos_value_size(st.vf1);
        const std::size_t vsz2 = gpos_value_size(st.vf2);
        const std::size_t cell = static_cast<std::size_t>(st.matrix_offset) +
                                 (static_cast<std::size_t>(c1) * st.c2_count + c2) * (vsz1 + vsz2);
        if (st.vf1 != 0) read_gpos_value(cell, m_data.size(), st.vf1, v1);
        if (st.vf2 != 0) read_gpos_value(cell + vsz1, m_data.size(), st.vf2, v2);
        advance_two = (st.vf2 != 0) && gpos_value_nonzero(v2);
        return true;
    }
    return false;
}

bool TrueTypeFont::apply_gpos_lookup_at(uint16_t lookup_index,
                                        std::vector<ShapingGlyph>& run,
                                        std::size_t pos,
                                        bool is_rtl,
                                        bool kerning,
                                        int depth,
                                        std::size_t& out_next_pos) const {
    (void)is_rtl;
    out_next_pos = pos + 1u;
    if (pos >= run.size() || lookup_index >= m_gpos_lookups.size()) return false;
    const GposLookup& lk = m_gpos_lookups[lookup_index];
    if (lk.subtables.empty()) return false;
    if (is_ignored_by_lookup(run[pos].glyph, lk.flag, lk.mark_filtering_set)) return false;

    const auto apply_value = [](const GposValue& v, ShapingGlyph& st) {
        st.x_offset += v.x_placement;
        st.y_offset += v.y_placement;
        st.x_advance += v.x_advance;
        st.y_advance += v.y_advance;
    };

    if (lk.type == 1) { // SinglePos
        for (const GposSubRef& ref : lk.subtables) {
            const auto& st = m_gpos_singles[ref.index];
            const int idx = st.cov.index(run[pos].glyph);
            if (idx < 0) continue;
            if (st.format == 1) {
                apply_value(st.single_val, run[pos]);
                return true;
            }
            if (st.format == 2 && static_cast<std::size_t>(idx) < st.vals.size()) {
                apply_value(st.vals[static_cast<std::size_t>(idx)], run[pos]);
                return true;
            }
        }
        return false;
    }

    if (lk.type == 2) { // PairPos
        if (!kerning && lk.feature == GposLookup::Feature::Kern) return false;
        const int32_t nx = next_unignored(run, pos, lk.flag, lk.mark_filtering_set);
        if (nx < 0) return false;
        const std::size_t j = static_cast<std::size_t>(nx);
        for (const GposSubRef& ref : lk.subtables) {
            GposValue v1, v2;
            bool step_two = false;
            if (match_pair_subtable(ref, run[pos].glyph, run[j].glyph, v1, v2, step_two)) {
                apply_value(v1, run[pos]);
                apply_value(v2, run[j]);
                out_next_pos = step_two ? (j + 1u) : (pos + 1u);
                return true;
            }
        }
        return false;
    }

    if (lk.type == 3) { // Cursive Attachment
        const int32_t nx = next_unignored(run, pos, lk.flag, lk.mark_filtering_set);
        if (nx < 0) return false;
        const std::size_t j = static_cast<std::size_t>(nx);
        const bool rtl_lookup = (lk.flag & 0x0001u) != 0;
        for (const GposSubRef& ref : lk.subtables) {
            const auto& st = m_gpos_cursives[ref.index];
            const int ii = st.cov.index(run[pos].glyph);
            const int ji = st.cov.index(run[j].glyph);
            if (ii < 0 || ji < 0) continue;
            if (static_cast<std::size_t>(ii) >= st.records.size() ||
                static_cast<std::size_t>(ji) >= st.records.size()) {
                continue;
            }
            const auto& exit_anc = st.records[static_cast<std::size_t>(ii)].exit;
            const auto& entry_anc = st.records[static_cast<std::size_t>(ji)].entry;
            if (!exit_anc.present || !entry_anc.present) continue;

            if (rtl_lookup) {
                run[pos].cursive_chain = static_cast<int32_t>(j);
                run[pos].cursive_dy = static_cast<int32_t>(entry_anc.y) - exit_anc.y;
                run[pos].x_offset -= exit_anc.x;
                run[pos].x_advance -= exit_anc.x;
                run[j].x_advance = static_cast<int32_t>(entry_anc.x) + run[j].x_offset;
            } else {
                run[j].cursive_chain = static_cast<int32_t>(pos);
                run[j].cursive_dy = static_cast<int32_t>(exit_anc.y) - entry_anc.y;
                run[pos].x_advance = static_cast<int32_t>(exit_anc.x) + run[pos].x_offset;
                run[j].x_offset -= entry_anc.x;
                run[j].x_advance -= entry_anc.x;
            }
            return true;
        }
        return false;
    }

    if (lk.type == 4 || lk.type == 5) { // MarkToBase / MarkToLig
        if (pos == 0) return false;
        std::size_t j = pos;
        bool found_base = false;
        while (j > 0) {
            --j;
            if (is_mark_glyph(run[j].glyph)) continue;
            found_base = true;
            break;
        }
        if (!found_base || is_ignored_by_lookup(run[j].glyph, lk.flag, lk.mark_filtering_set)) {
            return false;
        }
        if (lk.type == 4) {
            if (m_has_gdef_glyph_class && m_gdef_glyph_class.class_of(run[j].glyph) == 2) {
                return false;
            }
            for (const GposSubRef& ref : lk.subtables) {
                const auto& st = m_gpos_mark_base[ref.index];
                const int mi = st.mark_cov.index(run[pos].glyph);
                if (mi < 0 || static_cast<std::size_t>(mi) >= st.marks.size()) continue;
                const int bi = st.base_cov.index(run[j].glyph);
                if (bi < 0) continue;
                const GposMarkRecord& mrec = st.marks[static_cast<std::size_t>(mi)];
                if (!mrec.anchor.present || mrec.mark_class >= st.class_count) continue;
                const std::size_t bidx =
                    static_cast<std::size_t>(bi) * st.class_count + mrec.mark_class;
                if (bidx >= st.base_anchors.size() || !st.base_anchors[bidx].present) continue;
                run[pos].mark_base = static_cast<int32_t>(j);
                run[pos].cursive_dx = static_cast<int32_t>(st.base_anchors[bidx].x) - mrec.anchor.x;
                run[pos].cursive_dy = static_cast<int32_t>(st.base_anchors[bidx].y) - mrec.anchor.y;
                return true;
            }
            return false;
        }

        for (const GposSubRef& ref : lk.subtables) {
            const auto& st = m_gpos_mark_lig[ref.index];
            const int mi = st.mark_cov.index(run[pos].glyph);
            if (mi < 0 || static_cast<std::size_t>(mi) >= st.marks.size()) continue;
            const int li = st.lig_cov.index(run[j].glyph);
            if (li < 0 || static_cast<std::size_t>(li) >= st.lig_attaches.size()) continue;
            const GposMarkRecord& mrec = st.marks[static_cast<std::size_t>(mi)];
            if (!mrec.anchor.present || mrec.mark_class >= st.class_count) continue;
            const auto& attach = st.lig_attaches[static_cast<std::size_t>(li)];
            if (attach.component_count == 0) continue;

            GposAnchor lanc;
            if (run[j].lig_id != 0 && run[pos].lig_id == run[j].lig_id) {
                const uint16_t comp =
                    std::min<uint16_t>(run[pos].lig_comp, attach.component_count - 1u);
                const std::size_t idx =
                    static_cast<std::size_t>(comp) * st.class_count + mrec.mark_class;
                if (idx < attach.anchors.size()) lanc = attach.anchors[idx];
            } else {
                for (int comp = static_cast<int>(attach.component_count) - 1; comp >= 0; --comp) {
                    const std::size_t idx =
                        static_cast<std::size_t>(comp) * st.class_count + mrec.mark_class;
                    if (idx < attach.anchors.size() && attach.anchors[idx].present) {
                        lanc = attach.anchors[idx];
                        break;
                    }
                }
            }
            if (!lanc.present) continue;
            run[pos].mark_base = static_cast<int32_t>(j);
            run[pos].cursive_dx = static_cast<int32_t>(lanc.x) - mrec.anchor.x;
            run[pos].cursive_dy = static_cast<int32_t>(lanc.y) - mrec.anchor.y;
            return true;
        }
        return false;
    }

    if (lk.type == 6) { // MarkToMark
        if (pos == 0) return false;
        const int32_t pv = prev_unignored(run, pos, lk.flag, lk.mark_filtering_set);
        if (pv < 0) return false;
        const std::size_t j = static_cast<std::size_t>(pv);
        if (!is_mark_glyph(run[j].glyph)) return false;
        if (run[pos].lig_id != 0 && run[j].lig_id == run[pos].lig_id &&
            run[pos].lig_comp != run[j].lig_comp) {
            return false;
        }
        for (const GposSubRef& ref : lk.subtables) {
            const auto& st = m_gpos_mark_mark[ref.index];
            const int m1i = st.mark1_cov.index(run[pos].glyph);
            if (m1i < 0 || static_cast<std::size_t>(m1i) >= st.mark1s.size()) continue;
            const int m2i = st.mark2_cov.index(run[j].glyph);
            if (m2i < 0) continue;
            const GposMarkRecord& mrec = st.mark1s[static_cast<std::size_t>(m1i)];
            if (!mrec.anchor.present || mrec.mark_class >= st.class_count) continue;
            const std::size_t m2idx =
                static_cast<std::size_t>(m2i) * st.class_count + mrec.mark_class;
            if (m2idx >= st.mark2_anchors.size() || !st.mark2_anchors[m2idx].present) continue;
            run[pos].mark_base = static_cast<int32_t>(j);
            run[pos].cursive_dx = static_cast<int32_t>(st.mark2_anchors[m2idx].x) - mrec.anchor.x;
            run[pos].cursive_dy = static_cast<int32_t>(st.mark2_anchors[m2idx].y) - mrec.anchor.y;
            return true;
        }
        return false;
    }

    if (lk.type == 7 || lk.type == 8) { // ContextPos / ChainContextPos
        for (const GposSubRef& ref : lk.subtables) {
            const auto& ctx = m_gpos_contexts[ref.index];
            std::vector<std::size_t> match_pos;
            const std::vector<OtfLookupRecord>* actions = nullptr;
            if (!match_context_subtable(ctx, run, pos, lk.flag, lk.mark_filtering_set,
                                        match_pos, actions)) {
                continue;
            }
            if (depth < 16 && actions) {
                for (const OtfLookupRecord& act : *actions) {
                    if (act.sequence_index >= match_pos.size()) continue;
                    const std::size_t target = match_pos[act.sequence_index];
                    std::size_t dummy_next = target + 1u;
                    apply_gpos_lookup_at(act.lookup_index, run, target, is_rtl, kerning,
                                         depth + 1, dummy_next);
                }
            }
            out_next_pos = match_pos.empty() ? (pos + 1u)
                                             : std::min(run.size(), match_pos.back() + 1u);
            return true;
        }
    }
    return false;
}

std::vector<TrueTypeFont::PositionedGlyph> TrueTypeFont::shape_glyphs(
    const std::vector<uint32_t>& codepoints, bool kerning, bool visual_rtl) const {
    std::vector<PositionedGlyph> result;
    if (!m_loaded || codepoints.empty()) return result;

    const std::vector<uint32_t> scripts = itemize_codepoint_scripts(codepoints);
    uint16_t next_lig_id = 1;

    std::size_t run_start = 0;
    while (run_start < codepoints.size()) {
        const uint32_t script_tag = scripts[run_start];
        std::size_t run_end = run_start + 1u;
        while (run_end < codepoints.size() && scripts[run_end] == script_tag) ++run_end;
        const bool is_rtl = is_rtl_script_tag(script_tag);

        std::vector<uint32_t> feat_masks;
        if (script_tag == kTagArab || script_tag == kTagNko || script_tag == kTagSyrc) {
            feat_masks = compute_arabic_feature_masks(codepoints, run_start, run_end);
        } else {
            feat_masks.assign(run_end - run_start, kGsubFeatGlobal);
        }

        std::vector<ShapingGlyph> run;
        run.reserve(run_end - run_start);
        for (std::size_t i = run_start; i < run_end; ++i) {
            const uint32_t cp = codepoints[i];
            ShapingGlyph sg;
            sg.glyph = glyph_index(cp);
            sg.feature_mask = feat_masks[i - run_start];
            // Borrow `lig_comp` bit flag sentinel? No — track join control explicitly via codepoint
            // check? Wait: use feature_mask bit (1<<31) temporarily to strip surviving join controls!
            if (is_join_control(cp)) sg.feature_mask |= (1u << 31);
            run.push_back(sg);
        }

        const GsubScriptPlan* gsub_plan = find_gsub_script(script_tag);
        if (gsub_plan) {
            apply_gsub_plan(gsub_plan, run, next_lig_id);
        }

        // Strip surviving default-ignorable join controls (`U+200C`/`U+200D`) so
        // they never paint `.notdef` boxes or interfere with `GPOS` adjacency.
        run.erase(std::remove_if(run.begin(), run.end(),
                                 [](const ShapingGlyph& sg) {
                                     return (sg.feature_mask & (1u << 31)) != 0u;
                                 }),
                  run.end());

        const std::size_t n = run.size();
        for (std::size_t i = 0; i < n; ++i) {
            const uint16_t g = run[i].glyph;
            const uint16_t adv = (g < m_advance_widths.size()) ? m_advance_widths[g] : m_advance_widths[0];
            run[i].x_advance = static_cast<int32_t>(adv);
        }

        const GposScriptPlan* gpos_plan = find_gpos_script(script_tag);
        bool script_has_kern = false;
        if (gpos_plan) {
            // Stage 1: SinglePos (1), PairPos (2), Cursive (3), Contextual (7/8)
            for (const auto& [li, feat] : gpos_plan->lookups) {
                if (li >= m_gpos_lookups.size()) continue;
                const GposLookup& lk = m_gpos_lookups[li];
                if (feat == GposLookup::Feature::Kern) script_has_kern = true;
                if (lk.type == 4 || lk.type == 5 || lk.type == 6) continue;
                if (lk.type == 2 && !kerning && feat == GposLookup::Feature::Kern) continue;
                std::size_t pos = 0;
                while (pos < n) {
                    std::size_t next_pos = pos + 1u;
                    apply_gpos_lookup_at(li, run, pos, is_rtl, kerning, 0, next_pos);
                    pos = (next_pos > pos) ? next_pos : (pos + 1u);
                }
            }
            // Stage 2: MarkToBase (4) and MarkToLig (5)
            for (const auto& [li, feat] : gpos_plan->lookups) {
                (void)feat;
                if (li >= m_gpos_lookups.size()) continue;
                const GposLookup& lk = m_gpos_lookups[li];
                if (lk.type != 4 && lk.type != 5) continue;
                for (std::size_t pos = 0; pos < n; ++pos) {
                    std::size_t dummy_next = pos + 1u;
                    apply_gpos_lookup_at(li, run, pos, is_rtl, kerning, 0, dummy_next);
                }
            }
            // Stage 3: MarkToMark (6)
            for (const auto& [li, feat] : gpos_plan->lookups) {
                (void)feat;
                if (li >= m_gpos_lookups.size()) continue;
                const GposLookup& lk = m_gpos_lookups[li];
                if (lk.type != 6) continue;
                for (std::size_t pos = 0; pos < n; ++pos) {
                    std::size_t dummy_next = pos + 1u;
                    apply_gpos_lookup_at(li, run, pos, is_rtl, kerning, 0, dummy_next);
                }
            }
        }

        // Legacy `kern` fallback when this script has no `GPOS` `kern` lookup.
        if (kerning && !script_has_kern && m_kern_pairs > 0) {
            for (std::size_t i = 0; i + 1u < n; ++i) {
                run[i].x_advance += kerning_units_for_glyphs(run[i].glyph, run[i + 1u].glyph);
            }
        }

        // Stage 4a: Resolve CursiveAttachment chains via memoized DFS (works for
        // both LTR `parent < i` and RTL `parent > i`).
        std::vector<uint8_t> cursive_state(n, 0); // 0=unvisited, 1=visiting, 2=done
        auto resolve_cursive = [&](auto& self, std::size_t idx) -> void {
            if (cursive_state[idx] == 2 || cursive_state[idx] == 1) return;
            cursive_state[idx] = 1;
            const int32_t parent = run[idx].cursive_chain;
            if (parent >= 0 && static_cast<std::size_t>(parent) < n) {
                const std::size_t p = static_cast<std::size_t>(parent);
                self(self, p);
                run[idx].y_offset += run[p].y_offset + run[idx].cursive_dy;
            }
            cursive_state[idx] = 2;
        };
        for (std::size_t i = 0; i < n; ++i) {
            if (run[i].cursive_chain >= 0) resolve_cursive(resolve_cursive, i);
        }

        // Stage 4b: Resolve Mark attachment chains left to right (`mark_base < i`).
        for (std::size_t i = 0; i < n; ++i) {
            const int32_t j = run[i].mark_base;
            if (j >= 0 && static_cast<std::size_t>(j) < i) {
                int32_t sum_adv_x = 0;
                int32_t sum_adv_y = 0;
                for (std::size_t k = static_cast<std::size_t>(j); k < i; ++k) {
                    sum_adv_x += run[k].x_advance;
                    sum_adv_y += run[k].y_advance;
                }
                run[i].x_offset += run[static_cast<std::size_t>(j)].x_offset + run[i].cursive_dx - sum_adv_x;
                run[i].y_offset += run[static_cast<std::size_t>(j)].y_offset + run[i].cursive_dy - sum_adv_y;
            }
        }

        if (visual_rtl && is_rtl && n > 1u) {
            // Reorder base+marks clusters right-to-left while keeping each base
            // followed immediately by its own marks so pen advances match mark offsets.
            std::vector<std::pair<std::size_t, std::size_t>> clusters;
            std::size_t c = 0;
            while (c < n) {
                if (run[c].mark_base < 0 && is_mark_glyph(run[c].glyph)) {
                    clusters.emplace_back(c, c + 1u);
                    ++c;
                    continue;
                }
                std::size_t ce = c + 1u;
                while (ce < n && (run[ce].mark_base >= 0 || is_mark_glyph(run[ce].glyph))) {
                    ++ce;
                }
                clusters.emplace_back(c, ce);
                c = ce;
            }
            for (auto it = clusters.rbegin(); it != clusters.rend(); ++it) {
                for (std::size_t k = it->first; k < it->second; ++k) {
                    PositionedGlyph pg;
                    pg.glyph = run[k].glyph;
                    pg.x_advance = run[k].x_advance;
                    pg.y_advance = run[k].y_advance;
                    pg.x_offset = run[k].x_offset;
                    pg.y_offset = run[k].y_offset;
                    result.push_back(pg);
                }
            }
        } else {
            for (const ShapingGlyph& st : run) {
                PositionedGlyph pg;
                pg.glyph = st.glyph;
                pg.x_advance = st.x_advance;
                pg.y_advance = st.y_advance;
                pg.x_offset = st.x_offset;
                pg.y_offset = st.y_offset;
                result.push_back(pg);
            }
        }
        run_start = run_end;
    }
    return result;
}

int16_t TrueTypeFont::kerning_units(uint32_t left, uint32_t right) const {
    return kerning_units_for_glyphs(glyph_index(left), glyph_index(right));
}

int16_t TrueTypeFont::kerning_units_for_glyphs(uint16_t left_glyph, uint16_t right_glyph) const {
    // A `.notdef` box does not kern against anything: the font has no opinion
    // about a glyph it does not have.
    if (left_glyph == 0 || right_glyph == 0) return 0;

    if (m_has_gpos_kern) {
        const GposScriptPlan* plan = find_gpos_script(kTagLatn);
        if (plan) {
            int32_t total = 0;
            for (const auto& [li, feat] : plan->lookups) {
                if (feat != GposLookup::Feature::Kern || li >= m_gpos_lookups.size()) continue;
                const GposLookup& lk = m_gpos_lookups[li];
                if (lk.type != 2) continue;
                for (const GposSubRef& ref : lk.subtables) {
                    GposValue v1, v2;
                    bool adv2 = false;
                    if (match_pair_subtable(ref, left_glyph, right_glyph, v1, v2, adv2)) {
                        total += static_cast<int32_t>(v1.x_advance) + v2.x_advance;
                        break;
                    }
                }
            }
            return static_cast<int16_t>(std::clamp<int32_t>(total, -32768, 32767));
        }
    }

    if (m_kern_pairs == 0) return 0;

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
                                                                int family_id, bool italic,
                                                                bool kerning, bool visual_rtl) const {
    std::vector<ShapedGlyph> out;
    if (utf8_text.empty()) return out;

    std::vector<uint32_t> cps;
    cps.reserve(utf8_text.size());
    std::size_t pos = 0;
    while (pos < utf8_text.size()) {
        cps.push_back(next_utf8_codepoint(utf8_text, pos));
    }
    const std::vector<uint32_t> scripts = itemize_codepoint_scripts(cps);

    struct FaceRun {
        const TrueTypeFont* face{nullptr};
        bool shear{false};
        bool is_rtl{false};
        std::vector<uint32_t> cps;
        std::vector<ShapedGlyph> glyphs;
    };

    std::vector<FaceRun> runs;
    const TrueTypeFont* cur_face = nullptr;
    bool cur_shear = false;
    bool cur_rtl = false;
    uint32_t prev_script = 0;

    for (std::size_t k = 0; k < cps.size(); ++k) {
        const uint32_t cp = cps[k];
        const uint32_t sc = scripts[k];
        const bool rtl = is_rtl_script_tag(sc);

        const TrueTypeFont* face = nullptr;
        bool shear = false;
        if (!runs.empty() && cur_face &&
            (is_join_control(cp) ||
             (sc == prev_script && rtl && cur_face->glyph_index(cp) != 0))) {
            face = cur_face;
            shear = cur_shear;
        } else {
            face = face_for(cp, font_weight, family_id, italic, &shear);
        }

        if (runs.empty() || face != cur_face || shear != cur_shear || rtl != cur_rtl) {
            FaceRun fr;
            fr.face = face;
            fr.shear = shear;
            fr.is_rtl = rtl;
            runs.push_back(std::move(fr));
            cur_face = face;
            cur_shear = shear;
            cur_rtl = rtl;
        }
        runs.back().cps.push_back(cp);
        prev_script = sc;
    }

    for (FaceRun& fr : runs) {
        if (fr.face) {
            const float upem = static_cast<float>(fr.face->units_per_em());
            const float scale = (upem > 0.0f) ? (font_size / upem) : 0.0f;
            const std::vector<TrueTypeFont::PositionedGlyph> positioned =
                fr.face->shape_glyphs(fr.cps, kerning, visual_rtl);
            fr.glyphs.reserve(positioned.size());
            for (const TrueTypeFont::PositionedGlyph& pg : positioned) {
                ShapedGlyph shaped;
                shaped.glyph = pg.glyph;
                shaped.face = fr.face;
                shaped.shear = fr.shear;
                shaped.advance = static_cast<float>(pg.x_advance) * scale;
                shaped.x_offset = static_cast<float>(pg.x_offset) * scale;
                shaped.y_offset = static_cast<float>(pg.y_offset) * scale;
                if (fr.shear && shaped.y_offset != 0.0f) {
                    shaped.x_offset += kSyntheticObliqueSkew * shaped.y_offset;
                }
                fr.glyphs.push_back(shaped);
            }
        } else {
            std::vector<uint32_t> vis;
            vis.reserve(fr.cps.size());
            for (uint32_t cp : fr.cps) {
                if (!is_join_control(cp)) vis.push_back(cp);
            }
            if (visual_rtl && fr.is_rtl && vis.size() > 1u) {
                std::reverse(vis.begin(), vis.end());
            }
            for (uint32_t cp : vis) {
                ShapedGlyph shaped;
                shaped.glyph = static_cast<uint16_t>(cp <= 0xFFFFu ? cp : '?');
                shaped.face = nullptr;
                shaped.shear = fr.shear;
                shaped.advance = std::max(1.0f, font_size * 0.55f);
                fr.glyphs.push_back(shaped);
            }
        }
    }

    // Emit shaped runs; if a contiguous RTL script span split across fallback
    // faces, reverse those subruns visually while keeping cluster order intact.
    std::size_t r = 0;
    while (r < runs.size()) {
        if (visual_rtl && runs[r].is_rtl) {
            std::size_t re = r + 1u;
            while (re < runs.size() && runs[re].is_rtl) ++re;
            for (std::size_t k = re; k > r; --k) {
                const auto& gs = runs[k - 1u].glyphs;
                out.insert(out.end(), gs.begin(), gs.end());
            }
            r = re;
        } else {
            const auto& gs = runs[r].glyphs;
            out.insert(out.end(), gs.begin(), gs.end());
            ++r;
        }
    }
    return out;
}

std::vector<FontRenderer::ShapedGlyph> FontRenderer::shape_text(std::string_view utf8_text,
                                                               float font_size, int font_weight,
                                                               std::string_view css_family,
                                                               bool italic, bool kerning,
                                                               bool visual_rtl) const {
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    return shape_text(utf8_text, font_size, font_weight, family_id, italic, kerning, visual_rtl);
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
                                 std::string_view css_family, bool italic, bool kerning) {
    if (utf8_text.empty() || font_size <= 0.0f) return 0.0f;
    const int family_id = css_family.empty() ? kDefaultFamily : family_id_for(css_family);
    const std::vector<ShapedGlyph> shaped =
        shape_text(utf8_text, font_size, font_weight, family_id, italic, kerning);
    float total = 0.0f;
    for (const ShapedGlyph& sg : shaped) {
        total += sg.advance;
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
    bool italic,
    bool kerning,
    bool visual_rtl
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
    // shaping produced (`GSUB` ligatures and `GPOS`/legacy-`kern` positioning
    // already baked into `advance`, `x_offset`, `y_offset`), so measurement and
    // ink agree to the pixel.
    const std::vector<ShapedGlyph> shaped =
        shape_text(utf8_text, font_size, font_weight, family_id, italic, kerning, visual_rtl);
    for (std::size_t i = 0; i < shaped.size(); ++i) {
        const GlyphBitmap& g = get_glyph_shaped(shaped[i], px_size, font_weight);
        if (g.width > 0 && g.height > 0) {
            const int gx0 = static_cast<int>(std::lround(pen_x + shaped[i].x_offset)) + g.bearing_x;
            const int gy0 = baseline_y - static_cast<int>(std::lround(shaped[i].y_offset)) - g.bearing_y;
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
        pen_x += shaped[i].advance;
    }
}

} // namespace litebird
