#include "litebird/layout/LayoutBox.hpp"
#include "litebird/common/StackLimit.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <numeric>
#include <unordered_map>

namespace litebird {

void LayoutBox::apply_style_edges() {
    dimensions.margin = style.margin;
    dimensions.padding = style.padding;
    dimensions.border = style.border_width;
    if (border_scale != 1.0f) {
        auto scaled = [&](float value) { return std::ceil(value * border_scale); };
        dimensions.border = EdgeSizes(scaled(dimensions.border.top),
                                      scaled(dimensions.border.right),
                                      scaled(dimensions.border.bottom),
                                      scaled(dimensions.border.left));
    }
}

void LayoutBox::resolve_percentages(float containing_width, float containing_height) {
    // CSS 2.1: percentage widths, padding and margins resolve against the
    // containing block's *width*; percentage heights need a definite
    // containing height, and fall back to `auto` otherwise (which is what
    // happens for the usual auto-height ancestor).
    const float width_basis = std::max(0.0f, (width_basis_override >= 0.0f)
                                                ? width_basis_override
                                                : containing_width);
    const float height_basis = (containing_height > 0.0f) ? containing_height : -1.0f;

    auto apply = [](float& pixels, float percent, float basis) {
        if (percent < 0.0f) return;
        pixels = (basis >= 0.0f) ? basis * percent / 100.0f : -1.0f;
    };
    auto apply_edges = [&](EdgeSizes& pixels, const EdgeSizes& percent, float basis) {
        apply(pixels.top, percent.top, basis);
        apply(pixels.right, percent.right, basis);
        apply(pixels.bottom, percent.bottom, basis);
        apply(pixels.left, percent.left, basis);
    };

    apply(style.width, style.width_percent, width_basis);
    apply(style.min_width, style.min_width_percent, width_basis);
    apply(style.max_width, style.max_width_percent, width_basis);
    apply(style.height, style.height_percent, height_basis);
    apply(style.min_height, style.min_height_percent, height_basis);
    apply(style.max_height, style.max_height_percent, height_basis);
    apply_edges(style.margin, style.margin_percent, width_basis);
    apply_edges(style.padding, style.padding_percent, width_basis);
}

void LayoutBox::layout(float containing_width, float containing_height) {
    (void)containing_height;
    if (!needs_layout && !child_needs_layout) {
        return;
    }

    // Every way layout goes one level deeper passes through here (block, flex,
    // grid, table and inline layout all call layout() on their children), so
    // this is the one place that has to ask whether there is stack left for
    // another level. LayoutTree::build_box asks the same question when it
    // builds the tree, but a level of layout does not cost what a level of
    // tree building costs, so a tree that could be built is not thereby a
    // tree that can be laid out. A box that is too deep keeps its zero size.
    if (stack_is_exhausted()) return;

    resolve_percentages(containing_width, containing_height);
    apply_style_edges();

    switch (box_type) {
        case BoxType::FlexNode:
            layout_flex(containing_width);
            break;
        case BoxType::GridNode:
            layout_grid(containing_width);
            break;
        case BoxType::BlockNode:
        case BoxType::AnonymousBlock:
            layout_block(containing_width);
            break;
        case BoxType::InlineNode:
            layout_inline(containing_width);
            break;
        case BoxType::ImageNode:
            layout_image(containing_width);
            break;
        case BoxType::TextNode:
            layout_text(containing_width);
            break;
        case BoxType::TableNode:
            layout_table(containing_width);
            break;
        case BoxType::TableRowNode:
            layout_table_row(containing_width);
            break;
        case BoxType::TableCellNode:
            layout_table_cell(containing_width);
            break;
    }

    clear_layout_flags();
}

bool LayoutBox::establishes_ifc() const {
    if (box_type != BoxType::BlockNode && box_type != BoxType::AnonymousBlock &&
        box_type != BoxType::TableCellNode) {
        return false;
    }
    if (style.is_table() || style.is_table_row()) return false;
    if (children.empty()) return false;
    bool has_inline_content = false;
    for (const auto& child : children) {
        // Out-of-flow children do not take part in inline flow, and floats are
        // handled by the formatting context itself.
        if (child->style.is_out_of_flow() || child->style.is_floated()) continue;
        if (child->box_type == BoxType::BlockNode ||
            child->box_type == BoxType::FlexNode ||
            child->box_type == BoxType::GridNode ||
            child->box_type == BoxType::AnonymousBlock ||
            child->box_type == BoxType::TableNode ||
            child->box_type == BoxType::TableRowNode ||
            child->box_type == BoxType::TableCellNode) {
            return false;
        }
        has_inline_content = true;
    }
    // A box holding nothing but out-of-flow or floated children has no inline
    // content to format; it needs the block path so its floats can be placed.
    return has_inline_content;
}

void LayoutBox::layout_block(float containing_width) {
    if (establishes_ifc()) {
        layout_inline_formatting_context(containing_width);
        return;
    }

    float h_padding_border = dimensions.border.left + dimensions.border.right +
                            dimensions.padding.left + dimensions.padding.right;
    float v_padding_border = dimensions.border.top + dimensions.border.bottom +
                            dimensions.padding.top + dimensions.padding.bottom;
    float total_horizontal_spacing = dimensions.margin.left + dimensions.margin.right + h_padding_border;

    // 1. Calculate Content Width with BoxSizing & Min/Max constraints
    if (style.width >= 0.0f) {
        if (style.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.width = std::max(0.0f, style.width - h_padding_border);
        } else {
            dimensions.content.width = style.width;
        }
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_horizontal_spacing);
    }

