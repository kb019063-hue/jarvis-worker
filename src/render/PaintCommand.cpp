#include "litebird/render/PaintCommand.hpp"

namespace litebird {

DisplayList DisplayList::build(const std::shared_ptr<LayoutBox>& root_box) {
    DisplayList list;
    if (root_box) {
        build_recursive(root_box, list);
    }
    return list;
}

void DisplayList::build_recursive(const std::shared_ptr<LayoutBox>& box, DisplayList& list) {
    if (!box) return;

    // 0. Box Shadow (rendered underneath background)
    if (box->style.box_shadow.has_shadow && !box->style.box_shadow.color.is_transparent()) {
        DrawShadowCommand shadow;
        shadow.rect = box->dimensions.border_box();
        shadow.x_offset = box->style.box_shadow.x_offset;
        shadow.y_offset = box->style.box_shadow.y_offset;
        shadow.blur = box->style.box_shadow.blur;
        shadow.border_radius = box->style.border_radius;
        shadow.color = box->style.box_shadow.color;
        list.add(shadow);
    }

    // 1. Background
    if (!box->style.background_color.is_transparent()) {
        DrawRectCommand cmd;
        cmd.rect = box->dimensions.padding_box();
        cmd.color = box->style.background_color;
        cmd.border_radius = box->style.border_radius;
        list.add(cmd);
    }

    // 2. Borders
    const auto& bw = box->dimensions.border;
    if (bw.top > 0 || bw.right > 0 || bw.bottom > 0 || bw.left > 0) {
        DrawBorderCommand cmd;
        cmd.rect = box->dimensions.border_box();
        cmd.width = bw;
        cmd.color = box->style.border_color;
        cmd.border_radius = box->style.border_radius;
        list.add(cmd);
    }

    // 3. Text
    if (box->box_type == BoxType::TextNode) {
        float line_h = box->style.font_size * 1.25f;
        if (!box->lines.empty()) {
            for (size_t i = 0; i < box->lines.size(); ++i) {
                DrawTextCommand cmd;
                cmd.position = Point(box->dimensions.content.x, box->dimensions.content.y + i * line_h);
                cmd.text = box->lines[i];
                cmd.color = box->style.color;
                cmd.font_size = box->style.font_size;
                cmd.font_weight = box->style.font_weight;
                list.add(cmd);
            }
        } else if (!box->text.empty()) {
            DrawTextCommand cmd;
            cmd.position = Point(box->dimensions.content.x, box->dimensions.content.y);
            cmd.text = box->text;
            cmd.color = box->style.color;
            cmd.font_size = box->style.font_size;
            cmd.font_weight = box->style.font_weight;
            list.add(cmd);
        }
    }

    // 4. Children
    for (const auto& child : box->children) {
        build_recursive(child, list);
    }
}

} // namespace litebird
