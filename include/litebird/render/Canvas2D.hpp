#pragma once

#include "litebird/render/ImageDecoder.hpp"
#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/js/JsValue.hpp"
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

namespace litebird {

struct ColorStop {
    float offset{0.0f}; // 0.0 to 1.0
    Color color;
};

class CanvasGradient {
public:
    enum class Type { Linear, Radial } type{Type::Linear};
    // Linear coordinates
    float x0{0.0f}, y0{0.0f}, x1{0.0f}, y1{0.0f};
    // Radial coordinates
    float r0{0.0f}, r1{0.0f};
    std::vector<ColorStop> stops;

    void add_color_stop(float offset, const Color& color) {
        stops.push_back({std::clamp(offset, 0.0f, 1.0f), color});
        std::sort(stops.begin(), stops.end(), [](const ColorStop& a, const ColorStop& b) {
            return a.offset < b.offset;
        });
    }

    Color sample_at(float px, float py) const;
    JsValue to_js_value(std::shared_ptr<CanvasGradient> self);
};

class CanvasRenderingContext2D {
public:
    CanvasRenderingContext2D(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    void resize(int w, int h);

    std::shared_ptr<ImageData> image_data() const { return m_image; }

    // State properties
    Color fill_style{Color::black()};
    Color stroke_style{Color::black()};
    float line_width{1.0f};
    std::string global_composite_operation{"source-over"};

    // Gradients
    std::shared_ptr<CanvasGradient> fill_gradient{nullptr};
    std::shared_ptr<CanvasGradient> stroke_gradient{nullptr};

    std::shared_ptr<CanvasGradient> create_linear_gradient(float x0, float y0, float x1, float y1);
    std::shared_ptr<CanvasGradient> create_radial_gradient(float x0, float y0, float r0, float x1, float y1, float r1);

    // Rectangles
    void fill_rect(float x, float y, float w, float h);
    void stroke_rect(float x, float y, float w, float h);
    void clear_rect(float x, float y, float w, float h);

    // Paths
    void begin_path();
    void close_path();
    void move_to(float x, float y);
    void line_to(float x, float y);
    void arc(float cx, float cy, float radius, float start_angle, float end_angle);
    void quadratic_curve_to(float cpx, float cpy, float x, float y);
    void bezier_curve_to(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);
    void stroke();
    void fill();

    // Image Drawing
    void draw_image(const std::shared_ptr<ImageData>& img, float dx, float dy, float dw = 0.0f, float dh = 0.0f);

    // JS Bridge
    JsValue to_js_value(std::shared_ptr<CanvasRenderingContext2D> self = nullptr);

private:
    int m_width{300};
    int m_height{150};
    std::shared_ptr<ImageData> m_image;

    struct SubPath {
        std::vector<Point> points;
        bool closed{false};
    };
    std::vector<SubPath> m_paths;

    void draw_line(int x0, int y0, int x1, int y1, Color c, int thickness);
    void set_pixel_safe(int x, int y, Color c);
};

} // namespace litebird
