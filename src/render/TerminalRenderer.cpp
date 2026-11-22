#include "litebird/render/TerminalRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace litebird {

namespace {
Color over_opaque(Color src, Color dst) {
    const unsigned a = src.a;
    auto over = [a](unsigned s, unsigned d) -> uint8_t {
        return static_cast<uint8_t>((s * a + d * (255u - a) + 127u) / 255u);
    };
    return Color(over(src.r, dst.r), over(src.g, dst.g), over(src.b, dst.b));
}

struct CellRange { int first_col, last_col, first_row, last_row; };

// Return the terminal cells intersected by a rectangle. Its right and bottom
// edges are exclusive; also reject wholly offscreen rectangles rather than
// clamping them onto an edge cell. Clamp floats before converting them to int.
bool intersecting_cells(const Rect& rect, float scroll_x, float scroll_y,
                        int cols, int rows, float cell_w, float cell_h,
                        CellRange& cells) {
    if (cols <= 0 || rows <= 0 || cell_w <= 0 || cell_h <= 0 ||
        !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        !std::isfinite(scroll_x) || !std::isfinite(scroll_y) ||
        !std::isfinite(cell_w) || !std::isfinite(cell_h) ||
        rect.width <= 0 || rect.height <= 0 ||
        rect.x + rect.width <= scroll_x || rect.y + rect.height <= scroll_y ||
        rect.x >= scroll_x + cols * cell_w || rect.y >= scroll_y + rows * cell_h) return false;

    const float left = std::clamp((rect.x - scroll_x) / cell_w, 0.0f, static_cast<float>(cols));
    const float right = std::clamp((rect.x + rect.width - scroll_x) / cell_w, 0.0f, static_cast<float>(cols));
    const float top = std::clamp((rect.y - scroll_y) / cell_h, 0.0f, static_cast<float>(rows));
    const float bottom = std::clamp((rect.y + rect.height - scroll_y) / cell_h, 0.0f, static_cast<float>(rows));
    cells = {static_cast<int>(std::floor(left)), static_cast<int>(std::ceil(right)) - 1,
             static_cast<int>(std::floor(top)), static_cast<int>(std::ceil(bottom)) - 1};
    return cells.first_col <= cells.last_col && cells.first_row <= cells.last_row;
}
} // namespace

TerminalRenderer::TerminalRenderer(int cols, int rows, float cell_w, float cell_h)
    : m_cols(cols), m_rows(rows), m_cell_w(cell_w), m_cell_h(cell_h), m_grid(cols * rows) {
    // Fill with default background
    for (auto& cell : m_grid) {
        cell.ch = ' ';
        cell.fg = Color(220, 220, 220);
        cell.bg = Color(30, 30, 30);
    }
}

bool TerminalRenderer::inside_clip(int col, int row) const {
    if (m_clip_stack.empty()) return true;
    const float px = static_cast<float>(col) * m_cell_w + m_scroll_x;
    const float py = static_cast<float>(row) * m_cell_h + m_scroll_y;
    for (const auto& clip : m_clip_stack) {
        if (px < clip.x || py < clip.y ||
            px >= clip.x + clip.width || py >= clip.y + clip.height) {
            return false;
        }
    }
    return true;
}

