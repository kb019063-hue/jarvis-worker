#include "litebird/render/SvgRenderer.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/RasterBudget.hpp"
#include "litebird/common/Geometry.hpp"
#include "litebird/common/StackLimit.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace litebird {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr int kMaxUseDepth = 16;
constexpr int kMaxGradientHrefDepth = 8;

std::string to_lower_ascii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return out;
}

std::string_view trim_wsp(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return sv;
}

bool get_attr_ci(const std::shared_ptr<Element>& el, std::string_view name, std::string& out) {
    if (!el) return false;
    std::string exact(name);
    if (el->has_attribute(exact)) {
        out = el->get_attribute(exact);
        return true;
    }
    // SVG is parsed with the case it was written in — `startOffset`,
    // `gradientUnits`, `viewBox` — so a caller asking by another case has to
    // find it. The old fallback compared the lowercased query with the query
    // itself (`lower != exact`) and so could never fire.
    const std::string lower = to_lower_ascii(name);
    if (el->has_attribute(lower)) {
        out = el->get_attribute(lower);
        return true;
    }
    for (const auto& attr : el->attributes()) {
        if (to_lower_ascii(attr.name) == lower) {
            out = attr.value;
            return true;
        }
    }
    return false;
}

bool get_href_attr(const std::shared_ptr<Element>& el, std::string& out) {
    if (get_attr_ci(el, "href", out)) return true;
    if (get_attr_ci(el, "xlink:href", out)) return true;
    return false;
}

std::string extract_fragment_id(std::string_view ref) {
    ref = trim_wsp(ref);
    if (ref.starts_with("#")) {
        return std::string(ref.substr(1));
    }
    return {};
}

// Parses any CSS Color 4/5 color (#hex, rgb/rgba, hsl/hsla, hwb, lab/lch,
// oklab/oklch, color(), color-mix(), relative colors, light-dark(), named colors).
bool parse_svg_color(std::string_view raw, Color& out) {
    return Color::try_parse(raw, out);
}

bool parse_svg_color(std::string_view raw, Color& out, const Color& current_color) {
    return Color::try_parse(raw, out, current_color);
}

bool parse_float_value(std::string_view raw, float& out, float pct_ref = 100.0f) {
    std::string_view s = trim_wsp(raw);
    if (s.empty()) return false;
    std::string tmp(s);
    char* end = nullptr;
    const double val = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str() || !std::isfinite(val)) return false;

    std::string_view unit = trim_wsp(std::string_view(end));
    std::string u = to_lower_ascii(unit);
    double scale = 1.0;
    if (u == "%") {
        out = static_cast<float>(val * 0.01 * static_cast<double>(pct_ref));
        return true;
    }
    if (u == "pt") scale = 96.0 / 72.0;
    else if (u == "pc") scale = 16.0;
    else if (u == "in") scale = 96.0;
    else if (u == "cm") scale = 96.0 / 2.54;
    else if (u == "mm") scale = 96.0 / 25.4;
    else if (u == "em" || u == "rem") scale = 16.0;
    out = static_cast<float>(val * scale);
    return true;
}

bool parse_length_or_pct_unit(std::string_view raw, float& out, float ref, bool& is_pct) {
    std::string_view s = trim_wsp(raw);
    is_pct = false;
    if (s.empty()) return false;
    if (s.ends_with("%")) {
        is_pct = true;
        return parse_float_value(s, out, ref);
    }
    return parse_float_value(s, out, ref);
}

// Parses a list of SVG floating-point numbers separated by whitespace/commas (supporting exponents).
std::vector<float> parse_number_list(std::string_view str) {
    std::vector<float> nums;
    size_t i = 0;
    const size_t n = str.size();
    while (i < n) {
        while (i < n && (std::isspace(static_cast<unsigned char>(str[i])) || str[i] == ',')) ++i;
        if (i >= n) break;

        size_t start = i;
        if (str[i] == '+' || str[i] == '-') ++i;
        bool has_digits = false;
        while (i < n && std::isdigit(static_cast<unsigned char>(str[i]))) { has_digits = true; ++i; }
        if (i < n && str[i] == '.') {
            ++i;
            while (i < n && std::isdigit(static_cast<unsigned char>(str[i]))) { has_digits = true; ++i; }
        }
        if (has_digits && i < n && (str[i] == 'e' || str[i] == 'E')) {
            size_t exp_pos = i++;
            if (i < n && (str[i] == '+' || str[i] == '-')) ++i;
            bool exp_digits = false;
            while (i < n && std::isdigit(static_cast<unsigned char>(str[i]))) { exp_digits = true; ++i; }
            if (!exp_digits) i = exp_pos;
        }
        if (has_digits) {
            std::string tok(str.substr(start, i - start));
            const double v = std::strtod(tok.c_str(), nullptr);
            if (std::isfinite(v)) nums.push_back(static_cast<float>(v));
        } else {
            ++i;
        }
    }
    return nums;
}

TransformMatrix2D parse_svg_transform(std::string_view str) {
    TransformMatrix2D result = TransformMatrix2D::identity();
    size_t i = 0;
    const size_t n = str.size();
    while (i < n) {
        while (i < n && (std::isspace(static_cast<unsigned char>(str[i])) || str[i] == ',')) ++i;
        if (i >= n) break;

        const size_t name_start = i;
        while (i < n && std::isalpha(static_cast<unsigned char>(str[i]))) ++i;
        std::string name = to_lower_ascii(str.substr(name_start, i - name_start));

        while (i < n && std::isspace(static_cast<unsigned char>(str[i]))) ++i;
        if (i >= n || str[i] != '(') break;
        ++i;
        const size_t args_start = i;
        while (i < n && str[i] != ')') ++i;
        std::string_view args_sv = str.substr(args_start, i - args_start);
        if (i < n && str[i] == ')') ++i;

        auto args = parse_number_list(args_sv);
        TransformMatrix2D m = TransformMatrix2D::identity();

        if (name == "matrix" && args.size() >= 6) {
            m = TransformMatrix2D(args[0], args[1], args[2], args[3], args[4], args[5]);
        } else if (name == "translate" && !args.empty()) {
            const float tx = args[0];
            const float ty = (args.size() >= 2) ? args[1] : 0.0f;
            m = TransformMatrix2D::translation(tx, ty);
        } else if (name == "scale" && !args.empty()) {
            const float sx = args[0];
            const float sy = (args.size() >= 2) ? args[1] : sx;
            m = TransformMatrix2D::scale(sx, sy);
        } else if (name == "rotate" && !args.empty()) {
            const float rad = static_cast<float>(args[0] * kPi / 180.0);
            if (args.size() >= 3) {
                m = TransformMatrix2D::translation(args[1], args[2])
                        .multiply(TransformMatrix2D::rotation(rad))
                        .multiply(TransformMatrix2D::translation(-args[1], -args[2]));
            } else {
                m = TransformMatrix2D::rotation(rad);
            }
        } else if (name == "skewx" && !args.empty()) {
            const float rad = static_cast<float>(args[0] * kPi / 180.0);
            m = TransformMatrix2D::skew(rad, 0.0f);
        } else if (name == "skewy" && !args.empty()) {
            const float rad = static_cast<float>(args[0] * kPi / 180.0);
            m = TransformMatrix2D::skew(0.0f, rad);
        }

        result = result.multiply(m);
    }
    return result;
}

struct PaintServer {
    enum class Kind { None, Solid, CurrentColor, Url } kind{Kind::None};
    Color color{Color::black()};
    std::string url_id;
    bool has_fallback{false};
    Color fallback{Color::black()};
};

bool parse_url_ref(std::string_view raw, std::string& out_id, std::string_view& remainder) {
    std::string_view s = trim_wsp(raw);
    if (s.size() < 6) return false;
    std::string prefix = to_lower_ascii(s.substr(0, 4));
    if (prefix != "url(") return false;
    const size_t close = s.find(')');
    if (close == std::string_view::npos) return false;
    std::string_view inner = trim_wsp(s.substr(4, close - 4));
    if (inner.size() >= 2 && ((inner.front() == '"' && inner.back() == '"') ||
                              (inner.front() == '\'' && inner.back() == '\''))) {
        inner = trim_wsp(inner.substr(1, inner.size() - 2));
    }
    if (!inner.starts_with("#")) return false;
    out_id = std::string(inner.substr(1));
    remainder = trim_wsp(s.substr(close + 1));
    return !out_id.empty();
}

bool parse_paint_server(std::string_view raw, PaintServer& out, const Color& current_color) {
    std::string_view s = trim_wsp(raw);
    if (s.empty()) return false;
    std::string lower = to_lower_ascii(s);
    if (lower == "none") {
        out.kind = PaintServer::Kind::None;
        return true;
    }
    if (lower == "currentcolor") {
        out.kind = PaintServer::Kind::CurrentColor;
        return true;
    }
    std::string id;
    std::string_view rem;
    if (parse_url_ref(s, id, rem)) {
        out.kind = PaintServer::Kind::Url;
        out.url_id = std::move(id);
        out.has_fallback = false;
        if (!rem.empty() && to_lower_ascii(rem) != "none") {
            Color fb;
            if (parse_svg_color(rem, fb, current_color)) {
                out.has_fallback = true;
                out.fallback = fb;
            }
        }
        return true;
    }
    Color c;
    if (parse_svg_color(s, c, current_color)) {
        out.kind = PaintServer::Kind::Solid;
        out.color = c;
        return true;
    }
    return false;
}

struct SvgStyle {
    Color color{Color::black()};
    PaintServer fill{PaintServer::Kind::Solid, Color::black(), {}, false, Color::black()};
    PaintServer stroke{PaintServer::Kind::None, Color::transparent(), {}, false, Color::black()};
    float stroke_width{1.0f};
    std::string stroke_linecap{"butt"};
    std::string stroke_linejoin{"miter"};
    float stroke_miterlimit{4.0f};
    std::vector<float> stroke_dasharray;
    float stroke_dashoffset{0.0f};
    float opacity{1.0f}; // Effective cumulative opacity (multiplied down <g>)
    float fill_opacity{1.0f};
    float stroke_opacity{1.0f};
    FillRule fill_rule{FillRule::NonZero};
    float font_size{16.0f};
    int font_weight{400};
    std::string font_family{"sans-serif"};
    std::string font_style{"normal"};
    std::string text_anchor{"start"};
    bool display_none{false};
    bool visible{true};
};

