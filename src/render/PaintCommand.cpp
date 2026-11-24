#include <cctype>
#include "litebird/render/PaintCommand.hpp"
#include "litebird/common/StackLimit.hpp"
#include "litebird/render/FontRenderer.hpp"
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

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

/// A box that clips paints a clip command around its own subtree.
bool box_clips(const LayoutBox& box) {
    const ComputedStyle& s = *box.style;
    return s.overflow != Overflow::Visible || s.contains_paint();
}

/// A non-identity transform creates a stacking context (CSS Transforms 1 s3).
bool box_has_transform(const LayoutBox& box) {
    const ComputedStyle& s = *box.style;
    return s.has_transform && !s.transform_matrix.is_identity();
}

/// How a box takes part in its stacking context's paint order, in the terms of
/// CSS 2.1 Appendix E. `AtomicBlock` is the compromise the flat display list
/// forces: a box that clips must be painted in one piece, because its clip has
/// to span its own subtree and a flat list cannot interleave two phases across
/// a push/pop. Phases are therefore honoured inside such a box, not across it.
enum class PaintRole { Block, AtomicBlock, Float, OutOfFlow, Inline };

PaintRole paint_role(const LayoutBox& box) {
    const ComputedStyle& s = *box.style;
    if (box_has_transform(box) || s.is_positioned() || s.opacity < 1.0f) return PaintRole::OutOfFlow;
    if (s.is_floated()) return PaintRole::Float;
    if (box.box_type == BoxType::TextNode || box.box_type == BoxType::InlineNode) {
        return PaintRole::Inline;
    }
    return box_clips(box) ? PaintRole::AtomicBlock : PaintRole::Block;
}

// ---------------------------------------------------------------------------
// Step 1 of Appendix E for one box: its own shadow, background and borders.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Text decoration: the three lines `text-decoration` can ask for.
//
// `ComputedStyle` has carried all three bits since Phase 73 and the inline
// fragment carries the style, but only the underline was ever drawn: a
// `line-through` — the most-used decoration in the corpus after the link
// underline — was parsed, reported correctly by `getComputedStyle`, and
// painted as nothing at all. The information arrived at the painter and was
// dropped at the last step.
//
// The y of each line is the face's, not a fraction of the line box. The
// baseline inside a line box is `ascent + leading / 2` (the same expression
// `FontRenderer::draw_text` uses to place the glyphs), so a line drawn from it
// lands on the pixels the text occupies: `overline` at the ascent,
// `line-through` through the middle of the x-height, and `underline` half a
// descent below the baseline. The line is inset half a stroke from the
// fragment's own left edge and stops half a stroke short of its right edge, so
// the pieces of one word join up instead of leaving gaps.
// ---------------------------------------------------------------------------
void emit_text_decoration(const ComputedStyle& s, const Rect& rect, DisplayList& list) {
    if (!s.has_text_decoration() || rect.width <= 0.0f) return;

    const float font_size = s.font_size > 0.0f ? s.font_size : 16.0f;
    FontRenderer& fonts = FontRenderer::instance();
    const std::string family = s.font_family;
    const int weight = s.font_weight;
    const bool italic = s.font_style != FontStyle::Normal;
    const auto [ascent, descent] =
        fonts.em_ascent_descent(font_size, weight, family, italic);
    const float leading = std::max(0.0f, font_size * 1.25f - (ascent + descent));
    const float baseline = ascent + leading * 0.5f;

    // The stroke thickness is the one the underline always had; the three
    // lines are one shape of different y.
    const float thickness = std::max(1.0f, font_size * 0.08f);
    const float x = rect.x + thickness * 0.5f;
    const float width = std::max(0.0f, rect.width - thickness);

    auto line_at = [&](float y) {
        DrawRectCommand cmd;
        cmd.rect = Rect(x, rect.y + y, width, thickness);
        cmd.color = s.color;
        cmd.border_radius = 0.0f;
        list.add(cmd);
    };

    // The two upper lines sit where the face's own ink says: `overline` at the
    // capital height and `line-through` through the x-height, both measured
    // from the glyphs this renderer draws. The underline is the usual half a
    // descent below the baseline — every face's `post` table puts it there and
    // `text-underline-position` is not implemented, so this is `auto`.
    const auto [x_height, cap_height] = fonts.decoration_lines(font_size, weight, family, italic);
    if (s.has_overline()) line_at(std::max(0.0f, baseline - cap_height));
    if (s.has_line_through()) line_at(std::max(0.0f, baseline - x_height));
    if (s.has_underline()) line_at(baseline + descent * 0.5f);
}

