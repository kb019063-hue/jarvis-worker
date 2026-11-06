#include "litebird/render/Canvas2D.hpp"
#include <cmath>
#include <algorithm>

namespace litebird {

CanvasRenderingContext2D::CanvasRenderingContext2D(int width, int height)
    : m_width(std::max(1, width)), m_height(std::max(1, height)) {
    m_image = std::make_shared<ImageData>();
    m_image->width = m_width;
    m_image->height = m_height;
    m_image->pixels.resize(static_cast<size_t>(m_width * m_height), 0);
}

void CanvasRenderingContext2D::resize(int w, int h) {
    m_width = std::max(1, w);
    m_height = std::max(1, h);
    m_image->width = m_width;
    m_image->height = m_height;
    m_image->pixels.assign(static_cast<size_t>(m_width * m_height), 0);
}

void CanvasRenderingContext2D::set_pixel_safe(int x, int y, Color c) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    size_t idx = static_cast<size_t>(y * m_width + x);

    if (c.a == 255) {
        m_image->pixels[idx] = c.to_argb();
    } else if (c.a > 0) {
        Color dst = Color::from_argb(m_image->pixels[idx]);
        float sa = c.a / 255.0f;
        float da = dst.a / 255.0f;
        float out_a = sa + da * (1.0f - sa);

        if (out_a > 0.0f) {
            uint8_t r = static_cast<uint8_t>((c.r * sa + dst.r * da * (1.0f - sa)) / out_a);
            uint8_t g = static_cast<uint8_t>((c.g * sa + dst.g * da * (1.0f - sa)) / out_a);
            uint8_t b = static_cast<uint8_t>((c.b * sa + dst.b * da * (1.0f - sa)) / out_a);
            uint8_t a = static_cast<uint8_t>(out_a * 255.0f);
            m_image->pixels[idx] = Color(r, g, b, a).to_argb();
        }
    }
}

void CanvasRenderingContext2D::fill_rect(float x, float y, float w, float h) {
    int x0 = std::max(0, static_cast<int>(std::round(x)));
    int y0 = std::max(0, static_cast<int>(std::round(y)));
    int x1 = std::min(m_width, static_cast<int>(std::round(x + w)));
    int y1 = std::min(m_height, static_cast<int>(std::round(y + h)));

    uint32_t argb = fill_style.to_argb();
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            if (fill_style.a == 255) {
                m_image->pixels[py * m_width + px] = argb;
            } else {
                set_pixel_safe(px, py, fill_style);
            }
        }
    }
}

void CanvasRenderingContext2D::clear_rect(float x, float y, float w, float h) {
    int x0 = std::max(0, static_cast<int>(std::round(x)));
    int y0 = std::max(0, static_cast<int>(std::round(y)));
    int x1 = std::min(m_width, static_cast<int>(std::round(x + w)));
    int y1 = std::min(m_height, static_cast<int>(std::round(y + h)));

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            m_image->pixels[py * m_width + px] = 0;
        }
    }
}

void CanvasRenderingContext2D::stroke_rect(float x, float y, float w, float h) {
    int thick = std::max(1, static_cast<int>(std::round(line_width)));
    // Top
    fill_rect(x, y, w, static_cast<float>(thick));
    // Bottom
    fill_rect(x, y + h - thick, w, static_cast<float>(thick));
    // Left
    fill_rect(x, y, static_cast<float>(thick), h);
    // Right
    fill_rect(x + w - thick, y, static_cast<float>(thick), h);
}

void CanvasRenderingContext2D::begin_path() {
    m_paths.clear();
}

void CanvasRenderingContext2D::close_path() {
    if (!m_paths.empty()) {
        m_paths.back().closed = true;
    }
}

void CanvasRenderingContext2D::move_to(float x, float y) {
    m_paths.push_back({ { Point(x, y) }, false });
}

void CanvasRenderingContext2D::line_to(float x, float y) {
    if (m_paths.empty()) {
        move_to(x, y);
    } else {
        m_paths.back().points.push_back(Point(x, y));
    }
}

void CanvasRenderingContext2D::arc(float cx, float cy, float radius, float start_angle, float end_angle) {
    const int segments = 32;
    float delta = (end_angle - start_angle) / static_cast<float>(segments);

    for (int i = 0; i <= segments; ++i) {
        float angle = start_angle + delta * static_cast<float>(i);
        float px = cx + radius * std::cos(angle);
        float py = cy + radius * std::sin(angle);
        if (i == 0 && m_paths.empty()) {
            move_to(px, py);
        } else {
            line_to(px, py);
        }
    }
}

