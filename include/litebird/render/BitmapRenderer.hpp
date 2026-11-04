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

private:
    void execute_command(const PaintCommand& cmd);
    void set_pixel(int x, int y, Color color);
    void fill_rect(int rx, int ry, int rw, int rh, Color color);
    void draw_char(int x, int y, char c, Color color, int scale = 2);

    int m_width;
    int m_height;
    std::vector<Color> m_pixels;
};

} // namespace litebird
