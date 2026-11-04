#pragma once

#include <algorithm>

namespace litebird {

struct Point {
    float x{0.0f};
    float y{0.0f};

    constexpr Point() = default;
    constexpr Point(float x_val, float y_val) : x(x_val), y(y_val) {}
};

struct Rect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};

    constexpr Rect() = default;
    constexpr Rect(float x_val, float y_val, float w, float h)
        : x(x_val), y(y_val), width(w), height(h) {}

    float left() const   { return x; }
    float right() const  { return x + width; }
    float top() const    { return y; }
    float bottom() const { return y + height; }

    bool contains(float px, float py) const {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }
};

struct EdgeSizes {
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
    float left{0.0f};

    constexpr EdgeSizes() = default;
    constexpr EdgeSizes(float t, float r, float b, float l)
        : top(t), right(r), bottom(b), left(l) {}
    constexpr explicit EdgeSizes(float all)
        : top(all), right(all), bottom(all), left(all) {}
};

} // namespace litebird