void apply_style_property(SvgStyle& style, std::string_view prop_raw, std::string_view val_raw, float ref_size) {
    std::string prop = to_lower_ascii(trim_wsp(prop_raw));
    std::string_view val = trim_wsp(val_raw);
    if (prop.empty() || val.empty()) return;

    // Strip trailing !important if present
    if (val.size() >= 10) {
        std::string lower_tail = to_lower_ascii(val.substr(val.size() - 10));
        if (lower_tail == "!important") {
            val = trim_wsp(val.substr(0, val.size() - 10));
        }
    }
    std::string val_lower = to_lower_ascii(val);
    if (val_lower == "inherit") return;

    if (prop == "color") {
        Color c;
        if (parse_svg_color(val, c, style.color)) style.color = c;
    } else if (prop == "fill") {
        parse_paint_server(val, style.fill, style.color);
    } else if (prop == "stroke") {
        parse_paint_server(val, style.stroke, style.color);
    } else if (prop == "stroke-width") {
        float w = 1.0f;
        if (parse_float_value(val, w, ref_size) && w >= 0.0f) style.stroke_width = w;
    } else if (prop == "stroke-linecap") {
        if (val_lower == "butt" || val_lower == "round" || val_lower == "square") {
            style.stroke_linecap = val_lower;
        }
    } else if (prop == "stroke-linejoin") {
        if (val_lower == "miter" || val_lower == "round" || val_lower == "bevel") {
            style.stroke_linejoin = val_lower;
        }
    } else if (prop == "stroke-miterlimit") {
        float m = 4.0f;
        if (parse_float_value(val, m, 1.0f) && m >= 1.0f) style.stroke_miterlimit = m;
    } else if (prop == "stroke-dasharray") {
        if (val_lower == "none") {
            style.stroke_dasharray.clear();
        } else {
            auto nums = parse_number_list(val);
            bool ok = !nums.empty();
            for (float d : nums) {
                if (d < 0.0f) ok = false;
            }
            if (ok) style.stroke_dasharray = std::move(nums);
        }
    } else if (prop == "stroke-dashoffset") {
        float off = 0.0f;
        if (parse_float_value(val, off, ref_size)) style.stroke_dashoffset = off;
    } else if (prop == "opacity") {
        float op = 1.0f;
        if (parse_float_value(val, op, 1.0f)) {
            style.opacity *= std::clamp(op, 0.0f, 1.0f);
        }
    } else if (prop == "fill-opacity") {
        float op = 1.0f;
        if (parse_float_value(val, op, 1.0f)) style.fill_opacity = std::clamp(op, 0.0f, 1.0f);
    } else if (prop == "stroke-opacity") {
        float op = 1.0f;
        if (parse_float_value(val, op, 1.0f)) style.stroke_opacity = std::clamp(op, 0.0f, 1.0f);
    } else if (prop == "fill-rule") {
        if (val_lower == "evenodd") style.fill_rule = FillRule::EvenOdd;
        else if (val_lower == "nonzero") style.fill_rule = FillRule::NonZero;
    } else if (prop == "font-size") {
        float size = 0.0f;
        if (val_lower.ends_with("em")) {
            if (parse_float_value(val.substr(0, val.size() - 2), size)) size *= style.font_size;
        } else if (val_lower.ends_with("%")) {
            parse_float_value(val, size, style.font_size);
        } else {
            parse_float_value(val, size);
        }
        if (size > 0.0f && size <= 160.0f) style.font_size = size;
    } else if (prop == "font-family") {
        style.font_family = std::string(val);
    } else if (prop == "font-weight") {
        if (val_lower == "normal") style.font_weight = 400;
        else if (val_lower == "bold" || val_lower == "bolder") style.font_weight = 700;
        else if (val_lower == "lighter") style.font_weight = 300;
        else {
            float weight = 0.0f;
            if (parse_float_value(val, weight) && weight >= 1.0f && weight <= 1000.0f)
                style.font_weight = static_cast<int>(weight);
        }
    } else if (prop == "font-style") {
        if (val_lower == "normal" || val_lower == "italic" || val_lower == "oblique")
            style.font_style = val_lower;
    } else if (prop == "text-anchor") {
        if (val_lower == "start" || val_lower == "middle" || val_lower == "end")
            style.text_anchor = val_lower;
    } else if (prop == "display") {
        if (val_lower == "none") style.display_none = true;
    } else if (prop == "visibility") {
        if (val_lower == "hidden" || val_lower == "collapse") style.visible = false;
        else if (val_lower == "visible") style.visible = true;
    }
}

void parse_inline_style_attr(SvgStyle& style, std::string_view css_text, float ref_size) {
    size_t i = 0;
    const size_t n = css_text.size();
    while (i < n) {
        const size_t semi = css_text.find(';', i);
        const std::string_view decl = (semi == std::string_view::npos)
            ? css_text.substr(i)
            : css_text.substr(i, semi - i);
        const size_t colon = decl.find(':');
        if (colon != std::string_view::npos) {
            apply_style_property(style, decl.substr(0, colon), decl.substr(colon + 1), ref_size);
        }
        if (semi == std::string_view::npos) break;
        i = semi + 1;
    }
}

SvgStyle resolve_element_style(
    const std::shared_ptr<Element>& el,
    const SvgStyle& parent_style,
    float ref_size
) {
    SvgStyle style = parent_style;
    static constexpr const char* kPresAttrs[] = {
        "color", "fill", "stroke", "stroke-width", "stroke-linecap",
        "stroke-linejoin", "stroke-miterlimit", "stroke-dasharray",
        "stroke-dashoffset", "opacity", "fill-opacity", "stroke-opacity",
        "fill-rule", "font-size", "font-family", "font-weight", "font-style",
        "text-anchor", "display", "visibility"
    };
    std::string attr_val;
    for (const char* name : kPresAttrs) {
        if (get_attr_ci(el, name, attr_val)) {
            apply_style_property(style, name, attr_val, ref_size);
        }
    }
    if (get_attr_ci(el, "style", attr_val)) {
        parse_inline_style_attr(style, attr_val, ref_size);
    }
    return style;
}

struct ViewBox {
    float min_x{0.0f};
    float min_y{0.0f};
    float width{0.0f};
    float height{0.0f};
    bool valid{false};
};

ViewBox parse_viewbox(std::string_view str) {
    ViewBox vb;
    auto nums = parse_number_list(str);
    if (nums.size() >= 4 && nums[2] > 0.0f && nums[3] > 0.0f) {
        vb.min_x = nums[0];
        vb.min_y = nums[1];
        vb.width = nums[2];
        vb.height = nums[3];
        vb.valid = true;
    }
    return vb;
}

TransformMatrix2D compute_viewbox_matrix(
    const ViewBox& vb,
    float dest_w,
    float dest_h,
    std::string_view preserve_aspect_ratio
) {
    if (!vb.valid || vb.width <= 0.0f || vb.height <= 0.0f || dest_w <= 0.0f || dest_h <= 0.0f) {
        return TransformMatrix2D::identity();
    }

    std::string par = to_lower_ascii(trim_wsp(preserve_aspect_ratio));
    if (par.empty()) par = "xmidymid meet";

    std::stringstream ss(par);
    std::string align, meet_or_slice;
    ss >> align >> meet_or_slice;
    if (align == "defer") {
        align = meet_or_slice;
        meet_or_slice.clear();
        ss >> meet_or_slice;
    }

    if (align == "none") {
        const float sx = dest_w / vb.width;
        const float sy = dest_h / vb.height;
        return TransformMatrix2D::scale(sx, sy)
            .multiply(TransformMatrix2D::translation(-vb.min_x, -vb.min_y));
    }

    const float sx_raw = dest_w / vb.width;
    const float sy_raw = dest_h / vb.height;
    const float s = (meet_or_slice == "slice") ? std::max(sx_raw, sy_raw) : std::min(sx_raw, sy_raw);

    const float extra_x = dest_w - vb.width * s;
    const float extra_y = dest_h - vb.height * s;

    float tx = extra_x * 0.5f;
    float ty = extra_y * 0.5f;
    if (align.find("xmin") != std::string::npos) tx = 0.0f;
    else if (align.find("xmax") != std::string::npos) tx = extra_x;

    if (align.find("ymin") != std::string::npos) ty = 0.0f;
    else if (align.find("ymax") != std::string::npos) ty = extra_y;

    return TransformMatrix2D::translation(tx, ty)
        .multiply(TransformMatrix2D::scale(s, s))
        .multiply(TransformMatrix2D::translation(-vb.min_x, -vb.min_y));
}

struct BBox {
    float min_x{std::numeric_limits<float>::infinity()};
    float min_y{std::numeric_limits<float>::infinity()};
    float max_x{-std::numeric_limits<float>::infinity()};
    float max_y{-std::numeric_limits<float>::infinity()};

    void include(float x, float y) {
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }

    bool valid() const {
        return std::isfinite(min_x) && std::isfinite(min_y) &&
               std::isfinite(max_x) && std::isfinite(max_y);
    }
    float width() const { return valid() ? std::max(0.0f, max_x - min_x) : 0.0f; }
    float height() const { return valid() ? std::max(0.0f, max_y - min_y) : 0.0f; }
};

// A path sink that keeps the polyline instead of drawing it: <textPath> needs
// the geometry, not the pixels. A curve is sampled densely enough that a glyph
// origin along it lands where the eye expects, and at a fixed step so a test
// can predict the position without reading the renderer's mind.
class PathFlattener {
public:
    void move_to(float x, float y) {
        m_points.clear();
        m_points.push_back({x, y});
        m_cur_x = m_start_x = x;
        m_cur_y = m_start_y = y;
    }

    void line_to(float x, float y) {
        m_points.push_back({x, y});
        m_cur_x = x;
        m_cur_y = y;
    }

    void bezier_curve_to(float x1, float y1, float x2, float y2, float x, float y) {
        const float x0 = m_cur_x, y0 = m_cur_y;
        constexpr int kSteps = 24;
        for (int i = 1; i <= kSteps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kSteps);
            const float u = 1.0f - t;
            m_points.push_back({u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x,
                                u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y});
        }
        m_cur_x = x;
        m_cur_y = y;
    }

    void quadratic_curve_to(float x1, float y1, float x, float y) {
        const float x0 = m_cur_x, y0 = m_cur_y;
        constexpr int kSteps = 16;
        for (int i = 1; i <= kSteps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kSteps);
            const float u = 1.0f - t;
            m_points.push_back({u * u * x0 + 2 * u * t * x1 + t * t * x,
                                u * u * y0 + 2 * u * t * y1 + t * t * y});
        }
        m_cur_x = x;
        m_cur_y = y;
    }

    void close_path() { line_to(m_start_x, m_start_y); }

    const std::vector<std::array<float, 2>>& points() const { return m_points; }

private:
    std::vector<std::array<float, 2>> m_points;
    float m_cur_x{0.0f}, m_cur_y{0.0f}, m_start_x{0.0f}, m_start_y{0.0f};
};

// SVG 1.1 Implementation Notes F.6.5 & F.6.6: endpoint-to-center elliptical arc conversion.
template <typename Sink>
void emit_svg_arc(
    Sink& ctx,
    BBox& bbox,
    double x1, double y1,
    double rx, double ry,
    double x_rot_deg,
    bool large_arc,
    bool sweep,
    double x2, double y2
) {
    if (std::abs(x1 - x2) < 1e-7 && std::abs(y1 - y2) < 1e-7) return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-7 || ry < 1e-7) {
        ctx.line_to(static_cast<float>(x2), static_cast<float>(y2));
        bbox.include(static_cast<float>(x2), static_cast<float>(y2));
        return;
    }

    const double phi = x_rot_deg * kPi / 180.0;
    const double cos_phi = std::cos(phi);
    const double sin_phi = std::sin(phi);

    // Step 1: transform midpoint to prime frame
    const double dx2 = (x1 - x2) * 0.5;
    const double dy2 = (y1 - y2) * 0.5;
    const double x1p = cos_phi * dx2 + sin_phi * dy2;
    const double y1p = -sin_phi * dx2 + cos_phi * dy2;

    // Step 2: ensure radii are large enough
    const double x1p2 = x1p * x1p;
    const double y1p2 = y1p * y1p;
    double rx2 = rx * rx;
    double ry2 = ry * ry;
    const double lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0) {
        const double s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    // Step 3: compute center in prime frame
    const double denom = rx2 * y1p2 + ry2 * x1p2;
    double sq = 0.0;
    if (denom > 1e-12) {
        const double num = std::max(0.0, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2);
        sq = std::sqrt(num / denom);
    }
    if (large_arc == sweep) sq = -sq;

    const double cxp = sq * (rx * y1p / ry);
    const double cyp = sq * (-ry * x1p / rx);

    // Step 4: compute center in user space
    const double cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) * 0.5;
    const double cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) * 0.5;

    // Step 5: compute start angle and sweep angle
    auto vec_angle = [](double ux, double uy, double vx, double vy) -> double {
        const double dot = ux * vx + uy * vy;
        const double len = std::hypot(ux, uy) * std::hypot(vx, vy);
        double ang = (len > 1e-12) ? std::acos(std::clamp(dot / len, -1.0, 1.0)) : 0.0;
        if (ux * vy - uy * vx < 0.0) ang = -ang;
        return ang;
    };

    const double vx1 = (x1p - cxp) / rx;
    const double vy1 = (y1p - cyp) / ry;
    const double vx2 = (-x1p - cxp) / rx;
    const double vy2 = (-y1p - cyp) / ry;

    const double theta1 = vec_angle(1.0, 0.0, vx1, vy1);
    double dtheta = vec_angle(vx1, vy1, vx2, vy2);
    if (!sweep && dtheta > 0.0) dtheta -= kTwoPi;
    else if (sweep && dtheta < 0.0) dtheta += kTwoPi;

    const int segs = std::clamp(static_cast<int>(std::ceil(std::abs(dtheta) / (kPi / 18.0))), 4, 72);
    for (int i = 1; i <= segs; ++i) {
        const double t = theta1 + dtheta * (static_cast<double>(i) / static_cast<double>(segs));
        const double ct = std::cos(t);
        const double st = std::sin(t);
        const double px = cx + cos_phi * rx * ct - sin_phi * ry * st;
        const double py = cy + sin_phi * rx * ct + cos_phi * ry * st;
        ctx.line_to(static_cast<float>(px), static_cast<float>(py));
    }

    // The box is the disc's, not the flattening's: an ellipse's extreme points
    // are at four known angles, and the arc holds its box where the sweep
    // covers one of them. Including only the sampled points left the box 0.5%
    // short of a half circle's rim, which the curve-bound tests measured.
    auto on_arc = [&](double theta, float& px, float& py) {
        const double ct = std::cos(theta);
        const double st = std::sin(theta);
        px = static_cast<float>(cx + cos_phi * rx * ct - sin_phi * ry * st);
        py = static_cast<float>(cy + sin_phi * rx * ct + cos_phi * ry * st);
    };
    auto sweep_contains = [&](double theta) {
        // Normalise the offset from theta1 into the arc's own direction.
        double offset = theta - theta1;
        while (offset > kTwoPi) offset -= kTwoPi;
        while (offset < -kTwoPi) offset += kTwoPi;
        if (dtheta >= 0.0) return offset >= 0.0 && offset <= dtheta;
        return offset <= 0.0 && offset >= dtheta;
    };
    // x is extreme where d/dtheta (cos_phi*rx*cos t - sin_phi*ry*sin t) is
    // zero, which is t = -phi (mod pi); y's is t = pi/2 - phi (mod pi).
    const double axis_phi = std::atan2(sin_phi, cos_phi);
    for (const double base : {0.0, kPi * 0.5}) {
        for (int half = 0; half < 2; ++half) {
            const double candidate = base - axis_phi + static_cast<double>(half) * kPi;
            if (!sweep_contains(candidate)) continue;
            float px = 0.0f, py = 0.0f;
            on_arc(candidate, px, py);
            bbox.include(px, py);
        }
    }
    for (int i = 0; i <= segs; ++i) {
        const double t = theta1 + dtheta * (static_cast<double>(i) / static_cast<double>(segs));
        float px = 0.0f, py = 0.0f;
        on_arc(t, px, py);
        bbox.include(px, py);
    }
}