    if (style.min_width >= 0.0f) {
        float min_w = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.min_width - h_padding_border) : style.min_width;
        dimensions.content.width = std::max(dimensions.content.width, min_w);
    }
    if (style.max_width >= 0.0f) {
        float max_w = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.max_width - h_padding_border) : style.max_width;
        dimensions.content.width = std::min(dimensions.content.width, max_w);
    }

    // Margin: auto centering
    float remaining_h = std::max(0.0f, containing_width - (dimensions.content.width + h_padding_border));
    if (style.margin_left_auto && style.margin_right_auto) {
        dimensions.margin.left = remaining_h / 2.0f;
        dimensions.margin.right = remaining_h / 2.0f;
    } else if (style.margin_left_auto) {
        dimensions.margin.left = remaining_h;
    } else if (style.margin_right_auto) {
        dimensions.margin.right = remaining_h;
    }

    // A definite height gives descendants a basis for percentage heights.
    const float definite_height =
        (style.height >= 0.0f)
            ? ((style.box_sizing == BoxSizing::BorderBox)
                   ? std::max(0.0f, style.height - v_padding_border)
                   : style.height)
            : -1.0f;

    // 2. Layout Children, placing floats and propagating the float context
    float y_cursor = 0.0f;
    float_bands.clear();

    // `float_bands` holds the bands that can intrude on something. A band
    // of no height covers no position, so no question about a position is
    // ever answered by it, and every float placed looks at every band: with
    // those kept, 200,000 empty floats in one box took 20 seconds. What
    // such a band was still read for is noted here as the floats are placed:
    // that this box has floats at all, and where the lowest float of each
    // side ends (for `clear`, and for the height of a box that contains its
    // floats).
    bool has_floats = false;
    float lowest_float = -1.0f;         // bottom of the lowest float; -1 without one, as clearance_y says it
    float lowest_left_float = -1.0f;
    float lowest_right_float = -1.0f;
    auto add_band = [&](const FloatBand& band) {
        has_floats = true;
        lowest_float = std::max(lowest_float, band.bottom);
        if (band.left > 0.0f) lowest_left_float = std::max(lowest_left_float, band.bottom);
        if (band.right > 0.0f) lowest_right_float = std::max(lowest_right_float, band.bottom);
        if (band.bottom > band.top) float_bands.push_back(band);
    };

    for (auto& child : children) {
        if (child->style.is_out_of_flow()) {
            continue; // Handled in out-of-flow pass
        }

        // --- floated children are taken out of the normal flow -------------
        if (child->style.is_floated()) {
            child->layout_float(dimensions.content.width);

            const float margin_box_w = child->dimensions.margin_box().width;
            const float margin_box_h = child->dimensions.margin_box().height;
            const float inset_x = child->dimensions.margin.left + child->dimensions.border.left +
                                  child->dimensions.padding.left;

            // Slide down until the float fits horizontally next to the others.
            float float_top = y_cursor;
            float left_intrusion = 0.0f;
            float right_intrusion = 0.0f;
            for (int guard = 0; guard < 512; ++guard) {
                band_at(float_bands, float_top, left_intrusion, right_intrusion);
                const float room = dimensions.content.width - left_intrusion - right_intrusion;
                if (room >= margin_box_w || float_top > 100000.0f) break;
                float_top += std::max(1.0f, margin_box_h * 0.25f);
                left_intrusion = right_intrusion = 0.0f;
                band_at(float_bands, float_top, left_intrusion, right_intrusion);
            }

            float float_x;
            if (child->style.float_type == FloatType::Left) {
                float_x = dimensions.content.x + left_intrusion + inset_x;
                add_band(FloatBand{float_top, float_top + margin_box_h,
                                   left_intrusion + margin_box_w, 0.0f});
            } else {
                float_x = dimensions.content.x + dimensions.content.width - right_intrusion -
                          margin_box_w + inset_x;
                add_band(FloatBand{float_top, float_top + margin_box_h,
                                   0.0f, right_intrusion + margin_box_w});
            }

            const float float_y = dimensions.content.y + float_top +
                                  child->dimensions.margin.top + child->dimensions.border.top +
                                  child->dimensions.padding.top;
            child->shift_position(float_x - child->dimensions.content.x,
                                  float_y - child->dimensions.content.y);
            continue;  // floats never advance the flow cursor
        }

        // --- clearance ------------------------------------------------------
        if (child->style.clear_type != ClearType::None) {
            // What clearance_y() computes over the bands, kept as they came.
            const float clear_to = child->style.clear_type == ClearType::Both ? lowest_float
                                 : child->style.clear_type == ClearType::Left ? lowest_left_float
                                                                              : lowest_right_float;
            if (clear_to > y_cursor) y_cursor = clear_to;
        }

        // --- inline content wraps around this box's floats ------------------
        const std::vector<FloatBand>* child_bands = has_floats ? &float_bands : inherited_bands;
        child->inherited_bands = child_bands;
        child->inherited_bands_offset_y =
            (has_floats ? 0.0f : inherited_bands_offset_y) + y_cursor +
            child->style.margin.top + child->style.border_width.top + child->style.padding.top;

        child->layout(dimensions.content.width, definite_height);

        float new_x = dimensions.content.x +
                      child->dimensions.margin.left +
                      child->dimensions.border.left +
                      child->dimensions.padding.left;

        float new_y = dimensions.content.y + y_cursor +
                      child->dimensions.margin.top +
                      child->dimensions.border.top +
                      child->dimensions.padding.top;

        // Relative positioning offset
        if (child->style.position == Position::Relative) {
            if (child->style.has_left) new_x += child->style.left;
            else if (child->style.has_right) new_x -= child->style.right;
            if (child->style.has_top) new_y += child->style.top;
            else if (child->style.has_bottom) new_y -= child->style.bottom;
        }

        float dx = new_x - child->dimensions.content.x;
        float dy = new_y - child->dimensions.content.y;
        child->shift_position(dx, dy);

        y_cursor += child->dimensions.margin_box().height;
    }

    // 3. Content Height with BoxSizing & Min/Max constraints
    if (style.height >= 0.0f) {
        if (style.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.height = std::max(0.0f, style.height - v_padding_border);
        } else {
            dimensions.content.height = style.height;
        }
    } else {
        dimensions.content.height = y_cursor;
    }

    // A block formatting context contains its floats: grow to the lowest one.
    if (has_floats && establishes_bfc()) {
        dimensions.content.height = std::max(dimensions.content.height, std::max(0.0f, lowest_float));
    }

    if (style.min_height >= 0.0f) {
        float min_h = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.min_height - v_padding_border) : style.min_height;
        dimensions.content.height = std::max(dimensions.content.height, min_h);
    }
    if (style.max_height >= 0.0f) {
        float max_h = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.max_height - v_padding_border) : style.max_height;
        dimensions.content.height = std::min(dimensions.content.height, max_h);
    }
}

