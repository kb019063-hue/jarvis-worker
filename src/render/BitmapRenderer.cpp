#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/render/FontRenderer.hpp"
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

// Defined below, beside the fill and border code that draws with it. The clip
// asks the same question a background of the same box asks (finding 156).
static float rounded_box_sdf(float px, float py, float x, float y, float w, float h, float r);

void BitmapRenderer::clear(Color color) {
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

bool BitmapRenderer::clip_coverage(int x, int y, float& coverage) const {
    coverage = 1.0f;
    if (m_clip_stack.empty()) return true;
    const float px = static_cast<float>(x) + 0.5f;
    const float py = static_cast<float>(y) + 0.5f;
    for (const auto& clip : m_clip_stack) {
        const Rect& r = clip.rect;
        if (px < r.x || py < r.y || px >= r.x + r.width || py >= r.y + r.height) {
            return false;
        }
        if (clip.radius > 0.5f) {
            // An `overflow: hidden` box with a `border-radius` clips to the
            // rounded box, not to its bounding rectangle (CSS 2.1 §11.1.1 makes
            // the overflow clip the padding box shaped by the corner curves).
            // The same signed-distance field the background fill uses answers
            // it, so the ink of a child stops exactly where a background of the
            // same box would stop (finding 156) -- including on the curve
            // itself, where the answer is a fraction and not yes or no.
            const float radius = std::min({clip.radius, r.width / 2.0f, r.height / 2.0f});
            const float dist = rounded_box_sdf(px, py, r.x, r.y, r.width, r.height, radius);
            if (dist >= 0.5f) return false;
            coverage *= std::clamp(0.5f - dist, 0.0f, 1.0f);
            if (coverage <= 0.0f) return false;
        }
    }
    return true;
}

static void blend_over(Color& dest, Color color) {
    if (color.a == 0) return;
    if (color.a == 255 || dest.a == 0) {
        dest = color;
    } else if (dest.a == 255) {
        // Source-over on the usual opaque viewport. Integer arithmetic avoids
        // float truncation turning a half-red-on-white pixel into (255,126,126)
        // instead of the browser's (255,127,127).
        const unsigned a = color.a;
        auto over = [a](unsigned src, unsigned dst) -> uint8_t {
            return static_cast<uint8_t>((src * a + dst * (255u - a) + 127u) / 255u);
        };
        dest.r = over(color.r, dest.r);
        dest.g = over(color.g, dest.g);
        dest.b = over(color.b, dest.b);
    } else {
        // Iframe bitmaps and opacity layers start transparent. Preserve straight
        // alpha so they can later composite over the parent buffer rather than
        // being flattened to white. alpha_n is the output alpha with denominator
        // 255*255; each channel has the same divisor.
        const unsigned a = color.a;
        const unsigned remaining = 255u - a;
        const unsigned alpha_n = a * 255u + dest.a * remaining;
        auto over = [a, remaining, alpha_n, &dest](unsigned src, unsigned dst) -> uint8_t {
            const unsigned value = src * a * 255u + dst * dest.a * remaining;
            return static_cast<uint8_t>((value + alpha_n / 2u) / alpha_n);
        };
        dest.r = over(color.r, dest.r);
        dest.g = over(color.g, dest.g);
        dest.b = over(color.b, dest.b);
        dest.a = static_cast<uint8_t>((alpha_n + 127u) / 255u);
    }
}

static Color sample_linear_gradient(const LinearGradient& grad, float px, float py, const Rect& bounds) {
    if (grad.stops.empty()) return Color::transparent();
    if (grad.stops.size() == 1 || bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return grad.stops.front().color;
    }
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float rad = grad.angle_deg * kDegToRad;
    // CSS Images 3 §3.4.1: 0deg points upward (0, -1), 90deg rightward (+1, 0),
    // 180deg downward (0, +1).
    const float dx = std::sin(rad);
    const float dy = -std::cos(rad);
    const float grad_len = std::abs(bounds.width * dx) + std::abs(bounds.height * dy);
    if (grad_len <= 0.0f) return grad.stops.front().color;

    const float cx = bounds.x + bounds.width * 0.5f;
    const float cy = bounds.y + bounds.height * 0.5f;
    const float proj = (px - cx) * dx + (py - cy) * dy;
    const float t = (proj / grad_len) + 0.5f;

    if (t <= grad.stops.front().position) return grad.stops.front().color;
    if (t >= grad.stops.back().position) return grad.stops.back().color;

    for (std::size_t i = 0; i + 1 < grad.stops.size(); ++i) {
        const auto& s0 = grad.stops[i];
        const auto& s1 = grad.stops[i + 1];
        if (t <= s1.position) {
            const float span = s1.position - s0.position;
            const float f = (span > 0.0f) ? std::clamp((t - s0.position) / span, 0.0f, 1.0f) : 1.0f;
            auto lerp_ch = [f](uint8_t a, uint8_t b) -> uint8_t {
                return static_cast<uint8_t>(std::clamp(
                    static_cast<int>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * f)),
                    0, 255));
            };
            return Color(lerp_ch(s0.color.r, s1.color.r),
                         lerp_ch(s0.color.g, s1.color.g),
                         lerp_ch(s0.color.b, s1.color.b),
                         lerp_ch(s0.color.a, s1.color.a));
        }
    }
    return grad.stops.back().color;
}