// A cubic's control points are not on the cubic, so the box of the four points
// is not the box of the curve: `C0 100 100 100 100 0` reaches y=75, not 100.
// The extremes are where the derivative is zero, solved per axis. SVG 2 §8.10's
// box is the outer bound of the drawn curve, and this is the answer a browser
// gives (measured: Chromium says 75 for that curve, 50 for `Q50 100 100 0`).
void include_cubic(BBox& bbox, float x0, float y0, float x1, float y1,
                   float x2, float y2, float x3, float y3) {
    bbox.include(x0, y0);
    bbox.include(x3, y3);
    auto value_at = [](float p0, float p1, float p2, float p3, float t) {
        const float u = 1.0f - t;
        return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
    };
    // B'(t) = 3a t^2 + 2b t + c, with a = -p0 + 3p1 - 3p2 + p3 and so on.
    auto roots = [](float p0, float p1, float p2, float p3, float* out) {
        int count = 0;
        const float a = -p0 + 3.0f * p1 - 3.0f * p2 + p3;
        const float b = 2.0f * (p0 - 2.0f * p1 + p2);
        const float c = p1 - p0;
        auto take = [&](float t) { if (t > 0.0f && t < 1.0f) out[count++] = t; };
        if (std::abs(a) < 1e-6f) {
            if (std::abs(b) > 1e-6f) take(-c / b);
            return count;
        }
        const float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) return count;
        const float root = std::sqrt(disc);
        take((-b + root) / (2.0f * a));
        take((-b - root) / (2.0f * a));
        return count;
    };
    float ts[4];
    for (int i = 0, n = roots(x0, x1, x2, x3, ts); i < n; ++i) {
        bbox.include(value_at(x0, x1, x2, x3, ts[i]), value_at(y0, y1, y2, y3, ts[i]));
    }
    for (int i = 0, n = roots(y0, y1, y2, y3, ts); i < n; ++i) {
        bbox.include(value_at(x0, x1, x2, x3, ts[i]), value_at(y0, y1, y2, y3, ts[i]));
    }
}

void include_quadratic(BBox& bbox, float x0, float y0, float x1, float y1, float x2, float y2) {
    bbox.include(x0, y0);
    bbox.include(x2, y2);
    auto value_at = [](float p0, float p1, float p2, float t) {
        const float u = 1.0f - t;
        return u * u * p0 + 2.0f * u * t * p1 + t * t * p2;
    };
    // B'(t) = 2((p0 - 2p1 + p2) t + (p1 - p0)).
    auto root = [](float p0, float p1, float p2) {
        const float a = p0 - 2.0f * p1 + p2;
        if (std::abs(a) < 1e-6f) return -1.0f;
        const float t = (p0 - p1) / a;
        return (t > 0.0f && t < 1.0f) ? t : -1.0f;
    };
    for (const float t : {root(x0, x1, x2), root(y0, y1, y2)}) {
        if (t < 0.0f) continue;
        bbox.include(value_at(x0, x1, x2, t), value_at(y0, y1, y2, t));
    }
}

class PathStream {
public:
    explicit PathStream(std::string_view d) : m_d(d) {}

    void skip_wsp() {
        while (m_pos < m_d.size() && std::isspace(static_cast<unsigned char>(m_d[m_pos]))) ++m_pos;
    }

    void skip_wsp_comma() {
        skip_wsp();
        if (m_pos < m_d.size() && m_d[m_pos] == ',') {
            ++m_pos;
            skip_wsp();
        }
    }

    bool eof() const { return m_pos >= m_d.size(); }

    bool peek_is_command() {
        skip_wsp();
        return m_pos < m_d.size() && std::isalpha(static_cast<unsigned char>(m_d[m_pos]));
    }

    char read_command() {
        skip_wsp();
        if (m_pos < m_d.size() && std::isalpha(static_cast<unsigned char>(m_d[m_pos]))) {
            return m_d[m_pos++];
        }
        return '\0';
    }

    bool read_flag(bool& out) {
        skip_wsp_comma();
        if (m_pos >= m_d.size()) return false;
        if (m_d[m_pos] == '0') { out = false; ++m_pos; return true; }
        if (m_d[m_pos] == '1') { out = true; ++m_pos; return true; }
        return false;
    }

    bool read_number(float& out) {
        skip_wsp_comma();
        if (m_pos >= m_d.size()) return false;
        const size_t start = m_pos;
        const size_t n = m_d.size();

        if (m_d[m_pos] == '+' || m_d[m_pos] == '-') ++m_pos;
        bool has_digits = false;
        while (m_pos < n && std::isdigit(static_cast<unsigned char>(m_d[m_pos]))) {
            has_digits = true;
            ++m_pos;
        }
        if (m_pos < n && m_d[m_pos] == '.') {
            ++m_pos;
            while (m_pos < n && std::isdigit(static_cast<unsigned char>(m_d[m_pos]))) {
                has_digits = true;
                ++m_pos;
            }
        }
        if (!has_digits) {
            m_pos = start;
            return false;
        }
        if (m_pos < n && (m_d[m_pos] == 'e' || m_d[m_pos] == 'E')) {
            const size_t exp_start = m_pos++;
            if (m_pos < n && (m_d[m_pos] == '+' || m_d[m_pos] == '-')) ++m_pos;
            bool exp_digits = false;
            while (m_pos < n && std::isdigit(static_cast<unsigned char>(m_d[m_pos]))) {
                exp_digits = true;
                ++m_pos;
            }
            if (!exp_digits) m_pos = exp_start;
        }

        std::string tok(m_d.substr(start, m_pos - start));
        const double val = std::strtod(tok.c_str(), nullptr);
        if (!std::isfinite(val)) return false;
        out = static_cast<float>(val);
        return true;
    }

private:
    std::string_view m_d;
    size_t m_pos{0};
};

template <typename Sink>
void build_svg_path(Sink& ctx, std::string_view d, BBox& bbox) {
    PathStream ps(d);
    char cmd = '\0';
    char prev_cmd = '\0';
    float cur_x = 0.0f, cur_y = 0.0f;
    float start_x = 0.0f, start_y = 0.0f;
    float last_cubic_x = 0.0f, last_cubic_y = 0.0f;
    float last_quad_x = 0.0f, last_quad_y = 0.0f;

    constexpr int kMaxPathOps = 200000;
    int ops = 0;

    while (!ps.eof() && ++ops <= kMaxPathOps) {
        if (ps.peek_is_command()) {
            cmd = ps.read_command();
        } else if (cmd == '\0') {
            break;
        }

        switch (cmd) {
            case 'M':
            case 'm': {
                float x = 0.0f, y = 0.0f;
                if (!ps.read_number(x) || !ps.read_number(y)) return;
                if (cmd == 'm') { cur_x += x; cur_y += y; }
                else { cur_x = x; cur_y = y; }
                start_x = cur_x;
                start_y = cur_y;
                ctx.move_to(cur_x, cur_y);
                bbox.include(cur_x, cur_y);
                prev_cmd = cmd;
                // Per SVG spec, subsequent coordinate pairs after M/m are implicit L/l
                cmd = (cmd == 'm') ? 'l' : 'L';
                break;
            }
            case 'L':
            case 'l': {
                float x = 0.0f, y = 0.0f;
                if (!ps.read_number(x) || !ps.read_number(y)) return;
                if (cmd == 'l') { cur_x += x; cur_y += y; }
                else { cur_x = x; cur_y = y; }
                ctx.line_to(cur_x, cur_y);
                bbox.include(cur_x, cur_y);
                prev_cmd = cmd;
                break;
            }
            case 'H':
            case 'h': {
                float x = 0.0f;
                if (!ps.read_number(x)) return;
                cur_x = (cmd == 'h') ? (cur_x + x) : x;
                ctx.line_to(cur_x, cur_y);
                bbox.include(cur_x, cur_y);
                prev_cmd = cmd;
                break;
            }
            case 'V':
            case 'v': {
                float y = 0.0f;
                if (!ps.read_number(y)) return;
                cur_y = (cmd == 'v') ? (cur_y + y) : y;
                ctx.line_to(cur_x, cur_y);
                bbox.include(cur_x, cur_y);
                prev_cmd = cmd;
                break;
            }
            case 'C':
            case 'c': {
                float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;
                if (!ps.read_number(x1) || !ps.read_number(y1) ||
                    !ps.read_number(x2) || !ps.read_number(y2) ||
                    !ps.read_number(x) || !ps.read_number(y)) return;
                if (cmd == 'c') {
                    x1 += cur_x; y1 += cur_y;
                    x2 += cur_x; y2 += cur_y;
                    x += cur_x; y += cur_y;
                }
                ctx.bezier_curve_to(x1, y1, x2, y2, x, y);
                include_cubic(bbox, cur_x, cur_y, x1, y1, x2, y2, x, y);
                last_cubic_x = x2;
                last_cubic_y = y2;
                cur_x = x;
                cur_y = y;
                prev_cmd = cmd;
                break;
            }
            case 'S':
            case 's': {
                float x2 = 0, y2 = 0, x = 0, y = 0;
                if (!ps.read_number(x2) || !ps.read_number(y2) ||
                    !ps.read_number(x) || !ps.read_number(y)) return;
                float x1 = cur_x, y1 = cur_y;
                if (prev_cmd == 'C' || prev_cmd == 'c' || prev_cmd == 'S' || prev_cmd == 's') {
                    x1 = 2.0f * cur_x - last_cubic_x;
                    y1 = 2.0f * cur_y - last_cubic_y;
                }
                if (cmd == 's') {
                    x2 += cur_x; y2 += cur_y;
                    x += cur_x; y += cur_y;
                }
                ctx.bezier_curve_to(x1, y1, x2, y2, x, y);
                include_cubic(bbox, cur_x, cur_y, x1, y1, x2, y2, x, y);
                last_cubic_x = x2;
                last_cubic_y = y2;
                cur_x = x;
                cur_y = y;
                prev_cmd = cmd;
                break;
            }
            case 'Q':
            case 'q': {
                float x1 = 0, y1 = 0, x = 0, y = 0;
                if (!ps.read_number(x1) || !ps.read_number(y1) ||
                    !ps.read_number(x) || !ps.read_number(y)) return;
                if (cmd == 'q') {
                    x1 += cur_x; y1 += cur_y;
                    x += cur_x; y += cur_y;
                }
                ctx.quadratic_curve_to(x1, y1, x, y);
                include_quadratic(bbox, cur_x, cur_y, x1, y1, x, y);
                last_quad_x = x1;
                last_quad_y = y1;
                cur_x = x;
                cur_y = y;
                prev_cmd = cmd;
                break;
            }
            case 'T':
            case 't': {
                float x = 0, y = 0;
                if (!ps.read_number(x) || !ps.read_number(y)) return;
                float x1 = cur_x, y1 = cur_y;
                if (prev_cmd == 'Q' || prev_cmd == 'q' || prev_cmd == 'T' || prev_cmd == 't') {
                    x1 = 2.0f * cur_x - last_quad_x;
                    y1 = 2.0f * cur_y - last_quad_y;
                }
                if (cmd == 't') {
                    x += cur_x; y += cur_y;
                }
                ctx.quadratic_curve_to(x1, y1, x, y);
                include_quadratic(bbox, cur_x, cur_y, x1, y1, x, y);
                last_quad_x = x1;
                last_quad_y = y1;
                cur_x = x;
                cur_y = y;
                prev_cmd = cmd;
                break;
            }
            case 'A':
            case 'a': {
                float rx = 0, ry = 0, x_rot = 0, x = 0, y = 0;
                bool large_arc = false, sweep = false;
                if (!ps.read_number(rx) || !ps.read_number(ry) || !ps.read_number(x_rot) ||
                    !ps.read_flag(large_arc) || !ps.read_flag(sweep) ||
                    !ps.read_number(x) || !ps.read_number(y)) return;
                if (cmd == 'a') {
                    x += cur_x;
                    y += cur_y;
                }
                emit_svg_arc(ctx, bbox, cur_x, cur_y, rx, ry, x_rot, large_arc, sweep, x, y);
                cur_x = x;
                cur_y = y;
                prev_cmd = cmd;
                break;
            }
            case 'Z':
            case 'z': {
                ctx.close_path();
                cur_x = start_x;
                cur_y = start_y;
                prev_cmd = cmd;
                cmd = '\0'; // Require explicit next command after Z/z
                break;
            }
            default:
                return;
        }
    }
}

