#include "litebird/layout/LayoutBox.hpp"
#include <iomanip>
#include <sstream>
#include <numeric>

namespace litebird {

void LayoutBox::layout(float containing_width, float containing_height) {
    (void)containing_height;
    if (!needs_layout && !child_needs_layout) {
        return;
    }

    dimensions.margin = style.margin;
    dimensions.padding = style.padding;
    dimensions.border = style.border_width;

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
    }

    clear_layout_flags();
}

bool LayoutBox::establishes_ifc() const {
    if (box_type != BoxType::BlockNode && box_type != BoxType::AnonymousBlock) {
        return false;
    }
    if (children.empty()) return false;
    for (const auto& child : children) {
        if (child->box_type == BoxType::BlockNode ||
            child->box_type == BoxType::FlexNode ||
            child->box_type == BoxType::GridNode ||
            child->box_type == BoxType::AnonymousBlock) {
            return false;
        }
    }
    return true;
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

    // 2. Layout Children and compute content height
    float y_cursor = 0.0f;
    for (auto& child : children) {
        if (child->style.is_out_of_flow()) {
            continue; // Handled in out-of-flow pass
        }

        child->layout(dimensions.content.width);

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

                float test_w = cur_line.items.empty() ? item_w : (cur_line.line_items_width + (cur_line.items.size() * style.gap) + item_w);

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
            float gap_spacing = style.gap;

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

            y_line_cursor += cur_line_max_h + style.gap;
        }

        float total_h = y_line_cursor > 0.0f ? (y_line_cursor - style.gap) : 0.0f;
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

            y_cursor += child->dimensions.margin_box().height + style.gap;
        }

        float final_h = (style.height >= 0.0f) ? style.height : y_cursor;
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
    std::vector<float> col_widths;
    const auto& col_template = style.grid_template_columns.empty() ?
        std::vector<float>{-1.0f} : style.grid_template_columns;
    size_t num_cols = col_template.size();

    float col_gap = style.grid_column_gap > 0.0f ? style.grid_column_gap : style.gap;
    float row_gap = style.grid_row_gap > 0.0f ? style.grid_row_gap : style.gap;

    float total_gaps = (num_cols > 1) ? (num_cols - 1) * col_gap : 0.0f;
    float avail_for_tracks = std::max(0.0f, dimensions.content.width - total_gaps);

    float fixed_sum = 0.0f;
    float total_fr = 0.0f;
    for (float track : col_template) {
        if (track >= 0.0f) {
            fixed_sum += track;
        } else {
            total_fr += (-track);
        }
    }

    float fr_space = std::max(0.0f, avail_for_tracks - fixed_sum);
    for (float track : col_template) {
        if (track >= 0.0f) {
            col_widths.push_back(track);
        } else {
            float fr_w = (total_fr > 0.0f) ? ((-track) / total_fr) * fr_space : 0.0f;
            col_widths.push_back(fr_w);
        }
    }

    std::vector<float> col_x(num_cols, 0.0f);
    float cur_x = 0.0f;
    for (size_t c = 0; c < num_cols; ++c) {
        col_x[c] = cur_x;
        cur_x += col_widths[c] + col_gap;
    }

    size_t num_rows = (in_flow.size() + num_cols - 1) / num_cols;
    std::vector<float> row_heights(num_rows, 0.0f);

    for (size_t i = 0; i < in_flow.size(); ++i) {
        size_t c = i % num_cols;
        size_t r = i / num_cols;
        float item_w = col_widths[c];
        in_flow[i]->layout(item_w);
        float item_h = in_flow[i]->dimensions.margin_box().height;
        row_heights[r] = std::max(row_heights[r], item_h);
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
        size_t c = i % num_cols;
        size_t r = i / num_cols;

        auto& child = in_flow[i];
        float new_x = dimensions.content.x + col_x[c] + child->dimensions.margin.left;
        float new_y = dimensions.content.y + row_y[r] + child->dimensions.margin.top;

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

    if (box->box_type == BoxType::TextNode) {
        ComputedStyle effective_style = box->style;
        float char_w = effective_style.font_size * 0.55f;
        float line_h = effective_style.font_size * 1.25f;

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

    // 3. Line Breaking
    LineBox current_line;
    float current_x = 0.0f;

    auto finish_line = [&](LineBox& line, float width) {
        while (!line.fragments.empty() && line.fragments.back().is_space) {
            width -= line.fragments.back().rect.width;
            line.fragments.pop_back();
        }
        if (!line.fragments.empty()) {
            line.rect.width = width;
            float max_h = style.font_size * 1.25f;
            for (const auto& f : line.fragments) {
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

        if (!current_line.fragments.empty() && (current_x + item.width > available_width)) {
            // Line break
            finish_line(current_line, current_x);
            current_x = 0.0f;

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
        current_x += item.width;
    }

    if (!current_line.fragments.empty()) {
        finish_line(current_line, current_x);
    }

    // 4. Vertical Placement and text-align
    float y_cursor = 0.0f;
    for (auto& line : line_boxes) {
        float free_space = available_width - line.rect.width;
        float x_offset = 0.0f;

        if (style.text_align == "center") {
            x_offset = std::max(0.0f, free_space / 2.0f);
        } else if (style.text_align == "right") {
            x_offset = std::max(0.0f, free_space);
        }

        line.rect.x = dimensions.content.x;
        line.rect.y = dimensions.content.y + y_cursor;

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
    for (auto& child : children) {
        if (child->box_type == BoxType::TextNode) {
            child->lines.clear();
            for (const auto& line : line_boxes) {
                std::string line_text;
                for (const auto& frag : line.fragments) {
                    if (frag.box == child && !frag.text.empty()) {
                        line_text += frag.text;
                    }
                }
                if (!line_text.empty()) {
                    child->lines.push_back(line_text);
                }
            }
            if (child->lines.empty() && !child->text.empty()) {
                child->lines.push_back(child->text);
            }
            child->dimensions = dimensions;
        } else if (child->box_type == BoxType::ImageNode) {
            for (const auto& line : line_boxes) {
                for (const auto& frag : line.fragments) {
                    if (frag.box == child) {
                        child->dimensions.content = frag.rect;
                        break;
                    }
                }
            }
        }
    }
}

void LayoutBox::resolve_out_of_flow_positions(const std::shared_ptr<LayoutBox>& root, float viewport_w, float viewport_h) {
    if (!root) return;

    std::function<void(const std::shared_ptr<LayoutBox>&, const std::shared_ptr<LayoutBox>&)> pass =
        [&](const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<LayoutBox>& positioned_ancestor) {
        if (!box) return;

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
    os << spaces;

    switch (box_type) {
        case BoxType::FlexNode: os << "FlexBox"; break;
        case BoxType::GridNode: os << "GridBox"; break;
        case BoxType::BlockNode: os << "BlockBox"; break;
        case BoxType::InlineNode: os << "InlineBox"; break;
        case BoxType::ImageNode: os << "ImageBox"; break;
        case BoxType::AnonymousBlock: os << "AnonymousBlock"; break;
        case BoxType::TextNode: os << "TextBox"; break;
    }

    os << " [x=" << dimensions.content.x
       << " y=" << dimensions.content.y
       << " w=" << dimensions.content.width
       << " h=" << dimensions.content.height << "]";

    if (!line_boxes.empty()) {
        os << " IFC lines=" << line_boxes.size();
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