void emit_chrome(const std::shared_ptr<LayoutBox>& box, DisplayList& list) {
    const ComputedStyle& s = *box->style;
    if (!s.is_visible()) return;

    // CSS 2.1 §17.3: table columns and column groups contribute only
    // backgrounds (and collapsed borders already resolved onto cells).
    if (s.is_table_column() || s.is_table_column_group()) {
        const Rect bg_rect = box->dimensions.padding_box();
        if (bg_rect.width <= 0.0f || bg_rect.height <= 0.0f) return;
        if (!box->background_propagated && !s.background_color.is_transparent()) {
            DrawRectCommand cmd;
            cmd.rect = bg_rect;
            cmd.color = s.background_color;
            list.add(cmd);
        }
        if (!box->background_propagated && s.background_gradient) {
            DrawRectCommand cmd;
            cmd.rect = bg_rect;
            cmd.color = Color::transparent();
            cmd.gradient = s.background_gradient;
            list.add(cmd);
        }
        return;
    }

    // An empty cell in the separated border model paints no background, shadow
    // or borders (CSS 2.1 17.5.1.2, `empty-cells: hide`). The collapsed model
    // always paints the shared edges.
    const bool skip_cell_chrome =
        box->box_type == BoxType::TableCellNode && s.empty_cells == EmptyCells::Hide &&
        s.border_collapse != BorderCollapse::Collapse && !table_cell_has_content(*box);

// 0. Box Shadow (rendered underneath background)
if (!skip_cell_chrome && s.box_shadow.has_shadow &&
    !s.box_shadow.color.is_transparent()) {
    DrawShadowCommand shadow;
    shadow.rect = box->dimensions.border_box();
    shadow.x_offset = s.box_shadow.x_offset;
    shadow.y_offset = s.box_shadow.y_offset;
    shadow.blur = s.box_shadow.blur;
    shadow.border_radius = s.border_radius;
    shadow.color = s.box_shadow.color;
    list.add(shadow);
}

// 1. Background (solid color, then background-image gradient on top)
if (!skip_cell_chrome && !box->background_propagated && !s.background_color.is_transparent()) {
    DrawRectCommand cmd;
    cmd.rect = box->dimensions.padding_box();
    cmd.color = s.background_color;
    cmd.border_radius = s.border_radius;
    list.add(cmd);
}
if (!skip_cell_chrome && !box->background_propagated && s.background_gradient) {
    DrawRectCommand cmd;
    cmd.rect = box->dimensions.padding_box();
    cmd.color = Color::transparent();
    cmd.border_radius = s.border_radius;
    cmd.gradient = s.background_gradient;
    list.add(cmd);
}

// 2. Borders
const auto& bw = box->dimensions.border;
if (!skip_cell_chrome && (bw.top > 0 || bw.right > 0 || bw.bottom > 0 || bw.left > 0)) {
    DrawBorderCommand cmd;
    cmd.rect = box->dimensions.border_box();
    cmd.width = bw;
    cmd.color = s.border_color;
    cmd.border_radius = s.border_radius;
    if (box->border_color_override) cmd.edge_colors = *box->border_color_override;
    list.add(cmd);
}

// 3. Outline. CSS 2.1 §16.3: a ring outside the border box that takes no room
// in the layout, which is why nothing in the box model has a place for it —
// `outline: 5px solid` on a 100px box paints 5px around it and the box is
// still 100px. It is drawn on top of the border, so a zero offset puts it
// over the border's own pixels; a positive offset lifts it clear, and a
// negative one pulls it in over the padding.
if (s.outline_drawn && s.outline_width > 0.0f && !s.outline_color.is_transparent()) {
    const Rect box_rect = box->dimensions.border_box();
    const float grow = s.outline_offset + s.outline_width;
    DrawBorderCommand ring;
    ring.rect = Rect{box_rect.x - grow, box_rect.y - grow, box_rect.width + 2 * grow, box_rect.height + 2 * grow};
    ring.width = EdgeSizes(s.outline_width);
    ring.color = s.outline_color;
    list.add(ring);
}
}

