#include "litebird/render/SvgRenderer.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/RasterBudget.hpp"
#include "litebird/common/StackLimit.hpp"
#include <sstream>
#include <cmath>
#include <cctype>
#include <vector>
#include <algorithm>

namespace litebird {

namespace {

struct SvgStyle {
    Color fill{Color::black()};
    bool has_fill{true};
    Color stroke{Color::transparent()};
    bool has_stroke{false};
    float stroke_width{1.0f};
};

struct ViewBox {
    float min_x{0.0f};
    float min_y{0.0f};
    float width{0.0f};
    float height{0.0f};
    bool valid{false};
};

ViewBox parse_viewbox(const std::string& str) {
    ViewBox vb;
    // Replace commas with spaces
    std::string cleaned = str;
    for (char& c : cleaned) {
        if (c == ',') c = ' ';
    }
    std::stringstream ss2(cleaned);
    if (ss2 >> vb.min_x >> vb.min_y >> vb.width >> vb.height) {
        if (vb.width > 0.0f && vb.height > 0.0f) {
            vb.valid = true;
        }
    }
    return vb;
}

SvgStyle parse_style(const std::shared_ptr<Element>& el, const SvgStyle& parent_style) {
    SvgStyle style = parent_style;

    if (el->has_attribute("fill")) {
        std::string f = el->get_attribute("fill");
        if (f == "none" || f == "transparent") {
            style.has_fill = false;
        } else {
            style.fill = Color::parse(f);
            style.has_fill = true;
        }
    }

    if (el->has_attribute("stroke")) {
        std::string s = el->get_attribute("stroke");
        if (s == "none" || s == "transparent") {
            style.has_stroke = false;
        } else {
            style.stroke = Color::parse(s);
            style.has_stroke = true;
        }
    }

    if (el->has_attribute("stroke-width")) {
        try {
            style.stroke_width = std::stof(el->get_attribute("stroke-width"));
        } catch (...) {}
    }

    return style;
}

std::vector<float> parse_numbers(std::string_view str) {
    std::vector<float> nums;
    size_t i = 0;
    while (i < str.size()) {
        while (i < str.size() && (std::isspace(str[i]) || str[i] == ',')) i++;
        if (i >= str.size()) break;

        size_t start = i;
        if (str[i] == '+' || str[i] == '-') i++;
        bool has_dot = false;
        while (i < str.size() && (std::isdigit(str[i]) || (!has_dot && str[i] == '.'))) {
            if (str[i] == '.') has_dot = true;
            i++;
        }
        if (i > start) {
            try {
                nums.push_back(std::stof(std::string(str.substr(start, i - start))));
            } catch (...) {}
        } else {
            i++;
        }
    }
    return nums;
}

struct PointF {
    float x{0.0f};
    float y{0.0f};
};

PointF transform_point(float x, float y, const ViewBox& vb, float dest_w, float dest_h) {
    if (!vb.valid || vb.width <= 0.0f || vb.height <= 0.0f) {
        return {x, y};
    }
    float tx = (x - vb.min_x) / vb.width * dest_w;
    float ty = (y - vb.min_y) / vb.height * dest_h;
    return {tx, ty};
}

void parse_and_draw_path(
    CanvasRenderingContext2D& ctx,
    const std::string& d,
    const ViewBox& vb,
    float dest_w,
    float dest_h
) {
    ctx.begin_path();

    size_t i = 0;
    char current_cmd = ' ';
    float cur_x = 0.0f;
    float cur_y = 0.0f;
    float start_x = 0.0f;
    float start_y = 0.0f;

    auto next_token_or_cmd = [&]() -> char {
        while (i < d.size() && (std::isspace(d[i]) || d[i] == ',')) i++;
        if (i >= d.size()) return '\0';
        if (std::isalpha(d[i])) {
            return d[i++];
        }
        return ' ';
    };

    auto next_number = [&]() -> float {
        while (i < d.size() && (std::isspace(d[i]) || d[i] == ',')) i++;
        if (i >= d.size()) return 0.0f;
        size_t start = i;
        if (d[i] == '+' || d[i] == '-') i++;
        bool has_dot = false;
        while (i < d.size() && (std::isdigit(d[i]) || (!has_dot && d[i] == '.'))) {
            if (d[i] == '.') has_dot = true;
            i++;
        }
        if (i > start) {
            try {
                return std::stof(d.substr(start, i - start));
            } catch (...) {
                return 0.0f;
            }
        }
        return 0.0f;
    };

    while (i < d.size()) {
        char cmd = next_token_or_cmd();
        if (cmd == '\0') break;
        if (cmd != ' ') current_cmd = cmd;

        switch (current_cmd) {
            case 'M': { // moveto absolute
                float x = next_number();
                float y = next_number();
                cur_x = x; cur_y = y;
                start_x = cur_x; start_y = cur_y;
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.move_to(pt.x, pt.y);
                current_cmd = 'L'; // subsequent coordinates are lineto
                break;
            }
            case 'm': { // moveto relative
                float dx = next_number();
                float dy = next_number();
                cur_x += dx; cur_y += dy;
                start_x = cur_x; start_y = cur_y;
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.move_to(pt.x, pt.y);
                current_cmd = 'l';
                break;
            }
            case 'L': { // lineto absolute
                float x = next_number();
                float y = next_number();
                cur_x = x; cur_y = y;
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.line_to(pt.x, pt.y);
                break;
            }
            case 'l': { // lineto relative
                float dx = next_number();
                float dy = next_number();
                cur_x += dx; cur_y += dy;
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.line_to(pt.x, pt.y);
                break;
            }
            case 'H': { // horizontal lineto absolute
                cur_x = next_number();
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.line_to(pt.x, pt.y);
                break;
            }
            case 'h': { // horizontal lineto relative
                cur_x += next_number();
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.line_to(pt.x, pt.y);
                break;
            }
            case 'V': { // vertical lineto absolute
                cur_y = next_number();
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.line_to(pt.x, pt.y);
                break;
            }
            case 'v': { // vertical lineto relative
                cur_y += next_number();
                auto pt = transform_point(cur_x, cur_y, vb, dest_w, dest_h);
                ctx.line_to(pt.x, pt.y);
                break;
            }
            case 'C': { // cubic bezier absolute
                float x1 = next_number();
                float y1 = next_number();
                float x2 = next_number();
                float y2 = next_number();
                float x = next_number();
                float y = next_number();

                float p0x = cur_x, p0y = cur_y;
                for (int step = 1; step <= 8; ++step) {
                    float t = static_cast<float>(step) / 8.0f;
                    float u = 1.0f - t;
                    float bx = u * u * u * p0x + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x;
                    float by = u * u * u * p0y + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y;
                    auto pt = transform_point(bx, by, vb, dest_w, dest_h);
                    ctx.line_to(pt.x, pt.y);
                }
                cur_x = x; cur_y = y;
                break;
            }
            case 'c': { // cubic bezier relative
                float dx1 = next_number();
                float dy1 = next_number();
                float dx2 = next_number();
                float dy2 = next_number();
                float dx = next_number();
                float dy = next_number();

                float x1 = cur_x + dx1, y1 = cur_y + dy1;
                float x2 = cur_x + dx2, y2 = cur_y + dy2;
                float x = cur_x + dx, y = cur_y + dy;

                float p0x = cur_x, p0y = cur_y;
                for (int step = 1; step <= 8; ++step) {
                    float t = static_cast<float>(step) / 8.0f;
                    float u = 1.0f - t;
                    float bx = u * u * u * p0x + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x;
                    float by = u * u * u * p0y + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y;
                    auto pt = transform_point(bx, by, vb, dest_w, dest_h);
                    ctx.line_to(pt.x, pt.y);
                }
                cur_x = x; cur_y = y;
                break;
            }
            case 'Q': { // quadratic bezier absolute
                float x1 = next_number();
                float y1 = next_number();
                float x = next_number();
                float y = next_number();

                float p0x = cur_x, p0y = cur_y;
                for (int step = 1; step <= 6; ++step) {
                    float t = static_cast<float>(step) / 6.0f;
                    float u = 1.0f - t;
                    float bx = u * u * p0x + 2 * u * t * x1 + t * t * x;
                    float by = u * u * p0y + 2 * u * t * y1 + t * t * y;
                    auto pt = transform_point(bx, by, vb, dest_w, dest_h);
                    ctx.line_to(pt.x, pt.y);
                }
                cur_x = x; cur_y = y;
                break;
            }
            case 'q': { // quadratic bezier relative
                float dx1 = next_number();
                float dy1 = next_number();
                float dx = next_number();
                float dy = next_number();

                float x1 = cur_x + dx1, y1 = cur_y + dy1;
                float x = cur_x + dx, y = cur_y + dy;

                float p0x = cur_x, p0y = cur_y;
                for (int step = 1; step <= 6; ++step) {
                    float t = static_cast<float>(step) / 6.0f;
                    float u = 1.0f - t;
                    float bx = u * u * p0x + 2 * u * t * x1 + t * t * x;
                    float by = u * u * p0y + 2 * u * t * y1 + t * t * y;
                    auto pt = transform_point(bx, by, vb, dest_w, dest_h);
                    ctx.line_to(pt.x, pt.y);
                }
                cur_x = x; cur_y = y;
                break;
            }
            case 'Z':
            case 'z': { // closepath
                ctx.close_path();
                cur_x = start_x; cur_y = start_y;
                break;
            }
            default:
                i++;
                break;
        }
    }
}

void render_element(
    CanvasRenderingContext2D& ctx,
    const std::shared_ptr<Element>& el,
    const SvgStyle& parent_style,
    const ViewBox& vb,
    float dest_w,
    float dest_h
) {
    if (!el) return;
    // One call per level of <g> nesting, over a tree a script can make as deep
    // as it likes: stop where the stack does. What is nested deeper than that
    // is not drawn.
    if (stack_is_exhausted()) return;
    std::string tag = el->tag_name();
    SvgStyle style = parse_style(el, parent_style);

    ctx.fill_style = style.fill;
    ctx.stroke_style = style.stroke;
    ctx.line_width = style.stroke_width;

    if (tag == "g") {
        for (const auto& child : el->children()) {
            if (auto child_el = std::dynamic_pointer_cast<Element>(child)) {
                render_element(ctx, child_el, style, vb, dest_w, dest_h);
            }
        }
        return;
    }

    if (tag == "rect") {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        try {
            if (el->has_attribute("x")) x = std::stof(el->get_attribute("x"));
            if (el->has_attribute("y")) y = std::stof(el->get_attribute("y"));
            if (el->has_attribute("width")) w = std::stof(el->get_attribute("width"));
            if (el->has_attribute("height")) h = std::stof(el->get_attribute("height"));
        } catch (...) {}

        auto p0 = transform_point(x, y, vb, dest_w, dest_h);
        auto p1 = transform_point(x + w, y + h, vb, dest_w, dest_h);
        float rw = p1.x - p0.x;
        float rh = p1.y - p0.y;

        if (style.has_fill) {
            ctx.fill_rect(p0.x, p0.y, rw, rh);
        }
        if (style.has_stroke && style.stroke_width > 0) {
            ctx.stroke_rect(p0.x, p0.y, rw, rh);
        }
    } else if (tag == "circle") {
        float cx = 0.0f, cy = 0.0f, r = 0.0f;
        try {
            if (el->has_attribute("cx")) cx = std::stof(el->get_attribute("cx"));
            if (el->has_attribute("cy")) cy = std::stof(el->get_attribute("cy"));
            if (el->has_attribute("r")) r = std::stof(el->get_attribute("r"));
        } catch (...) {}

        auto pt = transform_point(cx, cy, vb, dest_w, dest_h);
        float scale = (vb.valid && vb.width > 0.0f) ? (dest_w / vb.width) : 1.0f;
        float r_scaled = r * scale;

        ctx.begin_path();
        ctx.arc(pt.x, pt.y, r_scaled, 0.0f, 6.2831853f);
        if (style.has_fill) ctx.fill();
        if (style.has_stroke && style.stroke_width > 0) ctx.stroke();
    } else if (tag == "ellipse") {
        float cx = 0.0f, cy = 0.0f, rx = 0.0f, ry = 0.0f;
        try {
            if (el->has_attribute("cx")) cx = std::stof(el->get_attribute("cx"));
            if (el->has_attribute("cy")) cy = std::stof(el->get_attribute("cy"));
            if (el->has_attribute("rx")) rx = std::stof(el->get_attribute("rx"));
            if (el->has_attribute("ry")) ry = std::stof(el->get_attribute("ry"));
        } catch (...) {}

        auto center = transform_point(cx, cy, vb, dest_w, dest_h);
        float scale_x = (vb.valid && vb.width > 0.0f) ? (dest_w / vb.width) : 1.0f;
        float scale_y = (vb.valid && vb.height > 0.0f) ? (dest_h / vb.height) : 1.0f;
        float rx_scaled = rx * scale_x;
        float ry_scaled = ry * scale_y;

        ctx.begin_path();
        for (int step = 0; step <= 32; ++step) {
            float angle = static_cast<float>(step) / 32.0f * 6.2831853f;
            float px = center.x + rx_scaled * std::cos(angle);
            float py = center.y + ry_scaled * std::sin(angle);
            if (step == 0) ctx.move_to(px, py);
            else ctx.line_to(px, py);
        }
        ctx.close_path();
        if (style.has_fill) ctx.fill();
        if (style.has_stroke && style.stroke_width > 0) ctx.stroke();
    } else if (tag == "line") {
        float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
        try {
            if (el->has_attribute("x1")) x1 = std::stof(el->get_attribute("x1"));
            if (el->has_attribute("y1")) y1 = std::stof(el->get_attribute("y1"));
            if (el->has_attribute("x2")) x2 = std::stof(el->get_attribute("x2"));
            if (el->has_attribute("y2")) y2 = std::stof(el->get_attribute("y2"));
        } catch (...) {}

        auto p1 = transform_point(x1, y1, vb, dest_w, dest_h);
        auto p2 = transform_point(x2, y2, vb, dest_w, dest_h);

        ctx.begin_path();
        ctx.move_to(p1.x, p1.y);
        ctx.line_to(p2.x, p2.y);
        if (style.has_stroke) ctx.stroke();
    } else if (tag == "polyline" || tag == "polygon") {
        std::string pts_str = el->get_attribute("points");
        auto nums = parse_numbers(pts_str);
        if (nums.size() >= 2) {
            ctx.begin_path();
            auto first = transform_point(nums[0], nums[1], vb, dest_w, dest_h);
            ctx.move_to(first.x, first.y);
            for (size_t k = 2; k + 1 < nums.size(); k += 2) {
                auto pt = transform_point(nums[k], nums[k + 1], vb, dest_w, dest_h);
                ctx.line_to(pt.x, pt.y);
            }
            if (tag == "polygon") {
                ctx.close_path();
                if (style.has_fill) ctx.fill();
            }
            if (style.has_stroke && style.stroke_width > 0) ctx.stroke();
        }
    } else if (tag == "path") {
        std::string d = el->get_attribute("d");
        if (!d.empty()) {
            parse_and_draw_path(ctx, d, vb, dest_w, dest_h);
            if (style.has_fill) ctx.fill();
            if (style.has_stroke && style.stroke_width > 0) ctx.stroke();
        }
    }
}

} // anonymous namespace

std::shared_ptr<ImageData> SvgRenderer::render(
    const std::shared_ptr<Element>& svg_element,
    float target_width,
    float target_height
) {
    if (!svg_element) return nullptr;

    float w = target_width;
    float h = target_height;

    if (w <= 0.0f && svg_element->has_attribute("width")) {
        try { w = std::stof(svg_element->get_attribute("width")); } catch (...) {}
    }
    if (h <= 0.0f && svg_element->has_attribute("height")) {
        try { h = std::stof(svg_element->get_attribute("height")); } catch (...) {}
    }

    ViewBox vb;
    if (svg_element->has_attribute("viewBox")) {
        vb = parse_viewbox(svg_element->get_attribute("viewBox"));
        if (w <= 0.0f && vb.valid) w = vb.width;
        if (h <= 0.0f && vb.valid) h = vb.height;
    }

    // Written so that a NaN also takes the default.
    if (!(w > 0.0f)) w = 300.0f;
    if (!(h > 0.0f)) h = 150.0f;

    // An <svg> that is too large to draw into a bitmap, or that comes when
    // the page's other bitmaps have used up the budget, keeps its box and
    // stays blank (RasterBudget.hpp). The comparison is made on the floats:
    // converting 1e12 to int would be undefined.
    if (w > static_cast<float>(RasterBudget::kMaxSide) || h > static_cast<float>(RasterBudget::kMaxSide)) return nullptr;
    int img_w = std::max(1, static_cast<int>(std::round(w)));
    int img_h = std::max(1, static_cast<int>(std::round(h)));

    CanvasRenderingContext2D ctx(img_w, img_h);
    if (!ctx.has_backing_store()) return nullptr;
    SvgStyle root_style;
    root_style.fill = Color::black();
    root_style.has_fill = true;
    root_style.stroke = Color::transparent();
    root_style.has_stroke = false;
    root_style.stroke_width = 1.0f;

    for (const auto& child : svg_element->children()) {
        if (auto child_el = std::dynamic_pointer_cast<Element>(child)) {
            render_element(ctx, child_el, root_style, vb, w, h);
        }
    }

    return ctx.image_data();
}

} // namespace litebird