void LayoutBox::layout_flex(float containing_width) {
    float h_padding_border = dimensions.border.left + dimensions.border.right +
                            dimensions.padding.left + dimensions.padding.right;
    float v_padding_border = dimensions.border.top + dimensions.border.bottom +
                            dimensions.padding.top + dimensions.padding.bottom;
    float total_h_spacing = dimensions.margin.left + dimensions.margin.right + h_padding_border;

    if (style.width >= 0.0f) {
        if (style.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.width = std::max(0.0f, style.width - h_padding_border);
        } else {
            dimensions.content.width = style.width;
        }
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_h_spacing);
    }

    if (style.min_width >= 0.0f) {
        float min_w = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.min_width - h_padding_border) : style.min_width;
        dimensions.content.width = std::max(dimensions.content.width, min_w);
    }
    if (style.max_width >= 0.0f) {
        float max_w = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.max_width - h_padding_border) : style.max_width;
        dimensions.content.width = std::min(dimensions.content.width, max_w);
    }

    // Filter in-flow children
    std::vector<std::shared_ptr<LayoutBox>> in_flow;
    for (const auto& child : children) {
        if (!child->style.is_out_of_flow()) {
            in_flow.push_back(child);
        }
    }

    if (in_flow.empty()) {
        float h = (style.height >= 0.0f) ? style.height : 0.0f;
        if (style.box_sizing == BoxSizing::BorderBox && style.height >= 0.0f) {
            h = std::max(0.0f, h - v_padding_border);
        }
        dimensions.content.height = h;
        return;
    }

    bool is_row = (style.flex_direction == FlexDirection::Row || style.flex_direction == FlexDirection::RowReverse);

    float col_gap = style.grid_column_gap > 0.0f ? style.grid_column_gap : style.gap;
    float row_gap = style.grid_row_gap > 0.0f ? style.grid_row_gap : style.gap;

    if (is_row) {
        struct FlexLine {
            std::vector<std::shared_ptr<LayoutBox>> items;
            float line_items_width{0.0f};
            float total_grow{0.0f};
            float max_height{0.0f};
        };

        std::vector<FlexLine> lines;
        if (style.flex_wrap == FlexWrap::Wrap) {
            FlexLine cur_line;
            for (auto& child : in_flow) {
                float child_avail_w = (child->style.width >= 0.0f) ? child->style.width : (dimensions.content.width / in_flow.size());
                child->layout(child_avail_w);
                float item_w = child->dimensions.margin_box().width;
                float item_h = child->dimensions.margin_box().height;

                float test_w = cur_line.items.empty() ? item_w : (cur_line.line_items_width + (cur_line.items.size() * col_gap) + item_w);

                if (!cur_line.items.empty() && test_w > dimensions.content.width) {
                    lines.push_back(std::move(cur_line));
                    cur_line = FlexLine();
                }

                cur_line.items.push_back(child);
                cur_line.line_items_width += item_w;
                cur_line.total_grow += child->style.flex_grow;
                cur_line.max_height = std::max(cur_line.max_height, item_h);
            }
            if (!cur_line.items.empty()) {
                lines.push_back(std::move(cur_line));
            }
        } else {
            FlexLine single;
            for (auto& child : in_flow) {
                float child_avail_w = (child->style.width >= 0.0f) ? child->style.width : (dimensions.content.width / in_flow.size());
                child->layout(child_avail_w);
                single.items.push_back(child);
                single.line_items_width += child->dimensions.margin_box().width;
                single.total_grow += child->style.flex_grow;
                single.max_height = std::max(single.max_height, child->dimensions.margin_box().height);
            }
            lines.push_back(std::move(single));
        }

        float y_line_cursor = 0.0f;
        for (auto& line : lines) {
            float total_gaps = (line.items.size() > 1) ? (line.items.size() - 1) * style.gap : 0.0f;
            float free_space = dimensions.content.width - (line.line_items_width + total_gaps);

            if (free_space > 0.0f && line.total_grow > 0.0f) {
                for (auto& child : line.items) {
                    if (child->style.flex_grow > 0.0f) {
                        float extra = (child->style.flex_grow / line.total_grow) * free_space;
                        child->dimensions.content.width += extra;
                        child->layout(child->dimensions.content.width);
                    }
                }
            }

            float total_used_w = 0.0f;
            float cur_line_max_h = 0.0f;
            for (const auto& child : line.items) {
                total_used_w += child->dimensions.margin_box().width;
                cur_line_max_h = std::max(cur_line_max_h, child->dimensions.margin_box().height);
            }
            float remaining = dimensions.content.width - (total_used_w + total_gaps);

            float x_cursor = 0.0f;
            float gap_spacing = col_gap;

            switch (style.justify_content) {
                case JustifyContent::FlexStart:
                    x_cursor = 0.0f;
                    break;
                case JustifyContent::FlexEnd:
                    x_cursor = std::max(0.0f, remaining);
                    break;
                case JustifyContent::Center:
                    x_cursor = std::max(0.0f, remaining / 2.0f);
                    break;
                case JustifyContent::SpaceBetween:
                    if (line.items.size() > 1) {
                        gap_spacing += std::max(0.0f, remaining / (line.items.size() - 1));
                    }
                    break;
                case JustifyContent::SpaceAround:
                    if (!line.items.empty()) {
                        float space = std::max(0.0f, remaining / line.items.size());
                        x_cursor = space / 2.0f;
                        gap_spacing += space;
                    }
                    break;
                case JustifyContent::SpaceEvenly:
                    if (!line.items.empty()) {
                        float space = std::max(0.0f, remaining / (line.items.size() + 1));
                        x_cursor = space;
                        gap_spacing += space;
                    }
                    break;
            }

            for (auto& child : line.items) {
                float y_offset = 0.0f;
                float child_h = child->dimensions.margin_box().height;

                switch (style.align_items) {
                    case AlignItems::Stretch:
                        if (child->style.height < 0.0f && cur_line_max_h > 0.0f) {
                            child->dimensions.content.height = cur_line_max_h;
                        }
                        break;
                    case AlignItems::FlexEnd:
                        y_offset = cur_line_max_h - child_h;
                        break;
                    case AlignItems::Center:
                        y_offset = (cur_line_max_h - child_h) / 2.0f;
                        break;
                    case AlignItems::FlexStart:
                    default:
                        y_offset = 0.0f;
                        break;
                }

                float new_x = dimensions.content.x + x_cursor + child->dimensions.margin.left;
                float new_y = dimensions.content.y + y_line_cursor + y_offset + child->dimensions.margin.top;

                if (child->style.position == Position::Relative) {
                    if (child->style.has_left) new_x += child->style.left;
                    else if (child->style.has_right) new_x -= child->style.right;
                    if (child->style.has_top) new_y += child->style.top;
                    else if (child->style.has_bottom) new_y -= child->style.bottom;
                }

                float dx = new_x - child->dimensions.content.x;
                float dy = new_y - child->dimensions.content.y;
                child->shift_position(dx, dy);

                x_cursor += child->dimensions.margin_box().width + gap_spacing;
            }

            y_line_cursor += cur_line_max_h + row_gap;
        }

        float total_h = y_line_cursor > 0.0f ? (y_line_cursor - row_gap) : 0.0f;
        float final_h = (style.height >= 0.0f) ? style.height : total_h;
        if (style.box_sizing == BoxSizing::BorderBox && style.height >= 0.0f) {
            final_h = std::max(0.0f, final_h - v_padding_border);
        }
        dimensions.content.height = final_h;
    } else {
        // Column layout: main axis is vertical (Y), cross axis is horizontal (X)
        float y_cursor = 0.0f;
        float max_child_w = 0.0f;

        for (auto& child : in_flow) {
            child->layout(dimensions.content.width);
            max_child_w = std::max(max_child_w, child->dimensions.margin_box().width);

            float new_x = dimensions.content.x + child->dimensions.margin.left;
            float new_y = dimensions.content.y + y_cursor + child->dimensions.margin.top;

            if (child->style.position == Position::Relative) {
                if (child->style.has_left) new_x += child->style.left;
                else if (child->style.has_right) new_x -= child->style.right;
                if (child->style.has_top) new_y += child->style.top;
                else if (child->style.has_bottom) new_y -= child->style.bottom;
            }

            float dx = new_x - child->dimensions.content.x;
            float dy = new_y - child->dimensions.content.y;
            child->shift_position(dx, dy);

            y_cursor += child->dimensions.margin_box().height + row_gap;
        }

        float total_h = y_cursor > 0.0f ? (y_cursor - row_gap) : 0.0f;
        float final_h = (style.height >= 0.0f) ? style.height : total_h;
        if (style.box_sizing == BoxSizing::BorderBox && style.height >= 0.0f) {
            final_h = std::max(0.0f, final_h - v_padding_border);
        }
        dimensions.content.height = final_h;
    }
}