/// The one box that is not painted where the document walk reaches it: the
/// modal dialog's, which `DisplayList::build` paints last, behind a backdrop.
/// The walk leaves it out and fills in `box` when it passes it.
struct TopLayer {
    const Node* node{nullptr};
    std::shared_ptr<LayoutBox> box;
    /// True when `candidate` is the top-layer box, which is remembered here so
    /// that `DisplayList::build` can paint it after everything else.
    bool take(const std::shared_ptr<LayoutBox>& candidate) {
        if (!node || candidate->node.get() != node) return false;
        if (!box) box = candidate;
        return true;
    }
};

void paint_atom(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top);
void paint_phases(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top);

// ---------------------------------------------------------------------------
// The box's own ink: its inline formatting context, or its text, or an image.
// ---------------------------------------------------------------------------
void emit_contents(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top) {
    const ComputedStyle& s = *box->style;

// 3. Inline Formatting Context Lines (if this box established an IFC)
if (!box->line_boxes.empty()) {
    for (const auto& line : box->line_boxes) {
        for (const auto& frag : line.fragments) {
            if (frag.is_space) {
                // Transparent margin/padding spacers carry empty text and paint nothing.
                // Real word-separating spaces inside a styled inline box paint its
                // background so the inline background stays continuous across words.
                if (frag.style->is_visible() && !frag.text.empty()) {
                    for (auto it = frag.ancestor_backgrounds.rbegin();
                         it != frag.ancestor_backgrounds.rend(); ++it) {
                        if (!it->is_transparent()) {
                            DrawRectCommand bg_cmd;
                            bg_cmd.rect = frag.rect;
                            bg_cmd.color = *it;
                            bg_cmd.border_radius = frag.style->border_radius;
                            list.add(bg_cmd);
                        }
                    }
                    if (!frag.style->background_color.is_transparent()) {
                        DrawRectCommand bg_cmd;
                        bg_cmd.rect = frag.rect;
                        bg_cmd.color = frag.style->background_color;
                        bg_cmd.border_radius = frag.style->border_radius;
                        list.add(bg_cmd);
                    }
                }
                continue;
            }
            if (frag.box && frag.box->style->is_inline_block()) {
                paint_atom(frag.box, list, top);
                continue;
            }
            const ComputedStyle& fs = *frag.style;
            if (!fs.is_visible()) continue;

            // Enclosing inline ancestor backgrounds (outermost -> innermost)
            // beneath this fragment's own background.
            for (auto it = frag.ancestor_backgrounds.rbegin();
                 it != frag.ancestor_backgrounds.rend(); ++it) {
                if (!it->is_transparent()) {
                    DrawRectCommand bg_cmd;
                    bg_cmd.rect = frag.rect;
                    bg_cmd.color = *it;
                    bg_cmd.border_radius = fs.border_radius;
                    list.add(bg_cmd);
                }
            }

            // Fragment background (e.g. <span style="background-color: yellow;">)
            if (!fs.background_color.is_transparent()) {
                DrawRectCommand bg_cmd;
                bg_cmd.rect = frag.rect;
                bg_cmd.color = fs.background_color;
                bg_cmd.border_radius = fs.border_radius;
                list.add(bg_cmd);
            }

            // Fragment border
            const auto& fbw = fs.border_width;
            if (fbw.top > 0 || fbw.right > 0 || fbw.bottom > 0 || fbw.left > 0) {
                DrawBorderCommand b_cmd;
                b_cmd.rect = frag.rect;
                b_cmd.width = fbw;
                b_cmd.color = fs.border_color;
                b_cmd.border_radius = fs.border_radius;
                list.add(b_cmd);
            }

            // Fragment text
            if (!frag.text.empty()) {
                DrawTextCommand text_cmd;
                text_cmd.position = Point(frag.rect.x, frag.rect.y);
                text_cmd.text = frag.text;
                text_cmd.color = fs.color;
                text_cmd.font_size = fs.font_size;
                text_cmd.font_weight = fs.font_weight;
                text_cmd.font_family = fs.font_family;
                text_cmd.font_italic = fs.font_style != FontStyle::Normal;
                text_cmd.font_kerning = fs.kerning_enabled();
                text_cmd.is_rtl_visual = frag.is_rtl_visual;
                list.add(text_cmd);
            }

            // Fragment image
            if (frag.box && frag.box->image) {
                DrawImageCommand img_cmd;
                img_cmd.rect = frag.rect;
                img_cmd.image = frag.box->image;
                img_cmd.border_radius = fs.border_radius;
                list.add(img_cmd);
            }

            // Text decoration (e.g. <a> link underline). The underline used to
            // be the only one drawn, at `rect.height - 0.08em`.
            emit_text_decoration(fs, frag.rect, list);
        }
    }
    return; // All inline children and text runs handled by IFC
}

// 4. Standalone Text Node (fallback if not in IFC)
if (!s.is_visible()) return;

if (box->box_type == BoxType::TextNode) {
    float line_h = s.font_size * 1.25f;
    if (!box->lines.empty()) {
        for (size_t i = 0; i < box->lines.size(); ++i) {
            const float top = box->dimensions.content.y + static_cast<float>(i) * line_h;
            DrawTextCommand cmd;
            cmd.position = Point(box->dimensions.content.x, top);
            cmd.text = box->lines[i];
            cmd.color = s.color;
            cmd.font_size = s.font_size;
            cmd.font_weight = s.font_weight;
            cmd.font_family = s.font_family;
            cmd.font_italic = s.font_style != FontStyle::Normal;
            cmd.font_kerning = s.kerning_enabled();
            list.add(cmd);
            // A text node outside an inline formatting context used to get no
            // decoration at all; it is one line, so the line is the node's.
            emit_text_decoration(
                s, Rect(box->dimensions.content.x, top, box->dimensions.content.width, line_h), list);
        }
    } else if (!box->text.empty()) {
        DrawTextCommand cmd;
        cmd.position = Point(box->dimensions.content.x, box->dimensions.content.y);
        cmd.text = box->text;
        cmd.color = s.color;
        cmd.font_size = s.font_size;
        cmd.font_weight = s.font_weight;
        cmd.font_family = s.font_family;
        cmd.font_italic = s.font_style != FontStyle::Normal;
            cmd.font_kerning = s.kerning_enabled();
        list.add(cmd);
        emit_text_decoration(
            s,
            Rect(box->dimensions.content.x, box->dimensions.content.y,
                 box->dimensions.content.width, line_h),
            list);
    }
}

// 4b. Image Node
if (box->image) {
    DrawImageCommand cmd;
    cmd.rect = box->dimensions.content;
    cmd.image = box->image;
    cmd.border_radius = s.border_radius;
    list.add(cmd);
}
}

