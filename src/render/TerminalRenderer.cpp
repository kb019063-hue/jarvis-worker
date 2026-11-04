#include "litebird/render/TerminalRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace litebird {

TerminalRenderer::TerminalRenderer(int cols, int rows, float cell_w, float cell_h)
    : m_cols(cols), m_rows(rows), m_cell_w(cell_w), m_cell_h(cell_h), m_grid(cols * rows) {
    // Fill with default background
    for (auto& cell : m_grid) {
        cell.ch = ' ';
        cell.fg = Color(220, 220, 220);
        cell.bg = Color(30, 30, 30);
    }
}

void TerminalRenderer::execute_command(const PaintCommand& cmd) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, DrawShadowCommand>) {
            int start_col = std::clamp(static_cast<int>((arg.rect.x + arg.x_offset) / m_cell_w), 0, m_cols - 1);
            int end_col   = std::clamp(static_cast<int>((arg.rect.x + arg.x_offset + arg.rect.width) / m_cell_w), 0, m_cols - 1);
            int start_row = std::clamp(static_cast<int>((arg.rect.y + arg.y_offset) / m_cell_h), 0, m_rows - 1);
            int end_row   = std::clamp(static_cast<int>((arg.rect.y + arg.y_offset + arg.rect.height) / m_cell_h), 0, m_rows - 1);

            for (int r = start_row; r <= end_row; ++r) {
                for (int c = start_col; c <= end_col; ++c) {
                    m_grid[r * m_cols + c].bg = Color(15, 23, 42);
                }
            }
        } else if constexpr (std::is_same_v<T, DrawRectCommand>) {
            int start_col = std::clamp(static_cast<int>(arg.rect.x / m_cell_w), 0, m_cols - 1);
            int end_col   = std::clamp(static_cast<int>((arg.rect.x + arg.rect.width) / m_cell_w), 0, m_cols - 1);
            int start_row = std::clamp(static_cast<int>(arg.rect.y / m_cell_h), 0, m_rows - 1);
            int end_row   = std::clamp(static_cast<int>((arg.rect.y + arg.rect.height) / m_cell_h), 0, m_rows - 1);

            for (int r = start_row; r <= end_row; ++r) {
                for (int c = start_col; c <= end_col; ++c) {
                    m_grid[r * m_cols + c].bg = arg.color;
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBorderCommand>) {
            int start_col = std::clamp(static_cast<int>(arg.rect.x / m_cell_w), 0, m_cols - 1);
            int end_col   = std::clamp(static_cast<int>((arg.rect.x + arg.rect.width) / m_cell_w), 0, m_cols - 1);
            int start_row = std::clamp(static_cast<int>(arg.rect.y / m_cell_h), 0, m_rows - 1);
            int end_row   = std::clamp(static_cast<int>((arg.rect.y + arg.rect.height) / m_cell_h), 0, m_rows - 1);

            // Horizontal borders
            for (int c = start_col; c <= end_col; ++c) {
                m_grid[start_row * m_cols + c].ch = '-';
                m_grid[start_row * m_cols + c].fg = arg.color;
                m_grid[end_row * m_cols + c].ch = '-';
                m_grid[end_row * m_cols + c].fg = arg.color;
            }
            // Vertical borders
            for (int r = start_row; r <= end_row; ++r) {
                m_grid[r * m_cols + start_col].ch = '|';
                m_grid[r * m_cols + start_col].fg = arg.color;
                m_grid[r * m_cols + end_col].ch = '|';
                m_grid[r * m_cols + end_col].fg = arg.color;
            }
            // Corners
            m_grid[start_row * m_cols + start_col].ch = '+';
            m_grid[start_row * m_cols + end_col].ch = '+';
            m_grid[end_row * m_cols + start_col].ch = '+';
            m_grid[end_row * m_cols + end_col].ch = '+';

        } else if constexpr (std::is_same_v<T, DrawTextCommand>) {
            int col = static_cast<int>(arg.position.x / m_cell_w);
            int row = static_cast<int>(arg.position.y / m_cell_h);

            for (char ch : arg.text) {
                if (col >= 0 && col < m_cols && row >= 0 && row < m_rows) {
                    auto& cell = m_grid[row * m_cols + col];
                    cell.ch = ch;
                    cell.fg = arg.color;
                    cell.bold = (arg.font_weight >= 700);
                }
                col++;
            }
        }
    }, cmd);
}

void TerminalRenderer::render(const DisplayList& display_list, std::ostream& os) {
    for (const auto& cmd : display_list.commands()) {
        execute_command(cmd);
    }

    // Render ANSI grid
    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            const auto& cell = m_grid[r * m_cols + c];
            // Set ANSI colors
            os << "\033[38;2;" << (int)cell.fg.r << ";" << (int)cell.fg.g << ";" << (int)cell.fg.b << "m";
            os << "\033[48;2;" << (int)cell.bg.r << ";" << (int)cell.bg.g << ";" << (int)cell.bg.b << "m";
            if (cell.bold) os << "\033[1m";
            os << cell.ch;
            if (cell.bold) os << "\033[22m";
        }
        // Reset and newline
        os << "\033[0m\n";
    }
    os << "\033[0m" << std::flush;
}

} // namespace litebird
