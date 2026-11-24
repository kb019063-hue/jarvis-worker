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
    enum class Type { Linear, Radial, Conic } type{Type::Linear};
    // Linear coordinates: (x0, y0) -> (x1, y1)
    // Radial coordinates: start circle (x0, y0, r0) -> end circle (x1, y1, r1)
    // Conic: the centre is (x0, y0) and `start_angle` (radians, from the
    // positive x axis, clockwise as the canvas' y axis grows down) is where
    // `t` is 0. §4.12.5.1's `createConicGradient` is the only way to make one.
    float x0{0.0f}, y0{0.0f}, x1{0.0f}, y1{0.0f};
    float start_angle{0.0f};
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

/// §4.12.5.1's `CanvasPattern`: a tile drawn from an image or a canvas, with
/// a repetition rule. It is not a `CanvasGradient`, so `fillStyle` needs a
/// second kind of paint; `sample_at` answers a device-space point's colour
/// the same way so that the rasteriser need not know which it has.
class CanvasPattern {
public:
    /// "repeat" | "repeat-x" | "repeat-y" | "no-repeat". The empty string is
    /// the `null` a `createPattern` with an unknown repetition returns.
    std::string repetition{"repeat"};
    int width{0};
    int height{0};
    std::vector<uint32_t> pixels; // 0xAARRGGBB, the tile
    /// Whether the source the tile came from was origin-clean; a pattern made
    /// from a tainted canvas taints what it paints.
    bool origin_clean{true};
    /// §4.12.5.1's `CanvasPattern.setTransform`, applied to the tile before it
    /// is sampled. Identity until a page sets one.
    TransformMatrix2D transform{TransformMatrix2D::identity()};
    Color sample_at(int px, int py) const;
};

class CanvasRenderingContext2D {
public:
    CanvasRenderingContext2D(int width, int height);

    /// The size the canvas was given: what `canvas.width` reads back and
    /// what layout uses.
    int width() const { return m_nominal_width; }
    int height() const { return m_nominal_height; }
    void resize(int w, int h);

    /// Dimensions of a glyph-anchored text run, as CSS pixels. Ink boxes are
    /// relative to the pen origin (the alignment point is not applied); the
    /// font boxes come from the loaded font's em metrics.
    struct TextMetrics {
        float width{0.0f};
        float actual_bounding_box_left{0.0f};
        float actual_bounding_box_right{0.0f};
        float actual_bounding_box_ascent{0.0f};
        float actual_bounding_box_descent{0.0f};
        float font_bounding_box_ascent{0.0f};
        float font_bounding_box_descent{0.0f};
        float em_height_ascent{0.0f};
        float em_height_descent{0.0f};
        float hanging_baseline{0.0f};
        float ideographic_baseline{0.0f};
    };

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

    // Text state (CSS/Canvas font shorthand; invalid assignments are ignored
    // by the `font` setter, keep the value valid at all times).
    std::string font{"10px sans-serif"};
    // "start" | "end" | "left" | "right" | "center" (direction is not
    // implemented: "start" is "left" and "end" is "right").
    std::string text_align{"start"};
    // "top" | "hanging" | "middle" | "alphabetic" | "ideographic" | "bottom"
    std::string text_baseline{"alphabetic"};

    // Gradients
    std::shared_ptr<CanvasGradient> fill_gradient{nullptr};
    std::shared_ptr<CanvasGradient> stroke_gradient{nullptr};
    // Patterns. A style is one or the other; setting `fillStyle` to a colour
    // clears both, and the rasteriser prefers the gradient when both are set.
    std::shared_ptr<CanvasPattern> fill_pattern{nullptr};
    std::shared_ptr<CanvasPattern> stroke_pattern{nullptr};

    std::shared_ptr<CanvasGradient> create_linear_gradient(float x0, float y0, float x1, float y1);
    std::shared_ptr<CanvasGradient> create_radial_gradient(float x0, float y0, float r0, float x1, float y1, float r1);
    /// §4.12.5.1's `createConicGradient(startAngle, x, y)`.
    std::shared_ptr<CanvasGradient> create_conic_gradient(float start_angle, float x, float y);
    /// §4.12.5.1's `createPattern(image, repetition)`. Null when the image is
    /// one this engine cannot read, or when `repetition` is not one of the
    /// four strings — which the binding turns into the `null` a page sees.
    std::shared_ptr<CanvasPattern> create_pattern(const std::shared_ptr<ImageData>& image,
                                                 const std::string& repetition);

