#pragma once

#include "litebird/render/ImageDecoder.hpp"
#include "litebird/common/Color.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/js/JsValue.hpp"
#include <vector>
#include <memory>
#include <string>

namespace litebird {

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