void LayoutBox::layout_grid(float containing_width) {
    float h_padding_border = dimensions.border.left + dimensions.border.right +
                            dimensions.padding.left + dimensions.padding.right;
    float v_padding_border = dimensions.border.top + dimensions.border.bottom +
                            dimensions.padding.top + dimensions.padding.bottom;
    float total_h_spacing = dimensions.margin.left + dimensions.margin.right + h_padding_border;

    if (style.width >= 0.0f) {
        if (style.box_sizing == BoxSizing::BorderBox) {
            dimensions.content.width = std::max(0.0f, style.width - h_padding_border);
        } else {
            dimensions.content.width = style.width;
        }
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_h_spacing);
    }

    if (style.min_width >= 0.0f) {
        float min_w = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.min_width - h_padding_border) : style.min_width;
        dimensions.content.width = std::max(dimensions.content.width, min_w);
    }
    if (style.max_width >= 0.0f) {
        float max_w = (style.box_sizing == BoxSizing::BorderBox) ?
            std::max(0.0f, style.max_width - h_padding_border) : style.max_width;
        dimensions.content.width = std::min(dimensions.content.width, max_w);
    }

    // Filter in-flow children
    std::vector<std::shared_ptr<LayoutBox>> in_flow;
    for (const auto& child : children) {
        if (!child->style.is_out_of_flow()) {
            in_flow.push_back(child);
        }
    }

    if (in_flow.empty()) {
        float h = (style.height >= 0.0f) ? style.height : 0.0f;
        if (style.box_sizing == BoxSizing::BorderBox && style.height >= 0.0f) {
            h = std::max(0.0f, h - v_padding_border);
        }
        dimensions.content.height = h;
        return;
    }

    // Compute column track widths
    float col_gap = style.grid_column_gap > 0.0f ? style.grid_column_gap : style.gap;
    float row_gap = style.grid_row_gap > 0.0f ? style.grid_row_gap : style.gap;

    std::vector<ComputedStyle::GridTrackDef> tracks = style.grid_col_track_defs;
    if (style.grid_auto_fit_columns) {
        float min_track = style.grid_auto_fit_def.min_px > 0.0f ? style.grid_auto_fit_def.min_px : 100.0f;
        int num_fit = std::max(1, static_cast<int>(std::floor((dimensions.content.width + col_gap) / (min_track + col_gap))));
        tracks.clear();
        for (int k = 0; k < num_fit; ++k) {
            tracks.push_back(style.grid_auto_fit_def);
        }
    } else if (tracks.empty()) {
        if (!style.grid_template_columns.empty()) {
            for (float t : style.grid_template_columns) {
                ComputedStyle::GridTrackDef d;
                if (t < 0.0f) d.max_fr = -t;
                else { d.min_px = t; d.max_px = t; }
                tracks.push_back(d);
            }
        } else {
            ComputedStyle::GridTrackDef d;
            d.max_fr = 1.0f;
            tracks.push_back(d);
        }
    }

    int max_area_col = 0;
    int max_area_row = 0;
    for (const auto& [name, rect] : style.grid_named_areas) {
        max_area_col = std::max(max_area_col, rect.col_end - 1);
        max_area_row = std::max(max_area_row, rect.row_end - 1);
    }
    while (tracks.size() < static_cast<size_t>(max_area_col)) {
        ComputedStyle::GridTrackDef d;
        d.max_fr = 1.0f;
        tracks.push_back(d);
    }

    size_t num_cols = tracks.size();
    float total_gaps = (num_cols > 1) ? (num_cols - 1) * col_gap : 0.0f;
    float avail_for_tracks = std::max(0.0f, dimensions.content.width - total_gaps);

    float sum_min = 0.0f;
    float total_fr = 0.0f;
    for (const auto& tr : tracks) {
        sum_min += tr.min_px;
        total_fr += tr.max_fr;
    }

    float fr_space = std::max(0.0f, avail_for_tracks - sum_min);
    std::vector<float> col_widths;
    for (const auto& tr : tracks) {
        float w = tr.min_px;
        if (total_fr > 0.0f && tr.max_fr > 0.0f) {
            w += (tr.max_fr / total_fr) * fr_space;
        } else if (tr.max_px > tr.min_px) {
            w = std::min(tr.max_px, tr.min_px + fr_space);
        }
        col_widths.push_back(w);
    }

    std::vector<float> col_x(num_cols, 0.0f);
    float cur_x = 0.0f;
    for (size_t c = 0; c < num_cols; ++c) {
        col_x[c] = cur_x;
        cur_x += col_widths[c] + col_gap;
    }

    struct ItemPlacement {
        size_t c_start{0};
        size_t c_end{1};
        size_t r_start{0};
        size_t r_end{1};
    };
    std::vector<ItemPlacement> placements(in_flow.size());
    size_t auto_c = 0;
    size_t auto_r = 0;
    size_t max_placed_row = (max_area_row > 0) ? static_cast<size_t>(max_area_row) : ((in_flow.size() + num_cols - 1) / num_cols);

    for (size_t i = 0; i < in_flow.size(); ++i) {
        auto& child = in_flow[i];
        bool placed = false;
        if (!child->style.grid_area_name.empty()) {
            auto it = style.grid_named_areas.find(child->style.grid_area_name);
            if (it != style.grid_named_areas.end()) {
                placements[i].c_start = static_cast<size_t>(std::max(0, it->second.col_start - 1));
                placements[i].c_end = std::max(placements[i].c_start + 1, static_cast<size_t>(it->second.col_end - 1));
                placements[i].r_start = static_cast<size_t>(std::max(0, it->second.row_start - 1));
                placements[i].r_end = std::max(placements[i].r_start + 1, static_cast<size_t>(it->second.row_end - 1));
                placed = true;
            }
        }
        if (!placed && (child->style.grid_column_start > 0 || child->style.grid_row_start > 0)) {
            size_t cs = child->style.grid_column_start > 0 ? static_cast<size_t>(child->style.grid_column_start - 1) : 0;
            size_t ce = child->style.grid_column_end > child->style.grid_column_start ? static_cast<size_t>(child->style.grid_column_end - 1) : cs + 1;
            size_t rs = child->style.grid_row_start > 0 ? static_cast<size_t>(child->style.grid_row_start - 1) : 0;
            size_t re = child->style.grid_row_end > child->style.grid_row_start ? static_cast<size_t>(child->style.grid_row_end - 1) : rs + 1;
            placements[i] = {cs, ce, rs, re};
            placed = true;
        }
        if (!placed) {
            placements[i] = {auto_c, auto_c + 1, auto_r, auto_r + 1};
            auto_c++;
            if (auto_c >= num_cols) {
                auto_c = 0;
                auto_r++;
            }
        }
        max_placed_row = std::max(max_placed_row, placements[i].r_end);
    }

    size_t num_rows = std::max(size_t(1), max_placed_row);
    std::vector<float> row_heights(num_rows, 0.0f);

    for (size_t i = 0; i < in_flow.size(); ++i) {
        size_t cs = std::min(placements[i].c_start, num_cols - 1);
        size_t ce = std::min(placements[i].c_end, num_cols);
        float item_w = 0.0f;
        for (size_t c = cs; c < ce; ++c) {
            item_w += col_widths[c];
            if (c + 1 < ce) item_w += col_gap;
        }

        in_flow[i]->layout(item_w);
        float item_h = in_flow[i]->dimensions.margin_box().height;
        size_t rs = std::min(placements[i].r_start, num_rows - 1);
        size_t span = std::max(size_t(1), placements[i].r_end - rs);
        float per_row_h = item_h / span;
        for (size_t r = rs; r < std::min(placements[i].r_end, num_rows); ++r) {
            row_heights[r] = std::max(row_heights[r], per_row_h);
        }
    }

    for (size_t r = 0; r < num_rows && r < style.grid_template_rows.size(); ++r) {
        float explicit_h = style.grid_template_rows[r];
        if (explicit_h >= 0.0f) {
            row_heights[r] = std::max(row_heights[r], explicit_h);
        }
    }

    std::vector<float> row_y(num_rows, 0.0f);
    float cur_y = 0.0f;
    for (size_t r = 0; r < num_rows; ++r) {
        row_y[r] = cur_y;
        cur_y += row_heights[r] + row_gap;
    }

    for (size_t i = 0; i < in_flow.size(); ++i) {
        size_t cs = std::min(placements[i].c_start, num_cols - 1);
        size_t rs = std::min(placements[i].r_start, num_rows - 1);

        auto& child = in_flow[i];
        float new_x = dimensions.content.x + col_x[cs] + child->dimensions.margin.left;
        float new_y = dimensions.content.y + row_y[rs] + child->dimensions.margin.top;

        if (child->style.position == Position::Relative) {
            if (child->style.has_left) new_x += child->style.left;
            else if (child->style.has_right) new_x -= child->style.right;
            if (child->style.has_top) new_y += child->style.top;
            else if (child->style.has_bottom) new_y -= child->style.bottom;
        }

        float dx = new_x - child->dimensions.content.x;
        float dy = new_y - child->dimensions.content.y;
        child->shift_position(dx, dy);
    }

    float total_grid_h = cur_y > 0.0f ? (cur_y - row_gap) : 0.0f;
    float final_h = (style.height >= 0.0f) ? style.height : total_grid_h;
    if (style.box_sizing == BoxSizing::BorderBox && style.height >= 0.0f) {
        final_h = std::max(0.0f, final_h - v_padding_border);
    }
    dimensions.content.height = final_h;
}

