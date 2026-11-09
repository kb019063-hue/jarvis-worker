#include "litebird/layout/LayoutTree.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include <cctype>

namespace litebird {

static bool is_all_whitespace(std::string_view s) {
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

static std::string collapse_whitespace(std::string_view s) {
    std::string res;
    bool in_ws = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_ws) {
                res += ' ';
                in_ws = true;
            }
        } else {
            res += c;
            in_ws = false;
        }
    }
    return res;
}

static bool is_block_level_box(const LayoutBox& box) {
    return box.box_type == BoxType::BlockNode || box.box_type == BoxType::FlexNode ||
           box.box_type == BoxType::GridNode || box.box_type == BoxType::AnonymousBlock ||
           box.box_type == BoxType::TableNode || box.box_type == BoxType::TableRowNode ||
           box.box_type == BoxType::TableCellNode;
}

static void normalize_block_children(const std::shared_ptr<LayoutBox>& box, const ComputedStyle& parent_style) {
    if (!box) return;
    if (box->box_type != BoxType::BlockNode && box->box_type != BoxType::AnonymousBlock) return;
    if (box->children.empty()) return;

    bool has_block = false;
    bool has_inline = false;

    for (const auto& ch : box->children) {
        if (is_block_level_box(*ch)) {
            has_block = true;
        } else {
            has_inline = true;
        }
    }

    if (!has_block || !has_inline) return;

    std::vector<std::shared_ptr<LayoutBox>> normalized;
    std::shared_ptr<LayoutBox> current_anon = nullptr;

    for (const auto& ch : box->children) {
        bool is_block_level = is_block_level_box(*ch);
        if (is_block_level) {
            current_anon = nullptr;
            normalized.push_back(ch);
        } else {
            if (ch->box_type == BoxType::TextNode && is_all_whitespace(ch->text)) {
                continue;
            }
            if (!current_anon) {
                ComputedStyle anon_style = parent_style;
                anon_style.display = Display::Block;
                current_anon = LayoutBox::create_anonymous(anon_style);
                normalized.push_back(current_anon);
            }
            current_anon->append_child(ch);
        }
    }

    box->children = std::move(normalized);
    for (auto& ch : box->children) {
        ch->parent = box;
    }
}

