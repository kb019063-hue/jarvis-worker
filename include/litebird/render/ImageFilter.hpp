#pragma once

#include "litebird/common/Color.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace litebird {

enum class FilterType {
    None,
    Blur,
    Brightness,
    Contrast,
    Grayscale,
    Sepia,
    Invert,
    Opacity,
    DropShadow
};

struct FilterFunction {
    FilterType type{FilterType::None};
    float amount{0.0f};
    Color color{Color::black()};
    float dx{0.0f};
    float dy{0.0f};
    float blur_radius{0.0f};
};

class ImageFilter {
public:
    static void apply_filter(std::vector<Color>& pixels, int width, int height, const FilterFunction& filter);
    static void apply_filters(std::vector<Color>& pixels, int width, int height, const std::vector<FilterFunction>& filters);

    static void blur(std::vector<Color>& pixels, int width, int height, float radius);
    static void brightness(std::vector<Color>& pixels, float amount);
    static void contrast(std::vector<Color>& pixels, float amount);
    static void grayscale(std::vector<Color>& pixels, float amount);
    static void sepia(std::vector<Color>& pixels, float amount);
    static void invert(std::vector<Color>& pixels, float amount);
    static void opacity(std::vector<Color>& pixels, float amount);
    static void drop_shadow(std::vector<Color>& pixels, int width, int height, float dx, float dy, float blur_r, Color color);
};

} // namespace litebird