// Step 3: the backgrounds and borders of in-flow, non-positioned block-level
// descendants, in document order. Floats and out-of-flow boxes are not here:
// they belong to steps 5 and 8.
void paint_block_backgrounds(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top) {
    // A box that established an inline formatting context painted its
    // children into its lines, so its subtree is not walked again.
    if (!box->line_boxes.empty()) return;

    // CSS 2.1 §17.5.1: column groups (layer 2) and columns (layer 3) paint
    // above the table background and below row groups, rows, and cells
    // regardless of DOM order.
    if (box->box_type == BoxType::TableNode) {
        for (const auto& child : box->children) {
            if (child->style->is_table_column_group()) {
                emit_chrome(child, list);
                for (const auto& col : child->children) {
                    if (col->style->is_table_column()) emit_chrome(col, list);
                }
            } else if (child->style->is_table_column()) {
                emit_chrome(child, list);
            }
        }
    }

    for (const auto& child : box->children) {
        if (top.take(child)) continue;   // the modal dialog: painted last
        if (box->box_type == BoxType::TableNode &&
            (child->style->is_table_column() || child->style->is_table_column_group())) {
            continue; // already painted in CSS 2.1 §17.5.1 column layers above
        }
        switch (paint_role(*child)) {
            case PaintRole::Block:
                emit_chrome(child, list);
                paint_block_backgrounds(child, list, top);
                break;
            case PaintRole::AtomicBlock:
                paint_atom(child, list, top);
                break;
            default:
                break;
        }
    }
}

