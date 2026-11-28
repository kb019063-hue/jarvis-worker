#include "litebird/render/ImageFilter.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace litebird {

static inline uint8_t clamp_u8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return static_cast<uint8_t>(std::round(v));
}

void ImageFilter::blur(std::vector<Color>& pixels, int width, int height, float radius) {
    if (!std::isfinite(radius) || radius <= 0.0f || width <= 0 || height <= 0) {
        return;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels.size() != pixel_count) {
        return;
    }

    // CSS `blur(<length>)` uses the length as a Gaussian standard deviation.
    // Keep the existing canvas-sized upper bound, but retain fractional sigma:
    // rounding it first makes small blurs jump in visible steps.
    const int max_dimension = std::max(width, height);
    const double sigma = std::min(static_cast<double>(radius),
                                  std::min(static_cast<double>(max_dimension),
                                           static_cast<double>(std::numeric_limits<int>::max() / 3)));
    const int kernel_radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    // Offsets farther apart than the largest image dimension cannot connect
    // two pixels in this bitmap. The omitted taps still contribute to the
    // normalization above, so the area beyond the bitmap is transparent rather
    // than a clamped copy of its edge.
    const int sample_radius = std::min(kernel_radius, max_dimension);
    std::vector<double> weights(static_cast<std::size_t>(sample_radius) * 2u + 1u, 0.0);
    double weight_sum = 0.0;
    for (int offset = -kernel_radius; offset <= kernel_radius; ++offset) {
        const double scaled = static_cast<double>(offset) / sigma;
        const double weight = std::exp(-0.5 * scaled * scaled);
        weight_sum += weight;
        if (offset >= -sample_radius && offset <= sample_radius) {
            weights[static_cast<std::size_t>(offset + sample_radius)] = weight;
        }
    }
    for (double& weight : weights) {
        weight /= weight_sum;
    }

    struct PremultipliedPixel {
        float red;
        float green;
        float blue;
        float alpha;
    };
    std::vector<PremultipliedPixel> horizontal(pixel_count);

    // Convolve each axis separately. Work in premultiplied alpha so a fading
    // edge of a solid colour stays that colour instead of acquiring a dark
    // fringe from averaging its RGB against transparent black.
    for (int y = 0; y < height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            const int first_x = std::max(0, x - sample_radius);
            const int last_x = std::min(width - 1, x + sample_radius);
            double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0, sum_a = 0.0;
            for (int source_x = first_x; source_x <= last_x; ++source_x) {
                const int offset = source_x - x;
                const double weight = weights[static_cast<std::size_t>(offset + sample_radius)];
                const Color& c = pixels[row + static_cast<std::size_t>(source_x)];
                const double alpha = static_cast<double>(c.a) / 255.0;
                sum_r += static_cast<double>(c.r) * alpha * weight;
                sum_g += static_cast<double>(c.g) * alpha * weight;
                sum_b += static_cast<double>(c.b) * alpha * weight;
                sum_a += alpha * weight;
            }
            horizontal[row + static_cast<std::size_t>(x)] = {
                static_cast<float>(sum_r), static_cast<float>(sum_g),
                static_cast<float>(sum_b), static_cast<float>(sum_a)};
        }
    }

    for (int y = 0; y < height; ++y) {
        const int first_y = std::max(0, y - sample_radius);
        const int last_y = std::min(height - 1, y + sample_radius);
        for (int x = 0; x < width; ++x) {
            double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0, sum_a = 0.0;
            for (int source_y = first_y; source_y <= last_y; ++source_y) {
                const int offset = source_y - y;
                const double weight = weights[static_cast<std::size_t>(offset + sample_radius)];
                const PremultipliedPixel& p =
                    horizontal[static_cast<std::size_t>(source_y) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)];
                sum_r += static_cast<double>(p.red) * weight;
                sum_g += static_cast<double>(p.green) * weight;
                sum_b += static_cast<double>(p.blue) * weight;
                sum_a += static_cast<double>(p.alpha) * weight;
            }

            const double alpha = std::clamp(sum_a, 0.0, 1.0);
            const uint8_t alpha_byte = clamp_u8(static_cast<float>(alpha * 255.0));
            if (alpha <= 0.0 || alpha_byte == 0) {
                pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(x)] = Color::transparent();
                continue;
            }
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] = Color(
                clamp_u8(static_cast<float>(sum_r / alpha)),
                clamp_u8(static_cast<float>(sum_g / alpha)),
                clamp_u8(static_cast<float>(sum_b / alpha)),
                alpha_byte);
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
