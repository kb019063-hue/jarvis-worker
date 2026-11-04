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

    // 3. Inline Formatting Context Lines (if this box established an IFC)
    if (!box->line_boxes.empty()) {
        for (const auto& line : box->line_boxes) {
            for (const auto& frag : line.fragments) {
                if (frag.is_space) continue;

                // Fragment background (e.g. <span style="background-color: yellow;">)
                if (!frag.style.background_color.is_transparent()) {
                    DrawRectCommand bg_cmd;
                    bg_cmd.rect = frag.rect;
                    bg_cmd.color = frag.style.background_color;
                    bg_cmd.border_radius = frag.style.border_radius;
                    list.add(bg_cmd);
                }

                // Fragment border
                const auto& fbw = frag.style.border_width;
                if (fbw.top > 0 || fbw.right > 0 || fbw.bottom > 0 || fbw.left > 0) {
                    DrawBorderCommand b_cmd;
                    b_cmd.rect = frag.rect;
                    b_cmd.width = fbw;
                    b_cmd.color = frag.style.border_color;
                    b_cmd.border_radius = frag.style.border_radius;
                    list.add(b_cmd);
                }

                // Fragment text
                if (!frag.text.empty()) {
                    DrawTextCommand text_cmd;
                    text_cmd.position = Point(frag.rect.x, frag.rect.y);
                    text_cmd.text = frag.text;
                    text_cmd.color = frag.style.color;
                    text_cmd.font_size = frag.style.font_size;
                    text_cmd.font_weight = frag.style.font_weight;
                    list.add(text_cmd);
                }

                // Text decoration (e.g. <a> link underline)
                if (frag.style.text_decoration == "underline") {
                    DrawRectCommand u_cmd;
                    float u_height = std::max(1.0f, frag.style.font_size * 0.08f);
                    u_cmd.rect = Rect(frag.rect.x, frag.rect.y + frag.rect.height - u_height, frag.rect.width, u_height);
                    u_cmd.color = frag.style.color;
                    u_cmd.border_radius = 0.0f;
                    list.add(u_cmd);
                }
            }
        }
        return; // All inline children and text runs handled by IFC
    }

    // 4. Standalone Text Node (fallback if not in IFC)
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

    // 5. Children
    for (const auto& child : box->children) {
        build_recursive(child, list);
    }
}

} // namespace litebird