// Step 5: the floats of this stacking context, in document order, each painted
// as a stacking context of its own. This is the step that was missing: a float
// used to be painted where the tree walk found it, which is under the
// background of the block that follows it.
void paint_floats(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top) {
    // A box that established an inline formatting context painted its
    // children into its lines, so its subtree is not walked again.
    if (!box->line_boxes.empty()) return;
    for (const auto& child : box->children) {
        if (top.take(child)) continue;   // the modal dialog: painted last
        switch (paint_role(*child)) {
            case PaintRole::Float:
                paint_atom(child, list, top);
                break;
            case PaintRole::Block:
                paint_floats(child, list, top);
                break;
            default:
                break;
        }
    }
}

// Step 7: the inline content of in-flow blocks, in document order.
void paint_inline_contents(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top) {
    // A box that established an inline formatting context painted its
    // children into its lines, so its subtree is not walked again.
    if (!box->line_boxes.empty()) return;
    for (const auto& child : box->children) {
        if (top.take(child)) continue;   // the modal dialog: painted last
        switch (paint_role(*child)) {
            case PaintRole::Block:
                emit_contents(child, list, top);
                paint_inline_contents(child, list, top);
                break;
            case PaintRole::Inline:
                paint_atom(child, list, top);
                break;
            default:
                break;
        }
    }
}

// Steps 2, 8 and 9: the stacking contexts this box owns, in document order
// within each z-index, ascending. `negative` selects step 2 (behind the
// backgrounds) from steps 8 and 9 (in front of everything in flow).
void paint_stacking_contexts(const std::shared_ptr<LayoutBox>& box, DisplayList& list,
                             bool negative, TopLayer& top) {
    std::vector<std::shared_ptr<LayoutBox>> pending;
    std::vector<std::pair<int, const LayoutBox*>> order;
    std::function<void(const std::shared_ptr<LayoutBox>&)> collect =
        [&](const std::shared_ptr<LayoutBox>& parent) {
            if (!parent->line_boxes.empty()) return;
            for (const auto& child : parent->children) {
                if (top.take(child)) continue;   // the modal dialog: painted last
                if (paint_role(*child) == PaintRole::OutOfFlow) {
                    const int z = child->style->has_z_index ? child->style->z_index : 0;
                    if ((z < 0) == negative) {
                        pending.push_back(child);
                        order.emplace_back(z, child.get());
                    }
                } else if (paint_role(*child) == PaintRole::Block) {
                    collect(child);
                }
            }
        };
    collect(box);
    if (pending.empty()) return;
    std::vector<size_t> indices(pending.size());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
    std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return order[a].first < order[b].first;
    });
    for (const size_t i : indices) paint_atom(pending[i], list, top);
}