// §4.12.5.1.7's `new Path2D(svgPathData)`: the same walk of the same grammar that
// an SVG `d` attribute goes through, recording into a `Path2D` instead of a
// context. Reusing `build_svg_path` rather than writing a second parser is the
// point: a page that draws a `Path2D` and an `<svg><path>` with the same data must
// get the same shape, and two parsers would drift.
//
// The sink has the five calls the walker makes. `arc_to` cannot appear in path
// data, so a linear approximation of it here would be dead code; the walker never
// calls it, which is why the signature matches `CanvasRenderingContext2D`'s rather
// than being invented.
struct Path2DRecorder {
    Path2D* path{nullptr};
    void move_to(float x, float y) {
        Path2D::Op op; op.kind = Path2D::Kind::MoveTo; op.a = x; op.b = y; path->add_op(std::move(op));
    }
    void line_to(float x, float y) {
        Path2D::Op op; op.kind = Path2D::Kind::LineTo; op.a = x; op.b = y; path->add_op(std::move(op));
    }
    void bezier_curve_to(float x1, float y1, float x2, float y2, float x, float y) {
        Path2D::Op op; op.kind = Path2D::Kind::BezierCurveTo;
        op.a = x1; op.b = y1; op.c = x2; op.d = y2; op.e = x; op.f = y;
        path->add_op(std::move(op));
    }
    void quadratic_curve_to(float cx, float cy, float x, float y) {
        Path2D::Op op; op.kind = Path2D::Kind::QuadraticCurveTo;
        op.a = cx; op.b = cy; op.c = x; op.d = y; path->add_op(std::move(op));
    }
    void close_path() {
        Path2D::Op op; op.kind = Path2D::Kind::ClosePath; path->add_op(std::move(op));
    }
};
}  // namespace

// §4.12.5.1.7, declared in the header: outside the file-local namespace, because
// the binding layer calls it. The recorder above is a sink for `build_svg_path`,
// whose template still reaches it, so a `Path2D` from path data cannot drift from
// the same data on an `<svg><path>`.
std::shared_ptr<Path2D> parse_svg_path_data(std::string_view d) {
    auto path = std::make_shared<Path2D>();
    Path2DRecorder recorder;
    recorder.path = path.get();
    BBox box;
    build_svg_path(recorder, d, box);
    // An empty `d` is a valid, empty path in SVG rather than an error, so an empty
    // string answers an empty `Path2D` and not `nullptr`.
    return path;
}

namespace {

// A polyline with its cumulative lengths: what <textPath> needs to answer "what
// point is 37 px along this path, and which way is it facing".
class PathSampler {
public:
    bool build(std::string_view d) {
        PathFlattener flattener;
        BBox scratch;
        build_svg_path(flattener, d, scratch);
        const auto& points = flattener.points();
        if (points.size() < 2) return false;
        m_points.assign(points.begin(), points.end());
        m_lengths.assign(m_points.size(), 0.0f);
        float total = 0.0f;
        for (size_t i = 1; i < m_points.size(); ++i) {
            total += std::hypot(m_points[i][0] - m_points[i - 1][0],
                                m_points[i][1] - m_points[i - 1][1]);
            m_lengths[i] = total;
        }
        return total > 0.0f;
    }

    float length() const { return m_lengths.empty() ? 0.0f : m_lengths.back(); }

    bool at(float s, float& x, float& y, float& angle_degrees) const {
        if (m_points.size() < 2 || !std::isfinite(s)) return false;
        if (s < 0.0f || s > length()) return false;
        size_t i = static_cast<size_t>(
            std::upper_bound(m_lengths.begin(), m_lengths.end(), s) - m_lengths.begin());
        if (i == 0) i = 1;
        if (i >= m_points.size()) i = m_points.size() - 1;
        const float segment = m_lengths[i] - m_lengths[i - 1];
        const float t = segment > 1e-6f ? (s - m_lengths[i - 1]) / segment : 0.0f;
        x = m_points[i - 1][0] + (m_points[i][0] - m_points[i - 1][0]) * t;
        y = m_points[i - 1][1] + (m_points[i][1] - m_points[i - 1][1]) * t;
        angle_degrees = std::atan2(m_points[i][1] - m_points[i - 1][1],
                                   m_points[i][0] - m_points[i - 1][0]) * (180.0f / kPi);
        return true;
    }

private:
    std::vector<std::array<float, 2>> m_points;
    std::vector<float> m_lengths;
};

struct SvgDocumentContext {
    std::unordered_map<std::string, std::shared_ptr<Element>> by_id;
    float viewport_w{300.0f};
    float viewport_h{150.0f};

    void index_tree(const std::shared_ptr<Element>& root) {
        if (!root) return;
        std::vector<std::shared_ptr<Element>> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            auto el = std::move(stack.back());
            stack.pop_back();
            std::string id;
            if (get_attr_ci(el, "id", id) && !id.empty()) {
                by_id.emplace(std::move(id), el);
            }
            for (auto it = el->children().rbegin(); it != el->children().rend(); ++it) {
                if (auto child = std::dynamic_pointer_cast<Element>(*it)) {
                    stack.push_back(std::move(child));
                }
            }
        }
    }
};

std::shared_ptr<CanvasGradient> build_svg_gradient(
    const SvgDocumentContext& doc_ctx,
    const std::string& id,
    const BBox& shape_bbox,
    float extra_opacity
) {
    auto it = doc_ctx.by_id.find(id);
    if (it == doc_ctx.by_id.end()) return nullptr;

    // Collect inheritance chain via href / xlink:href (child first, ancestors after)
    std::vector<std::shared_ptr<Element>> chain;
    std::unordered_set<std::string> visited;
    std::shared_ptr<Element> cur = it->second;
    visited.insert(id);
    for (int hop = 0; hop < kMaxGradientHrefDepth && cur; ++hop) {
        std::string tag = to_lower_ascii(cur->tag_name());
        if (tag != "lineargradient" && tag != "radialgradient") break;
        chain.push_back(cur);
        std::string href;
        if (!get_href_attr(cur, href)) break;
        std::string next_id = extract_fragment_id(href);
        if (next_id.empty() || !visited.insert(next_id).second) break;
        auto next_it = doc_ctx.by_id.find(next_id);
        if (next_it == doc_ctx.by_id.end()) break;
        cur = next_it->second;
    }
    if (chain.empty()) return nullptr;

    auto find_attr = [&](std::string_view name, std::string& out) -> bool {
        for (const auto& node : chain) {
            if (get_attr_ci(node, name, out)) return true;
        }
        return false;
    };

    const std::string root_tag = to_lower_ascii(chain.front()->tag_name());
    auto grad = std::make_shared<CanvasGradient>();
    grad->type = (root_tag == "radialgradient") ? CanvasGradient::Type::Radial : CanvasGradient::Type::Linear;

    std::string units_str;
    bool user_space = false;
    if (find_attr("gradientUnits", units_str) && to_lower_ascii(trim_wsp(units_str)) == "userspaceonuse") {
        user_space = true;
    }

    std::string spread_str;
    if (find_attr("spreadMethod", spread_str)) {
        std::string sp = to_lower_ascii(trim_wsp(spread_str));
        if (sp == "reflect") grad->spread = GradientSpread::Reflect;
        else if (sp == "repeat") grad->spread = GradientSpread::Repeat;
    }

    auto read_coord = [&](std::string_view attr_name, float def_val, float ref_size) -> float {
        std::string raw;
        if (!find_attr(attr_name, raw)) return def_val;
        float v = def_val;
        bool is_pct = false;
        if (parse_length_or_pct_unit(raw, v, user_space ? ref_size : 1.0f, is_pct)) {
            return v;
        }
        return def_val;
    };

    if (grad->type == CanvasGradient::Type::Linear) {
        grad->x0 = read_coord("x1", 0.0f, doc_ctx.viewport_w);
        grad->y0 = read_coord("y1", 0.0f, doc_ctx.viewport_h);
        grad->x1 = read_coord("x2", user_space ? doc_ctx.viewport_w : 1.0f, doc_ctx.viewport_w);
        grad->y1 = read_coord("y2", 0.0f, doc_ctx.viewport_h);
    } else {
        const float def_c = user_space ? (doc_ctx.viewport_w * 0.5f) : 0.5f;
        const float def_r = user_space ? (std::min(doc_ctx.viewport_w, doc_ctx.viewport_h) * 0.5f) : 0.5f;
        grad->x1 = read_coord("cx", def_c, doc_ctx.viewport_w);
        grad->y1 = read_coord("cy", user_space ? (doc_ctx.viewport_h * 0.5f) : 0.5f, doc_ctx.viewport_h);
        grad->r1 = std::max(0.0f, read_coord("r", def_r, std::min(doc_ctx.viewport_w, doc_ctx.viewport_h)));
        grad->x0 = read_coord("fx", grad->x1, doc_ctx.viewport_w);
        grad->y0 = read_coord("fy", grad->y1, doc_ctx.viewport_h);
        grad->r0 = std::max(0.0f, read_coord("fr", 0.0f, std::min(doc_ctx.viewport_w, doc_ctx.viewport_h)));
    }

    // Build mapping from user-space coordinates into gradient coordinate space
    TransformMatrix2D grad_to_user = TransformMatrix2D::identity();
    if (!user_space) {
        if (shape_bbox.valid() && shape_bbox.width() > 1e-5f && shape_bbox.height() > 1e-5f) {
            grad_to_user = TransformMatrix2D::translation(shape_bbox.min_x, shape_bbox.min_y)
                .multiply(TransformMatrix2D::scale(shape_bbox.width(), shape_bbox.height()));
        }
    }
    std::string gtf_str;
    if (find_attr("gradientTransform", gtf_str)) {
        grad_to_user = grad_to_user.multiply(parse_svg_transform(gtf_str));
    }
    if (!grad_to_user.is_identity()) {
        grad->transform = grad_to_user.inverse();
    }

    // Find first element in chain that owns <stop> children
    for (const auto& node : chain) {
        bool found_stops = false;
        for (const auto& ch : node->children()) {
            auto stop_el = std::dynamic_pointer_cast<Element>(ch);
            if (!stop_el || to_lower_ascii(stop_el->tag_name()) != "stop") continue;
            found_stops = true;

            float offset = 0.0f;
            std::string off_str;
            if (get_attr_ci(stop_el, "offset", off_str)) {
                bool is_pct = false;
                parse_length_or_pct_unit(off_str, offset, 1.0f, is_pct);
            }
            offset = std::clamp(offset, 0.0f, 1.0f);

            Color stop_color = Color::black();
            float stop_opacity = 1.0f;
            std::string val;
            if (get_attr_ci(stop_el, "stop-color", val)) parse_svg_color(val, stop_color);
            if (get_attr_ci(stop_el, "stop-opacity", val)) {
                float op = 1.0f;
                if (parse_float_value(val, op, 1.0f)) stop_opacity = std::clamp(op, 0.0f, 1.0f);
            }
            if (get_attr_ci(stop_el, "style", val)) {
                size_t p = 0;
                while (p < val.size()) {
                    size_t semi = val.find(';', p);
                    std::string_view decl = (semi == std::string::npos) ? std::string_view(val).substr(p)
                                                                        : std::string_view(val).substr(p, semi - p);
                    size_t colon = decl.find(':');
                    if (colon != std::string_view::npos) {
                        std::string k = to_lower_ascii(trim_wsp(decl.substr(0, colon)));
                        std::string_view v = trim_wsp(decl.substr(colon + 1));
                        if (k == "stop-color") parse_svg_color(v, stop_color);
                        else if (k == "stop-opacity") {
                            float op = 1.0f;
                            if (parse_float_value(v, op, 1.0f)) stop_opacity = std::clamp(op, 0.0f, 1.0f);
                        }
                    }
                    if (semi == std::string::npos) break;
                    p = semi + 1;
                }
            }

            const float combined_alpha = (static_cast<float>(stop_color.a) / 255.0f) *
                                         stop_opacity * std::clamp(extra_opacity, 0.0f, 1.0f);
            stop_color.a = static_cast<uint8_t>(std::clamp(std::lround(combined_alpha * 255.0f), 0l, 255l));
            grad->add_color_stop(offset, stop_color);
        }
        if (found_stops) break;
    }

    if (grad->stops.empty()) return nullptr;
    return grad;
}

