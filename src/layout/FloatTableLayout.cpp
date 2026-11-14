#include "litebird/layout/LayoutBox.hpp"
#include "litebird/common/StackLimit.hpp"

#include <algorithm>
#include <cmath>

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
    return scaled_border(box.style.border_width.left, scale) +
           scaled_border(box.style.border_width.right, scale) +
           box.style.padding.left + box.style.padding.right;
}

float vertical_spacing(const LayoutBox& box) {
    const float scale = box.border_scale;
    return scaled_border(box.style.border_width.top, scale) +
           scaled_border(box.style.border_width.bottom, scale) +
           box.style.padding.top + box.style.padding.bottom;
}

} // namespace

// ---------------------------------------------------------------------------
// Float context
// ---------------------------------------------------------------------------

bool LayoutBox::establishes_bfc() const {
    // CSS 2.1 9.4.1: floats, out-of-flow positioned boxes, non-visible
    // overflow, flex/grid containers, table cells and the root establish a
    // new block formatting context.
    if (!parent.lock()) return true;  // the root
    if (style.is_floated()) return true;
    if (style.overflow != Overflow::Visible) return true;
    if (style.is_flex() || style.is_grid()) return true;
    if (style.is_table() || style.is_table_cell() || style.is_inline_block()) return true;
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
    if (box.box_type == BoxType::TextNode) {
        return longest_word_width(box.text, box.style);
    }

    if (box.box_type == BoxType::ImageNode) {
        if (box.style.width >= 0.0f) return box.style.width;
        return 0.0f;
    }

    if (box.style.width >= 0.0f) {
        return box.style.width + horizontal_spacing(box) +
               box.dimensions.margin.left + box.dimensions.margin.right;
    }

    float widest = 0.0f;
    bool has_inline_text = false;

    // Inline content wraps at word boundaries, so the floor is the widest word.
    for (const auto& child : box.children) {
        if (child->box_type == BoxType::TextNode) {
            has_inline_text = true;
            widest = std::max(widest, longest_word_width(child->text, child->style));
        } else if (!child->style.is_block_level()) {
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
    return widest + horizontal_spacing(box);
}

void LayoutBox::layout_float(float containing_width) {
    // Floats bypass layout() (a floated inline keeps BoxType::InlineNode), so
    // percentages and the style edges have to be resolved here instead.
    resolve_percentages(containing_width, 0.0f);
    apply_style_edges();
    const float h_pb = horizontal_spacing(*this);
    const float available = std::max(0.0f, containing_width - dimensions.margin.left - dimensions.margin.right);

    if (style.width >= 0.0f) {
        layout_block(containing_width);
        return;
    }

    // Shrink-to-fit: the widest unbreakable run, capped by the available width.
    const float horizontal = style.margin.left + style.margin.right + h_pb;
    float natural = min_content_width(*this, 0.0f);
    if (natural <= 0.0f) natural = std::min(available, 120.0f);
    const float content_width = std::min(natural, std::max(0.0f, available - h_pb));
    layout_block(content_width + horizontal);
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

void LayoutBox::layout_table(float containing_width) {
    const float h_pb = horizontal_spacing(*this);
    const float v_pb = vertical_spacing(*this);
    const bool collapsed = (style.border_collapse == BorderCollapse::Collapse);
    // A collapsed table has no spacing at all: its cells share borders.
    const float spacing_x = collapsed ? 0.0f : style.border_spacing_x;
    const float spacing_y = collapsed ? 0.0f : style.border_spacing_y;

    // 1. Gather the row grid, flattening row groups and synthesising rows for
    //    cells that are direct children of the table.
    std::vector<std::vector<std::shared_ptr<LayoutBox>>> grid;
    std::vector<std::shared_ptr<LayoutBox>> row_boxes;

    for (const auto& child : children) {
        if (child->box_type == BoxType::TableRowNode) {
            std::vector<std::shared_ptr<LayoutBox>> cells;
            for (const auto& cell : child->children) {
                if (cell->box_type == BoxType::TableCellNode) cells.push_back(cell);
            }
            grid.push_back(std::move(cells));
            row_boxes.push_back(child);
        } else if (child->style.is_table_row_group()) {
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
        } else if (child->box_type == BoxType::TableCellNode) {
            // Consecutive cells directly in the table form one anonymous row.
            if (grid.empty() || !row_boxes.empty() && row_boxes.back() == nullptr) {
                grid.push_back({});
                row_boxes.push_back(nullptr);
            }
            grid.back().push_back(child);
        }
    }

    if (grid.empty()) {
        dimensions.content.width = (style.width >= 0.0f) ? style.width - h_pb : 0.0f;
        dimensions.content.height = 0.0f;
        return;
    }

    // 2. Spans: place every cell into an occupancy grid so rowspan/colspan
    //    cells reserve the slots they cover (CSS 2.1 17.5).
    struct PlacedCell {
        std::shared_ptr<LayoutBox> box;
        std::size_t row{0};
        std::size_t col{0};
        std::size_t rowspan{1};
        std::size_t colspan{1};
    };
    std::vector<PlacedCell> placed;

    auto numeric_attribute = [](const std::shared_ptr<LayoutBox>& cell, const char* name, int fallback) -> int {
        if (!cell->node || !cell->node->is_element()) return fallback;
        const std::string value = cell->node->as_element()->get_attribute(name);
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

    // Collapsed tables share borders: each cell paints half of a shared edge.
    for (auto& entry : placed) entry.box->border_scale = collapsed ? 0.5f : 1.0f;

    // 3. Column widths. A spanning cell distributes its requirement over the
    //    columns it covers instead of demanding one wide column.
    std::vector<float> column_widths(column_count, 0.0f);
    for (const auto& entry : placed) {
        const auto& cell = entry.box;
        const float cell_spacing = horizontal_spacing(*cell) +
                                   cell->style.margin.left + cell->style.margin.right;
        const float requirement = (cell->style.width >= 0.0f)
                                      ? cell->style.width + cell_spacing
                                      : min_content_width(*cell, 32.0f) +
                                            cell->style.margin.left + cell->style.margin.right;
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

    auto grid_width = [&]() {
        float total = spacing_x * static_cast<float>(column_count + 1);
        for (float width : column_widths) total += width;
        return total;
    };

    float total = grid_width();

    // 4. An explicit table width distributes the surplus evenly.
    if (style.width >= 0.0f) {
        const float target = std::max(0.0f, (style.box_sizing == BoxSizing::BorderBox)
                                                 ? style.width - h_pb
                                                 : style.width);
        if (target > total && column_count > 0) {
            const float extra = (target - total) / static_cast<float>(column_count);
            for (auto& width : column_widths) width += extra;
            total = target;
        }
    }

    // 4b. Percentage cell widths resolve against the table's own width (CSS
    //     2.1 17.5.2), so they are applied once that width is known.
    {
        const bool size_known = (style.width >= 0.0f);
        float percent_total = total;
        for (auto& entry : placed) {
            if (entry.box->style.width_percent < 0.0f) continue;
            const float required = percent_total * entry.box->style.width_percent / 100.0f;
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

    dimensions.content.width = total;

    // 5. Lay the cells out with the width of the columns they span. Cells go
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
    float y_cursor = spacing_y;
    for (std::size_t r = 0; r < row_count; ++r) {
        row_top[r] = y_cursor;
        y_cursor += row_heights[r] + spacing_y;
    }

    // 7. Position the cells. A cell that spans rows fills the box it covers.
    for (auto& entry : placed) {
        const auto& cell = entry.box;
        const float new_x = dimensions.content.x + column_left(entry.col) +
                            cell->dimensions.margin.left + cell->dimensions.border.left +
                            cell->dimensions.padding.left;
        const float new_y = dimensions.content.y + row_top[entry.row] +
                            cell->dimensions.margin.top + cell->dimensions.border.top +
                            cell->dimensions.padding.top;
        cell->shift_position(new_x - cell->dimensions.content.x, new_y - cell->dimensions.content.y);

        if (entry.rowspan > 1) {
            float spanned = spacing_y * static_cast<float>(entry.rowspan - 1);
            for (std::size_t r = entry.row; r < entry.row + entry.rowspan && r < row_count; ++r) {
                spanned += row_heights[r];
            }
            const float current = cell->dimensions.margin_box().height;
            if (spanned > current) cell->dimensions.content.height += spanned - current;
        }
    }

    for (std::size_t r = 0; r < row_count; ++r) {
        if (!row_boxes[r]) continue;
        auto& row = row_boxes[r];
        row->dimensions.content.x = dimensions.content.x;
        row->dimensions.content.y = dimensions.content.y + row_top[r];
        row->dimensions.content.width = total;
        row->dimensions.content.height = row_heights[r];
        row->dimensions.margin = row->style.margin;
        row->dimensions.padding = row->style.padding;
        row->dimensions.border = row->style.border_width;
    }

    // 8. Height: explicit, or the grid's own height.
    if (style.height >= 0.0f) {
        dimensions.content.height = (style.box_sizing == BoxSizing::BorderBox)
                                        ? std::max(0.0f, style.height - v_pb)
                                        : style.height;
    } else {
        dimensions.content.height = y_cursor;
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