void BitmapRenderer::set_pixel(int x, int y, Color color) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    float coverage = 1.0f;
    if (!clip_coverage(x, y, coverage)) return;
    // A pixel on the curve of a rounded clip is clipped by the *part of it*
    // that is inside, so the ink keeps that fraction of its alpha rather than
    // all of it or none. This is what makes the child's edge land on the same
    // anti-aliased curve a background of the same box paints (finding 156).
    if (coverage < 1.0f && color.a > 0) {
        color.a = static_cast<uint8_t>(color.a * coverage);
    }
    if (color.a == 0) return;

    blend_over(m_pixels[y * m_width + x], color);
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
        if constexpr (std::is_same_v<T, PushOpacityCommand>) {
            OpacityLayer layer;
            layer.opacity = std::clamp(arg.opacity, 0.0f, 1.0f);
            if (m_skip_depth > 0 || layer.opacity <= 0.0f) {
                layer.skipped = true;
                ++m_skip_depth;
            } else if (layer.opacity < 1.0f) {
                layer.saved_pixels = std::move(m_pixels);
                m_pixels.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height),
                                Color::transparent());
            }
            m_opacity_stack.push_back(std::move(layer));
            return;
        } else if constexpr (std::is_same_v<T, PopOpacityCommand>) {
            if (m_opacity_stack.empty()) return;
            OpacityLayer layer = std::move(m_opacity_stack.back());
            m_opacity_stack.pop_back();
            if (layer.skipped) {
                if (m_skip_depth > 0) --m_skip_depth;
            } else if (!layer.saved_pixels.empty()) {
                std::vector<Color> group = std::move(m_pixels);
                m_pixels = std::move(layer.saved_pixels);
                const std::size_t count = std::min(group.size(), m_pixels.size());
                for (std::size_t i = 0; i < count; ++i) {
                    Color c = group[i];
                    if (c.a == 0) continue;
                    c.a = static_cast<uint8_t>(std::clamp(
                        static_cast<int>(std::lround(static_cast<float>(c.a) * layer.opacity)),
                        0, 255));
                    blend_over(m_pixels[i], c);
                }
            }
            return;
        } else if (m_skip_depth > 0) {
            return;
        } else if constexpr (std::is_same_v<T, DrawCanvasCommand>) {
            // The canvas is in viewport/device space, not in the scroll or
            // element transform currently on the command stack. Clipping is
            // still applied by set_pixel, allowing an enclosing target clip.
            if (!arg.color.is_transparent()) fill_rect(0, 0, m_width, m_height, arg.color);
            if (arg.gradient && !arg.gradient->stops.empty()) {
                const Rect bounds(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
                for (int y = 0; y < m_height; ++y) {
                    for (int x = 0; x < m_width; ++x) {
                        Color c = sample_linear_gradient(*arg.gradient, static_cast<float>(x) + 0.5f,
                                                         static_cast<float>(y) + 0.5f, bounds);
                        if (!c.is_transparent()) set_pixel(x, y, c);
                    }
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBackdropCommand>) {
            // The UA `::backdrop`: a colour over the whole viewport, on top of
            // everything the document has drawn so far. Not clipped by an
            // enclosing target clip, for the same reason the canvas is not.
            fill_rect(0, 0, m_width, m_height, arg.color);
        } else if constexpr (std::is_same_v<T, PushTransformCommand>) {
            auto curr = current_transform();
            m_transform_stack.push_back(curr.multiply(arg.matrix));
        } else if constexpr (std::is_same_v<T, PopTransformCommand>) {
            if (!m_transform_stack.empty()) m_transform_stack.pop_back();
        } else if constexpr (std::is_same_v<T, PushClipCommand>) {
            ClipRegion region;
            region.rect = current_transform().map_rect(arg.rect);
            // The clip is pushed in device space with the rest of the geometry,
            // so the radius has to be mapped too: a scale of 2 makes a 16px
            // corner a 32px one on the bitmap it actually clips.
            const Rect& tr = region.rect;
            const Rect& src = arg.rect;
            const float sx = src.width > 0.0f ? tr.width / src.width : 1.0f;
            const float sy = src.height > 0.0f ? tr.height / src.height : 1.0f;
            region.radius = arg.border_radius * std::min(sx, sy);
            m_clip_stack.push_back(region);
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
            const bool has_grad = arg.gradient && !arg.gradient->stops.empty();
            if (arg.border_radius <= 0.5f) {
                if (!arg.color.is_transparent()) {
                    fill_rect(
                        static_cast<int>(tr.x),
                        static_cast<int>(tr.y),
                        static_cast<int>(tr.width),
                        static_cast<int>(tr.height),
                        arg.color
                    );
                }
                if (has_grad) {
                    int x0 = std::clamp(static_cast<int>(tr.x), 0, m_width);
                    int y0 = std::clamp(static_cast<int>(tr.y), 0, m_height);
                    int x1 = std::clamp(static_cast<int>(tr.x + tr.width), 0, m_width);
                    int y1 = std::clamp(static_cast<int>(tr.y + tr.height), 0, m_height);
                    for (int y = y0; y < y1; ++y) {
                        for (int x = x0; x < x1; ++x) {
                            Color c = sample_linear_gradient(*arg.gradient,
                                                             static_cast<float>(x) + 0.5f,
                                                             static_cast<float>(y) + 0.5f, tr);
                            if (!c.is_transparent()) set_pixel(x, y, c);
                        }
                    }
                }
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
                            if (!arg.color.is_transparent()) {
                                Color c = arg.color;
                                c.a = static_cast<uint8_t>(c.a * cov);
                                set_pixel(x, y, c);
                            }
                            if (has_grad) {
                                Color c = sample_linear_gradient(*arg.gradient,
                                                                 static_cast<float>(x) + 0.5f,
                                                                 static_cast<float>(y) + 0.5f, tr);
                                c.a = static_cast<uint8_t>(c.a * cov);
                                set_pixel(x, y, c);
                            }
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

            // Border/outline styles (phase 512). The band of one edge is
            // painted in its own colour, and the styles that shade one edge
            // differently from another (`inset`/`outset`) or split a band in
            // two (`groove`/`ridge`) derive their tones from that same colour:
            // halfway towards black for the dark side, halfway towards white
            // for the light one. Corners overlap (the horizontal edges own
            // them, being painted first), which is the same compromise the
            // solid painter always made with overlapping opaque bands.
            const auto darken = [](Color c) {
                return Color{static_cast<uint8_t>(c.r * 0.5f), static_cast<uint8_t>(c.g * 0.5f),
                            static_cast<uint8_t>(c.b * 0.5f), c.a};
            };
            const auto lighten = [](Color c) {
                return Color{static_cast<uint8_t>(c.r + (255 - c.r) * 0.5f), static_cast<uint8_t>(c.g + (255 - c.g) * 0.5f),
                            static_cast<uint8_t>(c.b + (255 - c.b) * 0.5f), c.a};
            };
            // The full-band tone for `inset`/`outset` and the band pair for
            // `groove`/`ridge`. `outer`/`inner` are only read by the two-tone
            // styles. `dark_side` is true for the top and left edges, the
            // sides `inset` makes dark.
            struct EdgePaint {
                Color full;
                Color outer;
                Color inner;
            };
            const auto edge_paint = [&](bool dark_side, BorderStyle style, Color base) {
                EdgePaint p{base, base, base};
                switch (style) {
                    case BorderStyle::Inset:
                        p.full = dark_side ? darken(base) : lighten(base);
                        break;
                    case BorderStyle::Outset:
                        p.full = dark_side ? lighten(base) : darken(base);
                        break;
                    case BorderStyle::Groove:
                        // Carved in: the outer half of each top/left edge is
                        // dark and its inner half light; bottom/right reverse.
                        if (dark_side) { p.outer = darken(base); p.inner = lighten(base); }
                        else { p.outer = lighten(base); p.inner = darken(base); }
                        break;
                    case BorderStyle::Ridge:
                        // The exact inverse of `groove`.
                        if (dark_side) { p.outer = lighten(base); p.inner = darken(base); }
                        else { p.outer = darken(base); p.inner = lighten(base); }
                        break;
                    default:
                        break;
                }
                return p;
            };
            // A horizontal band (top/bottom edges). `outer_at_top` is true for
            // the top edge, where "outside" is the smaller y.
            const auto paint_h = [&](int bx, int by, int length, int t, Color base, BorderStyle style,
                                     bool outer_at_top, bool dark_side) {
                if (t <= 0 || length <= 0 || !border_style_draws(style)) return;
                const EdgePaint paint = edge_paint(dark_side, style, base);
                switch (style) {
                    case BorderStyle::Double: {
                        if (t < 3) { fill_rect(bx, by, length, t, paint.full); break; }
                        const int band = t / 3;
                        fill_rect(bx, by, length, band, paint.full);
                        fill_rect(bx, by + t - band, length, band, paint.full);
                        break;
                    }
                    case BorderStyle::Dashed: {
                        const float dash = std::max(2.0f, static_cast<float>(t) * 3.0f);
                        const float gap = std::max(2.0f, static_cast<float>(t) * 2.0f);
                        for (float at = 0.0f; at < static_cast<float>(length); at += dash + gap) {
                            const int seg = std::min(static_cast<int>(dash), length - static_cast<int>(at));
                            if (seg > 0) fill_rect(bx + static_cast<int>(at), by, seg, t, paint.full);
                        }
                        break;
                    }
                    case BorderStyle::Dotted: {
                        const float spacing = std::max(2.0f, static_cast<float>(t) * 2.0f);
                        const float r = static_cast<float>(t) * 0.5f;
                        const float cy = static_cast<float>(by) + r;
                        for (float cx = static_cast<float>(bx) + r; cx < static_cast<float>(bx + length);
                             cx += spacing) {
                            const int iy0 = static_cast<int>(std::floor(cy - r));
                            const int iy1 = static_cast<int>(std::ceil(cy + r));
                            for (int py = iy0; py < iy1; ++py) {
                                const float dy = static_cast<float>(py) + 0.5f - cy;
                                const float dy2 = dy * dy;
                                const int ix0 = static_cast<int>(std::floor(cx - r));
                                const int ix1 = static_cast<int>(std::ceil(cx + r));
                                for (int px = ix0; px < ix1; ++px) {
                                    const float dx = static_cast<float>(px) + 0.5f - cx;
                                    if (dx * dx + dy2 <= r * r) set_pixel(px, py, paint.full);
                                }
                            }
                        }
                        break;
                    }
                    case BorderStyle::Groove:
                    case BorderStyle::Ridge: {
                        const int outer_t = std::max(1, t / 2);
                        if (outer_at_top) {
                            fill_rect(bx, by, length, outer_t, paint.outer);
                            fill_rect(bx, by + outer_t, length, t - outer_t, paint.inner);
                        } else {
                            fill_rect(bx, by, length, t - outer_t, paint.inner);
                            fill_rect(bx, by + t - outer_t, length, outer_t, paint.outer);
                        }
                        break;
                    }
                    default:
                        fill_rect(bx, by, length, t, paint.full);
                        break;
                }
            };
            // A vertical band (left/right edges). `outer_at_left` is true for
            // the left edge, where "outside" is the smaller x.
            const auto paint_v = [&](int bx, int by, int length, int t, Color base, BorderStyle style,
                                     bool outer_at_left, bool dark_side) {
                if (t <= 0 || length <= 0 || !border_style_draws(style)) return;
                const EdgePaint paint = edge_paint(dark_side, style, base);
                switch (style) {
                    case BorderStyle::Double: {
                        if (t < 3) { fill_rect(bx, by, t, length, paint.full); break; }
                        const int band = t / 3;
                        fill_rect(bx, by, band, length, paint.full);
                        fill_rect(bx + t - band, by, band, length, paint.full);
                        break;
                    }
                    case BorderStyle::Dashed: {
                        const float dash = std::max(2.0f, static_cast<float>(t) * 3.0f);
                        const float gap = std::max(2.0f, static_cast<float>(t) * 2.0f);
                        for (float at = 0.0f; at < static_cast<float>(length); at += dash + gap) {
                            const int seg = std::min(static_cast<int>(dash), length - static_cast<int>(at));
                            if (seg > 0) fill_rect(bx, by + static_cast<int>(at), t, seg, paint.full);
                        }
                        break;
                    }
                    case BorderStyle::Dotted: {
                        const float spacing = std::max(2.0f, static_cast<float>(t) * 2.0f);
                        const float r = static_cast<float>(t) * 0.5f;
                        const float cx = static_cast<float>(bx) + r;
                        for (float cy = static_cast<float>(by) + r; cy < static_cast<float>(by + length);
                             cy += spacing) {
                            const int ix0 = static_cast<int>(std::floor(cx - r));
                            const int ix1 = static_cast<int>(std::ceil(cx + r));
                            for (int px = ix0; px < ix1; ++px) {
                                const float dx = static_cast<float>(px) + 0.5f - cx;
                                const float dx2 = dx * dx;
                                const int iy0 = static_cast<int>(std::floor(cy - r));
                                const int iy1 = static_cast<int>(std::ceil(cy + r));
                                for (int py = iy0; py < iy1; ++py) {
                                    const float dy = static_cast<float>(py) + 0.5f - cy;
                                    if (dx2 + dy * dy <= r * r) set_pixel(px, py, paint.full);
                                }
                            }
                        }
                        break;
                    }
                    case BorderStyle::Groove:
                    case BorderStyle::Ridge: {
                        const int outer_t = std::max(1, t / 2);
                        if (outer_at_left) {
                            fill_rect(bx, by, outer_t, length, paint.outer);
                            fill_rect(bx + outer_t, by, t - outer_t, length, paint.inner);
                        } else {
                            fill_rect(bx, by, t - outer_t, length, paint.inner);
                            fill_rect(bx + t - outer_t, by, outer_t, length, paint.outer);
                        }
                        break;
                    }
                    default:
                        fill_rect(bx, by, t, length, paint.full);
                        break;
                }
            };

            const BorderStyle top_style = arg.styles.top;
            const BorderStyle right_style = arg.styles.right;
            const BorderStyle bottom_style = arg.styles.bottom;
            const BorderStyle left_style = arg.styles.left;
            // Top
            paint_h(x, y, w, static_cast<int>(arg.width.top),
                    arg.edge_colors ? arg.edge_colors->top : arg.color, top_style, /*outer_at_top=*/true,
                    /*dark_side=*/true);
            // Bottom (outside is the larger y, so outer_at_top=false; dark_side=false)
            paint_h(x, y + h - static_cast<int>(arg.width.bottom), w, static_cast<int>(arg.width.bottom),
                    arg.edge_colors ? arg.edge_colors->bottom : arg.color, bottom_style, /*outer_at_top=*/false,
                    /*dark_side=*/false);
            // Left
            paint_v(x, y, h, static_cast<int>(arg.width.left),
                    arg.edge_colors ? arg.edge_colors->left : arg.color, left_style, /*outer_at_left=*/true,
                    /*dark_side=*/true);
            // Right
            paint_v(x + w - static_cast<int>(arg.width.right), y, h, static_cast<int>(arg.width.right),
                    arg.edge_colors ? arg.edge_colors->right : arg.color, right_style, /*outer_at_left=*/false,
                    /*dark_side=*/false);

        } else if constexpr (std::is_same_v<T, DrawTextCommand>) {
            Point tp = current_transform().map_point(arg.position);
            FontRenderer::instance().draw_text(
                tp.x,
                tp.y,
                arg.text,
                arg.font_size,
                arg.font_weight,
                [this, &arg](int px, int py, uint8_t coverage) {
                    Color c = arg.color;
                    c.a = static_cast<uint8_t>((static_cast<uint32_t>(c.a) * coverage + 127u) / 255u);
                    set_pixel(px, py, c);
                },
                arg.font_family,
                arg.font_italic,
                arg.font_kerning,
                /*visual_rtl=*/true,
                /*pre_reversed=*/arg.is_rtl_visual
            );
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
    m_opacity_stack.clear();
    m_skip_depth = 0;
    for (const auto& cmd : display_list.commands()) {
        execute_command(cmd);
    }
    while (!m_opacity_stack.empty()) {
        execute_command(PopOpacityCommand{});
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