// One answer for "what colour is this paint server", for shapes and for glyphs:
// a solid colour, `currentColor`, a gradient built from the document, or the
// fallback colour SVG puts after an unresolvable `url(#...)`.
bool resolve_svg_paint(const SvgDocumentContext& doc_ctx, const SvgStyle& style,
                       const PaintServer& ps, float paint_opacity, const BBox& bbox,
                       Color& out_color, std::shared_ptr<CanvasGradient>& out_grad) {
    out_grad = nullptr;
    const float eff_op = std::clamp(style.opacity * paint_opacity, 0.0f, 1.0f);
    if (eff_op <= 0.0f) return false;

    if (ps.kind == PaintServer::Kind::None) return false;
    if (ps.kind == PaintServer::Kind::Solid) {
        out_color = ps.color;
        out_color.a = static_cast<uint8_t>(std::lround(static_cast<float>(out_color.a) * eff_op));
        return out_color.a > 0;
    }
    if (ps.kind == PaintServer::Kind::CurrentColor) {
        out_color = style.color;
        out_color.a = static_cast<uint8_t>(std::lround(static_cast<float>(out_color.a) * eff_op));
        return out_color.a > 0;
    }
    if (ps.kind == PaintServer::Kind::Url) {
        out_grad = build_svg_gradient(doc_ctx, ps.url_id, bbox, eff_op);
        if (out_grad) return true;
        if (ps.has_fallback) {
            out_color = ps.fallback;
            out_color.a = static_cast<uint8_t>(std::lround(static_cast<float>(out_color.a) * eff_op));
            return out_color.a > 0;
        }
        return false;
    }
    return false;
}

bool build_shape_geometry(
    CanvasRenderingContext2D& ctx,
    const std::shared_ptr<Element>& el,
    float ref_w,
    float ref_h,
    BBox& bbox,
    bool& allow_fill
) {
    const std::string tag = to_lower_ascii(el->tag_name());
    allow_fill = true;

    auto attr_f = [&](std::string_view name, float def_val, float pct_ref) -> float {
        std::string s;
        if (!get_attr_ci(el, name, s)) return def_val;
        float v = def_val;
        return parse_float_value(s, v, pct_ref) ? v : def_val;
    };

    if (tag == "rect") {
        const float x = attr_f("x", 0.0f, ref_w);
        const float y = attr_f("y", 0.0f, ref_h);
        const float w = attr_f("width", 0.0f, ref_w);
        const float h = attr_f("height", 0.0f, ref_h);
        if (w <= 0.0f || h <= 0.0f) return false;

        std::string rx_s, ry_s;
        const bool has_rx = get_attr_ci(el, "rx", rx_s);
        const bool has_ry = get_attr_ci(el, "ry", ry_s);
        float rx = 0.0f, ry = 0.0f;
        if (has_rx) parse_float_value(rx_s, rx, ref_w);
        if (has_ry) parse_float_value(ry_s, ry, ref_h);
        if (has_rx && !has_ry) ry = rx;
        if (has_ry && !has_rx) rx = ry;
        rx = std::clamp(rx, 0.0f, w * 0.5f);
        ry = std::clamp(ry, 0.0f, h * 0.5f);

        if (rx > 0.0f && ry > 0.0f) {
            if (std::abs(rx - ry) < 1e-4f) {
                ctx.round_rect(x, y, w, h, rx);
            } else {
                // Elliptical rounded rectangle
                ctx.move_to(x + rx, y);
                ctx.line_to(x + w - rx, y);
                ctx.ellipse(x + w - rx, y + ry, rx, ry, 0.0f, -static_cast<float>(kPi * 0.5), 0.0f, false);
                ctx.line_to(x + w, y + h - ry);
                ctx.ellipse(x + w - rx, y + h - ry, rx, ry, 0.0f, 0.0f, static_cast<float>(kPi * 0.5), false);
                ctx.line_to(x + rx, y + h);
                ctx.ellipse(x + rx, y + h - ry, rx, ry, 0.0f, static_cast<float>(kPi * 0.5), static_cast<float>(kPi), false);
                ctx.line_to(x, y + ry);
                ctx.ellipse(x + rx, y + ry, rx, ry, 0.0f, static_cast<float>(kPi), static_cast<float>(kPi * 1.5), false);
                ctx.close_path();
            }
        } else {
            ctx.rect(x, y, w, h);
        }
        bbox.include(x, y);
        bbox.include(x + w, y + h);
        return true;
    }

    if (tag == "circle") {
        const float cx = attr_f("cx", 0.0f, ref_w);
        const float cy = attr_f("cy", 0.0f, ref_h);
        const float r = attr_f("r", 0.0f, std::hypot(ref_w, ref_h) / 1.41421356f);
        if (r <= 0.0f) return false;
        ctx.arc(cx, cy, r, 0.0f, static_cast<float>(kTwoPi), false);
        ctx.close_path();
        bbox.include(cx - r, cy - r);
        bbox.include(cx + r, cy + r);
        return true;
    }

    if (tag == "ellipse") {
        const float cx = attr_f("cx", 0.0f, ref_w);
        const float cy = attr_f("cy", 0.0f, ref_h);
        const float rx = attr_f("rx", 0.0f, ref_w);
        const float ry = attr_f("ry", 0.0f, ref_h);
        if (rx <= 0.0f || ry <= 0.0f) return false;
        ctx.ellipse(cx, cy, rx, ry, 0.0f, 0.0f, static_cast<float>(kTwoPi), false);
        ctx.close_path();
        bbox.include(cx - rx, cy - ry);
        bbox.include(cx + rx, cy + ry);
        return true;
    }

    if (tag == "line") {
        const float x1 = attr_f("x1", 0.0f, ref_w);
        const float y1 = attr_f("y1", 0.0f, ref_h);
        const float x2 = attr_f("x2", 0.0f, ref_w);
        const float y2 = attr_f("y2", 0.0f, ref_h);
        ctx.move_to(x1, y1);
        ctx.line_to(x2, y2);
        bbox.include(x1, y1);
        bbox.include(x2, y2);
        allow_fill = false;
        return true;
    }

    if (tag == "polyline" || tag == "polygon") {
        std::string pts_str;
        if (!get_attr_ci(el, "points", pts_str)) return false;
        auto nums = parse_number_list(pts_str);
        if (nums.size() < 4) return false;
        ctx.move_to(nums[0], nums[1]);
        bbox.include(nums[0], nums[1]);
        for (size_t k = 2; k + 1 < nums.size(); k += 2) {
            ctx.line_to(nums[k], nums[k + 1]);
            bbox.include(nums[k], nums[k + 1]);
        }
        if (tag == "polygon") {
            ctx.close_path();
        }
        return true;
    }

    if (tag == "path") {
        std::string d;
        if (!get_attr_ci(el, "d", d) || d.empty()) return false;
        build_svg_path(ctx, d, bbox);
        return true;
    }

    return false;
}

void apply_clip_path_element(
    CanvasRenderingContext2D& ctx,
    const std::shared_ptr<Element>& clip_el,
    float ref_w,
    float ref_h
) {
    if (!clip_el) return;
    ctx.begin_path();
    FillRule rule = FillRule::NonZero;
    for (const auto& child : clip_el->children()) {
        auto child_el = std::dynamic_pointer_cast<Element>(child);
        if (!child_el) continue;
        const TransformMatrix2D saved_ctm = ctx.current_transform();
        std::string tf_str;
        if (get_attr_ci(child_el, "transform", tf_str)) {
            ctx.set_transform(saved_ctm.multiply(parse_svg_transform(tf_str)));
        }
        std::string cr;
        if (get_attr_ci(child_el, "clip-rule", cr) && to_lower_ascii(trim_wsp(cr)) == "evenodd") {
            rule = FillRule::EvenOdd;
        }
        BBox dummy;
        bool allow_fill = true;
        build_shape_geometry(ctx, child_el, ref_w, ref_h, dummy, allow_fill);
        ctx.set_transform(saved_ctm);
    }
    ctx.clip(rule);
}

void paint_shape(
    CanvasRenderingContext2D& ctx,
    const SvgDocumentContext& doc_ctx,
    const SvgStyle& style,
    const BBox& bbox,
    bool allow_fill
) {
    auto resolve_paint = [&](const PaintServer& ps, float paint_opacity,
                             Color& out_color, std::shared_ptr<CanvasGradient>& out_grad) -> bool {
        return resolve_svg_paint(doc_ctx, style, ps, paint_opacity, bbox, out_color, out_grad);
    };

    ctx.global_alpha = 1.0f; // Opacity already folded into color/gradient alpha

    if (allow_fill) {
        Color fc;
        std::shared_ptr<CanvasGradient> fg;
        if (resolve_paint(style.fill, style.fill_opacity, fc, fg)) {
            ctx.fill_style = fc;
            ctx.fill_gradient = fg;
            ctx.fill(style.fill_rule);
        }
    }

    if (style.stroke_width > 0.0f) {
        Color sc;
        std::shared_ptr<CanvasGradient> sg;
        if (resolve_paint(style.stroke, style.stroke_opacity, sc, sg)) {
            ctx.stroke_style = sc;
            ctx.stroke_gradient = sg;
            ctx.line_width = style.stroke_width;
            ctx.line_cap = style.stroke_linecap;
            ctx.line_join = style.stroke_linejoin;
            ctx.miter_limit = style.stroke_miterlimit;
            ctx.set_line_dash(style.stroke_dasharray);
            ctx.line_dash_offset = style.stroke_dashoffset;
            ctx.stroke();
        }
    }
}

