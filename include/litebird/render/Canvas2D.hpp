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

enum class FillRule {
    NonZero,
    EvenOdd
};

enum class GradientSpread {
    Pad,
    Reflect,
    Repeat
};

struct ColorStop {
    float offset{0.0f}; // 0.0 to 1.0
    Color color;
};

class CanvasGradient {
public:
    enum class Type { Linear, Radial } type{Type::Linear};
    // Linear coordinates: (x0, y0) -> (x1, y1)
    // Radial coordinates: start circle (x0, y0, r0) -> end circle (x1, y1, r1)
    float x0{0.0f}, y0{0.0f}, x1{0.0f}, y1{0.0f};
    float r0{0.0f}, r1{0.0f};
    GradientSpread spread{GradientSpread::Pad};
    TransformMatrix2D transform{TransformMatrix2D::identity()};
    std::vector<ColorStop> stops;

    void add_color_stop(float offset, const Color& color) {
        stops.push_back({std::clamp(offset, 0.0f, 1.0f), color});
        std::stable_sort(stops.begin(), stops.end(), [](const ColorStop& a, const ColorStop& b) {
            return a.offset < b.offset;
        });
    }

    Color sample_at(float px, float py) const;
    JsValue to_js_value(std::shared_ptr<CanvasGradient> self);
};

class CanvasRenderingContext2D {
public:
    CanvasRenderingContext2D(int width, int height);

    /// The size the canvas was given: what `canvas.width` reads back and
    /// what layout uses.
    int width() const { return m_nominal_width; }
    int height() const { return m_nominal_height; }
    void resize(int w, int h);

    /// False when the canvas was given a size that is over a limit of
    /// RasterBudget. It then has a backing store of one pixel: every drawing
    /// call is accepted and changes nothing, and the canvas paints blank.
    bool has_backing_store() const { return m_width == m_nominal_width && m_height == m_nominal_height; }

    std::shared_ptr<ImageData> image_data() const { return m_image; }

    // State properties (existing public fields kept for source compatibility)
    Color fill_style{Color::black()};
    Color stroke_style{Color::black()};
    float line_width{1.0f};
    std::string global_composite_operation{"source-over"};

    // Extended Canvas 2D state properties
    float global_alpha{1.0f};
    std::string line_cap{"butt"};   // "butt" | "round" | "square"
    std::string line_join{"miter"}; // "miter" | "round" | "bevel"
    float miter_limit{10.0f};
    std::vector<float> line_dash;
    float line_dash_offset{0.0f};

    // Gradients
    std::shared_ptr<CanvasGradient> fill_gradient{nullptr};
    std::shared_ptr<CanvasGradient> stroke_gradient{nullptr};

    std::shared_ptr<CanvasGradient> create_linear_gradient(float x0, float y0, float x1, float y1);
    std::shared_ptr<CanvasGradient> create_radial_gradient(float x0, float y0, float r0, float x1, float y1, float r1);

    // State stack
    void save();
    void restore();

    // Current Transformation Matrix (CTM)
    void translate(float tx, float ty);
    void rotate(float angle_rad);
    void scale(float sx, float sy);
    void transform(float a, float b, float c, float d, float e, float f);
    void set_transform(float a, float b, float c, float d, float e, float f);
    void set_transform(const TransformMatrix2D& matrix);
    void reset_transform();
    const TransformMatrix2D& current_transform() const { return m_ctm; }

    // Dash pattern
    void set_line_dash(const std::vector<float>& segments);
    const std::vector<float>& get_line_dash() const { return line_dash; }

    // Rectangles
    void fill_rect(float x, float y, float w, float h);
    void stroke_rect(float x, float y, float w, float h);
    void clear_rect(float x, float y, float w, float h);

    // Paths
    void begin_path();
    void close_path();
    void move_to(float x, float y);
    void line_to(float x, float y);
    void rect(float x, float y, float w, float h);
    void round_rect(float x, float y, float w, float h, float r);
    void round_rect(float x, float y, float w, float h, float r_tl, float r_tr, float r_br, float r_bl);
    void arc(float cx, float cy, float radius, float start_angle, float end_angle, bool anticlockwise = false);
    void ellipse(float cx, float cy, float rx, float ry, float rotation, float start_angle, float end_angle, bool anticlockwise = false);
    void arc_to(float x1, float y1, float x2, float y2, float radius);
    void quadratic_curve_to(float cpx, float cpy, float x, float y);
    void bezier_curve_to(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y);
    void stroke();
    void fill(FillRule rule = FillRule::NonZero);
    void clip(FillRule rule = FillRule::NonZero);
    bool is_point_in_path(float x, float y, FillRule rule = FillRule::NonZero) const;

    // Pixel manipulation
    std::vector<uint8_t> get_image_data_rgba(int sx, int sy, int sw, int sh) const;
    void put_image_data_rgba(const uint8_t* rgba, int w, int h, int dx, int dy);

    // Image Drawing
    void draw_image(const std::shared_ptr<ImageData>& img, float dx, float dy, float dw = 0.0f, float dh = 0.0f);

    // JS Bridge
    JsValue to_js_value(std::shared_ptr<CanvasRenderingContext2D> self = nullptr);

private:
    static constexpr size_t kMaxStateStackDepth = 256;

    // The size of the backing store. Everything that touches pixels uses
    // these, never the nominal size.
    int m_width{300};
    int m_height{150};
    int m_nominal_width{300};
    int m_nominal_height{150};
    std::shared_ptr<ImageData> m_image;

    TransformMatrix2D m_ctm{TransformMatrix2D::identity()};
    std::vector<uint8_t> m_clip_mask; // Empty = unclipped; otherwise m_width * m_height coverage (0..255)

    struct SavedState {
        Color fill_style;
        Color stroke_style;
        float line_width{1.0f};
        std::string global_composite_operation;
        float global_alpha{1.0f};
        std::string line_cap;
        std::string line_join;
        float miter_limit{10.0f};
        std::vector<float> line_dash;
        float line_dash_offset{0.0f};
        std::shared_ptr<CanvasGradient> fill_gradient;
        std::shared_ptr<CanvasGradient> stroke_gradient;
        TransformMatrix2D ctm;
        std::vector<uint8_t> clip_mask;
    };
    std::vector<SavedState> m_state_stack;

    void allocate(int width, int height);

    struct SubPath {
        std::vector<Point> points; // Stored in device space
        bool closed{false};
    };
    std::vector<SubPath> m_paths;

    void rasterize_paths(
        const std::vector<SubPath>& paths,
        FillRule rule,
        const Color& solid_color,
        const std::shared_ptr<CanvasGradient>& gradient,
        const TransformMatrix2D& grad_inv_ctm
    );
    void rasterize_paths_to_mask(
        const std::vector<SubPath>& paths,
        FillRule rule,
        std::vector<uint8_t>& out_mask
    ) const;
    void stroke_paths(const std::vector<SubPath>& paths);
    void set_pixel_safe(int x, int y, Color c, uint8_t coverage = 255);
};

} // namespace litebird
