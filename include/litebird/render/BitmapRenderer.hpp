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
    /// How much of the device pixel survives every active overflow clip: 1
    /// when all of it does, 0 when none does, and a fraction on the curve of a
    /// rounded clip. Returns false and leaves `coverage` at 0 when the pixel
    /// is outside.
    bool clip_coverage(int x, int y, float& coverage) const;
    void fill_rect(int rx, int ry, int rw, int rh, Color color);
    void draw_char(int x, int y, char c, Color color, int scale = 2);

    TransformMatrix2D current_transform() const {
        return m_transform_stack.empty() ? TransformMatrix2D::identity() : m_transform_stack.back();
    }

    /// One overflow clip: the box that was clipped and, when it had one, the
    /// corner radius it was clipped to. The radius is not part of `Rect`, and a
    /// clip of a 40x40 box with `border-radius: 16px` is not a rectangle.
    struct ClipRegion {
        Rect rect;
        float radius = 0.0f;
    };

    struct OpacityLayer {
        std::vector<Color> saved_pixels;
        float opacity{1.0f};
        bool skipped{false};
    };

    int m_width;
    int m_height;
    std::vector<Color> m_pixels;
    std::vector<TransformMatrix2D> m_transform_stack;
    std::vector<ClipRegion> m_clip_stack;
    std::vector<OpacityLayer> m_opacity_stack;
    int m_skip_depth{0};
};

} // namespace litebird
