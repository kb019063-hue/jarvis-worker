#include "litebird/layout/LayoutBox.hpp"
#include "litebird/common/StackLimit.hpp"
#include "litebird/render/FontRenderer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string_view>

namespace litebird {

namespace {

float char_width(const ComputedStyle& style) {
    return style.font_size * 0.55f;
}

/// Longest unbreakable word in a text box, in pixels.
float longest_word_width(const std::string& text, const ComputedStyle& style) {
    float widest = 0.0f;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            widest = std::max(widest, static_cast<float>(current.size()) * char_width(style));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    widest = std::max(widest, static_cast<float>(current.size()) * char_width(style));
    return widest;
}

float scaled_border(float value, float scale) {
    return (scale == 1.0f) ? value : std::ceil(value * scale);
}

/// Reads the *style* rather than the laid-out dimensions: the table algorithm
/// needs cell spacing before the cells have been laid out, and in a collapsed
/// table each cell only owns half of a shared border.
float horizontal_spacing(const LayoutBox& box) {
    const float scale = box.border_scale;
    const ComputedStyle& bs = *box.style;
    const EdgeSizes& edges = box.border_override ? *box.border_override : bs.border_width;
    return scaled_border(edges.left, scale) +
           scaled_border(edges.right, scale) +
           bs.padding.left + bs.padding.right;
}

float vertical_spacing(const LayoutBox& box) {
    const float scale = box.border_scale;
    const ComputedStyle& bs = *box.style;
    const EdgeSizes& edges = box.border_override ? *box.border_override : bs.border_width;
    return scaled_border(edges.top, scale) +
           scaled_border(edges.bottom, scale) +
           bs.padding.top + bs.padding.bottom;
}

} // namespace

// ---------------------------------------------------------------------------
// Float context
// ---------------------------------------------------------------------------

/// A cell placed on the occupancy grid: which row/column band it covers.
struct PlacedCell {
    std::shared_ptr<LayoutBox> box;
    std::size_t row{0};
    std::size_t col{0};
    std::size_t rowspan{1};
    std::size_t colspan{1};
};

/// CSS 2.1 17.6.2.1, the border-conflict resolution, reduced to what this
/// engine can express: on a shared edge the wider used border wins and every
/// cell touching the edge paints half of the winner's border, colour included
/// (the two halves add up to the winning border). Positional priority
/// (cell > row > table) and style-based priority (double > solid > …) are
/// not modeled — borders carry a single solid style here, and row borders
/// are not painted at all. Outer edges resolve against nothing and keep the
/// cell's own width; the table's own border stays the table's.
struct ResolvedBorders {
    std::vector<EdgeSizes> widths;
    std::vector<EdgeColors> colors;
};

ResolvedBorders resolve_collapsed_borders(const std::vector<PlacedCell>& placed,
                                          std::size_t row_count,
                                          std::size_t column_count) {
    const std::size_t n = placed.size();
    std::vector<EdgeSizes> used;
    std::vector<EdgeColors> used_colors;
    used.reserve(n);
    used_colors.reserve(n);
    for (const auto& entry : placed) {
        used.push_back(entry.box->style->border_width);
        used_colors.push_back(EdgeColors(entry.box->style->border_color));
    }

    auto relax = [&](std::size_t index, int side, float width, const Color& color) {
        float& edge = (side == 0) ? used[index].top
                    : (side == 1) ? used[index].right
                    : (side == 2) ? used[index].bottom
                                  : used[index].left;
        if (width >= edge) {
            edge = width;
            Color& colored = (side == 0) ? used_colors[index].top
                           : (side == 1) ? used_colors[index].right
                           : (side == 2) ? used_colors[index].bottom
                                         : used_colors[index].left;
            colored = color;
        }
    };

    // Vertical boundaries between column b - 1 and b, one segment per row band.
    for (std::size_t b = 1; b < column_count; ++b) {
        for (std::size_t r = 0; r < row_count; ++r) {
            float winner = 0.0f;
            Color winner_color = Color::black();
            std::vector<std::pair<std::size_t, int>> members;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& entry = placed[i];
                if (r < entry.row || r >= entry.row + entry.rowspan) continue;
                float width = -1.0f;
                Color color = Color::black();
                if (entry.col + entry.colspan == b) {
                    width = entry.box->style->border_width.right;
                    color = entry.box->style->border_color;
                    members.emplace_back(i, 1);
                } else if (entry.col == b) {
                    width = entry.box->style->border_width.left;
                    color = entry.box->style->border_color;
                    members.emplace_back(i, 3);
                } else {
                    continue;
                }
                if (width >= winner) {
                    winner = width;
                    winner_color = color;
                }
            }
            for (const auto& [i, side] : members) relax(i, side, winner, winner_color);
        }
    }

