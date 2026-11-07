#pragma once

#include <algorithm>
#include <cmath>

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

    bool operator==(const EdgeSizes& other) const = default;
};

/**
 * 2D Affine Transformation Matrix (3x3):
 * [ a c e ]
 * [ b d f ]
 * [ 0 0 1 ]
 * Maps (x, y) to (a*x + c*y + e, b*x + d*y + f).
 */
class TransformMatrix2D {
public:
    float a{1.0f}, b{0.0f};
    float c{0.0f}, d{1.0f};
    float e{0.0f}, f{0.0f};

    constexpr TransformMatrix2D() = default;
    constexpr TransformMatrix2D(float a_, float b_, float c_, float d_, float e_, float f_)
        : a(a_), b(b_), c(c_), d(d_), e(e_), f(f_) {}

    static TransformMatrix2D identity() {
        return TransformMatrix2D(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    }

    static TransformMatrix2D translation(float tx, float ty) {
        return TransformMatrix2D(1.0f, 0.0f, 0.0f, 1.0f, tx, ty);
    }

    static TransformMatrix2D scale(float sx, float sy) {
        return TransformMatrix2D(sx, 0.0f, 0.0f, sy, 0.0f, 0.0f);
    }

    static TransformMatrix2D rotation(float angle_rad) {
        float cos_a = std::cos(angle_rad);
        float sin_a = std::sin(angle_rad);
        return TransformMatrix2D(cos_a, sin_a, -sin_a, cos_a, 0.0f, 0.0f);
    }

    static TransformMatrix2D skew(float skew_x_rad, float skew_y_rad) {
        return TransformMatrix2D(1.0f, std::tan(skew_y_rad), std::tan(skew_x_rad), 1.0f, 0.0f, 0.0f);
    }

    TransformMatrix2D multiply(const TransformMatrix2D& o) const {
        return TransformMatrix2D(
            a * o.a + c * o.b,
            b * o.a + d * o.b,
            a * o.c + c * o.d,
            b * o.c + d * o.d,
            a * o.e + c * o.f + e,
            b * o.e + d * o.f + f
        );
    }

    Point map_point(const Point& p) const {
        return Point(
            a * p.x + c * p.y + e,
            b * p.x + d * p.y + f
        );
    }

    Point map_point(float px, float py) const {
        return Point(
            a * px + c * py + e,
            b * px + d * py + f
        );
    }

    Rect map_rect(const Rect& r) const {
        Point p1 = map_point(r.x, r.y);
        Point p2 = map_point(r.x + r.width, r.y);
        Point p3 = map_point(r.x, r.y + r.height);
        Point p4 = map_point(r.x + r.width, r.y + r.height);

        float min_x = std::min({p1.x, p2.x, p3.x, p4.x});
        float max_x = std::max({p1.x, p2.x, p3.x, p4.x});
        float min_y = std::min({p1.y, p2.y, p3.y, p4.y});
        float max_y = std::max({p1.y, p2.y, p3.y, p4.y});

        return Rect(min_x, min_y, max_x - min_x, max_y - min_y);
    }

    bool is_identity() const {
        return a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f && e == 0.0f && f == 0.0f;
    }

    float determinant() const {
        return a * d - b * c;
    }

    TransformMatrix2D inverse() const {
        float det = determinant();
        if (std::abs(det) < 1e-6f) return identity();
        float inv_det = 1.0f / det;
        return TransformMatrix2D(
            d * inv_det,
            -b * inv_det,
            -c * inv_det,
            a * inv_det,
            (c * f - d * e) * inv_det,
            (b * e - a * f) * inv_det
        );
    }
};

} // namespace litebird