// ---------------------------------------------------------------------------
// SVG text.
//
// Canvas draws a string, one origin, one direction. SVG does not: `x`, `y`,
// `dx`, `dy` and `rotate` are lists with one value per character, an absolute
// `x` or `y` starts a new *text chunk*, `text-anchor` is a property of that
// chunk rather than of one text node, `<textPath>` lays the glyphs along a
// path, and a gradient can be the paint of a glyph. So the text is collected
// into glyphs first (with the style and the path each one belongs to), and
// then measured and drawn a segment at a time.
// ---------------------------------------------------------------------------

std::vector<std::string> split_utf8_codepoints(std::string_view text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        size_t len = 1;
        if ((lead & 0xE0) == 0xC0) len = 2;
        else if ((lead & 0xF0) == 0xE0) len = 3;
        else if ((lead & 0xF8) == 0xF0) len = 4;
        if (i + len > text.size()) len = 1;
        out.emplace_back(text.substr(i, len));
        i += len;
    }
    return out;
}

// A length list: SVG allows `10, 20 30` and percentages, and each value is
// resolved against the viewport axis it belongs to.
std::vector<float> parse_length_list(std::string_view raw, float pct_ref) {
    std::vector<float> out;
    size_t i = 0;
    while (i < raw.size() && out.size() < 4096) {
        while (i < raw.size() &&
               (std::isspace(static_cast<unsigned char>(raw[i])) || raw[i] == ',')) ++i;
        if (i >= raw.size()) break;
        const size_t start = i;
        while (i < raw.size() &&
               !std::isspace(static_cast<unsigned char>(raw[i])) && raw[i] != ',') ++i;
        float value = 0.0f;
        if (parse_float_value(raw.substr(start, i - start), value, pct_ref)) out.push_back(value);
    }
    return out;
}

struct PositionList {
    std::vector<float> x, y, dx, dy, rotate;
    size_t index{0};

    bool empty() const {
        return x.empty() && y.empty() && dx.empty() && dy.empty() && rotate.empty();
    }
};

struct TextGlyph {
    std::string utf8;   // one Unicode code point
    int style{0};       // index into TextLayoutData::styles
    int path{-1};       // index into TextLayoutData::paths, -1 for straight text
    int transform{-1};  // index into TextLayoutData::transforms, -1 for none
    float x{0.0f}, y{0.0f};
    bool has_x{false}, has_y{false};
    float dx{0.0f}, dy{0.0f};
    float rotate{0.0f};
    bool has_rotate{false};
};

struct TextPathRef {
    std::string id;
    std::string d;
    float start_offset{0.0f};
    bool start_is_percent{false};
};

struct TextLayoutData {
    std::vector<TextGlyph> glyphs;
    std::vector<SvgStyle> styles;
    std::vector<TextPathRef> paths;
    std::vector<TransformMatrix2D> transforms;
};

class TextCollector {
public:
    TextCollector(const SvgDocumentContext& doc_ctx, float ref_w, float ref_h)
        : m_doc_ctx(doc_ctx), m_ref_w(ref_w), m_ref_h(ref_h) {}

    TextLayoutData collect(const std::shared_ptr<Element>& el, const SvgStyle& style) {
        TextLayoutData out;
        out.styles.push_back(style);
        bool started = false, pending_space = false;
        walk(el, style, 0, 0, started, pending_space, false, -1, -1, out);
        return out;
    }

private:
    void walk(const std::shared_ptr<Element>& el, const SvgStyle& style, int style_id, int depth,
              bool& started, bool& pending_space, bool preserve_space, int path, int transform,
              TextLayoutData& out) {
        if (!el || depth > 256 || stack_is_exhausted() || style.display_none) return;

        std::string attr;
        if (get_attr_ci(el, "xml:space", attr)) {
            if (attr == "preserve") preserve_space = true;
            else if (attr == "default") preserve_space = false;
        }

        PositionList lists;
        if (get_attr_ci(el, "x", attr)) lists.x = parse_length_list(attr, m_ref_w);
        if (get_attr_ci(el, "y", attr)) lists.y = parse_length_list(attr, m_ref_h);
        if (get_attr_ci(el, "dx", attr)) lists.dx = parse_length_list(attr, m_ref_w);
        if (get_attr_ci(el, "dy", attr)) lists.dy = parse_length_list(attr, m_ref_h);
        if (get_attr_ci(el, "rotate", attr)) lists.rotate = parse_number_list(attr);
        if (!lists.empty()) m_lists.push_back(std::move(lists));

        int child_transform = transform;
        if (depth != 0 && get_attr_ci(el, "transform", attr)) {
            TransformMatrix2D composed = parse_svg_transform(attr);
            if (transform >= 0) {
                composed = out.transforms[static_cast<size_t>(transform)].multiply(composed);
            }
            out.transforms.push_back(composed);
            child_transform = static_cast<int>(out.transforms.size()) - 1;
        }

        int child_path = path;
        if (to_lower_ascii(el->tag_name()) == "textpath") {
            TextPathRef ref;
            std::string href;
            if (get_href_attr(el, href)) ref.id = extract_fragment_id(href);
            auto it = ref.id.empty() ? m_doc_ctx.by_id.end() : m_doc_ctx.by_id.find(ref.id);
            if (it != m_doc_ctx.by_id.end()) get_attr_ci(it->second, "d", ref.d);
            if (get_attr_ci(el, "startoffset", attr)) {
                const std::string value = to_lower_ascii(trim_wsp(attr));
                if (!value.empty() && value.back() == '%') {
                    ref.start_is_percent = true;
                    float pct = 0.0f;
                    if (parse_float_value(value.substr(0, value.size() - 1), pct, 100.0f))
                        ref.start_offset = pct;
                } else {
                    float len = 0.0f;
                    if (parse_float_value(value, len, m_ref_w)) ref.start_offset = len;
                }
            }
            if (!ref.d.empty()) {
                out.paths.push_back(std::move(ref));
                child_path = static_cast<int>(out.paths.size()) - 1;
            }
        }

        const float ref_size = std::hypot(m_ref_w, m_ref_h) / 1.41421356f;
        for (const auto& child : el->children()) {
            if (child->is_text()) {
                const std::string raw = child->text_content();
                std::string collapsed;
                for (size_t i = 0; i < raw.size() && i < 16384; ++i) {
                    const unsigned char c = static_cast<unsigned char>(raw[i]);
                    if (!preserve_space && std::isspace(c)) {
                        pending_space = true;
                        continue;
                    }
                    if (pending_space && started) collapsed.push_back(' ');
                    pending_space = false;
                    collapsed.push_back(static_cast<char>(c));
                    started = true;
                }
                if (!style.visible) continue;
                for (const auto& codepoint : split_utf8_codepoints(collapsed)) {
                    TextGlyph glyph;
                    glyph.utf8 = codepoint;
                    glyph.style = style_id;
                    // The element's own <textPath> and transform apply to the
                    // characters it holds, not only to the ones below it.
                    glyph.path = child_path;
                    glyph.transform = child_transform;
                    for (auto& pl : m_lists) {
                        const size_t i = pl.index;
                        if (i < pl.x.size()) { glyph.x = pl.x[i]; glyph.has_x = true; }
                        if (i < pl.y.size()) { glyph.y = pl.y[i]; glyph.has_y = true; }
                        if (i < pl.dx.size()) glyph.dx += pl.dx[i];
                        if (i < pl.dy.size()) glyph.dy += pl.dy[i];
                        // SVG 1.1 §10.5: the last `rotate` value applies to the
                        // characters after the list is exhausted; x, y, dx and
                        // dy do not repeat.
                        if (!pl.rotate.empty()) {
                            glyph.rotate = (i < pl.rotate.size()) ? pl.rotate[i] : pl.rotate.back();
                            glyph.has_rotate = true;
                        }
                        ++pl.index;
                    }
                    out.glyphs.push_back(std::move(glyph));
                }
            } else if (auto span = std::dynamic_pointer_cast<Element>(child)) {
                const std::string tag = to_lower_ascii(span->tag_name());
                if (tag != "tspan" && tag != "textpath") continue;
                const SvgStyle span_style = resolve_element_style(span, style, ref_size);
                if (span_style.display_none) continue;
                out.styles.push_back(span_style);
                const int child_style = static_cast<int>(out.styles.size()) - 1;
                // A copy: the table can grow while the child walks, and a
                // reference into it would then point at freed storage.
                const SvgStyle child_style_value = out.styles[static_cast<size_t>(child_style)];
                walk(span, child_style_value, child_style, depth + 1, started, pending_space,
                     preserve_space, child_path, child_transform, out);
            }
        }

        if (!lists.empty()) m_lists.pop_back();
    }

    const SvgDocumentContext& m_doc_ctx;
    float m_ref_w{100.0f};
    float m_ref_h{100.0f};
    std::vector<PositionList> m_lists;
};

struct TextSegment {
    int style{0};
    std::string text;
    float x{0.0f}, y{0.0f};
    float rotate{0.0f};
    bool rotated{false};
    bool has_transform{false};
    TransformMatrix2D transform{};
    float width{0.0f};
};

void set_svg_canvas_font(CanvasRenderingContext2D& ctx, const SvgStyle& style) {
    ctx.font = style.font_style + " " + std::to_string(style.font_weight) + " " +
               std::to_string(style.font_size) + "px " + style.font_family;
}

void paint_text_segment(CanvasRenderingContext2D& ctx, const SvgDocumentContext& doc_ctx,
                        const SvgStyle& style, const TextSegment& seg, const BBox& bbox) {
    Color fill_color;
    std::shared_ptr<CanvasGradient> fill_gradient;
    const bool filled = resolve_svg_paint(doc_ctx, style, style.fill, style.fill_opacity, bbox,
                                          fill_color, fill_gradient);
    Color stroke_color;
    std::shared_ptr<CanvasGradient> stroke_gradient;
    const bool stroked = style.stroke_width > 0.0f &&
        resolve_svg_paint(doc_ctx, style, style.stroke, style.stroke_opacity, bbox,
                          stroke_color, stroke_gradient);
    if (!filled && !stroked) return;
    if (seg.text.empty()) return;

    ctx.save();
    if (seg.has_transform) {
        // A <tspan transform> applies to the run it wraps, exactly as the
        // element's own transform would.
        ctx.set_transform(ctx.current_transform().multiply(seg.transform));
    }
    if (seg.rotated) {
        ctx.translate(seg.x, seg.y);
        ctx.rotate(seg.rotate * (kPi / 180.0f));
    }
    ctx.text_align = "left";
    ctx.text_baseline = "alphabetic";
    ctx.global_alpha = 1.0f;
    const float pen_x = seg.rotated ? 0.0f : seg.x;
    const float pen_y = seg.rotated ? 0.0f : seg.y;
    if (filled) {
        ctx.fill_style = fill_color;
        ctx.fill_gradient = fill_gradient;
        ctx.fill_text(seg.text, pen_x, pen_y);
    }
    if (stroked) {
        ctx.stroke_style = stroke_color;
        ctx.stroke_gradient = stroke_gradient;
        ctx.line_width = style.stroke_width;
        ctx.stroke_text(seg.text, pen_x, pen_y);
    }
    ctx.restore();
}

