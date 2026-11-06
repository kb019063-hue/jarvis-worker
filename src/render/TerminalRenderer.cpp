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
        if constexpr (std::is_same_v<T, PushTransformCommand>) {
            auto curr = current_transform();
            m_transform_stack.push_back(curr.multiply(arg.matrix));
        } else if constexpr (std::is_same_v<T, PopTransformCommand>) {
            if (!m_transform_stack.empty()) m_transform_stack.pop_back();
        } else if constexpr (std::is_same_v<T, DrawShadowCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            int start_col = std::clamp(static_cast<int>((tr.x + arg.x_offset - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int end_col   = std::clamp(static_cast<int>((tr.x + arg.x_offset + tr.width - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int start_row = std::clamp(static_cast<int>((tr.y + arg.y_offset - m_scroll_y) / m_cell_h), 0, m_rows - 1);
            int end_row   = std::clamp(static_cast<int>((tr.y + arg.y_offset + tr.height - m_scroll_y) / m_cell_h), 0, m_rows - 1);

            for (int r = start_row; r <= end_row; ++r) {
                for (int c = start_col; c <= end_col; ++c) {
                    m_grid[r * m_cols + c].bg = Color(15, 23, 42);
                }
            }
        } else if constexpr (std::is_same_v<T, DrawRectCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            int start_col = std::clamp(static_cast<int>((tr.x - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int end_col   = std::clamp(static_cast<int>((tr.x + tr.width - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int start_row = std::clamp(static_cast<int>((tr.y - m_scroll_y) / m_cell_h), 0, m_rows - 1);
            int end_row   = std::clamp(static_cast<int>((tr.y + tr.height - m_scroll_y) / m_cell_h), 0, m_rows - 1);

            for (int r = start_row; r <= end_row; ++r) {
                for (int c = start_col; c <= end_col; ++c) {
                    m_grid[r * m_cols + c].bg = arg.color;
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBorderCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            int start_col = std::clamp(static_cast<int>((tr.x - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int end_col   = std::clamp(static_cast<int>((tr.x + tr.width - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int start_row = std::clamp(static_cast<int>((tr.y - m_scroll_y) / m_cell_h), 0, m_rows - 1);
            int end_row   = std::clamp(static_cast<int>((tr.y + tr.height - m_scroll_y) / m_cell_h), 0, m_rows - 1);

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
            Point tp = current_transform().map_point(arg.position);
            int col = static_cast<int>((tp.x - m_scroll_x) / m_cell_w);
            int row = static_cast<int>((tp.y - m_scroll_y) / m_cell_h);

            for (char ch : arg.text) {
                if (col >= 0 && col < m_cols && row >= 0 && row < m_rows) {
                    auto& cell = m_grid[row * m_cols + col];
                    cell.ch = ch;
                    cell.fg = arg.color;
                    cell.bold = (arg.font_weight >= 700);
                }
                col++;
            }
        } else if constexpr (std::is_same_v<T, DrawImageCommand>) {
            if (arg.image && arg.image->is_valid()) {
                Rect tr = current_transform().map_rect(arg.rect);
                int start_col = std::clamp(static_cast<int>((tr.x - m_scroll_x) / m_cell_w), 0, m_cols - 1);
                int end_col   = std::clamp(static_cast<int>((tr.x + tr.width - m_scroll_x) / m_cell_w), 0, m_cols - 1);
                int start_row = std::clamp(static_cast<int>((tr.y - m_scroll_y) / m_cell_h), 0, m_rows - 1);
                int end_row   = std::clamp(static_cast<int>((tr.y + tr.height - m_scroll_y) / m_cell_h), 0, m_rows - 1);

                int span_cols = std::max(1, end_col - start_col);
                int span_rows = std::max(1, end_row - start_row);

                for (int r = start_row; r <= end_row; ++r) {
                    for (int c = start_col; c <= end_col; ++c) {
                        int sx = std::clamp((c - start_col) * arg.image->width / span_cols, 0, arg.image->width - 1);
                        int sy = std::clamp((r - start_row) * arg.image->height / span_rows, 0, arg.image->height - 1);
                        Color pixel = arg.image->get_pixel_color(sx, sy);
                        m_grid[r * m_cols + c].bg = pixel;
                        m_grid[r * m_cols + c].ch = ' ';
                    }
                }
            }
        }
    }, cmd);
}

void TerminalRenderer::draw_focus_ring(const Rect& rect, Color color) {
    int start_col = std::clamp(static_cast<int>((rect.x - m_scroll_x) / m_cell_w), 0, m_cols - 1);
    int end_col   = std::clamp(static_cast<int>((rect.x + rect.width - m_scroll_x) / m_cell_w), 0, m_cols - 1);
    int start_row = std::clamp(static_cast<int>((rect.y - m_scroll_y) / m_cell_h), 0, m_rows - 1);
    int end_row   = std::clamp(static_cast<int>((rect.y + rect.height - m_scroll_y) / m_cell_h), 0, m_rows - 1);

    for (int c = start_col; c <= end_col; ++c) {
        if (start_row >= 0 && start_row < m_rows) {
            m_grid[start_row * m_cols + c].fg = color;
            m_grid[start_row * m_cols + c].bold = true;
        }
        if (end_row >= 0 && end_row < m_rows) {
            m_grid[end_row * m_cols + c].fg = color;
            m_grid[end_row * m_cols + c].bold = true;
        }
    }
    for (int r = start_row; r <= end_row; ++r) {
        if (r >= 0 && r < m_rows) {
            m_grid[r * m_cols + start_col].fg = color;
            m_grid[r * m_cols + start_col].bold = true;
            m_grid[r * m_cols + end_col].fg = color;
            m_grid[r * m_cols + end_col].bold = true;
        }
    }
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

std::string TerminalRenderer::to_ansi_string(const DisplayList& display_list) {
    for (const auto& cmd : display_list.commands()) {
        execute_command(cmd);
    }

    std::ostringstream ss;
    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            const auto& cell = m_grid[r * m_cols + c];
            ss << "\033[38;2;" << (int)cell.fg.r << ";" << (int)cell.fg.g << ";" << (int)cell.fg.b << "m";
            ss << "\033[48;2;" << (int)cell.bg.r << ";" << (int)cell.bg.g << ";" << (int)cell.bg.b << "m";
            if (cell.bold) ss << "\033[1m";
            ss << cell.ch;
            if (cell.bold) ss << "\033[22m";
        }
        ss << "\033[0m\n";
    }
    ss << "\033[0m";
    return ss.str();
}

} // namespace litebird
