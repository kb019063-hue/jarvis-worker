#include "litebird/render/ImageFilter.hpp"
#include <cmath>
#include <algorithm>

namespace litebird {

static inline uint8_t clamp_u8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return static_cast<uint8_t>(std::round(v));
}

void ImageFilter::blur(std::vector<Color>& pixels, int width, int height, float radius) {
    if (radius <= 0.0f || width <= 0 || height <= 0 || pixels.size() != static_cast<size_t>(width * height)) {
        return;
    }
    // The radius may be as wide as the canvas; clamping it to 50 pixels made
    // `blur(60)` on a 200-pixel box hard-edged beyond the clamp, which is a
    // visible lie about what was asked for. The window is still bounded by the
    // bitmap, so it cannot walk off either end.
    int r = std::clamp(static_cast<int>(std::round(radius)), 1, std::max(width, height));

    std::vector<Color> temp(pixels.size());

    // Horizontal pass
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
            int count = 0;
            for (int kx = -r; kx <= r; ++kx) {
                int px = std::clamp(x + kx, 0, width - 1);
                const Color& c = pixels[y * width + px];
                sum_r += c.r;
                sum_g += c.g;
                sum_b += c.b;
                sum_a += c.a;
                count++;
            }
            temp[y * width + x] = Color(
                clamp_u8(sum_r / count),
                clamp_u8(sum_g / count),
                clamp_u8(sum_b / count),
                clamp_u8(sum_a / count)
            );
        }
    }

    // Vertical pass
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
            int count = 0;
            for (int ky = -r; ky <= r; ++ky) {
                int py = std::clamp(y + ky, 0, height - 1);
                const Color& c = temp[py * width + x];
                sum_r += c.r;
                sum_g += c.g;
                sum_b += c.b;
                sum_a += c.a;
                count++;
            }
            pixels[y * width + x] = Color(
                clamp_u8(sum_r / count),
                clamp_u8(sum_g / count),
                clamp_u8(sum_b / count),
                clamp_u8(sum_a / count)
            );
        }
    }
}

void ImageFilter::brightness(std::vector<Color>& pixels, float amount) {
    if (amount < 0.0f) amount = 0.0f;
    for (auto& c : pixels) {
        c.r = clamp_u8(c.r * amount);
        c.g = clamp_u8(c.g * amount);
        c.b = clamp_u8(c.b * amount);
    }
}

void ImageFilter::contrast(std::vector<Color>& pixels, float amount) {
    if (amount < 0.0f) amount = 0.0f;
    for (auto& c : pixels) {
        c.r = clamp_u8((c.r - 128.0f) * amount + 128.0f);
        c.g = clamp_u8((c.g - 128.0f) * amount + 128.0f);
        c.b = clamp_u8((c.b - 128.0f) * amount + 128.0f);
    }
}

void ImageFilter::grayscale(std::vector<Color>& pixels, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    for (auto& c : pixels) {
        float gray = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        c.r = clamp_u8(c.r * (1.0f - amount) + gray * amount);
        c.g = clamp_u8(c.g * (1.0f - amount) + gray * amount);
        c.b = clamp_u8(c.b * (1.0f - amount) + gray * amount);
    }
}

void ImageFilter::sepia(std::vector<Color>& pixels, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    for (auto& c : pixels) {
        float sr = 0.393f * c.r + 0.769f * c.g + 0.189f * c.b;
        float sg = 0.349f * c.r + 0.686f * c.g + 0.168f * c.b;
        float sb = 0.272f * c.r + 0.534f * c.g + 0.131f * c.b;
        c.r = clamp_u8(c.r * (1.0f - amount) + sr * amount);
        c.g = clamp_u8(c.g * (1.0f - amount) + sg * amount);
        c.b = clamp_u8(c.b * (1.0f - amount) + sb * amount);
    }
}

void ImageFilter::invert(std::vector<Color>& pixels, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    for (auto& c : pixels) {
        c.r = clamp_u8(c.r * (1.0f - amount) + (255.0f - c.r) * amount);
        c.g = clamp_u8(c.g * (1.0f - amount) + (255.0f - c.g) * amount);
        c.b = clamp_u8(c.b * (1.0f - amount) + (255.0f - c.b) * amount);
    }
}

void ImageFilter::opacity(std::vector<Color>& pixels, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    for (auto& c : pixels) {
        c.a = clamp_u8(c.a * amount);
    }
}

// The CSS Filter Effects matrices, used verbatim. `s` and `c` are the sine and
// cosine of the angle for `hue-rotate`; for `saturate` they are 0 and the
// saturation amount. Keeping one matrix routine for both is what the
// specification does, and it is the only way `saturate(1)` and
// `hue-rotate(0deg)` are exactly the identity rather than near it.
static void apply_color_matrix(std::vector<Color>& pixels, const float m[9]) {
    for (auto& c : pixels) {
        const float r = c.r, g = c.g, b = c.b;
        c.r = clamp_u8(m[0] * r + m[1] * g + m[2] * b);
        c.g = clamp_u8(m[3] * r + m[4] * g + m[5] * b);
        c.b = clamp_u8(m[6] * r + m[7] * g + m[8] * b);
    }
}