// Measure a chunk's segments, then draw them shifted by the chunk's anchor.
void paint_text(CanvasRenderingContext2D& ctx, const SvgDocumentContext& doc_ctx,
                const TextLayoutData& layout) {
    if (layout.glyphs.empty()) return;

    std::unordered_map<int, PathSampler> samplers;
    for (size_t i = 0; i < layout.paths.size(); ++i) {
        PathSampler sampler;
        if (!layout.paths[i].d.empty() && sampler.build(layout.paths[i].d)) {
            samplers.emplace(static_cast<int>(i), std::move(sampler));
        }
    }

    // The pen does not reset between chunks: a <tspan> that names only `y`
    // keeps the x the previous run left it at.
    float carry_x = 0.0f, carry_y = 0.0f;
    size_t begin = 0;
    while (begin < layout.glyphs.size()) {
        size_t end = begin + 1;
        while (end < layout.glyphs.size()) {
            const TextGlyph& glyph = layout.glyphs[end];
            if (glyph.path != layout.glyphs[begin].path) break;
            if (glyph.transform != layout.glyphs[begin].transform) break;
            // An absolute position starts a new text chunk; text-anchor is a
            // property of the chunk, so it must not reach past this point.
            if (glyph.has_x || glyph.has_y) break;
            ++end;
        }

        const int chunk_transform = layout.glyphs[begin].transform;
        auto give_transform = [&](TextSegment& seg) {
            if (chunk_transform >= 0) {
                seg.has_transform = true;
                seg.transform = layout.transforms[static_cast<size_t>(chunk_transform)];
            }
        };

        std::vector<TextSegment> segments;
        const int chunk_path = layout.glyphs[begin].path;
        if (chunk_path >= 0) {
            auto it = samplers.find(chunk_path);
            if (it == samplers.end()) { begin = end; continue; }
            const PathSampler& sampler = it->second;
            const TextPathRef& ref = layout.paths[static_cast<size_t>(chunk_path)];

            std::vector<float> widths;
            float total = 0.0f;
            for (size_t i = begin; i < end; ++i) {
                const TextGlyph& glyph = layout.glyphs[i];
                set_svg_canvas_font(ctx, layout.styles[static_cast<size_t>(glyph.style)]);
                float width = ctx.measure_text(glyph.utf8).width;
                if (!std::isfinite(width)) width = 0.0f;
                widths.push_back(width);
                total += width;
            }
            float offset = ref.start_is_percent
                ? sampler.length() * (std::clamp(ref.start_offset, -100000.0f, 100000.0f) / 100.0f)
                : ref.start_offset;
            const std::string& anchor =
                layout.styles[static_cast<size_t>(layout.glyphs[begin].style)].text_anchor;
            if (anchor == "middle") offset -= total * 0.5f;
            else if (anchor == "end") offset -= total;

            float along = offset;
            for (size_t i = begin; i < end; ++i) {
                const TextGlyph& glyph = layout.glyphs[i];
                const float width = widths[i - begin];
                if (along < 0.0f) { along += width; continue; }
                float px = 0.0f, py = 0.0f, angle = 0.0f;
                if (!sampler.at(along, px, py, angle)) break;
                TextSegment seg;
                seg.style = glyph.style;
                seg.text = glyph.utf8;
                seg.x = px;
                seg.y = py;
                seg.rotated = true;
                seg.rotate = angle;
                seg.width = width;
                give_transform(seg);
                segments.push_back(std::move(seg));
                along += width;
            }
        } else {
            float cursor_x = carry_x, cursor_y = carry_y;
            std::string open_text;
            int open_style = -1;
            float open_x = 0.0f, open_y = 0.0f;

            auto flush = [&]() {
                if (open_text.empty()) return;
                TextSegment seg;
                seg.style = open_style;
                seg.text = open_text;
                seg.x = open_x;
                seg.y = open_y;
                give_transform(seg);
                set_svg_canvas_font(ctx, layout.styles[static_cast<size_t>(seg.style)]);
                float width = ctx.measure_text(seg.text).width;
                if (!std::isfinite(width)) width = 0.0f;
                seg.width = width;
                cursor_x = seg.x + width;
                cursor_y = seg.y;
                segments.push_back(std::move(seg));
                open_text.clear();
                open_style = -1;
            };

            for (size_t i = begin; i < end; ++i) {
                const TextGlyph& glyph = layout.glyphs[i];
                const bool moves = glyph.has_x || glyph.has_y || glyph.dx != 0.0f ||
                                   glyph.dy != 0.0f || glyph.has_rotate;
                if (moves) {
                    flush();
                    if (glyph.has_x) cursor_x = glyph.x;
                    if (glyph.has_y) cursor_y = glyph.y;
                    cursor_x += glyph.dx;
                    cursor_y += glyph.dy;
                }
                if (glyph.has_rotate) {
                    // A rotated glyph turns about its own origin, so it cannot
                    // share a segment with its neighbours.
                    TextSegment seg;
                    seg.style = glyph.style;
                    seg.text = glyph.utf8;
                    seg.x = cursor_x;
                    seg.y = cursor_y;
                    seg.rotated = true;
                    seg.rotate = glyph.rotate;
                    give_transform(seg);
                    set_svg_canvas_font(ctx, layout.styles[static_cast<size_t>(seg.style)]);
                    float width = ctx.measure_text(seg.text).width;
                    if (!std::isfinite(width)) width = 0.0f;
                    seg.width = width;
                    cursor_x += width;
                    segments.push_back(std::move(seg));
                    continue;
                }
                if (open_text.empty() || glyph.style != open_style) {
                    flush();
                    open_style = glyph.style;
                    open_x = cursor_x;
                    open_y = cursor_y;
                }
                open_text += glyph.utf8;
            }
            flush();

            if (segments.empty()) { begin = end; continue; }

            // text-anchor is measured over the whole chunk, which is why the
            // segments are laid out before any of them is drawn.
            const float extent = cursor_x - segments.front().x;
            const std::string& anchor =
                layout.styles[static_cast<size_t>(layout.glyphs[begin].style)].text_anchor;
            float shift = 0.0f;
            if (anchor == "middle") shift = -extent * 0.5f;
            else if (anchor == "end") shift = -extent;
            for (auto& seg : segments) seg.x += shift;

            carry_x = cursor_x + shift;
            carry_y = cursor_y;
        }

        if (segments.empty()) { begin = end; continue; }

        // The bounding box a `objectBoundingBox` gradient is resolved against
        // is the box the chunk's glyphs occupy.
        BBox bbox;
        for (const auto& seg : segments) {
            set_svg_canvas_font(ctx, layout.styles[static_cast<size_t>(seg.style)]);
            const auto metrics = ctx.measure_text(seg.text);
            const float ascent = metrics.font_bounding_box_ascent;
            const float descent = metrics.font_bounding_box_descent;
            if (seg.rotated) {
                // A turned glyph can reach in any direction; its own advance and
                // height are the reach, on both axes.
                const float reach = ascent + descent + seg.width;
                bbox.include(seg.x - reach, seg.y - reach);
                bbox.include(seg.x + reach, seg.y + reach);
            } else {
                bbox.include(seg.x, seg.y - ascent);
                bbox.include(seg.x + seg.width, seg.y + descent);
            }
        }
        for (const auto& seg : segments) {
            paint_text_segment(ctx, doc_ctx, layout.styles[static_cast<size_t>(seg.style)], seg,
                               bbox);
        }
        begin = end;
    }
}