void LayoutBox::layout_inline(float containing_width) {
    float x_cursor = 0.0f;
    float max_child_height = 0.0f;

    for (auto& child : children) {
        child->layout(containing_width);
        child->dimensions.content.x = dimensions.content.x + x_cursor + child->dimensions.margin.left;
        child->dimensions.content.y = dimensions.content.y + child->dimensions.margin.top;

        x_cursor += child->dimensions.margin_box().width;
        max_child_height = std::max(max_child_height, child->dimensions.margin_box().height);
    }

    dimensions.content.width = x_cursor;
    dimensions.content.height = max_child_height;
}

void LayoutBox::layout_text(float containing_width) {
    lines.clear();
    float char_w = style.font_size * 0.55f;
    float line_h = style.font_size * 1.25f;

    if (containing_width <= 0.0f) containing_width = 800.0f;

    // Word wrap
    std::stringstream ss(text);
    std::string word;
    std::string current_line;
    float max_w = 0.0f;

    while (ss >> word) {
        float word_w = word.length() * char_w;
        float space_w = current_line.empty() ? 0.0f : char_w;

        if (!current_line.empty() && (current_line.length() * char_w + space_w + word_w) > containing_width) {
            lines.push_back(current_line);
            max_w = std::max(max_w, current_line.length() * char_w);
            current_line = word;
        } else {
            if (!current_line.empty()) current_line += " ";
            current_line += word;
        }
    }

    if (!current_line.empty()) {
        lines.push_back(current_line);
        max_w = std::max(max_w, current_line.length() * char_w);
    }

    if (lines.empty()) {
        lines.push_back(text);
        max_w = text.length() * char_w;
    }

    dimensions.content.width = max_w;
    dimensions.content.height = lines.size() * line_h;
}

