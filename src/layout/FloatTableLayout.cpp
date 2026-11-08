#include "litebird/layout/LayoutBox.hpp"

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

float horizontal_spacing(const LayoutBox& box) {
    return box.dimensions.border.left + box.dimensions.border.right +
           box.dimensions.padding.left + box.dimensions.padding.right;
}

float vertical_spacing(const LayoutBox& box) {
    return box.dimensions.border.top + box.dimensions.border.bottom +
           box.dimensions.padding.top + box.dimensions.padding.bottom;
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

float LayoutBox::clearance_y(const std::vector<FloatBand>& bands, ClearType clear) {
    if (clear == ClearType::None) return -1.0f;
    float lowest = -1.0f;
    for (const auto& band : bands) {
        const bool relevant = (clear == ClearType::Both) ||
                              (clear == ClearType::Left && band.left > 0.0f) ||
                              (clear == ClearType::Right && band.right > 0.0f);
        if (relevant) lowest = std::max(lowest, band.bottom);
    }
    return lowest;
}

float LayoutBox::min_content_width(const LayoutBox& box, float fallback) {
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
    const float h_pb = horizontal_spacing(*this);
    const float available = std::max(0.0f, containing_width - dimensions.margin.left - dimensions.margin.right);

    if (style.width >= 0.0f) {
        layout_block(containing_width);
        return;
    }

    // Shrink-to-fit: the widest unbreakable run, capped by the available width.
    float natural = min_content_width(*this, 0.0f);
    if (natural <= 0.0f) natural = std::min(available, 120.0f);
    const float content_width = std::min(natural, std::max(0.0f, available - h_pb));
    layout_block(content_width + dimensions.margin.left + dimensions.margin.right + h_pb);
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

void LayoutBox::layout_table(float containing_width) {
    const float h_pb = horizontal_spacing(*this);
    const float v_pb = vertical_spacing(*this);

    // 1. Gather the row grid, flattening row groups and synthesising rows for
    //    cells that are direct children of the table.
    std::vector<std::vector<std::shared_ptr<LayoutBox>>> grid;
    std::vector<std::shared_ptr<LayoutBox>> row_boxes;

    auto cells_of = [](const std::shared_ptr<LayoutBox>& container,
                       std::vector<std::shared_ptr<LayoutBox>>& cells) {
        for (const auto& child : container->children) {
            if (child->box_type == BoxType::TableCellNode) cells.push_back(child);
            else if (child->style.is_table_row_group() || child->box_type == BoxType::TableRowNode) {
                for (const auto& grandchild : child->children) {
                    if (grandchild->box_type == BoxType::TableCellNode) cells.push_back(grandchild);
                }
            }
        }
    };

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

    std::size_t column_count = 0;
    for (const auto& row : grid) column_count = std::max(column_count, row.size());
    if (column_count == 0) column_count = 1;

    // 2. Auto table layout: each column is at least as wide as its widest
    //    cell's min-content width, and as wide as any explicit cell width.
    std::vector<float> column_widths(column_count, 0.0f);
    for (const auto& row : grid) {
        for (std::size_t c = 0; c < row.size(); ++c) {
            const auto& cell = row[c];
            float width;
            if (cell->style.width >= 0.0f) {
                width = cell->style.width + horizontal_spacing(*cell) +
                        cell->dimensions.margin.left + cell->dimensions.margin.right;
            } else {
                width = min_content_width(*cell, 32.0f) +
                        cell->dimensions.margin.left + cell->dimensions.margin.right;
            }
            column_widths[c] = std::max(column_widths[c], width);
        }
    }

    float total = 0.0f;
    for (float width : column_widths) total += width;

    // 3. An explicit table width distributes the extra space evenly.
    if (style.width >= 0.0f) {
        const float target = std::max(0.0f, (style.box_sizing == BoxSizing::BorderBox)
                                                 ? style.width - h_pb
                                                 : style.width);
        if (target > total && column_count > 0) {
            const float extra = (target - total) / static_cast<float>(column_count);
            for (auto& width : column_widths) width += extra;
            total = target;
        }
    } else if (containing_width > total + h_pb + dimensions.margin.left + dimensions.margin.right) {
        // Tables shrink-wrap their content; they do not fill the container.
    }

    // 3b. Percentage cell widths resolve against the table's own width (CSS
    //     2.1 17.5.2), so they are applied once that width is known.
    {
        const bool size_known = (style.width >= 0.0f);
        float percent_total = total;
        for (const auto& row : grid) {
            for (std::size_t c = 0; c < row.size(); ++c) {
                const auto& cell = row[c];
                if (cell->style.width_percent < 0.0f) continue;
                const float required = percent_total * cell->style.width_percent / 100.0f;
                if (required > column_widths[c]) percent_total += required - column_widths[c];
                column_widths[c] = std::max(column_widths[c], required);
            }
        }
        if (!size_known) {
            total = 0.0f;
            for (float width : column_widths) total += width;
        }
    }

    dimensions.content.width = total;

    // 4. Place the rows and cells.
    float y_cursor = 0.0f;
    for (std::size_t r = 0; r < grid.size(); ++r) {
        float x_cursor = 0.0f;
        float row_height = 0.0f;

        for (std::size_t c = 0; c < grid[r].size(); ++c) {
            const auto& cell = grid[r][c];
            const float column_width = column_widths[c];

            cell->layout_table_cell(column_width);

            const float new_x = dimensions.content.x + x_cursor +
                                cell->dimensions.margin.left +
                                cell->dimensions.border.left +
                                cell->dimensions.padding.left;
            const float new_y = dimensions.content.y + y_cursor +
                                cell->dimensions.margin.top +
                                cell->dimensions.border.top +
                                cell->dimensions.padding.top;

            cell->shift_position(new_x - cell->dimensions.content.x, new_y - cell->dimensions.content.y);

            row_height = std::max(row_height, cell->dimensions.margin_box().height);
            x_cursor += column_width;
        }

        if (row_boxes[r]) {
            auto& row = row_boxes[r];
            row->dimensions.content.x = dimensions.content.x;
            row->dimensions.content.y = dimensions.content.y + y_cursor;
            row->dimensions.content.width = total;
            row->dimensions.content.height = row_height;
            row->dimensions.margin = row->style.margin;
            row->dimensions.padding = row->style.padding;
            row->dimensions.border = row->style.border_width;
        }

        y_cursor += row_height;
    }

    // 5. Height: explicit, or the sum of the rows.
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
