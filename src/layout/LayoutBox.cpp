#include "litebird/layout/LayoutBox.hpp"
#include <iomanip>
#include <sstream>
#include <numeric>

namespace litebird {

void LayoutBox::layout(float containing_width, float containing_height) {
    (void)containing_height;
    dimensions.margin = style.margin;
    dimensions.padding = style.padding;
    dimensions.border = style.border_width;

    switch (box_type) {
        case BoxType::FlexNode:
            layout_flex(containing_width);
            break;
        case BoxType::BlockNode:
        case BoxType::AnonymousBlock:
            layout_block(containing_width);
            break;
        case BoxType::InlineNode:
            layout_inline(containing_width);
            break;
        case BoxType::TextNode:
            layout_text(containing_width);
            break;
    }
}

void LayoutBox::layout_block(float containing_width) {
    // 1. Calculate Content Width
    float total_horizontal_spacing = dimensions.margin.left + dimensions.margin.right +
                                    dimensions.border.left + dimensions.border.right +
                                    dimensions.padding.left + dimensions.padding.right;

    if (style.width >= 0.0f) {
        dimensions.content.width = style.width;
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_horizontal_spacing);
    }

    // 2. Layout Children and compute content height
    float y_cursor = 0.0f;
    for (auto& child : children) {
        child->layout(dimensions.content.width);

        child->dimensions.content.x = dimensions.content.x +
                                     child->dimensions.margin.left +
                                     child->dimensions.border.left +
                                     child->dimensions.padding.left;

        child->dimensions.content.y = dimensions.content.y + y_cursor +
                                     child->dimensions.margin.top +
                                     child->dimensions.border.top +
                                     child->dimensions.padding.top;

        y_cursor += child->dimensions.margin_box().height;
    }

    // 3. Content Height
    if (style.height >= 0.0f) {
        dimensions.content.height = style.height;
    } else {
        dimensions.content.height = y_cursor;
    }
}

void LayoutBox::layout_flex(float containing_width) {
    float total_h_spacing = dimensions.margin.left + dimensions.margin.right +
                           dimensions.border.left + dimensions.border.right +
                           dimensions.padding.left + dimensions.padding.right;

    if (style.width >= 0.0f) {
        dimensions.content.width = style.width;
    } else {
        dimensions.content.width = std::max(0.0f, containing_width - total_h_spacing);
    }

    if (children.empty()) {
        dimensions.content.height = (style.height >= 0.0f) ? style.height : 0.0f;
        return;
    }

    bool is_row = (style.flex_direction == FlexDirection::Row || style.flex_direction == FlexDirection::RowReverse);

    if (is_row) {
        // Initial layout pass for all items
        float total_items_width = 0.0f;
        float total_grow = 0.0f;

        for (auto& child : children) {
            float child_avail_w = (child->style.width >= 0.0f) ? child->style.width : (dimensions.content.width / children.size());
            child->layout(child_avail_w);
            total_items_width += child->dimensions.margin_box().width;
            total_grow += child->style.flex_grow;
        }

        float total_gaps = (children.size() > 1) ? (children.size() - 1) * style.gap : 0.0f;
        float free_space = dimensions.content.width - (total_items_width + total_gaps);

        // Distribute flex-grow if free space exists
        if (free_space > 0.0f && total_grow > 0.0f) {
            for (auto& child : children) {
                if (child->style.flex_grow > 0.0f) {
                    float extra = (child->style.flex_grow / total_grow) * free_space;
                    child->dimensions.content.width += extra;
                    child->layout(child->dimensions.content.width);
                }
            }
        }

        // Compute max child height for cross axis
        float max_child_h = 0.0f;
        for (const auto& child : children) {
            max_child_h = std::max(max_child_h, child->dimensions.margin_box().height);
        }

        // Justify-content spacing along Main Axis (X)
        float total_used_w = 0.0f;
        for (const auto& child : children) {
            total_used_w += child->dimensions.margin_box().width;
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
                if (children.size() > 1) {
                    gap_spacing += std::max(0.0f, remaining / (children.size() - 1));
                }
                break;
            case JustifyContent::SpaceAround:
                if (!children.empty()) {
                    float space = std::max(0.0f, remaining / children.size());
                    x_cursor = space / 2.0f;
                    gap_spacing += space;
                }
                break;
            case JustifyContent::SpaceEvenly:
                if (!children.empty()) {
                    float space = std::max(0.0f, remaining / (children.size() + 1));
                    x_cursor = space;
                    gap_spacing += space;
                }
                break;
        }

        // Position items
        for (auto& child : children) {
            float y_offset = 0.0f;
            float child_h = child->dimensions.margin_box().height;

            switch (style.align_items) {
                case AlignItems::Stretch:
                    if (child->style.height < 0.0f && max_child_h > 0.0f) {
                        child->dimensions.content.height = max_child_h;
                    }
                    break;
                case AlignItems::FlexEnd:
                    y_offset = max_child_h - child_h;
                    break;
                case AlignItems::Center:
                    y_offset = (max_child_h - child_h) / 2.0f;
                    break;
                case AlignItems::FlexStart:
                default:
                    y_offset = 0.0f;
                    break;
            }

            child->dimensions.content.x = dimensions.content.x + x_cursor + child->dimensions.margin.left;
            child->dimensions.content.y = dimensions.content.y + y_offset + child->dimensions.margin.top;

            x_cursor += child->dimensions.margin_box().width + gap_spacing;
        }

        dimensions.content.height = (style.height >= 0.0f) ? style.height : max_child_h;
    } else {
        // Column layout: main axis is vertical (Y), cross axis is horizontal (X)
        float y_cursor = 0.0f;
        float max_child_w = 0.0f;

        for (auto& child : children) {
            child->layout(dimensions.content.width);
            max_child_w = std::max(max_child_w, child->dimensions.margin_box().width);

            child->dimensions.content.x = dimensions.content.x + child->dimensions.margin.left;
            child->dimensions.content.y = dimensions.content.y + y_cursor + child->dimensions.margin.top;

            y_cursor += child->dimensions.margin_box().height + style.gap;
        }

        dimensions.content.height = (style.height >= 0.0f) ? style.height : y_cursor;
    }
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

void LayoutBox::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces;

    switch (box_type) {
        case BoxType::FlexNode: os << "FlexBox"; break;
        case BoxType::BlockNode: os << "BlockBox"; break;
        case BoxType::InlineNode: os << "InlineBox"; break;
        case BoxType::AnonymousBlock: os << "AnonymousBlock"; break;
        case BoxType::TextNode: os << "TextBox"; break;
    }

    os << " [x=" << dimensions.content.x
       << " y=" << dimensions.content.y
       << " w=" << dimensions.content.width
       << " h=" << dimensions.content.height << "]";

    if (!lines.empty()) {
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
