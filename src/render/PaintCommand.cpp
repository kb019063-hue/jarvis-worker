#include <cctype>
#include "litebird/render/PaintCommand.hpp"
#include "litebird/common/StackLimit.hpp"
#include <algorithm>

namespace litebird {

namespace {
/// True when the cell holds something other than white space (CSS 2.1
/// 17.5.1.2): any element box is content; a text node counts only if it
/// contains a non-white-space character.
bool table_cell_has_content(const LayoutBox& cell) {
    for (const auto& child : cell.children) {
        if (child->box_type == BoxType::TextNode) {
            for (const unsigned char c : child->text) {
                if (!std::isspace(c)) return true;
            }
        } else {
            return true;
        }
    }
    return false;
}
} // namespace

DisplayList DisplayList::build(const std::shared_ptr<LayoutBox>& root_box) {
    DisplayList list;
    if (root_box) {
        build_recursive(root_box, list);
    }
    return list;
}

void DisplayList::build_recursive(const std::shared_ptr<LayoutBox>& box, DisplayList& list) {
    if (!box) return;
    // The box tree is only as deep as the stack allowed when it was built, but
    // a level of painting is not a level of building: ask again.
    if (stack_is_exhausted()) return;

    bool has_t = box->style.has_transform && !box->style.transform_matrix.is_identity();
    if (has_t) {
        list.add(PushTransformCommand{box->style.transform_matrix});
    }

    // Overflow clipping: a box that clips paints a clip command before its own
    // background and pops it after its subtree.
    const bool clips =
        box->style.overflow != Overflow::Visible || box->style.contains_paint();
    if (clips) {
        PushClipCommand clip;
        clip.rect = box->dimensions.padding_box();
        clip.border_radius = box->style.border_radius;
        list.add(clip);
    }

    // An empty cell in the separated border model paints no background,
    // shadow or borders (CSS 2.1 17.5.1.2, `empty-cells: hide`). The
    // collapsed model always paints the shared edges.
    const bool skip_cell_chrome =
        box->box_type == BoxType::TableCellNode && box->style.empty_cells == EmptyCells::Hide &&
        box->style.border_collapse != BorderCollapse::Collapse && !table_cell_has_content(*box);

    // 0. Box Shadow (rendered underneath background)
    if (!skip_cell_chrome && box->style.box_shadow.has_shadow &&
        !box->style.box_shadow.color.is_transparent()) {
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
    if (!skip_cell_chrome && !box->style.background_color.is_transparent()) {
        DrawRectCommand cmd;
        cmd.rect = box->dimensions.padding_box();
        cmd.color = box->style.background_color;
        cmd.border_radius = box->style.border_radius;
        list.add(cmd);
    }

    // 2. Borders
    const auto& bw = box->dimensions.border;
    if (!skip_cell_chrome && (bw.top > 0 || bw.right > 0 || bw.bottom > 0 || bw.left > 0)) {
        DrawBorderCommand cmd;
        cmd.rect = box->dimensions.border_box();
        cmd.width = bw;
        cmd.color = box->style.border_color;
        cmd.border_radius = box->style.border_radius;
        if (box->border_color_override) cmd.edge_colors = *box->border_color_override;
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
                    text_cmd.font_family = frag.style.font_family;
                    list.add(text_cmd);
                }

                // Fragment image
                if (frag.box && frag.box->image) {
                    DrawImageCommand img_cmd;
                    img_cmd.rect = frag.rect;
                    img_cmd.image = frag.box->image;
                    img_cmd.border_radius = frag.style.border_radius;
                    list.add(img_cmd);
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
                cmd.font_family = box->style.font_family;
                list.add(cmd);
            }
        } else if (!box->text.empty()) {
            DrawTextCommand cmd;
            cmd.position = Point(box->dimensions.content.x, box->dimensions.content.y);
            cmd.text = box->text;
            cmd.color = box->style.color;
            cmd.font_size = box->style.font_size;
            cmd.font_weight = box->style.font_weight;
            cmd.font_family = box->style.font_family;
            list.add(cmd);
        }
    }

    // 4b. Image Node
    if (box->image) {
        DrawImageCommand cmd;
        cmd.rect = box->dimensions.content;
        cmd.image = box->image;
        cmd.border_radius = box->style.border_radius;
        list.add(cmd);
    }

    // 5. Partition and paint children according to CSS Stacking Context rules
    std::vector<std::shared_ptr<LayoutBox>> neg_z_children;
    std::vector<std::shared_ptr<LayoutBox>> normal_children;
    std::vector<std::shared_ptr<LayoutBox>> pos_z_children;

    for (const auto& child : box->children) {
        if (child->style.is_positioned() && child->style.has_z_index) {
            if (child->style.z_index < 0) {
                neg_z_children.push_back(child);
            } else if (child->style.z_index > 0) {
                pos_z_children.push_back(child);
            } else {
                normal_children.push_back(child);
            }
        } else {
            normal_children.push_back(child);
        }
    }

    std::stable_sort(neg_z_children.begin(), neg_z_children.end(), [](const auto& a, const auto& b) {
        return a->style.z_index < b->style.z_index;
    });

    std::stable_sort(pos_z_children.begin(), pos_z_children.end(), [](const auto& a, const auto& b) {
        return a->style.z_index < b->style.z_index;
    });

    // 1. Negative z-index elements paint first (behind normal flow)
    for (const auto& child : neg_z_children) {
        build_recursive(child, list);
    }

    // 2. Normal flow / auto z-index elements paint next
    for (const auto& child : normal_children) {
        build_recursive(child, list);
    }

    // 3. Positive z-index elements paint on top (ordered by z-index ascending)
    for (const auto& child : pos_z_children) {
        build_recursive(child, list);
    }

    if (clips) {
        list.add(PopClipCommand{});
    }

    if (has_t) {
        list.add(PopTransformCommand{});
    }
}

} // namespace litebird
