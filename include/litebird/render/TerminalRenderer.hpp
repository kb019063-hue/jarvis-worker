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

    void render(const DisplayList& display_list, std::ostream& os = std::cout);

private:
    void execute_command(const PaintCommand& cmd);

    int m_cols;
    int m_rows;
    float m_cell_w;
    float m_cell_h;
    std::vector<TerminalCell> m_grid;
};

} // namespace litebird
