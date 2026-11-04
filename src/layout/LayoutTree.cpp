#include "litebird/layout/LayoutTree.hpp"
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

std::shared_ptr<LayoutBox> LayoutTree::build_box(
    const std::shared_ptr<Node>& node,
    const StyleResolver& resolver,
    const ComputedStyle* parent_style
) {
    if (!node) return nullptr;

    if (node->is_comment()) return nullptr;

    if (node->is_text()) {
        auto text_node = std::static_pointer_cast<Text>(node);
        std::string raw = text_node->text();
        if (is_all_whitespace(raw)) {
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

        ComputedStyle style = resolver.resolve_style(element, parent_style);
        if (style.is_none()) return nullptr;

        std::shared_ptr<LayoutBox> box;
        if (style.is_flex()) {
            box = LayoutBox::create_flex(element, style);
        } else if (style.is_block()) {
            box = LayoutBox::create_block(element, style);
        } else {
            box = LayoutBox::create_inline(element, style);
        }

        // Process children
        for (const auto& ch : element->children()) {
            auto child_box = build_box(ch, resolver, &style);
            if (child_box) {
                box->append_child(child_box);
            }
        }

        return box;
    }

    if (node->is_document()) {
        auto doc = std::static_pointer_cast<Document>(node);
        auto body = doc->body();
        if (body) {
            return build_box(body, resolver, nullptr);
        }
        auto doc_el = doc->document_element();
        if (doc_el) {
            return build_box(doc_el, resolver, nullptr);
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
    return root_box;
}

} // namespace litebird