    // Horizontal boundaries between row b - 1 and b, one segment per column band.
    for (std::size_t b = 1; b < row_count; ++b) {
        for (std::size_t c = 0; c < column_count; ++c) {
            float winner = 0.0f;
            Color winner_color = Color::black();
            std::vector<std::pair<std::size_t, int>> members;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& entry = placed[i];
                if (c < entry.col || c >= entry.col + entry.colspan) continue;
                float width = -1.0f;
                Color color = Color::black();
                if (entry.row + entry.rowspan == b) {
                    width = entry.box->style->border_width.bottom;
                    color = entry.box->style->border_color;
                    members.emplace_back(i, 2);
                } else if (entry.row == b) {
                    width = entry.box->style->border_width.top;
                    color = entry.box->style->border_color;
                    members.emplace_back(i, 0);
                } else {
                    continue;
                }
                if (width >= winner) {
                    winner = width;
                    winner_color = color;
                }
            }
            for (const auto& [i, side] : members) relax(i, side, winner, winner_color);
        }
    }
    return {used, used_colors};
}

bool LayoutBox::establishes_bfc() const {
    // CSS 2.1 9.4.1: floats, out-of-flow positioned boxes, non-visible
    // overflow, flex/grid containers, table cells and the root establish a
    // new block formatting context.
    if (!parent.lock()) return true;  // the root
    const ComputedStyle& s = *style;
    if (s.is_floated()) return true;
    if (s.overflow != Overflow::Visible) return true;
    if (s.is_flex() || s.is_grid()) return true;
    if (s.is_table() || s.is_table_cell() || s.is_inline_block()) return true;
    return false;
}

void LayoutBox::band_at(const std::vector<FloatBand>& bands, float y, float& left, float& right) {
    left = 0.0f;
    right = 0.0f;
    for (const auto& band : bands) {
        if (y >= band.top && y < band.bottom) {
            left = std::max(left, band.left);
            right = std::max(right, band.right);
        }
    }
}

float LayoutBox::min_content_width(const LayoutBox& box, float fallback) {
    if (stack_is_exhausted()) return fallback;
    const ComputedStyle& bs = *box.style;
    if (box.box_type == BoxType::TextNode) {
        return longest_word_width(box.text, bs);
    }

    if (box.box_type == BoxType::ImageNode) {
        if (bs.width >= 0.0f) return bs.width;
        return 0.0f;
    }

    // A definite maximum clamps the contribution of this box to its
    // parent's intrinsic size, not just the width it uses later in layout.
    // Descendants may overflow the box without demanding a wider column.
    const float edges = horizontal_spacing(box);
    const float adjustment = bs.box_sizing == BoxSizing::BorderBox ? 0.0f : edges;
    auto constrained_outer_width = [&](float outer) {
        if (bs.max_width >= 0) outer = std::min(outer, bs.max_width + adjustment);
        if (bs.min_width >= 0) outer = std::max(outer, bs.min_width + adjustment);
        return std::max(edges, outer);
    };
    if (bs.width >= 0.0f) {
        return constrained_outer_width(bs.width + adjustment) +
               box.dimensions.margin.left + box.dimensions.margin.right;
    }

    float widest = 0.0f;
    bool has_inline_text = false;

    // Inline content wraps at word boundaries, so the floor is the widest word.
    for (const auto& child : box.children) {
        if (child->box_type == BoxType::TextNode) {
            has_inline_text = true;
            widest = std::max(widest, longest_word_width(child->text, *child->style));
        } else if (!child->style->is_block_level()) {
            has_inline_text = true;
            widest += min_content_width(*child, 0.0f);
        }
    }

    if (!has_inline_text) {
        for (const auto& child : box.children) {
            widest = std::max(widest, min_content_width(*child, 0.0f));
        }
    }

    if (widest <= 0.0f) widest = fallback;
    return constrained_outer_width(widest + edges);
}