void render_element(
    CanvasRenderingContext2D& ctx,
    const SvgDocumentContext& doc_ctx,
    const std::shared_ptr<Element>& el,
    const SvgStyle& parent_style,
    float ref_w,
    float ref_h,
    int use_depth,
    std::unordered_set<std::string>& active_use_ids
) {
    if (!el || stack_is_exhausted()) return;

    const std::string tag = to_lower_ascii(el->tag_name());
    if (tag == "defs" || tag == "clippath" || tag == "mask" || tag == "symbol" ||
        tag == "lineargradient" || tag == "radialgradient" ||
        tag == "title" || tag == "desc" || tag == "metadata" ||
        tag == "style" || tag == "script") {
        return;
    }

    const float ref_size = std::hypot(ref_w, ref_h) / 1.41421356f;
    SvgStyle style = resolve_element_style(el, parent_style, ref_size);
    if (style.display_none || style.opacity <= 0.0f) return;

    ctx.save();

    // Apply element transform
    std::string tf_str;
    if (get_attr_ci(el, "transform", tf_str)) {
        ctx.set_transform(ctx.current_transform().multiply(parse_svg_transform(tf_str)));
    }

    // Apply clip-path="url(#id)" if present
    std::string clip_attr;
    if (get_attr_ci(el, "clip-path", clip_attr)) {
        std::string clip_id;
        std::string_view rem;
        if (parse_url_ref(clip_attr, clip_id, rem)) {
            auto cit = doc_ctx.by_id.find(clip_id);
            if (cit != doc_ctx.by_id.end() && to_lower_ascii(cit->second->tag_name()) == "clippath") {
                apply_clip_path_element(ctx, cit->second, ref_w, ref_h);
            }
        }
    }

    if (tag == "text") {
        TextCollector collector(doc_ctx, ref_w, ref_h);
        const TextLayoutData layout = collector.collect(el, style);
        paint_text(ctx, doc_ctx, layout);
        ctx.restore();
        return;
    }

    if (tag == "g" || tag == "a") {
        for (const auto& child : el->children()) {
            if (auto child_el = std::dynamic_pointer_cast<Element>(child)) {
                render_element(ctx, doc_ctx, child_el, style, ref_w, ref_h, use_depth, active_use_ids);
            }
        }
        ctx.restore();
        return;
    }

    if (tag == "svg") {
        // Nested <svg> element
        float nx = 0.0f, ny = 0.0f, nw = ref_w, nh = ref_h;
        std::string s;
        if (get_attr_ci(el, "x", s)) parse_float_value(s, nx, ref_w);
        if (get_attr_ci(el, "y", s)) parse_float_value(s, ny, ref_h);
        if (get_attr_ci(el, "width", s)) parse_float_value(s, nw, ref_w);
        if (get_attr_ci(el, "height", s)) parse_float_value(s, nh, ref_h);
        if (nw > 0.0f && nh > 0.0f) {
            ctx.translate(nx, ny);
            float sub_ref_w = nw, sub_ref_h = nh;
            std::string vb_str, par_str;
            if (get_attr_ci(el, "viewBox", vb_str)) {
                ViewBox sub_vb = parse_viewbox(vb_str);
                if (sub_vb.valid) {
                    get_attr_ci(el, "preserveAspectRatio", par_str);
                    ctx.set_transform(ctx.current_transform().multiply(
                        compute_viewbox_matrix(sub_vb, nw, nh, par_str)));
                    sub_ref_w = sub_vb.width;
                    sub_ref_h = sub_vb.height;
                }
            }
            for (const auto& child : el->children()) {
                if (auto child_el = std::dynamic_pointer_cast<Element>(child)) {
                    render_element(ctx, doc_ctx, child_el, style, sub_ref_w, sub_ref_h, use_depth, active_use_ids);
                }
            }
        }
        ctx.restore();
        return;
    }

    if (tag == "use") {
        if (use_depth < kMaxUseDepth) {
            std::string href;
            if (get_href_attr(el, href)) {
                std::string target_id = extract_fragment_id(href);
                auto it = doc_ctx.by_id.find(target_id);
                if (!target_id.empty() && it != doc_ctx.by_id.end() &&
                    active_use_ids.find(target_id) == active_use_ids.end()) {
                    float ux = 0.0f, uy = 0.0f;
                    std::string s;
                    if (get_attr_ci(el, "x", s)) parse_float_value(s, ux, ref_w);
                    if (get_attr_ci(el, "y", s)) parse_float_value(s, uy, ref_h);
                    if (ux != 0.0f || uy != 0.0f) ctx.translate(ux, uy);

                    active_use_ids.insert(target_id);
                    auto target = it->second;
                    std::string target_tag = to_lower_ascii(target->tag_name());
                    if (target_tag == "symbol") {
                        float uw = ref_w, uh = ref_h;
                        if (get_attr_ci(el, "width", s)) parse_float_value(s, uw, ref_w);
                        if (get_attr_ci(el, "height", s)) parse_float_value(s, uh, ref_h);
                        float sym_ref_w = uw, sym_ref_h = uh;
                        std::string vb_str, par_str;
                        if (get_attr_ci(target, "viewBox", vb_str)) {
                            ViewBox sym_vb = parse_viewbox(vb_str);
                            if (sym_vb.valid && uw > 0.0f && uh > 0.0f) {
                                get_attr_ci(target, "preserveAspectRatio", par_str);
                                ctx.set_transform(ctx.current_transform().multiply(
                                    compute_viewbox_matrix(sym_vb, uw, uh, par_str)));
                                sym_ref_w = sym_vb.width;
                                sym_ref_h = sym_vb.height;
                            }
                        }
                        for (const auto& ch : target->children()) {
                            if (auto ch_el = std::dynamic_pointer_cast<Element>(ch)) {
                                render_element(ctx, doc_ctx, ch_el, style, sym_ref_w, sym_ref_h,
                                               use_depth + 1, active_use_ids);
                            }
                        }
                    } else {
                        render_element(ctx, doc_ctx, target, style, ref_w, ref_h,
                                       use_depth + 1, active_use_ids);
                    }
                    active_use_ids.erase(target_id);
                }
            }
        }
        ctx.restore();
        return;
    }

    if (style.visible) {
        ctx.begin_path();
        BBox bbox;
        bool allow_fill = true;
        if (build_shape_geometry(ctx, el, ref_w, ref_h, bbox, allow_fill)) {
            paint_shape(ctx, doc_ctx, style, bbox, allow_fill);
        }
    }

    ctx.restore();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// The box an element draws: SVG 2 §8.10, answered from the painter's geometry
// ---------------------------------------------------------------------------
//
// `getBBox()` asks this. It must not be a second implementation of where a
// shape is: the paths below are `build_shape_geometry` and `build_svg_path`,
// the same two the painter fills and strokes, so a shape cannot be in one
// place for the DOM and another on the screen. Nothing here parses an
// attribute the first time.
//
// A canvas is the sink those two write their path into, and a 1x1 one holds no
// pixels worth the name; the path commands are not drawn. The box they report
// is in the element's own user space -- `build_shape_geometry` resolves each
// length against the viewport's, and no transform has been applied yet.

namespace {

// Elements that are not rendered where they stand, so a box walk contributes
// nothing from them. This is the set `render_element` returns on, minus the
// elements that *are* drawn somewhere else (`use`).
bool svg_is_unrendered_container(std::string_view tag) {
    return tag == "defs" || tag == "clippath" || tag == "mask" || tag == "symbol" ||
           tag == "lineargradient" || tag == "radialgradient" ||
           tag == "title" || tag == "desc" || tag == "metadata" ||
           tag == "style" || tag == "script";
}

// The union of two boxes. An invalid box has no say in the answer: a
// container's box is the union of the children that *draw*, and a child that
// draws nothing must not pull the box to the origin.
BBox svg_union_bbox(const BBox& a, const BBox& b) {
    if (!a.valid()) return b;
    if (!b.valid()) return a;
    BBox both;
    both.include(a.min_x, a.min_y);
    both.include(a.max_x, a.max_y);
    both.include(b.min_x, b.min_y);
    both.include(b.max_x, b.max_y);
    return both;
}

// One element's own geometry, in its own user space, and whether it drew.
//
// A shape that draws nothing still *has* a box when its geometry is merely
// degenerate: a `rect` with `width="0"` and a position answers a zero-width box
// where that position is (`[5,5,0,4]`), not an empty one, which is what a
// browser answers. `build_shape_geometry` returns false for it because there is
// nothing to paint, so the degenerate shapes are answered here.
BBox svg_own_geometry(const std::shared_ptr<Element>& el, float ref_w, float ref_h) {
    BBox box;
    CanvasRenderingContext2D sink(1, 1);
    bool allow_fill = true;
    if (build_shape_geometry(sink, el, ref_w, ref_h, box, allow_fill)) return box;

    const std::string tag = to_lower_ascii(el->tag_name());
    if (tag == "rect") {
        auto attr = [&](std::string_view name, float def, float pct) {
            std::string text;
            float value = def;
            return (get_attr_ci(el, name, text) && parse_float_value(text, value, pct)) ? value : def;
        };
        const float x = attr("x", 0.0f, ref_w);
        const float y = attr("y", 0.0f, ref_h);
        const float w = attr("width", 0.0f, ref_w);
        const float h = attr("height", 0.0f, ref_h);
        // Empty only when both sides vanish: one zero side is a line of a box.
        if (w <= 0.0f && h <= 0.0f) return BBox();
        box.include(x, y);
        box.include(x + std::max(0.0f, w), y + std::max(0.0f, h));
    }
    return box;
}

// Transform the four corners of a box, as the container walk does for a child.
BBox svg_map_bbox(const BBox& box, const TransformMatrix2D& matrix) {
    if (!box.valid()) return box;
    BBox mapped;
    for (float x : {box.min_x, box.max_x}) {
        for (float y : {box.min_y, box.max_y}) {
            const Point p = matrix.map_point(x, y);
            mapped.include(p.x, p.y);
        }
    }
    return mapped;
}

// The box of `el` and everything it draws below it, with every descendant's
// `transform` applied and `el`'s own excluded.
BBox svg_bbox_walk(const std::shared_ptr<Element>& el, float ref_w, float ref_h, int depth,
                   std::unordered_set<const Element*>& active) {
    if (!el || depth > 64) return BBox();
    const std::string tag = to_lower_ascii(el->tag_name());
    if (svg_is_unrendered_container(tag)) return BBox();

    BBox box = svg_own_geometry(el, ref_w, ref_h);
    if (tag == "use") {
        std::string href;
        auto document = el->owner_document();
        if (!document || !get_href_attr(el, href)) return box;
        const std::string id = extract_fragment_id(href);
        auto target = id.empty() ? nullptr : document->get_element_by_id(id);
        // Symbols and nested SVG viewports need their own viewBox handling.
        if (!target || to_lower_ascii(target->tag_name()) == "symbol" ||
            to_lower_ascii(target->tag_name()) == "svg") return box;
        if (!active.insert(target.get()).second) return box;
        BBox target_box = svg_bbox_walk(target, ref_w, ref_h, depth + 1, active);
        active.erase(target.get());
        std::string value;
        if (get_attr_ci(target, "transform", value))
            target_box = svg_map_bbox(target_box, parse_svg_transform(value));
        float x = 0, y = 0;
        if (get_attr_ci(el, "x", value)) parse_float_value(value, x, ref_w);
        if (get_attr_ci(el, "y", value)) parse_float_value(value, y, ref_h);
        if (target_box.valid()) {
            target_box.min_x += x; target_box.max_x += x;
            target_box.min_y += y; target_box.max_y += y;
        }
        return target_box;
    }
    for (const auto& child : el->children()) {
        auto child_el = std::dynamic_pointer_cast<Element>(child);
        if (!child_el) continue;
        BBox child_box = svg_bbox_walk(child_el, ref_w, ref_h, depth + 1, active);
        if (!child_box.valid()) continue;
        std::string transform_text;
        if (get_attr_ci(child_el, "transform", transform_text)) {
            const TransformMatrix2D matrix = parse_svg_transform(transform_text);
            const TransformMatrix2D identity = TransformMatrix2D::identity();
            if (!(matrix == identity)) {
                child_box = svg_map_bbox(child_box, matrix);
            }
        }
        box = svg_union_bbox(box, child_box);
    }
    return box;
}

// The coordinate system a `<svg>`'s children draw in, which is what their
// percentages resolve against. A viewBox replaces it: `<svg width="200"
// viewBox="0 0 20 10">` is 20 user units wide inside, so `width="50%"` on a
// child is 10 and not 100. This is the same rule `SvgRenderer::render` applies
// when it hands `doc_ctx.viewport_w`/`_h` to the children it paints, and
// Chromium answers it the same way.
void svg_viewport_of(const std::shared_ptr<Element>& svg, float& width, float& height) {
    width = 300.0f;
    height = 150.0f;
    std::string attr;
    float parsed = 0.0f;
    const bool has_view_box = get_attr_ci(svg, "viewBox", attr);
    ViewBox view_box;
    if (has_view_box) view_box = parse_viewbox(attr);
    if (has_view_box && view_box.valid && view_box.width > 0.0f && view_box.height > 0.0f) {
        width = view_box.width;
        height = view_box.height;
        return;
    }
    if (get_attr_ci(svg, "width", attr) && parse_float_value(attr, parsed, 300.0f) && parsed > 0.0f) {
        width = parsed;
    }
    if (get_attr_ci(svg, "height", attr) && parse_float_value(attr, parsed, 150.0f) && parsed > 0.0f) {
        height = parsed;
    }
}

}  // namespace

SvgUserBBox svg_element_bbox(const std::shared_ptr<Element>& element) {
    SvgUserBBox answer;
    if (!element) return answer;

    // The lengths inside a box resolve against the viewport the element is
    // drawn in, exactly as the painter resolves them: the nearest `<svg>`
    // ancestor's. A percentage on a `<rect>` is a share of that, not of the
    // rectangle.
    float ref_w = 300.0f, ref_h = 150.0f;
    for (std::shared_ptr<Node> node = element; node; node = node->parent()) {
        auto el = std::dynamic_pointer_cast<Element>(node);
        if (el && to_lower_ascii(el->tag_name()) == "svg") {
            svg_viewport_of(el, ref_w, ref_h);
            break;
        }
    }

    std::unordered_set<const Element*> active{element.get()};
    const BBox box = svg_bbox_walk(element, ref_w, ref_h, 0, active);
    if (!box.valid()) return answer;
    answer.x = box.min_x;
    answer.y = box.min_y;
    answer.width = box.width();
    answer.height = box.height();
    answer.valid = true;
    return answer;
}

std::shared_ptr<ImageData> SvgRenderer::render(
    const std::shared_ptr<Element>& svg_element,
    float target_width,
    float target_height
) {
    if (!svg_element) return nullptr;

    float w = target_width;
    float h = target_height;
    std::string attr;

    if (w <= 0.0f && get_attr_ci(svg_element, "width", attr)) {
        float parsed = 0.0f;
        if (parse_float_value(attr, parsed, 300.0f)) w = parsed;
    }
    if (h <= 0.0f && get_attr_ci(svg_element, "height", attr)) {
        float parsed = 0.0f;
        if (parse_float_value(attr, parsed, 150.0f)) h = parsed;
    }

    ViewBox vb;
    if (get_attr_ci(svg_element, "viewBox", attr)) {
        vb = parse_viewbox(attr);
        if (w <= 0.0f && vb.valid) w = vb.width;
        if (h <= 0.0f && vb.valid) h = vb.height;
    }

    // Written so that a NaN also takes the default.
    if (!(w > 0.0f)) w = 300.0f;
    if (!(h > 0.0f)) h = 150.0f;

    // An <svg> that is too large to draw into a bitmap, or that comes when
    // the page's other bitmaps have used up the budget, keeps its box and
    // stays blank (RasterBudget.hpp).
    if (w > static_cast<float>(RasterBudget::kMaxSide) ||
        h > static_cast<float>(RasterBudget::kMaxSide)) {
        return nullptr;
    }
    const int img_w = std::max(1, static_cast<int>(std::round(w)));
    const int img_h = std::max(1, static_cast<int>(std::round(h)));

    CanvasRenderingContext2D ctx(img_w, img_h);
    if (!ctx.has_backing_store()) return nullptr;

    SvgDocumentContext doc_ctx;
    doc_ctx.viewport_w = vb.valid ? vb.width : w;
    doc_ctx.viewport_h = vb.valid ? vb.height : h;
    doc_ctx.index_tree(svg_element);

    std::string par_str;
    get_attr_ci(svg_element, "preserveAspectRatio", par_str);
    if (vb.valid) {
        ctx.set_transform(compute_viewbox_matrix(vb, w, h, par_str));
    }

    // Root <svg> can itself specify presentation attributes (fill, stroke, color, opacity, style)
    SvgStyle default_style;
    const float root_ref_size = std::hypot(doc_ctx.viewport_w, doc_ctx.viewport_h) / 1.41421356f;
    SvgStyle root_style = resolve_element_style(svg_element, default_style, root_ref_size);
    if (root_style.display_none || root_style.opacity <= 0.0f) {
        return ctx.image_data();
    }

    std::unordered_set<std::string> active_use_ids;
    for (const auto& child : svg_element->children()) {
        if (auto child_el = std::dynamic_pointer_cast<Element>(child)) {
            render_element(ctx, doc_ctx, child_el, root_style,
                           doc_ctx.viewport_w, doc_ctx.viewport_h, 0, active_use_ids);
        }
    }

    return ctx.image_data();
}

} // namespace litebird