struct InlineItem {
    enum class Type { Text, Space };
    Type type{Type::Text};
    std::string text;
    float width{0.0f};
    float height{0.0f};
    ComputedStyle style;
    std::shared_ptr<LayoutBox> source_box;
};

static void collect_items(
    const std::shared_ptr<LayoutBox>& box,
    std::vector<InlineItem>& items,
    const ComputedStyle& parent_style
) {
    (void)parent_style;
    if (!box) return;
    if (stack_is_exhausted()) return;   // inline boxes nested too deeply to follow

    if (box->box_type == BoxType::TextNode) {
        ComputedStyle effective_style = box->style;
        float char_w = effective_style.font_size * 0.55f;
        float line_h = effective_style.effective_line_height();

        size_t i = 0;
        const std::string& str = box->text;
        while (i < str.length()) {
            if (std::isspace(static_cast<unsigned char>(str[i]))) {
                while (i < str.length() && std::isspace(static_cast<unsigned char>(str[i]))) {
                    ++i;
                }
                if (!items.empty() && items.back().type == InlineItem::Type::Space) {
                    continue;
                }
                InlineItem item;
                item.type = InlineItem::Type::Space;
                item.text = " ";
                item.width = char_w;
                item.height = line_h;
                item.style = effective_style;
                item.source_box = box;
                items.push_back(item);
            } else {
                size_t start = i;
                while (i < str.length() && !std::isspace(static_cast<unsigned char>(str[i]))) {
                    ++i;
                }
                std::string word = str.substr(start, i - start);
                InlineItem item;
                item.type = InlineItem::Type::Text;
                item.text = word;
                item.width = word.length() * char_w;
                item.height = line_h;
                item.style = effective_style;
                item.source_box = box;
                items.push_back(item);
            }
        }
    } else if (box->box_type == BoxType::InlineNode) {
        size_t initial_count = items.size();
        for (const auto& ch : box->children) {
            collect_items(ch, items, box->style);
        }

        if (items.size() > initial_count) {
            float extra_left = box->style.padding.left + box->style.border_width.left;
            float extra_right = box->style.padding.right + box->style.border_width.right;

            if (extra_left > 0.0f) {
                items[initial_count].width += extra_left;
            }
            if (extra_right > 0.0f) {
                items.back().width += extra_right;
            }

            for (size_t idx = initial_count; idx < items.size(); ++idx) {
                if (!box->style.background_color.is_transparent()) {
                    items[idx].style.background_color = box->style.background_color;
                }
                if (box->style.border_radius > 0.0f) {
                    items[idx].style.border_radius = box->style.border_radius;
                }
                if (!box->style.text_decoration.empty() && box->style.text_decoration != "none") {
                    items[idx].style.text_decoration = box->style.text_decoration;
                }
                if (box->style.border_width.left > 0.0f || box->style.border_width.top > 0.0f ||
                    box->style.border_width.right > 0.0f || box->style.border_width.bottom > 0.0f) {
                    items[idx].style.border_width = box->style.border_width;
                    items[idx].style.border_color = box->style.border_color;
                }
            }
        }
    } else if (box->box_type == BoxType::ImageNode) {
        box->layout(0.0f);
        InlineItem item;
        item.type = InlineItem::Type::Text;
        item.width = box->dimensions.content.width;
        item.height = box->dimensions.content.height;
        item.style = box->style;
        item.source_box = box;
        items.push_back(item);
    }
}