float LayoutBox::max_content_width(const LayoutBox& box, float fallback) {
    if (stack_is_exhausted()) return fallback;
    const ComputedStyle& bs = *box.style;

    float line_width = 0.0f;
    float widest_line = 0.0f;
    bool line_has_content = false;
    bool pending_collapsed_space = false;
    float pending_space_width = 0.0f;

    auto finish_line = [&] {
        widest_line = std::max(widest_line, line_width);
        line_width = 0.0f;
        line_has_content = false;
        pending_collapsed_space = false;
        pending_space_width = 0.0f;
    };
    auto add_atomic_width = [&](float width) {
        if (width == 0.0f) return;
        if (pending_collapsed_space && line_has_content) line_width += pending_space_width;
        pending_collapsed_space = false;
        pending_space_width = 0.0f;
        line_width += width;
        line_has_content = true;
    };
    auto add_text = [&](std::string_view text, const ComputedStyle& style) {
        FontRenderer& font = FontRenderer::instance();
        const bool italic = style.font_style != FontStyle::Normal;
        const bool kerning = style.kerning_enabled();
        const float space_width = font.measure_text(" ", style.font_size, style.font_weight,
                                                    style.font_family, italic, kerning);
        const WhiteSpace white_space = style.white_space;
        const bool preserve_newlines = white_space == WhiteSpace::Pre ||
                                       white_space == WhiteSpace::PreWrap ||
                                       white_space == WhiteSpace::PreLine ||
                                       white_space == WhiteSpace::BreakSpaces;
        const bool preserve_spaces = white_space == WhiteSpace::Pre ||
                                     white_space == WhiteSpace::PreWrap ||
                                     white_space == WhiteSpace::BreakSpaces;
        std::string word;
        auto finish_word = [&] {
            if (word.empty()) return;
            if (pending_collapsed_space && line_has_content) line_width += pending_space_width;
            pending_collapsed_space = false;
            pending_space_width = 0.0f;
            line_width += font.measure_text(word, style.font_size, style.font_weight,
                                            style.font_family, italic, kerning);
            line_has_content = true;
            word.clear();
        };

        for (std::size_t i = 0; i < text.size();) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (preserve_newlines && (c == '\n' || c == '\r')) {
                finish_word();
                if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') i += 2;
                else ++i;
                finish_line();
                continue;
            }
            if (std::isspace(c)) {
                finish_word();
                if (preserve_spaces) {
                    if (pending_collapsed_space && line_has_content) line_width += pending_space_width;
                    pending_collapsed_space = false;
                    pending_space_width = 0.0f;
                    std::size_t count = 0;
                    while (i < text.size() &&
                           std::isspace(static_cast<unsigned char>(text[i])) &&
                           text[i] != '\n' && text[i] != '\r') {
                        ++count;
                        ++i;
                    }
                    line_width += space_width * static_cast<float>(count);
                    line_has_content = line_has_content || count != 0;
                } else {
                    ++i;
                    if (line_has_content) {
                        pending_collapsed_space = true;
                        pending_space_width = space_width;
                    }
                }
                continue;
            }
            word.push_back(static_cast<char>(c));
            ++i;
        }
        finish_word();
    };

    if (box.box_type == BoxType::TextNode) {
        add_text(box.text, bs);
        finish_line();
        return widest_line;
    }

    const float edges = horizontal_spacing(box);
    const float adjustment = bs.box_sizing == BoxSizing::BorderBox ? 0.0f : edges;
    auto constrained_outer_width = [&](float outer) {
        if (bs.min_width >= 0.0f) {
            outer = std::max(outer, bs.min_width + adjustment);
        }
        if (bs.max_width >= 0.0f) {
            outer = std::min(outer, bs.max_width + adjustment);
        }
        return std::max(edges, outer);
    };

    if (box.box_type == BoxType::ImageNode) {
        float content = box.image ? static_cast<float>(box.image->width)
                                  : box.dimensions.content.width;
        if (bs.width >= 0.0f) {
            content = (bs.box_sizing == BoxSizing::BorderBox)
                          ? std::max(0.0f, bs.width - edges)
                          : bs.width;
        }
        return constrained_outer_width(content + edges);
    }

    if (bs.width >= 0.0f) {
        return constrained_outer_width(bs.width + adjustment);
    }

    std::function<void(const LayoutBox&)> append_inline;
    std::function<void(const LayoutBox&)> measure_children;
    append_inline = [&](const LayoutBox& child) {
        if (child.style->is_out_of_flow() || child.style->is_floated()) return;
        if (child.box_type == BoxType::TextNode) {
            add_text(child.text, *child.style);
            return;
        }
        if (child.box_type == BoxType::InlineNode && child.node &&
            child.node->is_element() && child.node->as_element()->tag_name() == "br") {
            finish_line();
            return;
        }
        if (child.box_type == BoxType::ImageNode || child.style->is_inline_block()) {
            add_atomic_width(max_content_width(child, 0.0f) +
                             child.style->margin.left + child.style->margin.right);
            return;
        }

        const ComputedStyle& child_style = *child.style;
        const EdgeSizes& border = child.border_override
                                      ? *child.border_override
                                      : child_style.border_width;
        const float border_left = scaled_border(border.left, child.border_scale);
        const float border_right = scaled_border(border.right, child.border_scale);
        add_atomic_width(child_style.margin.left + child_style.padding.left + border_left);
        measure_children(child);
        add_atomic_width(child_style.padding.right + border_right + child_style.margin.right);
    };
    measure_children = [&](const LayoutBox& container) {
        for (const auto& child : container.children) {
            if (!child || child->style->is_out_of_flow() || child->style->is_floated()) continue;
            if (child->style->is_block_level() && !child->style->is_inline_block()) {
                finish_line();
                widest_line = std::max(
                    widest_line,
                    max_content_width(*child, 0.0f) +
                        child->style->margin.left + child->style->margin.right);
                continue;
            }
            append_inline(*child);
        }
    };

    measure_children(box);
    finish_line();
    return constrained_outer_width(widest_line + edges);
}