    // State stack
    void save();
    void restore();
    /// HTML §4.12.5.1's `reset()`: the drawing state back to its initial
    /// values, the current default path emptied, the transformation matrix
    /// identity, the clip region empty, and the state stack emptied. It is
    /// not `restore()`, which puts back the state that `save()` recorded:
    /// `reset()` ignores what was saved.
    void reset();

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
    /// Whether `(x, y)` is on the stroke of the current default path: within
    /// half the line width of one of its segments, which is the region
    /// `stroke()` would ink. The same device-space geometry `is_point_in_path`
    /// walks, so the two answer consistently for one path.
    bool is_point_in_stroke(float x, float y) const;
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

    // Text drawing. `max_width` <= 0 means "not given"; when the natural
    // advance exceeds it, the run is condensed horizontally (glyphs are
    // drawn narrower, not re-spaced).
    void fill_text(const std::string& text, float x, float y, float max_width = -1.0f);
    void stroke_text(const std::string& text, float x, float y, float max_width = -1.0f);
    TextMetrics measure_text(const std::string& text) const;

    // Pixel manipulation
    std::vector<uint8_t> get_image_data_rgba(int sx, int sy, int sw, int sh) const;
    void put_image_data_rgba(const uint8_t* rgba, int w, int h, int dx, int dy);

    // Image Drawing
    void draw_image(const std::shared_ptr<ImageData>& img, float dx, float dy, float dw = 0.0f, float dh = 0.0f);

    /// True while every pixel the bitmap holds came from a source that is not
    /// known to be `CORS-same-origin` (HTML §4.12.5.1, "origin-clean flag").
    /// A canvas starts origin-clean; `drawImage` of a source that is not
    /// origin-clean sets it, and nothing clears it again except resizing the
    /// canvas, which replaces the bitmap. `toDataURL()`/`toBlob()` must refuse
    /// a bitmap that is not origin-clean, because what they hand back is the
    /// pixels themselves.
    bool origin_clean() const { return m_origin_clean; }
    void set_origin_clean(bool clean) { m_origin_clean = clean; }

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

    /// HTML §4.12.5.1's origin-clean flag. Starts true; `drawImage` of a
    /// source that is not known to be CORS-same-origin clears it. Resizing
    /// the canvas replaces the bitmap, and a new bitmap is a clean one.
    bool m_origin_clean{true};

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
        std::string font;
        std::string text_align;
        std::string text_baseline;
        std::shared_ptr<CanvasGradient> fill_gradient;
        std::shared_ptr<CanvasGradient> stroke_gradient;
        TransformMatrix2D ctm;
        std::vector<uint8_t> clip_mask;
    };
    std::vector<SavedState> m_state_stack;

    /// A glyph run rasterised into a tight coverage buffer, in CSS pixels at
    /// the uncondensed size. `condense` is <= 1 when `max_width` requested it.
    struct TextLayout {
        std::vector<uint8_t> coverage; // width * height, coverage [0..255]
        int width{0};
        int height{0};
        int baseline_px{0};   // row of the alphabetic baseline inside the buffer
        float pad_x{2.0f};    // buffer column of the pen origin
        float ascent{0.0f};   // px above baseline
        float descent{0.0f};  // px below baseline
        float advance{0.0f};  // natural (uncondensed) advance, px
        float condense{1.0f}; // horizontal scale applied at draw time
        float ink_left{0.0f};   // leftmost ink, px relative to the pen origin
        float ink_right{0.0f};  // rightmost ink, px relative to the pen origin
        float ink_ascent{0.0f}; // highest ink, px above the baseline
        float ink_descent{0.0f};// lowest ink, px below the baseline
    };
    /// Rasterises `text` with the current `font` state. `max_width` <= 0
    /// disables horizontal condensing. Never allocates more than kMaxText*px.
    TextLayout layout_text(const std::string& text, float max_width) const;
    /// Draws `coverage` into the backing store through the CTM, mapping each
    /// device pixel back into the source with nearest/max sampling.
    void blit_text(const TextLayout& layout, float dest_x, float dest_y, bool stroke);
    /// Shared body of fill_text and stroke_text: alignment and baseline
    /// resolution, then blit_text. `max_width` < 0 means "not given".
    void draw_text_run(const std::string& text, float x, float y, float max_width, bool stroke);

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