void LayoutBox::layout_inline_formatting_context(float containing_width) {
    line_boxes.clear();

    // 1. Calculate Content Width
    float total_horizontal_spacing = dimensions.margin.left + dimensions.margin.right +
                                    dimensions.border.left + dimensions.border.right +
                                    dimensions.padding.left + dimensions.padding.right;

    if (style.width >= 0.0f) {
        dimensions.content.width = style.width;
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_horizontal_spacing);
    }

    float available_width = dimensions.content.width;
    if (available_width <= 0.0f) available_width = 800.0f;

    // 2. Collect Inline Items
    std::vector<InlineItem> items;
    for (const auto& ch : children) {
        collect_items(ch, items, style);
    }

    if (items.empty()) {
        dimensions.content.height = (style.height >= 0.0f) ? style.height : 0.0f;
        return;
    }

    // 2b. Floats that share this formatting context are placed first, so the
    //     line boxes can wrap around them.
    //
    //     They all start at the top, so how far the floats before this one
    //     reach in from each side is a running maximum; asking the bands
    //     each time made n floats in a paragraph cost n * n. A float of no
    //     height covers no line and leaves no band (see layout_block).
    float_bands.clear();
    bool has_floats = false;
    float left_reach = 0.0f;
    float right_reach = 0.0f;
    for (auto& child : children) {
        if (!child->style.is_floated()) continue;

        child->layout_float(dimensions.content.width);

        const float margin_box_w = child->dimensions.margin_box().width;
        const float margin_box_h = child->dimensions.margin_box().height;
        const float inset_x = child->dimensions.margin.left + child->dimensions.border.left +
                              child->dimensions.padding.left;

        const float left_intrusion = left_reach;
        const float right_intrusion = right_reach;

        float float_x;
        FloatBand band;
        if (child->style.float_type == FloatType::Left) {
            float_x = dimensions.content.x + left_intrusion + inset_x;
            band = FloatBand{0.0f, margin_box_h, left_intrusion + margin_box_w, 0.0f};
        } else {
            float_x = dimensions.content.x + dimensions.content.width - right_intrusion -
                      margin_box_w + inset_x;
            band = FloatBand{0.0f, margin_box_h, 0.0f, right_intrusion + margin_box_w};
        }
        has_floats = true;
        if (band.bottom > band.top) {
            // What band_at(float_bands, 0, ...) would add to its answer.
            left_reach = std::max(left_reach, band.left);
            right_reach = std::max(right_reach, band.right);
            float_bands.push_back(band);
        }

        child->shift_position(float_x - child->dimensions.content.x,
                              dimensions.content.y - child->dimensions.content.y +
                                  child->dimensions.margin.top + child->dimensions.border.top +
                                  child->dimensions.padding.top);
    }

    // 3. Line Breaking, wrapping around any floats in scope
    const std::vector<FloatBand>* bands = has_floats ? &float_bands : inherited_bands;
    const float bands_offset = has_floats ? 0.0f : inherited_bands_offset_y;
    auto measure_band = [&](float y, float& left, float& right) -> float {
        if (!bands) {
            left = 0.0f;
            right = 0.0f;
            return available_width;
        }
        LayoutBox::band_at(*bands, bands_offset + y, left, right);
        return std::max(1.0f, available_width - left - right);
    };

    LineBox current_line;
    float current_x = 0.0f;
    float line_top = 0.0f;              // running y used for band queries
    float line_left = 0.0f;
    float line_right = 0.0f;
    float line_capacity = measure_band(line_top, line_left, line_right);

    const float estimated_line_height = style.effective_line_height();

    auto finish_line = [&](LineBox& line, float width) {
        while (!line.fragments.empty() && line.fragments.back().is_space) {
            width -= line.fragments.back().rect.width;
            line.fragments.pop_back();
        }
        if (!line.fragments.empty()) {
            line.rect.width = width;
            float max_h = style.effective_line_height();
            for (const auto& f : line.fragments) {
                // CSS 2.1, 10.8.1: the line box's height is at least the
                // largest of the inline boxes on it, strut included.
                max_h = std::max(max_h, f.style.effective_line_height());
                max_h = std::max(max_h, f.rect.height);
            }
            line.rect.height = max_h;
            line_boxes.push_back(std::move(line));
        }
        line = LineBox();
    };

    for (const auto& item : items) {
        if (current_line.fragments.empty() && item.type == InlineItem::Type::Space) {
            // Drop leading space at line start
            continue;
        }

        if (!current_line.fragments.empty() && (current_x + item.width > line_capacity)) {
            // Line break: the next line may have a different float shape.
            finish_line(current_line, current_x);
            current_x = 0.0f;
            line_top += estimated_line_height;
            line_capacity = measure_band(line_top, line_left, line_right);

            if (item.type == InlineItem::Type::Space) {
                continue;
            }
        }

        InlineFragment frag;
        frag.rect.x = current_x;
        frag.rect.y = 0.0f;
        frag.rect.width = item.width;
        frag.rect.height = item.height;
        frag.text = item.text;
        frag.style = item.style;
        frag.box = item.source_box;
        frag.is_text = (item.type == InlineItem::Type::Text);
        frag.is_space = (item.type == InlineItem::Type::Space);

        current_line.fragments.push_back(frag);
        current_line.left_intrusion = line_left;
        current_line.right_intrusion = line_right;
        current_x += item.width;
    }

    if (!current_line.fragments.empty()) {
        finish_line(current_line, current_x);
    }

    // 4. Vertical Placement, text-align and float offsets
    float y_cursor = 0.0f;
    for (auto& line : line_boxes) {
        float left = 0.0f;
        float right = 0.0f;
        const float capacity = measure_band(y_cursor, left, right);

        float free_space = std::max(0.0f, capacity - line.rect.width);
        float x_offset = left;

        if (style.text_align == "center") {
            x_offset = left + free_space / 2.0f;
        } else if (style.text_align == "right") {
            x_offset = left + free_space;
        }

        line.rect.x = dimensions.content.x + x_offset;
        line.rect.y = dimensions.content.y + y_cursor;
        line.left_intrusion = left;
        line.right_intrusion = right;

        for (auto& frag : line.fragments) {
            frag.rect.x = dimensions.content.x + x_offset + frag.rect.x;
            frag.rect.y = dimensions.content.y + y_cursor;
            // Vertically center inside line if smaller than line height
            if (frag.rect.height < line.rect.height) {
                frag.rect.y += (line.rect.height - frag.rect.height) * 0.5f;
            }
        }

        y_cursor += line.rect.height;
    }

    dimensions.content.height = (style.height >= 0.0f) ? style.height : y_cursor;

    // 5. Populate child TextNodes for backward-compatibility & inspection
    //
    // One pass over the fragments notes, for each box, what it has on each
    // line; the children then pick up their own. Each child used to search
    // every fragment of every line for itself, so a paragraph of n images,
    // or of n short runs of text between <br> tags, cost n * n: 50,000
    // images took 6.7 seconds to lay out, 200,000 two minutes.
    struct OnLines {
        std::vector<std::string> text;          // the box's text on each line it has text on
        Rect image;                             // its first fragment on the last line it is on
        size_t last_text_line{static_cast<size_t>(-1)};
        size_t last_line{static_cast<size_t>(-1)};
        bool placed{false};
    };
    std::unordered_map<const LayoutBox*, OnLines> on_lines;
    for (size_t line_index = 0; line_index < line_boxes.size(); ++line_index) {
        for (const auto& frag : line_boxes[line_index].fragments) {
            if (!frag.box) continue;
            if (frag.box->box_type == BoxType::TextNode) {
                if (frag.text.empty()) continue;
                OnLines& on = on_lines[frag.box.get()];
                if (on.last_text_line != line_index) {
                    on.text.emplace_back();
                    on.last_text_line = line_index;
                }
                on.text.back() += frag.text;
            } else if (frag.box->box_type == BoxType::ImageNode) {
                OnLines& on = on_lines[frag.box.get()];
                if (on.last_line != line_index) {
                    on.image = frag.rect;
                    on.last_line = line_index;
                    on.placed = true;
                }
            }
        }
    }
    for (auto& child : children) {
        if (child->box_type == BoxType::TextNode) {
            child->lines.clear();
            if (auto found = on_lines.find(child.get()); found != on_lines.end()) {
                child->lines = std::move(found->second.text);
            }
            if (child->lines.empty() && !child->text.empty()) {
                child->lines.push_back(child->text);
            }
            child->dimensions = dimensions;
        } else if (child->box_type == BoxType::ImageNode) {
            if (auto found = on_lines.find(child.get()); found != on_lines.end() && found->second.placed) {
                child->dimensions.content = found->second.image;
            }
        }
    }
}