void LayoutBox::layout_float(float containing_width) {
    // Floats bypass layout() (a floated inline keeps BoxType::InlineNode), so
    // percentages and the style edges have to be resolved here instead.
    resolve_percentages(containing_width, 0.0f);
    apply_style_edges();
    const ComputedStyle& s = *style;
    const float h_pb = horizontal_spacing(*this);
    const float available = std::max(0.0f, containing_width - dimensions.margin.left - dimensions.margin.right);

    if (s.width >= 0.0f) {
        layout_block(containing_width);
        return;
    }

    // CSS 2.1 §10.3.5 shrink-to-fit is the same clamp used by `fit-content`:
    // min(max-content, max(min-content, available)).
    const float horizontal = s.margin.left + s.margin.right + h_pb;
    const float min_content = std::max(
        0.0f, min_content_width(*this, 0.0f) - h_pb);
    float max_content = std::max(
        min_content, max_content_width(*this, 0.0f) - h_pb);
    const float available_content = std::max(0.0f, available - h_pb);
    if (max_content <= 0.0f) {
        max_content = std::min(available_content, 120.0f);
    }
    const float content_width =
        std::min(max_content, std::max(min_content, available_content));
    layout_block(content_width + horizontal);
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

void LayoutBox::layout_table(float containing_width) {
    const ComputedStyle& s = *style;
    const float h_pb = horizontal_spacing(*this);
    const float v_pb = vertical_spacing(*this);
    const bool collapsed = (s.border_collapse == BorderCollapse::Collapse);
    // A collapsed table has no spacing at all: its cells share borders.
    const float spacing_x = collapsed ? 0.0f : s.border_spacing_x;
    const float spacing_y = collapsed ? 0.0f : s.border_spacing_y;

    // Captions are block-level boxes of their own: full grid width, stacked
    // above or below the grid (CSS 2.1 17.4.1, `caption-side`). They are not
    // rows, cells or groups, so the grid gather skips them.
    std::vector<std::shared_ptr<LayoutBox>> top_captions;
    std::vector<std::shared_ptr<LayoutBox>> bottom_captions;
    for (const auto& child : children) {
        if (child->style->is_table_caption()) {
            (child->style->caption_side == CaptionSide::Bottom ? bottom_captions : top_captions)
                .push_back(child);
        }
    }

    // 1. Gather the row grid, flattening row groups and synthesising rows for
    //    cells that are direct children of the table, plus table columns and
    //    column groups (CSS 2.1 §17.2 / §17.5.1).
    struct RowGroupSpan {
        std::shared_ptr<LayoutBox> box;
        std::size_t first_row{0};
        std::size_t last_row{0}; // exclusive
    };
    struct ColumnSpan {
        std::shared_ptr<LayoutBox> box;
        std::size_t first_col{0};
        std::size_t span{1};
    };
    auto numeric_attribute = [](const std::shared_ptr<LayoutBox>& box, const char* name, int fallback) -> int {
        if (!box->node || !box->node->is_element()) return fallback;
        const std::string value = box->node->as_element()->get_attribute(name);
        if (value.empty()) return fallback;
        long long parsed = 0;
        bool any = false;
        for (char c : value) {
            if (c < '0' || c > '9') break;
            any = true;
            parsed = std::min<long long>(parsed * 10 + (c - '0'), 1000);
        }
        return any ? static_cast<int>(parsed) : fallback;
    };

    std::vector<std::vector<std::shared_ptr<LayoutBox>>> grid;
    std::vector<std::shared_ptr<LayoutBox>> row_boxes;
    std::vector<RowGroupSpan> row_groups;
    std::vector<ColumnSpan> col_groups;
    std::vector<ColumnSpan> cols;
    std::size_t col_cursor = 0;

    for (const auto& child : children) {
        if (child->style->is_table_caption()) continue;
        if (child->style->is_table_column_group()) {
            const std::size_t group_start = col_cursor;
            bool has_col_children = false;
            for (const auto& col : child->children) {
                if (!col->style->is_table_column()) continue;
                has_col_children = true;
                const auto span = static_cast<std::size_t>(std::max(1, numeric_attribute(col, "span", 1)));
                cols.push_back(ColumnSpan{col, col_cursor, span});
                col_cursor += span;
            }
            if (!has_col_children) {
                const auto span = static_cast<std::size_t>(std::max(1, numeric_attribute(child, "span", 1)));
                col_cursor += span;
            }
            col_groups.push_back(ColumnSpan{child, group_start, col_cursor - group_start});
            continue;
        }
        if (child->style->is_table_column()) {
            const auto span = static_cast<std::size_t>(std::max(1, numeric_attribute(child, "span", 1)));
            cols.push_back(ColumnSpan{child, col_cursor, span});
            col_cursor += span;
            continue;
        }
        if (child->box_type == BoxType::TableRowNode) {
            std::vector<std::shared_ptr<LayoutBox>> cells;
            for (const auto& cell : child->children) {
                if (cell->box_type == BoxType::TableCellNode) cells.push_back(cell);
            }
            grid.push_back(std::move(cells));
            row_boxes.push_back(child);
        } else if (child->style->is_table_row_group()) {
            const std::size_t group_first = grid.size();
            for (const auto& row : child->children) {
                std::vector<std::shared_ptr<LayoutBox>> cells;
                if (row->box_type == BoxType::TableRowNode) {
                    for (const auto& cell : row->children) {
                        if (cell->box_type == BoxType::TableCellNode) cells.push_back(cell);
                    }
                    grid.push_back(std::move(cells));
                    row_boxes.push_back(row);
                } else if (row->box_type == BoxType::TableCellNode) {
                    grid.push_back({row});
                    row_boxes.push_back(nullptr);
                }
            }
            row_groups.push_back(RowGroupSpan{child, group_first, grid.size()});
        } else if (child->box_type == BoxType::TableCellNode) {
            // Consecutive cells directly in the table form one anonymous row.
            if (grid.empty() || (!row_boxes.empty() && row_boxes.back() == nullptr)) {
                grid.push_back({});
                row_boxes.push_back(nullptr);
            }
            grid.back().push_back(child);
        }
    }

    if (grid.empty()) {
        float empty_w = (s.width >= 0.0f) ? s.width - h_pb
                       : std::max(0.0f, containing_width - h_pb);
        float cap_h = 0.0f;
        for (auto& cap : top_captions) {
            cap->width_basis_override = empty_w;
            cap->layout(empty_w);
            cap->shift_position(dimensions.content.x - cap->dimensions.content.x,
                                dimensions.content.y + cap_h - cap->dimensions.content.y);
            cap_h += cap->dimensions.margin_box().height;
        }
        for (auto& cap : bottom_captions) {
            cap->width_basis_override = empty_w;
            cap->layout(empty_w);
            cap->shift_position(dimensions.content.x - cap->dimensions.content.x,
                                dimensions.content.y + cap_h - cap->dimensions.content.y);
            cap_h += cap->dimensions.margin_box().height;
        }
        dimensions.content.width = empty_w;
        dimensions.content.height = cap_h;
        return;
    }

    // 2. Spans: place every cell into an occupancy grid so rowspan/colspan
    //    cells reserve the slots they cover (CSS 2.1 17.5).
    std::vector<PlacedCell> placed;

    const std::size_t row_count = grid.size();
    std::vector<std::vector<bool>> occupied(row_count);
    std::size_t column_count = 0;

    for (std::size_t r = 0; r < row_count; ++r) {
        std::size_t c = 0;
        for (const auto& cell : grid[r]) {
            while (c < occupied[r].size() && occupied[r][c]) ++c;

            const auto colspan = static_cast<std::size_t>(std::max(1, numeric_attribute(cell, "colspan", 1)));
            const int rowspan_attribute = numeric_attribute(cell, "rowspan", 1);
            // rowspan="0" means "to the end of the row group".
            std::size_t rowspan = (rowspan_attribute == 0)
                                      ? (row_count - r)
                                      : static_cast<std::size_t>(std::max(1, rowspan_attribute));
            rowspan = std::min(rowspan, row_count - r);

            for (std::size_t rr = r; rr < r + rowspan; ++rr) {
                if (occupied[rr].size() < c + colspan) occupied[rr].resize(c + colspan, false);
                for (std::size_t cc = c; cc < c + colspan; ++cc) occupied[rr][cc] = true;
            }
            placed.push_back(PlacedCell{cell, r, c, rowspan, colspan});
            c += colspan;
        }
        column_count = std::max(column_count, occupied[r].size());
    }
    if (column_count == 0) column_count = 1;

    // Collapsed tables: resolve the shared edges first (widest wins), handing
    // the winner to every cell on the edge, then each cell paints half.
    if (collapsed) {
        const ResolvedBorders resolved = resolve_collapsed_borders(placed, row_count, column_count);
        for (std::size_t i = 0; i < placed.size(); ++i) {
            placed[i].box->border_override = resolved.widths[i];
            placed[i].box->border_color_override = resolved.colors[i];
        }
    }
    for (auto& entry : placed) entry.box->border_scale = collapsed ? 0.5f : 1.0f;

    // 3. Column widths. A spanning cell distributes its requirement over the
    //    columns it covers instead of demanding one wide column. In the fixed
    //    algorithm (CSS 2.1 17.5.2.1) the first row decides: a cell or column
    //    with a specified width sets its column, and the rest of the table's
    //    width is divided equally between the remaining columns. Later rows
    //    never widen a column, and `auto` table width shares the containing
    //    width equally (there is no shrink-wrap in fixed layout here).
    std::vector<float> column_widths;
    if (s.table_layout == TableLayout::Fixed) {
        const float explicit_w = (s.width >= 0.0f)
            ? ((s.box_sizing == BoxSizing::BorderBox) ? std::max(0.0f, s.width - h_pb)
                                                      : s.width)
            : -1.0f;
        const float basis_w = (explicit_w >= 0.0f)
            ? explicit_w
            : std::max(0.0f, containing_width - h_pb);
        std::vector<float> widths(column_count, -1.0f);
        for (const auto& col_entry : cols) {
            const ComputedStyle& cs = *col_entry.box->style;
            float col_w = -1.0f;
            if (cs.width >= 0.0f) col_w = cs.width;
            else if (cs.width_percent >= 0.0f && explicit_w >= 0.0f) {
                col_w = explicit_w * cs.width_percent / 100.0f;
            }
            if (col_w < 0.0f) continue;
            for (std::size_t c = col_entry.first_col;
                 c < col_entry.first_col + col_entry.span && c < column_count; ++c) {
                widths[c] = std::max(widths[c], col_w);
            }
        }
        for (const auto& entry : placed) {
            if (entry.row != 0) continue;
            const ComputedStyle& es = *entry.box->style;
            const float spacing = horizontal_spacing(*entry.box) + es.margin.left + es.margin.right;
            float required = -1.0f;
            if (es.width >= 0.0f) required = es.width + spacing;
            else if (es.width_percent >= 0.0f && explicit_w >= 0.0f) {
                required = explicit_w * es.width_percent / 100.0f + spacing;
            }
            if (required < 0.0f) continue;
            if (entry.colspan == 1) {
                widths[entry.col] = std::max(widths[entry.col], required);
            } else {
                // A spanning cell with a width divides its share evenly; an
                // already-specified column keeps the larger of the two.
                const float share = required / static_cast<float>(entry.colspan);
                for (std::size_t c = entry.col; c < entry.col + entry.colspan && c < column_count; ++c) {
                    widths[c] = std::max(widths[c], share);
                }
            }
        }
        float used_sum = 0.0f;
        std::size_t unspecified = 0;
        for (const float w : widths) {
            if (w >= 0.0f) used_sum += w;
            else ++unspecified;
        }
        const float available = std::max(0.0f, basis_w - spacing_x * static_cast<float>(column_count + 1));
        const float share = (unspecified > 0)
            ? std::max(0.0f, available - used_sum) / static_cast<float>(unspecified)
            : 0.0f;
        for (float& w : widths) {
            if (w < 0.0f) w = share;
        }
        column_widths = std::move(widths);
    } else {
    column_widths.assign(column_count, 0.0f);
    for (const auto& col_entry : cols) {
        const ComputedStyle& cs = *col_entry.box->style;
        if (cs.width >= 0.0f) {
            for (std::size_t c = col_entry.first_col;
                 c < col_entry.first_col + col_entry.span && c < column_count; ++c) {
                column_widths[c] = std::max(column_widths[c], cs.width);
            }
        }
    }
    for (const auto& entry : placed) {
        const auto& cell = entry.box;
        const ComputedStyle& cs = *cell->style;
        const float cell_spacing = horizontal_spacing(*cell) + cs.margin.left + cs.margin.right;
        const float requirement = (cs.width >= 0.0f)
                                      ? cs.width + cell_spacing
                                      : min_content_width(*cell, 32.0f) +
                                            cs.margin.left + cs.margin.right;
        if (entry.colspan == 1) {
            column_widths[entry.col] = std::max(column_widths[entry.col], requirement);
        } else {
            float span_have = 0.0f;
            for (std::size_t c = entry.col; c < entry.col + entry.colspan && c < column_count; ++c) {
                span_have += column_widths[c];
            }
            span_have += spacing_x * static_cast<float>(entry.colspan - 1);
            if (requirement > span_have) {
                const float extra = (requirement - span_have) / static_cast<float>(entry.colspan);
                for (std::size_t c = entry.col; c < entry.col + entry.colspan && c < column_count; ++c) {
                    column_widths[c] += extra;
                }
            }
        }
    }

    }

    auto grid_width = [&]() {
        float total = spacing_x * static_cast<float>(column_count + 1);
        for (float width : column_widths) total += width;
        return total;
    };

    float total = grid_width();

    // 4. An explicit table width distributes the surplus evenly.
    if (s.table_layout != TableLayout::Fixed && s.width >= 0.0f) {
        const float target = std::max(0.0f, (s.box_sizing == BoxSizing::BorderBox)
                                                 ? s.width - h_pb
                                                 : s.width);
        if (target > total && column_count > 0) {
            const float extra = (target - total) / static_cast<float>(column_count);
            for (auto& width : column_widths) width += extra;
            total = target;
        }
    }

    // 4b. Percentage cell widths resolve against the table's own width (CSS
    //     2.1 17.5.2), so they are applied once that width is known. Not in
    //     fixed layout: there, columns are fixed and cells overflow them.
    if (s.table_layout != TableLayout::Fixed) {
        const bool size_known = (s.width >= 0.0f);
        float percent_total = total;
        for (auto& entry : placed) {
            if (entry.box->style->width_percent < 0.0f) continue;
            const float required = percent_total * entry.box->style->width_percent / 100.0f;
            float span_have = 0.0f;
            for (std::size_t c = entry.col; c < entry.col + entry.colspan && c < column_count; ++c) {
                span_have += column_widths[c];
            }
            if (required > span_have) {
                const float extra = (required - span_have) / static_cast<float>(entry.colspan);
                for (std::size_t c = entry.col; c < entry.col + entry.colspan && c < column_count; ++c) {
                    column_widths[c] += extra;
                }
                percent_total += required - span_have;
            }
        }
        if (!size_known) total = grid_width();
    }

    // 4c. Captions are block-level children: full grid width, stacked above or
    //     below the grid. An explicitly sized caption that is wider than the
    //     grid widens the table box (the grid keeps its width and stays
    //     left-aligned; the cells do not stretch).
    float caption_extent = total;
    for (const auto* caps_ptr : {&top_captions, &bottom_captions}) {
        for (const auto& cap : *caps_ptr) {
            const ComputedStyle& cs = *cap->style;
            if (cs.width < 0.0f) continue;
            const float cap_w = cs.width + horizontal_spacing(*cap) +
                                cs.margin.left + cs.margin.right;
            caption_extent = std::max(caption_extent, cap_w);
        }
    }
    total = caption_extent;
    float caption_top = 0.0f;
    float caption_bottom = 0.0f;
    for (auto& cap : top_captions) {
        cap->width_basis_override = total;
        cap->layout(total);
        caption_top += cap->dimensions.margin_box().height;
    }
    for (auto& cap : bottom_captions) {
        cap->width_basis_override = total;
        cap->layout(total);
        caption_bottom += cap->dimensions.margin_box().height;
    }

    dimensions.content.width = total;

    // 5. Lay the cells out with the width of the columns they span. Cells go
    //
    // Fixed layout reads the first row once (CSS 2.1 17.5.2.1): a later cell's
    // own width must not resize its column, so the cell hugs the column it
    // was given instead of the width it asked for.
    if (s.table_layout == TableLayout::Fixed) {
        for (const auto& entry : placed) {
            entry.box->ignore_own_width = true;
        }
    }
    //
    //    through layout() rather than layout_table_cell(): only layout()
    //    copies margin/padding/border from the computed style onto the box.
    //    Percentage lengths inside a cell are relative to the table, so give
    //    the cell that basis before it resolves them.
    auto column_left = [&](std::size_t c) {
        float x = spacing_x;
        for (std::size_t i = 0; i < c && i < column_count; ++i) x += column_widths[i] + spacing_x;
        return x;
    };
    auto span_width = [&](std::size_t c, std::size_t span) {
        float width = 0.0f;
        for (std::size_t i = c; i < c + span && i < column_count; ++i) width += column_widths[i];
        if (span > 1) width += spacing_x * static_cast<float>(span - 1);
        return width;
    };

    for (auto& entry : placed) {
        entry.box->width_basis_override = total;
        entry.box->layout(span_width(entry.col, entry.colspan));
    }

    // 6. Row heights: single-row cells first, then rowspan cells grow the last
    //    row they cover if they need more room than the rows already provide.
    std::vector<float> row_heights(row_count, 0.0f);
    for (const auto& entry : placed) {
        if (entry.rowspan == 1) {
            row_heights[entry.row] =
                std::max(row_heights[entry.row], entry.box->dimensions.margin_box().height);
        }
    }
    for (const auto& entry : placed) {
        if (entry.rowspan <= 1) continue;
        float available = spacing_y * static_cast<float>(entry.rowspan - 1);
        for (std::size_t r = entry.row; r < entry.row + entry.rowspan && r < row_count; ++r) {
            available += row_heights[r];
        }
        const float needed = entry.box->dimensions.margin_box().height;
        if (needed > available) {
            row_heights[entry.row + entry.rowspan - 1] += needed - available;
        }
    }

    std::vector<float> row_top(row_count, 0.0f);
    float y_cursor = caption_top + spacing_y;
    for (std::size_t r = 0; r < row_count; ++r) {
        row_top[r] = y_cursor;
        y_cursor += row_heights[r] + spacing_y;
    }

    // 7. Position the cells. Every cell stretches to cover the full height of
    //    the row (or spanned rows) it occupies (CSS 2.1 §17.5.3).
    for (auto& entry : placed) {
        const auto& cell = entry.box;
        const float new_x = dimensions.content.x + column_left(entry.col) +
                            cell->dimensions.margin.left + cell->dimensions.border.left +
                            cell->dimensions.padding.left;
        const float new_y = dimensions.content.y + row_top[entry.row] +
                            cell->dimensions.margin.top + cell->dimensions.border.top +
                            cell->dimensions.padding.top;
        cell->shift_position(new_x - cell->dimensions.content.x, new_y - cell->dimensions.content.y);

        float spanned = spacing_y * static_cast<float>(entry.rowspan - 1);
        for (std::size_t r = entry.row; r < entry.row + entry.rowspan && r < row_count; ++r) {
            spanned += row_heights[r];
        }
        const float current = cell->dimensions.margin_box().height;
        if (spanned > current) cell->dimensions.content.height += spanned - current;
    }

    for (std::size_t r = 0; r < row_count; ++r) {
        if (!row_boxes[r]) continue;
        auto& row = row_boxes[r];
        row->dimensions.content.x = dimensions.content.x;
        row->dimensions.content.y = dimensions.content.y + row_top[r];
        row->dimensions.content.width = total;
        row->dimensions.content.height = row_heights[r];
        row->dimensions.margin = row->style->margin;
        row->dimensions.padding = row->style->padding;
        row->dimensions.border = row->style->border_width;
    }

    for (const auto& group : row_groups) {
        auto& gb = group.box;
        gb->dimensions.margin = gb->style->margin;
        gb->dimensions.padding = gb->style->padding;
        gb->dimensions.border = gb->style->border_width;
        gb->dimensions.content.x = dimensions.content.x;
        gb->dimensions.content.width = total;
        if (group.last_row > group.first_row) {
            const float top = row_top[group.first_row];
            const float bottom = row_top[group.last_row - 1] + row_heights[group.last_row - 1];
            gb->dimensions.content.y = dimensions.content.y + top;
            gb->dimensions.content.height = std::max(0.0f, bottom - top);
        } else {
            gb->dimensions.content.y = dimensions.content.y;
            gb->dimensions.content.height = 0.0f;
        }
    }

    // CSS 2.1 §17.5.1: column groups and columns span the vertical rows region
    // across their assigned columns so transparent cells/rows expose their
    // backgrounds above the table background.
    const float grid_top = row_count > 0 ? row_top.front() : caption_top;
    const float grid_bottom = row_count > 0 ? (row_top.back() + row_heights.back()) : caption_top;
    const float grid_height = std::max(0.0f, grid_bottom - grid_top);
    auto place_column_box = [&](const ColumnSpan& entry) {
        auto& cb = entry.box;
        cb->dimensions.margin = EdgeSizes(0.0f);
        cb->dimensions.padding = EdgeSizes(0.0f);
        cb->dimensions.border = EdgeSizes(0.0f);
        const std::size_t start = entry.first_col;
        const std::size_t end = std::min(entry.first_col + entry.span, column_count);
        if (start < end && grid_height > 0.0f) {
            cb->dimensions.content.x = dimensions.content.x + column_left(start);
            cb->dimensions.content.y = dimensions.content.y + grid_top;
            cb->dimensions.content.width = span_width(start, end - start);
            cb->dimensions.content.height = grid_height;
        } else {
            cb->dimensions.content = Rect(dimensions.content.x, dimensions.content.y, 0.0f, 0.0f);
        }
    };
    for (const auto& cg : col_groups) place_column_box(cg);
    for (const auto& col : cols) place_column_box(col);

    // 8. Height: explicit, or the grid's own height, plus the bottom
    //    captions. The rows region stays whatever it was; the captions sit
    //    outside it (CSS 2.1 17.4.1).
    float content_height = 0.0f;
    if (s.height >= 0.0f) {
        content_height = (s.box_sizing == BoxSizing::BorderBox)
                             ? std::max(0.0f, s.height - v_pb)
                             : s.height;
    } else {
        content_height = y_cursor;
    }
    dimensions.content.height = content_height + caption_bottom;

    // Position the captions: top ones stack at the top of the table's
    // content box, bottom ones below the rows region.
    float top_y = dimensions.content.y;
    for (auto& cap : top_captions) {
        cap->shift_position(dimensions.content.x - cap->dimensions.content.x,
                            top_y - cap->dimensions.content.y);
        top_y += cap->dimensions.margin_box().height;
    }
    float bottom_y = dimensions.content.y + content_height;
    for (auto& cap : bottom_captions) {
        cap->shift_position(dimensions.content.x - cap->dimensions.content.x,
                            bottom_y - cap->dimensions.content.y);
        bottom_y += cap->dimensions.margin_box().height;
    }
}

void LayoutBox::layout_table_row(float containing_width) {
    // A row laid out outside a table behaves like a block.
    layout_block(containing_width);
}

void LayoutBox::layout_table_cell(float containing_width) {
    // A cell is a block that establishes a formatting context of its own.
    layout_block(containing_width);
}

} // namespace litebird
