#pragma once

#include "litebird/render/PaintCommand.hpp"
#include <iostream>
#include <vector>
#include <string>

namespace litebird {

struct TerminalCell {
    char ch{' '};
    Color fg{Color::black()};
    Color bg{Color::white()};
    bool bold{false};
};

class TerminalRenderer {
public:
    TerminalRenderer(int cols = 80, int rows = 30, float cell_w = 10.0f, float cell_h = 20.0f);

    void set_scroll(float x, float y) { m_scroll_x = x; m_scroll_y = y; }
    float scroll_x() const { return m_scroll_x; }
    float scroll_y() const { return m_scroll_y; }

    void draw_focus_ring(const Rect& rect, Color color = Color(245, 158, 11));

    void render(const DisplayList& display_list, std::ostream& os = std::cout);
    std::string to_ansi_string(const DisplayList& display_list);

private:
    void execute_command(const PaintCommand& cmd);

    int m_cols;
    int m_rows;
    float m_cell_w;
    float m_cell_h;
    float m_scroll_x{0.0f};
    float m_scroll_y{0.0f};
    std::vector<TerminalCell> m_grid;
};

} // namespace litebird