void LayoutBox::resolve_out_of_flow_positions(const std::shared_ptr<LayoutBox>& root, float viewport_w, float viewport_h) {
    if (!root) return;

    std::function<void(const std::shared_ptr<LayoutBox>&, const std::shared_ptr<LayoutBox>&)> pass =
        [&](const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<LayoutBox>& positioned_ancestor) {
        if (!box) return;
        if (stack_is_exhausted()) return;

        if (box->style.is_out_of_flow()) {
            Rect cb;
            if (box->style.position == Position::Fixed) {
                cb = Rect(0.0f, 0.0f, viewport_w, viewport_h);
            } else {
                auto cb_box = positioned_ancestor ? positioned_ancestor : root;
                cb.x = cb_box->dimensions.content.x - cb_box->dimensions.padding.left;
                cb.y = cb_box->dimensions.content.y - cb_box->dimensions.padding.top;
                cb.width = cb_box->dimensions.padding_box().width;
                cb.height = cb_box->dimensions.padding_box().height;
            }

            box->layout(cb.width, cb.height);

            // If both insets are given and dimension is auto, size to insets
            if (box->style.has_left && box->style.has_right && box->style.width < 0.0f) {
                float total_h = box->dimensions.margin.left + box->dimensions.margin.right +
                                box->dimensions.border.left + box->dimensions.border.right +
                                box->dimensions.padding.left + box->dimensions.padding.right;
                box->dimensions.content.width = std::max(0.0f, cb.width - box->style.left - box->style.right - total_h);
            }
            if (box->style.has_top && box->style.has_bottom && box->style.height < 0.0f) {
                float total_v = box->dimensions.margin.top + box->dimensions.margin.bottom +
                                box->dimensions.border.top + box->dimensions.border.bottom +
                                box->dimensions.padding.top + box->dimensions.padding.bottom;
                box->dimensions.content.height = std::max(0.0f, cb.height - box->style.top - box->style.bottom - total_v);
            }

            // Position X
            if (box->style.has_left) {
                box->dimensions.content.x = cb.x + box->style.left +
                                            box->dimensions.margin.left +
                                            box->dimensions.border.left +
                                            box->dimensions.padding.left;
            } else if (box->style.has_right) {
                box->dimensions.content.x = cb.x + cb.width - box->style.right -
                                            box->dimensions.margin.right -
                                            box->dimensions.border.right -
                                            box->dimensions.padding.right -
                                            box->dimensions.content.width;
            } else {
                box->dimensions.content.x = cb.x + box->dimensions.margin.left +
                                            box->dimensions.border.left +
                                            box->dimensions.padding.left;
            }

            // Position Y
            if (box->style.has_top) {
                box->dimensions.content.y = cb.y + box->style.top +
                                            box->dimensions.margin.top +
                                            box->dimensions.border.top +
                                            box->dimensions.padding.top;
            } else if (box->style.has_bottom) {
                box->dimensions.content.y = cb.y + cb.height - box->style.bottom -
                                            box->dimensions.margin.bottom -
                                            box->dimensions.border.bottom -
                                            box->dimensions.padding.bottom -
                                            box->dimensions.content.height;
            } else {
                box->dimensions.content.y = cb.y + box->dimensions.margin.top +
                                            box->dimensions.border.top +
                                            box->dimensions.padding.top;
            }
        }

        std::shared_ptr<LayoutBox> next_positioned = positioned_ancestor;
        if (box->style.is_positioned()) {
            next_positioned = box;
        }

        for (const auto& child : box->children) {
            pass(child, next_positioned);
        }
    };

    std::shared_ptr<LayoutBox> root_pos = root->style.is_positioned() ? root : nullptr;
    for (const auto& child : root->children) {
        pass(child, root_pos);
    }
}

void LayoutBox::shift_position(float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f) return;
    if (stack_is_exhausted()) return;
    dimensions.content.x += dx;
    dimensions.content.y += dy;
    for (auto& line : line_boxes) {
        line.rect.x += dx;
        line.rect.y += dy;
        for (auto& frag : line.fragments) {
            frag.rect.x += dx;
            frag.rect.y += dy;
        }
    }
    for (auto& child : children) {
        if (!child->style.is_out_of_flow()) {
            child->shift_position(dx, dy);
        }
    }
}

void LayoutBox::layout_image(float containing_width) {
    (void)containing_width;
    float w = (style.width >= 0.0f) ? style.width : (image ? static_cast<float>(image->width) : 100.0f);
    float h = (style.height >= 0.0f) ? style.height : (image ? static_cast<float>(image->height) : 100.0f);

    if (style.box_sizing == BoxSizing::BorderBox) {
        float h_pb = dimensions.border.left + dimensions.border.right + dimensions.padding.left + dimensions.padding.right;
        float v_pb = dimensions.border.top + dimensions.border.bottom + dimensions.padding.top + dimensions.padding.bottom;
        w = std::max(0.0f, w - h_pb);
        h = std::max(0.0f, h - v_pb);
    }
    dimensions.content.width = w;
    dimensions.content.height = h;
}

void LayoutBox::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    if (stack_is_exhausted()) {
        os << spaces << "... (nested too deeply to print)\n";
        return;
    }
    os << spaces;

    switch (box_type) {
        case BoxType::FlexNode: os << "FlexBox"; break;
        case BoxType::GridNode: os << "GridBox"; break;
        case BoxType::BlockNode: os << "BlockBox"; break;
        case BoxType::InlineNode: os << "InlineBox"; break;
        case BoxType::ImageNode: os << "ImageBox"; break;
        case BoxType::AnonymousBlock: os << "AnonymousBlock"; break;
        case BoxType::TextNode: os << "TextBox"; break;
        case BoxType::TableNode: os << "TableBox"; break;
        case BoxType::TableRowNode: os << "TableRowBox"; break;
        case BoxType::TableCellNode: os << "TableCellBox"; break;
    }

    if (node && node->is_element() && !node->as_element()->id().empty()) {
        os << " #" << node->as_element()->id();
    }

    os << " [x=" << dimensions.content.x
       << " y=" << dimensions.content.y
       << " w=" << dimensions.content.width
       << " h=" << dimensions.content.height << "]";

    if (!line_boxes.empty()) {
        os << " IFC lines=" << line_boxes.size()
           << " first_line=(" << line_boxes.front().rect.x << "," << line_boxes.front().rect.y
           << " w=" << line_boxes.front().rect.width
           << " intrude=" << line_boxes.front().left_intrusion << ")";
    } else if (!lines.empty()) {
        os << " lines=" << lines.size() << " text=\"" << lines[0] << (lines.size() > 1 ? "...\"" : "\"");
    } else if (!text.empty()) {
        os << " text=\"" << text << "\"";
    }

    os << "\n";
    for (const auto& ch : children) {
        ch->dump(os, indent + 1);
    }
}

} // namespace litebird