void TerminalRenderer::execute_command(const PaintCommand& cmd) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, DrawCanvasCommand>) {
            // A canvas covers the viewport even after terminal scrolling. Its
            // transparent channels composite over the ordinary white canvas,
            // just as they do in the bitmap/PNG renderer.
            const Color background = over_opaque(arg.color, Color::white());
            for (int row = 0; row < m_rows; ++row) {
                for (int col = 0; col < m_cols; ++col) {
                    if (inside_clip(col, row)) m_grid[row * m_cols + col].bg = background;
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBackdropCommand>) {
            // A terminal cell has no alpha to blend with, so the backdrop's
            // colour is composited over whatever each cell already shows. The
            // UA colour is 10% black, which is what makes a modal dialog's
            // surroundings visibly dimmer here too.
            for (int row = 0; row < m_rows; ++row) {
                for (int col = 0; col < m_cols; ++col) {
                    if (!inside_clip(col, row)) continue;
                    auto& cell = m_grid[row * m_cols + col];
                    cell.bg = over_opaque(arg.color, cell.bg);
                }
            }
        } else if constexpr (std::is_same_v<T, PushTransformCommand>) {
            auto curr = current_transform();
            m_transform_stack.push_back(curr.multiply(arg.matrix));
        } else if constexpr (std::is_same_v<T, PopTransformCommand>) {
            if (!m_transform_stack.empty()) m_transform_stack.pop_back();
        } else if constexpr (std::is_same_v<T, PushClipCommand>) {
            m_clip_stack.push_back(current_transform().map_rect(arg.rect));
        } else if constexpr (std::is_same_v<T, PopClipCommand>) {
            if (!m_clip_stack.empty()) m_clip_stack.pop_back();
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
            CellRange cells;
            if (!intersecting_cells(tr, m_scroll_x, m_scroll_y, m_cols, m_rows,
                                    m_cell_w, m_cell_h, cells)) return;
            for (int r = cells.first_row; r <= cells.last_row; ++r) {
                for (int c = cells.first_col; c <= cells.last_col; ++c) {
                    if (!inside_clip(c, r)) continue;
                    auto& cell = m_grid[r * m_cols + c];
                    cell.bg = over_opaque(arg.color, cell.bg);
                }
            }
        } else if constexpr (std::is_same_v<T, DrawBorderCommand>) {
            Rect tr = current_transform().map_rect(arg.rect);
            int start_col = std::clamp(static_cast<int>((tr.x - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int end_col   = std::clamp(static_cast<int>((tr.x + tr.width - m_scroll_x) / m_cell_w), 0, m_cols - 1);
            int start_row = std::clamp(static_cast<int>((tr.y - m_scroll_y) / m_cell_h), 0, m_rows - 1);
            int end_row   = std::clamp(static_cast<int>((tr.y + tr.height - m_scroll_y) / m_cell_h), 0, m_rows - 1);

            const Color top_color = arg.edge_colors ? arg.edge_colors->top : arg.color;
            const Color right_color = arg.edge_colors ? arg.edge_colors->right : arg.color;
            const Color bottom_color = arg.edge_colors ? arg.edge_colors->bottom : arg.color;
            const Color left_color = arg.edge_colors ? arg.edge_colors->left : arg.color;
            // Horizontal borders
            for (int c = start_col; c <= end_col; ++c) {
                m_grid[start_row * m_cols + c].ch = '-';
                m_grid[start_row * m_cols + c].fg = top_color;
                m_grid[end_row * m_cols + c].ch = '-';
                m_grid[end_row * m_cols + c].fg = bottom_color;
            }
            // Vertical borders
            for (int r = start_row; r <= end_row; ++r) {
                m_grid[r * m_cols + start_col].ch = '|';
                m_grid[r * m_cols + start_col].fg = left_color;
                m_grid[r * m_cols + end_col].ch = '|';
                m_grid[r * m_cols + end_col].fg = right_color;
            }
            // Corners take their horizontal edge's colour.
            m_grid[start_row * m_cols + start_col].ch = '+';
            m_grid[start_row * m_cols + start_col].fg = top_color;
            m_grid[start_row * m_cols + end_col].ch = '+';
            m_grid[start_row * m_cols + end_col].fg = top_color;
            m_grid[end_row * m_cols + start_col].ch = '+';
            m_grid[end_row * m_cols + start_col].fg = bottom_color;
            m_grid[end_row * m_cols + end_col].ch = '+';
            m_grid[end_row * m_cols + end_col].fg = bottom_color;

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
                CellRange cells;
                if (!intersecting_cells(tr, m_scroll_x, m_scroll_y, m_cols, m_rows,
                                        m_cell_w, m_cell_h, cells)) return;
                for (int r = cells.first_row; r <= cells.last_row; ++r) {
                    for (int c = cells.first_col; c <= cells.last_col; ++c) {
                        if (!inside_clip(c, r)) continue;
                        // Sample in the image's own coordinates, not relative
                        // to the clamped first cell. That matters when a frame
                        // has been partially scrolled out of the viewport.
                        const float u = std::clamp((c * m_cell_w + m_scroll_x - tr.x) / tr.width, 0.0f, 1.0f);
                        const float v = std::clamp((r * m_cell_h + m_scroll_y - tr.y) / tr.height, 0.0f, 1.0f);
                        int sx = std::min(static_cast<int>(u * arg.image->width), arg.image->width - 1);
                        int sy = std::min(static_cast<int>(v * arg.image->height), arg.image->height - 1);
                        Color pixel = arg.image->get_pixel_color(sx, sy);
                        if (pixel.a == 0) continue;
                        auto& cell = m_grid[r * m_cols + c];
                        cell.bg = over_opaque(pixel, cell.bg);
                        cell.ch = ' ';
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
