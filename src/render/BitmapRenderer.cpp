#include "litebird/render/BitmapRenderer.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace litebird {

// Embedded minimal 8x8 monochrome font for basic printable ASCII characters (32 to 126)
// A simple 5x7 / 8x8 standard font representation
static const uint8_t FONT_8X8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00}, // 35 '#'
    {0x18,0x7e,0xc0,0x7c,0x06,0xfc,0x18,0x00}, // 36 '$'
    {0x00,0xc6,0xcc,0x18,0x30,0x66,0xc6,0x00}, // 37 '%'
    {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00}, // 38 '&'
    {0x30,0x30,0x10,0x20,0x00,0x00,0x00,0x00}, // 39 '''
    {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00}, // 40 '('
    {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00}, // 41 ')'
    {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00}, // 42 '*'
    {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // 44 ','
    {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 46 '.'
    {0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00}, // 47 '/'
    {0x7c,0xc6,0xce,0xd6,0xe6,0xc6,0x7c,0x00}, // 48 '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00}, // 49 '1'
    {0x7c,0xc6,0x06,0x1c,0x30,0x60,0xfe,0x00}, // 50 '2'
    {0x7c,0xc6,0x06,0x3c,0x06,0xc6,0x7c,0x00}, // 51 '3'
    {0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00}, // 52 '4'
    {0xfe,0xc0,0xfc,0x06,0x06,0xc6,0x7c,0x00}, // 53 '5'
    {0x38,0x60,0xc0,0xfc,0xc6,0xc6,0x7c,0x00}, // 54 '6'
    {0xfe,0xc6,0x0c,0x18,0x30,0x30,0x30,0x00}, // 55 '7'
    {0x7c,0xc6,0xc6,0x7c,0xc6,0xc6,0x7c,0x00}, // 56 '8'
    {0x7c,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0x00}, // 57 '9'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // 58 ':'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // 59 ';'
    {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00}, // 60 '<'
    {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00}, // 61 '='
    {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00}, // 62 '>'
    {0x7c,0xc6,0x0c,0x18,0x18,0x00,0x18,0x00}, // 63 '?'
    {0x7c,0xc6,0xde,0xde,0xdc,0xc0,0x7c,0x00}, // 64 '@'
    {0x38,0x6c,0xc6,0xfe,0xc6,0xc6,0xc6,0x00}, // 65 'A'
    {0xfc,0x66,0x66,0x7c,0x66,0x66,0xfc,0x00}, // 66 'B'
    {0x3c,0x66,0xc0,0xc0,0xc0,0x66,0x3c,0x00}, // 67 'C'
    {0xf8,0x6c,0x66,0x66,0x66,0x6c,0xf8,0x00}, // 68 'D'
    {0xfe,0x62,0x68,0x78,0x68,0x62,0xfe,0x00}, // 69 'E'
    {0xfe,0x62,0x68,0x78,0x68,0x60,0xf0,0x00}, // 70 'F'
    {0x3c,0x66,0xc0,0xc0,0xce,0x66,0x3e,0x00}, // 71 'G'
    {0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0x00}, // 72 'H'
    {0x7e,0x18,0x18,0x18,0x18,0x18,0x7e,0x00}, // 73 'I'
    {0x1e,0x0c,0x0c,0x0c,0xcc,0xcc,0x78,0x00}, // 74 'J'
    {0xe6,0x66,0x6c,0x78,0x6c,0x66,0xe6,0x00}, // 75 'K'
    {0xf0,0x60,0x60,0x60,0x62,0x66,0xfe,0x00}, // 76 'L'
    {0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0x00}, // 77 'M'
    {0xc6,0xe6,0xf6,0xfe,0xde,0xce,0xc6,0x00}, // 78 'N'
    {0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00}, // 79 'O'
    {0xfc,0x66,0x66,0x7c,0x60,0x60,0xf0,0x00}, // 80 'P'
    {0x7c,0xc6,0xc6,0xc6,0xd6,0x7c,0x0e,0x00}, // 81 'Q'
    {0xfc,0x66,0x66,0x7c,0x6c,0x66,0xe6,0x00}, // 82 'R'
    {0x7c,0xc6,0x60,0x38,0x0c,0xc6,0x7c,0x00}, // 83 'S'
    {0x7e,0x7e,0x5a,0x18,0x18,0x18,0x3c,0x00}, // 84 'T'
    {0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00}, // 85 'U'
    {0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x10,0x00}, // 86 'V'
    {0xc6,0xc6,0xd6,0xfe,0xfe,0xee,0xc6,0x00}, // 87 'W'
    {0xc6,0x6c,0x38,0x38,0x6c,0xc6,0xc6,0x00}, // 88 'X'
    {0x66,0x66,0x66,0x3c,0x18,0x18,0x3c,0x00}, // 89 'Y'
    {0xfe,0xc6,0x8c,0x18,0x32,0x66,0xfe,0x00}, // 90 'Z'
    {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00}, // 91 '['
    {0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00}, // 92 '\'
    {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00}, // 93 ']'
    {0x10,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00}, // 95 '_'
    {0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x78,0x0c,0x7c,0xcc,0x76,0x00}, // 97 'a'
    {0xe0,0x60,0x7c,0x66,0x66,0x66,0xdc,0x00}, // 98 'b'
    {0x00,0x00,0x7c,0xc6,0xc0,0xc6,0x7c,0x00}, // 99 'c'
    {0x1c,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0x00}, // 100 'd'
    {0x00,0x00,0x7c,0xc6,0xfe,0xc0,0x7c,0x00}, // 101 'e'
    {0x1c,0x36,0x30,0x78,0x30,0x30,0x78,0x00}, // 102 'f'
    {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0xf8}, // 103 'g'
    {0xe0,0x60,0x6c,0x76,0x66,0x66,0xe6,0x00}, // 104 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00}, // 105 'i'
    {0x06,0x00,0x0e,0x06,0x06,0x66,0x3c,0x00}, // 106 'j'
    {0xe0,0x60,0x66,0x6c,0x78,0x6c,0xe6,0x00}, // 107 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // 108 'l'
    {0x00,0x00,0xec,0xfe,0xd6,0xd6,0xd6,0x00}, // 109 'm'
    {0x00,0x00,0xdc,0x66,0x66,0x66,0x66,0x00}, // 110 'n'
    {0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7c,0x00}, // 111 'o'
    {0x00,0x00,0xdc,0x66,0x66,0x7c,0x60,0xf0}, // 112 'p'
    {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0x1e}, // 113 'q'
    {0x00,0x00,0xdc,0x76,0x60,0x60,0xf0,0x00}, // 114 'r'
    {0x00,0x00,0x7c,0xc0,0x7c,0x06,0xfc,0x00}, // 115 's'
    {0x30,0x30,0xfc,0x30,0x30,0x34,0x18,0x00}, // 116 't'
    {0x00,0x00,0xcc,0xcc,0xcc,0xcc,0x76,0x00}, // 117 'u'
    {0x00,0x00,0xc6,0xc6,0x6c,0x38,0x10,0x00}, // 118 'v'
    {0x00,0x00,0xc6,0xd6,0xfe,0xee,0xc6,0x00}, // 119 'w'
    {0x00,0x00,0xc6,0x6c,0x38,0x6c,0xc6,0x00}, // 120 'x'
    {0x00,0x00,0xc6,0xc6,0x7e,0x06,0x0c,0xf8}, // 121 'y'
    {0x00,0x00,0xfe,0xcc,0x18,0x32,0xfe,0x00}, // 122 'z'
    {0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0x00}, // 123 '{'
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 124 '|'
    {0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0x00}, // 125 '}'
    {0x76,0xdc,0x00,0x00,0x00,0x00,0x00,0x00}  // 126 '~'
};

BitmapRenderer::BitmapRenderer(int width, int height)
    : m_width(width), m_height(height), m_pixels(width * height, Color::white()) {}

void BitmapRenderer::clear(Color color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

bool BitmapRenderer::inside_clip(int x, int y) const {
    if (m_clip_stack.empty()) return true;
    const float px = static_cast<float>(x) + 0.5f;
    const float py = static_cast<float>(y) + 0.5f;
    for (const auto& clip : m_clip_stack) {
        if (px < clip.x || py < clip.y ||
            px >= clip.x + clip.width || py >= clip.y + clip.height) {
            return false;
        }
    }
    return true;
}

void BitmapRenderer::set_pixel(int x, int y, Color color) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    if (!inside_clip(x, y)) return;
    if (color.a == 0) return;

    if (color.a == 255) {
        m_pixels[y * m_width + x] = color;
    } else {
        // Alpha blending
        auto& dest = m_pixels[y * m_width + x];
        float alpha = color.a / 255.0f;
        dest.r = static_cast<uint8_t>(color.r * alpha + dest.r * (1.0f - alpha));
        dest.g = static_cast<uint8_t>(color.g * alpha + dest.g * (1.0f - alpha));
        dest.b = static_cast<uint8_t>(color.b * alpha + dest.b * (1.0f - alpha));
    }
}

void BitmapRenderer::fill_rect(int rx, int ry, int rw, int rh, Color color) {
    int x0 = std::clamp(rx, 0, m_width);
    int y0 = std::clamp(ry, 0, m_height);
    int x1 = std::clamp(rx + rw, 0, m_width);
    int y1 = std::clamp(ry + rh, 0, m_height);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            set_pixel(x, y, color);
        }
    }
}