void CanvasRenderingContext2D::draw_line(int x0, int y0, int x1, int y1, Color c, int thickness) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int radius = thickness / 2;

    while (true) {
        if (thickness <= 1) {
            set_pixel_safe(x0, y0, c);
        } else {
            for (int oy = -radius; oy <= radius; ++oy) {
                for (int ox = -radius; ox <= radius; ++ox) {
                    if (ox * ox + oy * oy <= radius * radius + 1) {
                        set_pixel_safe(x0 + ox, y0 + oy, c);
                    }
                }
            }
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void CanvasRenderingContext2D::stroke() {
    int thick = std::max(1, static_cast<int>(std::round(line_width)));

    for (const auto& sp : m_paths) {
        if (sp.points.size() < 2) continue;
        for (size_t i = 0; i + 1 < sp.points.size(); ++i) {
            draw_line(static_cast<int>(std::round(sp.points[i].x)),
                      static_cast<int>(std::round(sp.points[i].y)),
                      static_cast<int>(std::round(sp.points[i + 1].x)),
                      static_cast<int>(std::round(sp.points[i + 1].y)),
                      stroke_style, thick);
        }
        if (sp.closed && sp.points.size() > 2) {
            draw_line(static_cast<int>(std::round(sp.points.back().x)),
                      static_cast<int>(std::round(sp.points.back().y)),
                      static_cast<int>(std::round(sp.points.front().x)),
                      static_cast<int>(std::round(sp.points.front().y)),
                      stroke_style, thick);
        }
    }
}

void CanvasRenderingContext2D::fill() {
    // Scanline fill algorithm for closed paths
    for (const auto& sp : m_paths) {
        if (sp.points.size() < 3) continue;

        float min_y = sp.points[0].y;
        float max_y = sp.points[0].y;
        for (const auto& pt : sp.points) {
            min_y = std::min(min_y, pt.y);
            max_y = std::max(max_y, pt.y);
        }

        int start_y = std::max(0, static_cast<int>(std::floor(min_y)));
        int end_y = std::min(m_height - 1, static_cast<int>(std::ceil(max_y)));

        for (int y = start_y; y <= end_y; ++y) {
            std::vector<float> node_x;
            size_t n = sp.points.size();
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                float y0 = sp.points[i].y;
                float y1 = sp.points[j].y;
                float x0 = sp.points[i].x;
                float x1 = sp.points[j].x;

                if ((y0 < static_cast<float>(y) && y1 >= static_cast<float>(y)) ||
                    (y1 < static_cast<float>(y) && y0 >= static_cast<float>(y))) {
                    float x = x0 + (static_cast<float>(y) - y0) / (y1 - y0) * (x1 - x0);
                    node_x.push_back(x);
                }
            }

            std::sort(node_x.begin(), node_x.end());

            for (size_t i = 0; i + 1 < node_x.size(); i += 2) {
                int px0 = std::max(0, static_cast<int>(std::round(node_x[i])));
                int px1 = std::min(m_width, static_cast<int>(std::round(node_x[i + 1])));
                for (int px = px0; px < px1; ++px) {
                    set_pixel_safe(px, y, fill_style);
                }
            }
        }
    }
}

void CanvasRenderingContext2D::draw_image(const std::shared_ptr<ImageData>& img, float dx, float dy, float dw, float dh) {
    if (!img || !img->is_valid()) return;

    int dest_w = (dw > 0.0f) ? static_cast<int>(std::round(dw)) : img->width;
    int dest_h = (dh > 0.0f) ? static_cast<int>(std::round(dh)) : img->height;

    for (int sy = 0; sy < dest_h; ++sy) {
        int py = static_cast<int>(std::round(dy)) + sy;
        if (py < 0 || py >= m_height) continue;

        for (int sx = 0; sx < dest_w; ++sx) {
            int px = static_cast<int>(std::round(dx)) + sx;
            if (px < 0 || px >= m_width) continue;

            int ix = (dest_w > 1) ? static_cast<int>(std::round(static_cast<float>(sx) / static_cast<float>(dest_w - 1) * static_cast<float>(img->width - 1))) : 0;
            int iy = (dest_h > 1) ? static_cast<int>(std::round(static_cast<float>(sy) / static_cast<float>(dest_h - 1) * static_cast<float>(img->height - 1))) : 0;
            Color c = img->get_pixel_color(ix, iy);
            set_pixel_safe(px, py, c);
        }
    }
}

JsValue CanvasRenderingContext2D::to_js_value(std::shared_ptr<CanvasRenderingContext2D> self) {
    JsValue ctx = JsValue::make_object();
    ctx.canvas_ctx = self;

    ctx.set_property("fillRect", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            fill_rect(static_cast<float>(args[0].to_number()),
                      static_cast<float>(args[1].to_number()),
                      static_cast<float>(args[2].to_number()),
                      static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("strokeRect", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            stroke_rect(static_cast<float>(args[0].to_number()),
                        static_cast<float>(args[1].to_number()),
                        static_cast<float>(args[2].to_number()),
                        static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("clearRect", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() >= 4) {
            clear_rect(static_cast<float>(args[0].to_number()),
                       static_cast<float>(args[1].to_number()),
                       static_cast<float>(args[2].to_number()),
                       static_cast<float>(args[3].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("beginPath", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        begin_path();
        return JsValue::make_undefined();
    }));

    ctx.set_property("closePath", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        close_path();
        return JsValue::make_undefined();
    }));

    ctx.set_property("moveTo", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            move_to(static_cast<float>(args[0].to_number()),
                    static_cast<float>(args[1].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("lineTo", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            line_to(static_cast<float>(args[0].to_number()),
                    static_cast<float>(args[1].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("arc", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.size() >= 5) {
            arc(static_cast<float>(args[0].to_number()),
                static_cast<float>(args[1].to_number()),
                static_cast<float>(args[2].to_number()),
                static_cast<float>(args[3].to_number()),
                static_cast<float>(args[4].to_number()));
        }
        return JsValue::make_undefined();
    }));

    ctx.set_property("stroke", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        stroke();
        return JsValue::make_undefined();
    }));

    ctx.set_property("fill", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        fill();
        return JsValue::make_undefined();
    }));

    // Properties: fillStyle, strokeStyle, lineWidth
    ctx.set_property("fillStyle", JsValue::make_string(fill_style.to_hex()));
    ctx.set_property("strokeStyle", JsValue::make_string(stroke_style.to_hex()));
    ctx.set_property("lineWidth", JsValue::make_number(line_width));

    return ctx;
}

} // namespace litebird
