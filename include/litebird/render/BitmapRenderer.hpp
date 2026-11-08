#pragma once

#include "litebird/render/PaintCommand.hpp"
#include <string>
#include <vector>

namespace litebird {

class BitmapRenderer {
public:
    BitmapRenderer(int width = 800, int height = 600);

    void clear(Color color);
    void render(const DisplayList& display_list);
    bool save_bmp(const std::string& filepath) const;
    Color get_pixel(int x, int y) const {
        if (x < 0 || x >= m_width || y < 0 || y >= m_height) return Color::transparent();
        return m_pixels[y * m_width + x];
    }

private:
    void execute_command(const PaintCommand& cmd);
    void set_pixel(int x, int y, Color color);
    /// True when the device pixel survives every active overflow clip.
    bool inside_clip(int x, int y) const;
    void fill_rect(int rx, int ry, int rw, int rh, Color color);
    void draw_char(int x, int y, char c, Color color, int scale = 2);

    TransformMatrix2D current_transform() const {
        return m_transform_stack.empty() ? TransformMatrix2D::identity() : m_transform_stack.back();
    }

    int m_width;
    int m_height;
    std::vector<Color> m_pixels;
    std::vector<TransformMatrix2D> m_transform_stack;
    std::vector<Rect> m_clip_stack;
};

} // namespace litebird