static float rounded_box_sdf(float px, float py, float x, float y, float w, float h, float r) {
    float cx = x + w / 2.0f;
    float cy = y + h / 2.0f;
    float bx = w / 2.0f - r;
    float by = h / 2.0f - r;

    float dx = std::abs(px - cx) - bx;
    float dy = std::abs(py - cy) - by;

    float ox = std::max(dx, 0.0f);
    float oy = std::max(dy, 0.0f);
    float outside_dist = std::sqrt(ox * ox + oy * oy);
    float inside_dist = std::min(std::max(dx, dy), 0.0f);

    return outside_dist + inside_dist - r;
}

void BitmapRenderer::draw_char(int x, int y, char c, Color color, int scale) {
    if (c < 32 || c > 126) c = '?';
    int glyph_idx = c - 32;

    for (int row = 0; row < 8; ++row) {
        uint8_t line = FONT_8X8[glyph_idx][row];
        for (int col = 0; col < 8; ++col) {
            if ((line >> (7 - col)) & 1) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void BitmapRenderer::execute_command(const PaintCommand& cmd) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, PushTransformCommand>) {
            auto curr = current_transform();
            m_transform_stack.push_back(curr.multiply(arg.matrix));
        } else if constexpr (std::is_same_v<T, PopTransformCommand>) {
            if (!m_transform_stack.empty()) m_transform_stack.pop_back();
        } else if constexpr (std::is_same_v<T, PushClipCommand>) {
            m_clip_stack.push_back(current_transform().map_rect(arg.rect));
        } else if constexpr (std::is_same_v<T, PopClipCommand>) {
            if (!m_clip_stack.empty()) m_clip_stack.pop_back();
        } else if constexpr (std::is_same_v<T, DrawShadowCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            float sx = tr.x + arg.x_offset;
            float sy = tr.y + arg.y_offset;
            float sw = tr.width;
            float sh = tr.height;
            float blur = std::max(1.0f, arg.blur);
            float r = arg.border_radius;

            int x0 = std::clamp(static_cast<int>(sx - blur), 0, m_width);
            int y0 = std::clamp(static_cast<int>(sy - blur), 0, m_height);
            int x1 = std::clamp(static_cast<int>(sx + sw + blur), 0, m_width);
            int y1 = std::clamp(static_cast<int>(sy + sh + blur), 0, m_height);

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    float dist = rounded_box_sdf(x + 0.5f, y + 0.5f, sx, sy, sw, sh, r);
                    if (dist < blur) {
                        float shadow_alpha = (dist <= 0.0f) ? 1.0f : (1.0f - dist / blur);
                        shadow_alpha = std::clamp(shadow_alpha, 0.0f, 1.0f);
                        Color c = arg.color;
                        c.a = static_cast<uint8_t>(c.a * shadow_alpha * 0.6f);
                        set_pixel(x, y, c);
                    }
                }
            }
        } else if constexpr (std::is_same_v<T, DrawRectCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            if (arg.border_radius <= 0.5f) {
                fill_rect(
                    static_cast<int>(tr.x),
                    static_cast<int>(tr.y),
                    static_cast<int>(tr.width),
                    static_cast<int>(tr.height),
                    arg.color
                );
            } else {
                float rx = tr.x;
                float ry = tr.y;
                float rw = tr.width;
                float rh = tr.height;
                float r = std::min({arg.border_radius, rw / 2.0f, rh / 2.0f});

                int x0 = std::clamp(static_cast<int>(rx), 0, m_width);
                int y0 = std::clamp(static_cast<int>(ry), 0, m_height);
                int x1 = std::clamp(static_cast<int>(rx + rw + 1.0f), 0, m_width);
                int y1 = std::clamp(static_cast<int>(ry + rh + 1.0f), 0, m_height);

                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        float dist = rounded_box_sdf(x + 0.5f, y + 0.5f, rx, ry, rw, rh, r);
                        if (dist <= 0.5f) {
                            float cov = std::clamp(0.5f - dist, 0.0f, 1.0f);
                            Color c = arg.color;
                            c.a = static_cast<uint8_t>(c.a * cov);
                            set_pixel(x, y, c);
                        }
                    }
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBorderCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            int x = static_cast<int>(tr.x);
            int y = static_cast<int>(tr.y);
            int w = static_cast<int>(tr.width);
            int h = static_cast<int>(tr.height);

            // Top
            fill_rect(x, y, w, static_cast<int>(arg.width.top), arg.color);
            // Bottom
            fill_rect(x, y + h - static_cast<int>(arg.width.bottom), w, static_cast<int>(arg.width.bottom), arg.color);
            // Left
            fill_rect(x, y, static_cast<int>(arg.width.left), h, arg.color);
            // Right
            fill_rect(x + w - static_cast<int>(arg.width.right), y, static_cast<int>(arg.width.right), h, arg.color);

        } else if constexpr (std::is_same_v<T, DrawTextCommand>) {
            Point tp = current_transform().map_point(arg.position);

            // Advance must match the metric the layout engine assumes, otherwise
            // glyphs collide and line widths disagree with the box geometry.
            const int advance = std::max(1, static_cast<int>(std::lround(arg.font_size * 0.55f)));
            const int scale = std::max(1, advance / 8);
            int cur_x = static_cast<int>(tp.x);
            int cur_y = static_cast<int>(tp.y);

            for (char ch : arg.text) {
                draw_char(cur_x, cur_y, ch, arg.color, scale);
                if (arg.font_weight >= 600) {
                    draw_char(cur_x + 1, cur_y, ch, arg.color, scale);
                }
                cur_x += advance;
            }
        } else if constexpr (std::is_same_v<T, DrawImageCommand>) {
            if (arg.image && arg.image->is_valid()) {
                Rect tr = current_transform().map_rect(arg.rect);
                int dst_x = static_cast<int>(tr.x);
                int dst_y = static_cast<int>(tr.y);
                int dst_w = static_cast<int>(tr.width);
                int dst_h = static_cast<int>(tr.height);

                if (dst_w > 0 && dst_h > 0) {
                    int src_w = arg.image->width;
                    int src_h = arg.image->height;

                    int x0 = std::clamp(dst_x, 0, m_width);
                    int y0 = std::clamp(dst_y, 0, m_height);
                    int x1 = std::clamp(dst_x + dst_w, 0, m_width);
                    int y1 = std::clamp(dst_y + dst_h, 0, m_height);

                    float r = std::min({arg.border_radius, dst_w / 2.0f, dst_h / 2.0f});

                    for (int y = y0; y < y1; ++y) {
                        for (int x = x0; x < x1; ++x) {
                            if (r > 0.5f) {
                                float dist = rounded_box_sdf(x + 0.5f, y + 0.5f, dst_x, dst_y, dst_w, dst_h, r);
                                if (dist > 0.5f) continue;
                            }
                            int sx = std::clamp((x - dst_x) * src_w / dst_w, 0, src_w - 1);
                            int sy = std::clamp((y - dst_y) * src_h / dst_h, 0, src_h - 1);
                            Color c = arg.image->get_pixel_color(sx, sy);
                            set_pixel(x, y, c);
                        }
                    }
                }
            }
        }
    }, cmd);
}