void paint_phases(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top) {
    paint_stacking_contexts(box, list, /*negative=*/true, top);   // step 2
    paint_block_backgrounds(box, list, top);                 // step 3
    paint_floats(box, list, top);                            // step 5
    paint_inline_contents(box, list, top);                   // step 7
    paint_stacking_contexts(box, list, /*negative=*/false, top);   // steps 8 and 9
}

// One box painted as a unit: its own chrome and ink, then its children in
// Appendix E order, all of it inside the box's transform and clip.
void paint_atom(const std::shared_ptr<LayoutBox>& box, DisplayList& list, TopLayer& top) {
    if (!box) return;
    // The modal dialog is not painted where the walk reaches it; it is painted
    // after everything the document drew, with a backdrop behind it.
    if (top.take(box)) return;
    // The box tree is only as deep as the stack allowed when it was built, but
    // a level of painting is not a level of building: ask again.
    if (stack_is_exhausted()) return;

    const ComputedStyle& s = *box->style;
    const bool has_opacity = s.opacity < 1.0f;
    if (has_opacity) list.add(PushOpacityCommand{s.opacity});
    const bool has_t = box_has_transform(*box);
    if (has_t) list.add(PushTransformCommand{s.transform_matrix});

    // The box's own border and background are not inside its overflow clip:
    // CSS 2.1 11.1.1 clips a box's *contents*, not its chrome. This used to
    // push the clip first and at `padding_box()`, which painted a box's border
    // and background away entirely -- invisible while the only boxes that
    // clipped were small, and fatal once the dialog UA rule gave the top-layer
    // dialog `overflow: auto`. The clip now starts at the padding box edge (the
    // inner edge of the border) and wraps everything after the chrome.
    emit_chrome(box, list);
    const bool clips = box_clips(*box);
    if (clips) {
        PushClipCommand clip;
        clip.rect = box->dimensions.padding_box();
        clip.border_radius = s.border_radius;
        list.add(clip);
    }
    emit_contents(box, list, top);
    // A box that established an inline formatting context painted all of its
    // children into its lines; there is nothing left to walk.
    if (box->line_boxes.empty()) paint_phases(box, list, top);

    if (clips) list.add(PopClipCommand{});
    if (has_t) list.add(PopTransformCommand{});
    if (has_opacity) list.add(PopOpacityCommand{});
}
} // namespace

DisplayList DisplayList::build(const std::shared_ptr<LayoutBox>& root_box, const Node* top_layer) {
    DisplayList list;
    if (root_box) {
        // CSS Color 4 §3.2: opacity on the root element (`<html>`) applies to
        // the entire document paint, including its propagated canvas.
        const bool has_root_opacity = root_box->has_document_canvas && root_box->root_opacity < 1.0f;
        if (has_root_opacity) list.add(PushOpacityCommand{root_box->root_opacity});

        // The canvas is behind every box, including the body's shadow and
        // border, and is not subject to the body's transform or overflow.
        // Emit even a transparent document canvas: the terminal renderer must
        // distinguish it from an arbitrary subtree and use the white backdrop.
        if (root_box->has_document_canvas) {
            DrawCanvasCommand canvas_cmd{root_box->canvas_background};
            if (root_box->canvas_gradient) canvas_cmd.gradient = *root_box->canvas_gradient;
            list.add(std::move(canvas_cmd));
        }
        TopLayer top{top_layer, nullptr};
        paint_atom(root_box, list, top);
        if (top.box) {
            // The top layer: the UA's `::backdrop` over the whole viewport, and
            // then the dialog's own subtree in front of it. Author styles reach
            // the dialog but not the backdrop, which is why its colour is here
            // and not in a stylesheet.
            list.add(DrawBackdropCommand{Color(0, 0, 0, 26)});
            TopLayer none;
            paint_atom(top.box, list, none);
        }
        if (has_root_opacity) list.add(PopOpacityCommand{});
    }
    return list;
}

} // namespace litebird