void ImageFilter::hue_rotate(std::vector<Color>& pixels, float degrees) {
    // The CSS value wraps, so 360deg is the identity and -90deg equals 270deg.
    degrees = std::fmod(degrees, 360.0f);
    if (degrees < 0.0f) degrees += 360.0f;
    const float a = degrees * 3.14159265358979323846f / 180.0f;
    const float s = std::sin(a), c = std::cos(a);
    const float m[9] = {
        0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f,
        0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f,
        0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f};
    apply_color_matrix(pixels, m);
}

void ImageFilter::saturate(std::vector<Color>& pixels, float amount) {
    // No clamp to 1: CSS allows saturation above it, and `saturate(2)` is a
    // legal, stronger colour. The matrix clamps the result per channel instead.
    const float s = amount;
    const float m[9] = {
        0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s,
        0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s,
        0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s};
    apply_color_matrix(pixels, m);
}

void ImageFilter::drop_shadow(std::vector<Color>& pixels, int width, int height, float dx, float dy, float blur_r, Color color) {
    if (width <= 0 || height <= 0 || pixels.size() != static_cast<size_t>(width * height)) {
        return;
    }

    // Nothing to cast: a transparent shadow colour would copy the input back
    // unchanged, so return before allocating a second buffer per filter.
    if (color.a == 0) return;

    std::vector<Color> shadow(pixels.size(), Color::transparent());
    int idx_x = static_cast<int>(std::round(dx));
    int idx_y = static_cast<int>(std::round(dy));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int src_x = x - idx_x;
            int src_y = y - idx_y;
            if (src_x >= 0 && src_x < width && src_y >= 0 && src_y < height) {
                uint8_t src_a = pixels[src_y * width + src_x].a;
                if (src_a > 0) {
                    uint8_t shadow_a = clamp_u8((src_a / 255.0f) * (color.a / 255.0f) * 255.0f);
                    shadow[y * width + x] = Color(color.r, color.g, color.b, shadow_a);
                }
            }
        }
    }

    if (blur_r > 0.0f) {
        blur(shadow, width, height, blur_r);
    }

    // Blend original over shadow
    for (size_t i = 0; i < pixels.size(); ++i) {
        const auto& fg = pixels[i];
        const auto& bg = shadow[i];

        if (fg.a == 255) {
            // fully opaque fg
            continue;
        }
        if (fg.a == 0) {
            pixels[i] = bg;
            continue;
        }

        float a_fg = fg.a / 255.0f;
        float a_bg = (bg.a / 255.0f) * (1.0f - a_fg);
        float a_out = a_fg + a_bg;

        if (a_out > 0.0f) {
            uint8_t r = clamp_u8((fg.r * a_fg + bg.r * a_bg) / a_out);
            uint8_t g = clamp_u8((fg.g * a_fg + bg.g * a_bg) / a_out);
            uint8_t b = clamp_u8((fg.b * a_fg + bg.b * a_bg) / a_out);
            uint8_t a = clamp_u8(a_out * 255.0f);
            pixels[i] = Color(r, g, b, a);
        }
    }
}

void ImageFilter::apply_filter(std::vector<Color>& pixels, int width, int height, const FilterFunction& filter) {
    switch (filter.type) {
        case FilterType::Blur:
            blur(pixels, width, height, filter.amount);
            break;
        case FilterType::Brightness:
            brightness(pixels, filter.amount);
            break;
        case FilterType::Contrast:
            contrast(pixels, filter.amount);
            break;
        case FilterType::Grayscale:
            grayscale(pixels, filter.amount);
            break;
        case FilterType::Sepia:
            sepia(pixels, filter.amount);
            break;
        case FilterType::Invert:
            invert(pixels, filter.amount);
            break;
        case FilterType::Opacity:
            opacity(pixels, filter.amount);
            break;
        case FilterType::DropShadow:
            drop_shadow(pixels, width, height, filter.dx, filter.dy, filter.blur_radius, filter.color);
            break;
        case FilterType::HueRotate:
            hue_rotate(pixels, filter.amount);
            break;
        case FilterType::Saturate:
            saturate(pixels, filter.amount);
            break;
        default:
            break;
    }
}

void ImageFilter::apply_filters(std::vector<Color>& pixels, int width, int height, const std::vector<FilterFunction>& filters) {
    for (const auto& f : filters) {
        apply_filter(pixels, width, height, f);
    }
}

} // namespace litebird