void BitmapRenderer::render(const DisplayList& display_list) {
    for (const auto& cmd : display_list.commands()) {
        execute_command(cmd);
    }
}

bool BitmapRenderer::save_bmp(const std::string& filepath) const {
    std::ofstream file(filepath, std::ios::binary);
    if (!file) return false;

    // Row padding to 4 bytes
    int row_stride = (m_width * 3 + 3) & ~3;
    uint32_t image_size = row_stride * m_height;
    uint32_t file_size = 54 + image_size;

    // BMP Header (14 bytes)
    uint8_t file_header[14] = {
        'B', 'M',
        static_cast<uint8_t>(file_size & 0xFF),
        static_cast<uint8_t>((file_size >> 8) & 0xFF),
        static_cast<uint8_t>((file_size >> 16) & 0xFF),
        static_cast<uint8_t>((file_size >> 24) & 0xFF),
        0, 0, 0, 0, // Reserved
        54, 0, 0, 0 // Pixel offset
    };

    // DIB Header (40 bytes - BITMAPINFOHEADER)
    uint8_t info_header[40] = {0};
    info_header[0] = 40; // Header size
    // Width
    info_header[4] = static_cast<uint8_t>(m_width & 0xFF);
    info_header[5] = static_cast<uint8_t>((m_width >> 8) & 0xFF);
    info_header[6] = static_cast<uint8_t>((m_width >> 16) & 0xFF);
    info_header[7] = static_cast<uint8_t>((m_width >> 24) & 0xFF);
    // Height
    info_header[8] = static_cast<uint8_t>(m_height & 0xFF);
    info_header[9] = static_cast<uint8_t>((m_height >> 8) & 0xFF);
    info_header[10] = static_cast<uint8_t>((m_height >> 16) & 0xFF);
    info_header[11] = static_cast<uint8_t>((m_height >> 24) & 0xFF);
    // Planes
    info_header[12] = 1;
    // Bits per pixel (24)
    info_header[14] = 24;
    // Compression (0 = none)
    // Image size
    info_header[20] = static_cast<uint8_t>(image_size & 0xFF);
    info_header[21] = static_cast<uint8_t>((image_size >> 8) & 0xFF);
    info_header[22] = static_cast<uint8_t>((image_size >> 16) & 0xFF);
    info_header[23] = static_cast<uint8_t>((image_size >> 24) & 0xFF);

    file.write(reinterpret_cast<const char*>(file_header), 14);
    file.write(reinterpret_cast<const char*>(info_header), 40);

    // Pixels (stored bottom-to-top, BGR order)
    std::vector<uint8_t> row_buffer(row_stride, 0);
    for (int y = m_height - 1; y >= 0; --y) {
        for (int x = 0; x < m_width; ++x) {
            const auto& p = m_pixels[y * m_width + x];
            row_buffer[x * 3 + 0] = p.b;
            row_buffer[x * 3 + 1] = p.g;
            row_buffer[x * 3 + 2] = p.r;
        }
        file.write(reinterpret_cast<const char*>(row_buffer.data()), row_stride);
    }

    return true;
}

} // namespace litebird