std::shared_ptr<LayoutBox> LayoutTree::build_box(
    const std::shared_ptr<Node>& node,
    const StyleResolver& resolver,
    const ComputedStyle* parent_style,
    int depth
) {
    if (!node) return nullptr;

    // Bound the recursion. Without this, deeply nested markup overflows the
    // C++ stack and the process dies mid-layout. The stack probe adapts to the
    // build (an ASan frame costs ~12x an -O2 one); kMaxBuildDepth is only a
    // backstop for platforms that cannot report stack bounds.
    if (depth >= kMaxBuildDepth) return nullptr;
    if (stack_is_exhausted()) return nullptr;
    if (std::getenv("LITEBIRD_DEBUG_STACK") && depth % 100 == 0) {
        std::fprintf(stderr, "[bb] depth=%d headroom_kb=%ld\n", depth,
                     (long)(detail::current_stack_pointer() - stack_floor()) / 1024);
    }

    if (node->is_comment()) return nullptr;

    if (node->is_text()) {
        auto text_node = std::static_pointer_cast<Text>(node);
        std::string raw = text_node->text();
        if (is_all_whitespace(raw)) {
            if (parent_style && parent_style->is_inline()) {
                ComputedStyle text_style = *parent_style;
                return LayoutBox::create_text(node, " ", text_style);
            }
            return nullptr;
        }

        ComputedStyle text_style;
        if (parent_style) text_style = *parent_style;
        std::string collapsed = collapse_whitespace(raw);
        return LayoutBox::create_text(node, collapsed, text_style);
    }

    if (node->is_element()) {
        auto element = std::static_pointer_cast<Element>(node);
        const std::string& tag = element->tag_name();
        if (tag == "head" || tag == "style" || tag == "script" || tag == "meta" || tag == "link") {
            return nullptr;
        }

        // Held by pointer rather than by value: this object is only needed as
        // `parent_style` for the recursive calls below, but as a local it stayed
        // alive on the stack across every recursive descent, costing ~1 KB of
        // frame per nesting level. The box owns its own copy, made once.
        auto style_ptr = std::make_shared<ComputedStyle>(resolver.resolve_style(element, parent_style));
        const ComputedStyle& style = *style_ptr;
        if (style.is_none()) return nullptr;

        std::shared_ptr<LayoutBox> box;
        if (style.is_table()) {
            box = LayoutBox::create_table(element, style);
        } else if (style.is_table_row()) {
            box = LayoutBox::create_table_row(element, style);
        } else if (style.is_table_cell()) {
            box = LayoutBox::create_table_cell(element, style);
        } else if (style.is_flex()) {
            box = LayoutBox::create_flex(element, style);
        } else if (style.is_grid()) {
            box = LayoutBox::create_grid(element, style);
        } else if (style.is_block() || style.is_block_level() || style.is_inline_block()) {
            box = LayoutBox::create_block(element, style);
        } else {
            box = LayoutBox::create_inline(element, style);
        }

        // NOTE: `style` is still referenced by the child recursion below. It is
        // released as soon as the children are done, before the image/canvas/svg
        // fixups that only touch box->style.
        // Process children (supporting Shadow DOM and Slot projection)
        const auto& children_to_process = (element->shadow_root()) ? element->shadow_root()->children() : element->children();
        for (const auto& ch : children_to_process) {
            if (ch->is_element() && ch->as_element()->tag_name() == "slot") {
                auto slot_el = std::static_pointer_cast<Element>(ch);
                auto assigned = ShadowRoot::assigned_nodes(slot_el);
                if (!assigned.empty()) {
                    for (const auto& a : assigned) {
                        auto child_box = build_box(a, resolver, &style, depth + 1);
                        if (child_box) box->append_child(child_box);
                    }
                } else {
                    // Fallback slot content
                    for (const auto& slot_ch : slot_el->children()) {
                        auto child_box = build_box(slot_ch, resolver, &style, depth + 1);
                        if (child_box) box->append_child(child_box);
                    }
                }
            } else {
                auto child_box = build_box(ch, resolver, &style, depth + 1);
                if (child_box) {
                    box->append_child(child_box);
                }
            }
        }

        if (box->box_type == BoxType::BlockNode) {
            normalize_block_children(box, style);
        }

        if (tag == "img") {
            box->box_type = BoxType::ImageNode;
            std::string src = element->get_attribute("src");
            std::shared_ptr<ImageData> img_data = nullptr;
            if (src.starts_with("data:image/")) {
                img_data = ImageDecoder::decode_data_uri(src);
            } else if (!src.empty()) {
                img_data = ImageDecoder::decode_file(src);
            }

            if (box->style.width < 0.0f) {
                if (element->has_attribute("width")) {
                    try { box->style.width = std::stof(element->get_attribute("width")); } catch (...) {}
                } else if (img_data) {
                    box->style.width = static_cast<float>(img_data->width);
                } else {
                    box->style.width = 100.0f;
                }
            }
            if (box->style.height < 0.0f) {
                if (element->has_attribute("height")) {
                    try { box->style.height = std::stof(element->get_attribute("height")); } catch (...) {}
                } else if (img_data) {
                    box->style.height = static_cast<float>(img_data->height);
                } else {
                    box->style.height = 100.0f;
                }
            }
            box->image = img_data;
        } else if (tag == "canvas") {
            box->box_type = BoxType::ImageNode;
            auto ctx = element->get_or_create_canvas_context_2d();
            if (box->style.width < 0.0f) {
                if (element->has_attribute("width")) {
                    try { box->style.width = std::stof(element->get_attribute("width")); } catch (...) {}
                } else if (ctx) {
                    box->style.width = static_cast<float>(ctx->width());
                } else {
                    box->style.width = 300.0f;
                }
            }
            if (box->style.height < 0.0f) {
                if (element->has_attribute("height")) {
                    try { box->style.height = std::stof(element->get_attribute("height")); } catch (...) {}
                } else if (ctx) {
                    box->style.height = static_cast<float>(ctx->height());
                } else {
                    box->style.height = 150.0f;
                }
            }
            if (ctx) {
                box->image = ctx->image_data();
            }
        } else if (tag == "svg") {
            box->box_type = BoxType::ImageNode;
            float svg_w = -1.0f;
            float svg_h = -1.0f;
            if (box->style.width >= 0.0f) svg_w = box->style.width;
            else if (element->has_attribute("width")) {
                try { svg_w = std::stof(element->get_attribute("width")); } catch (...) {}
            }
            if (box->style.height >= 0.0f) svg_h = box->style.height;
            else if (element->has_attribute("height")) {
                try { svg_h = std::stof(element->get_attribute("height")); } catch (...) {}
            }

            if (svg_w <= 0.0f) svg_w = 300.0f;
            if (svg_h <= 0.0f) svg_h = 150.0f;

            box->style.width = svg_w;
            box->style.height = svg_h;
            box->image = SvgRenderer::render(element, svg_w, svg_h);
        }

        return box;
    }

    if (node->is_document()) {
        auto doc = std::static_pointer_cast<Document>(node);
        auto body = doc->body();
        if (body) {
            return build_box(body, resolver, nullptr, depth + 1);
        }
        auto doc_el = doc->document_element();
        if (doc_el) {
            return build_box(doc_el, resolver, nullptr, depth + 1);
        }
    }

    return nullptr;
}

std::shared_ptr<LayoutBox> LayoutTree::build(
    const std::shared_ptr<Node>& root_node,
    const StyleResolver& resolver,
    float viewport_width,
    float viewport_height
) {
    auto root_box = build_box(root_node, resolver, nullptr);
    if (!root_box) {
        ComputedStyle root_style;
        root_style.display = Display::Block;
        root_box = LayoutBox::create_block(nullptr, root_style);
    }

    root_box->dimensions.content.x = 0.0f;
    root_box->dimensions.content.y = 0.0f;
    root_box->layout(viewport_width, viewport_height);
    LayoutBox::resolve_out_of_flow_positions(root_box, viewport_width, viewport_height);

    std::function<void(const std::shared_ptr<LayoutBox>&)> sync_boxes = [&](const std::shared_ptr<LayoutBox>& box) {
        if (!box) return;
        if (box->node && box->node->is_element()) {
            box->node->as_element()->set_bounding_box(box->dimensions.border_box());
        }
        for (const auto& ch : box->children) {
            sync_boxes(ch);
        }
    };
    sync_boxes(root_box);

    return root_box;
}

} // namespace litebird
