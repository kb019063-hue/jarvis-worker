#pragma once

#include "litebird/render/PaintCommand.hpp"
#include <iostream>
#include <vector>
#include <string>

namespace litebird {

/// One character cell.
///
/// A cell used to be a `char`, so the terminal view could only ever show ASCII:
/// every byte of a UTF-8 sequence was one cell, and every byte outside
/// `0x20..0x7e` was replaced by `?`. A cell is now one *character* — a code
/// point and everything joined to it — and it carries its own width, because a
/// East Asian character, an emoji or a combining sequence does not occupy the
/// same space as an `a`.
struct TerminalCell {
    /// The grapheme cluster this cell draws: one base character and the marks
    /// or joiners (§UAX #29) that belong to it. Empty for a cell that only
    /// carries colour — the second half of a double-width character, or blank.
    std::string cluster;
    /// The first code point of `cluster`, for a cell that must answer "what
    /// character is this" without parsing the cluster again.
    uint32_t codepoint{0};
    /// How many columns the cluster occupies: 0 for a combining mark that has
    /// joined a neighbour, 1 for an ordinary character, 2 for a wide one.
    int width{1};
    /// True for the second column of a wide character: the cell holds a space
    /// and exists so the cells after it keep their columns.
    bool continuation{false};
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

    /// The terminal width of one grapheme cluster, in columns (0, 1 or 2).
    /// Exposed because a page's text is the caller's business and this is the
    /// rule the renderer applies to it.
    static int cluster_width(const std::string& cluster);

private:
    void execute_command(const PaintCommand& cmd);
    TransformMatrix2D current_transform() const {
        return m_transform_stack.empty() ? TransformMatrix2D::identity() : m_transform_stack.back();
    }

    /// Writes one decoded cluster into the grid at `row`, starting at `col`,
    /// and answers the next free column. The second cell of a wide cluster is
    /// left carrying its colour with no cluster of its own, so the cell to its
    /// right is not overwritten by half a character.
    int place_cluster(int col, int row, const std::string& cluster, Color fg, bool bold);

    int m_cols;
    int m_rows;
    float m_cell_w;
    float m_cell_h;
    float m_scroll_x{0.0f};
    float m_scroll_y{0.0f};
    std::vector<TerminalCell> m_grid;
    std::vector<TransformMatrix2D> m_transform_stack;
    std::vector<Rect> m_clip_stack;

    /// True when the terminal cell's top-left device point survives clipping.
    bool inside_clip(int col, int row) const;
};

} // namespace litebird
