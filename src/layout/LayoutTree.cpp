#include "litebird/layout/LayoutTree.hpp"
#include "litebird/css/DocumentStyles.hpp"
#include "litebird/dom/FormState.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include <cctype>

namespace litebird {

namespace {

bool is_all_whitespace(std::string_view s) {
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string collapse_whitespace(std::string_view s) {
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

std::string_view trim_ascii_sv(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool starts_with_data_scheme(std::string_view s) {
    if (s.size() < 5) return false;
    auto lo = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return lo(s[0]) == 'd' && lo(s[1]) == 'a' && lo(s[2]) == 't' && lo(s[3]) == 'a' && s[4] == ':';
}

bool is_block_level_box(const LayoutBox& box) {
    return box.box_type == BoxType::BlockNode || box.box_type == BoxType::FlexNode ||
           box.box_type == BoxType::GridNode || box.box_type == BoxType::AnonymousBlock ||
           box.box_type == BoxType::TableNode || box.box_type == BoxType::TableRowNode ||
           box.box_type == BoxType::TableCellNode;
}

ComputedStyle inherited_only_style(const ComputedStyle& parent) {
    ComputedStyle s;
    s.color = parent.color;
    s.font_size = parent.font_size;
    s.font_weight = parent.font_weight;
    s.line_height = parent.line_height;
    s.line_height_number = parent.line_height_number;
    s.text_align = parent.text_align;
    s.text_decoration = parent.text_decoration;
    s.border_collapse = parent.border_collapse;
    s.border_spacing_x = parent.border_spacing_x;
    s.border_spacing_y = parent.border_spacing_y;
    s.custom_properties = parent.custom_properties;
    return s;
}

void normalize_block_children(const std::shared_ptr<LayoutBox>& box, const ComputedStyle& parent_style) {
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
                ComputedStyle anon_style = inherited_only_style(parent_style);
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

// Origin-safe `<img src>` loader with per-document subresource caching.
// Kept out of `build_boxes` so recursive stack frames stay tiny.
std::shared_ptr<ImageData> load_image_for_element(const Element& element) {
    const std::string src(trim_ascii_sv(element.get_attribute("src")));
    if (src.empty()) return nullptr;

    if (starts_with_data_scheme(src)) {
        return ImageDecoder::decode_data_uri(src);
    }

    std::shared_ptr<Document> doc = element.owner_document();
    ResourceRequestContext ctx;
    if (doc) {
        RequestMode mode = RequestMode::NoCors;
        bool creds = false;
        if (element.has_attribute("crossorigin")) {
            mode = RequestMode::Cors;
            std::string co(trim_ascii_sv(element.get_attribute("crossorigin")));
            for (char& c : co) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (co == "use-credentials") creds = true;
        }
        ctx = DocumentStyles::request_context(*doc, mode, creds);
    } else {
        ctx.document_url = "about:blank";
        ctx.base_url = "about:blank";
        ctx.mode = RequestMode::NoCors;
    }

    const std::string resolved = ResourceLoader::resolve_url(ctx.base_url, src);
    if (resolved.empty()) return nullptr;

    if (doc) {
        bool in_cache = false;
        auto cached = doc->find_cached_image(resolved, in_cache);
        if (in_cache) return cached;
        if (!doc->allow_external_image_fetch()) return nullptr;
    }

    ResourceResponse resp = ResourceLoader::load_for_page(src, ctx);
    std::shared_ptr<ImageData> decoded = nullptr;
    if (resp.success && !resp.body.empty()) {
        decoded = ImageDecoder::decode_bytes(
            reinterpret_cast<const uint8_t*>(resp.body.data()),
            resp.body.size(),
            resp.content_type
        );
    }

    if (doc) {
        doc->store_cached_image(resolved, decoded);
    }
    return decoded;
}

void populate_special_element_box(
    const std::shared_ptr<Element>& element,
    const std::shared_ptr<LayoutBox>& box
) {
    const std::string& tag = element->tag_name();
    if (tag == "img") {
        box->box_type = BoxType::ImageNode;
        std::shared_ptr<ImageData> img_data = load_image_for_element(*element);

        if (box->style.width < 0.0f) {
            if (element->has_attribute("width")) {
                try { box->style.width = std::stof(element->get_attribute("width")); } catch (...) {}
            } else if (img_data) {
                box->style.width = static_cast<float>(img_data->width);
            } else {
                box->style.width = 0.0f;
            }
        }
        if (box->style.height < 0.0f) {
            if (element->has_attribute("height")) {
                try { box->style.height = std::stof(element->get_attribute("height")); } catch (...) {}
            } else if (img_data) {
                box->style.height = static_cast<float>(img_data->height);
            } else {
                box->style.height = 0.0f;
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
            int target_w = static_cast<int>(box->style.width);
            int target_h = static_cast<int>(box->style.height);
            if (target_w > 0 && target_h > 0 && (ctx->width() != target_w || ctx->height() != target_h)) {
                ctx->resize(target_w, target_h);
            }
            box->image = ctx->image_data();
        }
    } else if (tag == "svg") {
        box->box_type = BoxType::ImageNode;
        float svg_w = box->style.width;
        float svg_h = box->style.height;
        if (svg_w < 0.0f) {
            if (element->has_attribute("width")) {
                try { svg_w = std::stof(element->get_attribute("width")); } catch (...) {}
            }
            if (svg_w < 0.0f) svg_w = 300.0f;
        }
        if (svg_h < 0.0f) {
            if (element->has_attribute("height")) {
                try { svg_h = std::stof(element->get_attribute("height")); } catch (...) {}
            }
            if (svg_h < 0.0f) svg_h = 150.0f;
        }
        box->style.width = svg_w;
        box->style.height = svg_h;
        box->image = SvgRenderer::render(element, svg_w, svg_h);
    }
}

// Splits an `InlineNode` that contains one or more block-level children into
// alternating inline runs (clones of `inline_box` referencing the same DOM
// `element`) and lifted block-level boxes (CSS 2.1 §9.2.1.1), in O(children).
void split_inline_box_around_blocks(
    const std::shared_ptr<Element>& element,
    const std::shared_ptr<LayoutBox>& inline_box,
    std::vector<std::shared_ptr<LayoutBox>>& out,
    std::vector<std::pair<std::shared_ptr<Element>, std::shared_ptr<LayoutBox>>>& split_inline_blocks
) {
    bool has_block = false;
    for (const auto& ch : inline_box->children) {
        if (is_block_level_box(*ch) && !ch->style.is_out_of_flow() && !ch->style.is_floated()) {
            has_block = true;
            break;
        }
    }
    if (!has_block) {
        out.push_back(inline_box);
        return;
    }

    std::shared_ptr<LayoutBox> current_inline = nullptr;
    for (const auto& ch : inline_box->children) {
        const bool lift_block = is_block_level_box(*ch) && !ch->style.is_out_of_flow() && !ch->style.is_floated();
        if (lift_block) {
            current_inline = nullptr;
            out.push_back(ch);
            split_inline_blocks.emplace_back(element, ch);
        } else {
            if (ch->box_type == BoxType::TextNode && is_all_whitespace(ch->text) && !current_inline) {
                continue;
            }
            if (!current_inline) {
                current_inline = LayoutBox::create_inline(element, inline_box->style);
                out.push_back(current_inline);
            }
            current_inline->append_child(ch);
        }
    }
}

} // namespace

void LayoutTree::build_boxes(
    const std::shared_ptr<Node>& node,
    const StyleResolver& resolver,
    const ComputedStyle* parent_style,
    int depth,
    std::vector<std::shared_ptr<LayoutBox>>& out,
    std::vector<std::pair<std::shared_ptr<Element>, std::shared_ptr<LayoutBox>>>& split_inline_blocks
) {
    if (!node) return;
    if (depth >= kMaxBuildDepth) return;
    if (stack_is_exhausted()) return;
    if (node->is_comment()) return;

    if (node->is_text()) {
        auto text_node = std::static_pointer_cast<Text>(node);
        std::string raw = text_node->text();
        if (is_all_whitespace(raw)) {
            if (parent_style && parent_style->is_inline()) {
                ComputedStyle text_style = inherited_only_style(*parent_style);
                out.push_back(LayoutBox::create_text(node, " ", text_style));
            }
            return;
        }

        ComputedStyle text_style = parent_style ? inherited_only_style(*parent_style) : ComputedStyle{};
        std::string collapsed = collapse_whitespace(raw);
        out.push_back(LayoutBox::create_text(node, collapsed, text_style));
        return;
    }

    if (node->is_element()) {
        auto element = std::static_pointer_cast<Element>(node);
        const std::string& tag = element->tag_name();
        if (tag == "head" || tag == "style" || tag == "script" || tag == "meta" ||
            tag == "link" || tag == "base" || tag == "title" || tag == "template") {
            return;
        }
        if (tag == "br") {
            // Represent <br> as a newline/line-break inline box if needed, or preserve existing behavior
        }

        // Held on heap to keep recursive stack frames small (~1 KB).
        auto style_ptr = std::make_shared<ComputedStyle>(resolver.resolve_style(element, parent_style));
        if (!element->is_foreign()) {
            if (tag == "input" && forms::input_type(*element) == "hidden") {
                return;
            }
            if ((tag == "button" || tag == "input" || tag == "select" || tag == "textarea") &&
                style_ptr->display == Display::Inline) {
                style_ptr->display = Display::InlineBlock;
            }
            if (tag == "input") {
                const std::string itype = forms::input_type(*element);
                if (itype != "checkbox" && itype != "radio" && itype != "submit" &&
                    itype != "button" && itype != "reset") {
                    if (style_ptr->width < 0.0f && style_ptr->width_percent < 0.0f) {
                        style_ptr->width = 180.0f;
                    }
                    if (style_ptr->height < 0.0f && style_ptr->height_percent < 0.0f) {
                        style_ptr->min_height = std::max(style_ptr->min_height, style_ptr->font_size * 1.4f);
                    }
                    if (style_ptr->background_color.is_transparent()) {
                        style_ptr->background_color = Color(255, 255, 255, 255);
                    }
                }
            }
        }
        const ComputedStyle& style = *style_ptr;
        if (style.is_none()) return;

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

        // Synthesize visual content for void/form controls (<input>, <select>, dirty <textarea>).
        if (!element->is_foreign() && tag == "input") {
            const std::string itype = forms::input_type(*element);
            ComputedStyle ctrl_text = inherited_only_style(style);
            std::string label;
            if (itype == "checkbox") {
                label = forms::checked(*element) ? "[x]" : "[ ]";
            } else if (itype == "radio") {
                label = forms::checked(*element) ? "(*)" : "( )";
            } else if (itype == "submit" || itype == "reset" || itype == "button") {
                label = forms::value(*element);
                if (label.empty()) {
                    label = (itype == "reset") ? "Reset" : (itype == "submit" ? "Submit" : "");
                }
            } else {
                std::string cur = forms::value(*element);
                if (!cur.empty()) {
                    if (itype == "password") {
                        label.assign(cur.size(), '*');
                    } else {
                        label = collapse_whitespace(cur);
                    }
                } else if (element->has_attribute("placeholder")) {
                    label = collapse_whitespace(element->get_attribute("placeholder"));
                    ctrl_text.color = Color(100, 116, 139, 255);
                }
            }
            if (!label.empty()) {
                box->append_child(LayoutBox::create_text(nullptr, label, ctrl_text));
            }
        } else if (!element->is_foreign() && tag == "select") {
            auto sel_opts = forms::selected_options(*element);
            if (!sel_opts.empty() && sel_opts.front()) {
                ComputedStyle ctrl_text = inherited_only_style(style);
                std::string label = collapse_whitespace(forms::option_label(*sel_opts.front()));
                if (!label.empty()) {
                    box->append_child(LayoutBox::create_text(nullptr, label, ctrl_text));
                }
            }
        } else if (!element->is_foreign() && tag == "textarea") {
            std::string cur = forms::value(*element);
            if (!cur.empty() || element->has_attribute("placeholder")) {
                ComputedStyle ctrl_text = inherited_only_style(style);
                std::string label = !cur.empty()
                                        ? collapse_whitespace(cur)
                                        : collapse_whitespace(element->get_attribute("placeholder"));
                if (cur.empty()) ctrl_text.color = Color(100, 116, 139, 255);
                if (!label.empty()) {
                    box->append_child(LayoutBox::create_text(nullptr, label, ctrl_text));
                }
            }
        } else if (tag != "svg") {
            const auto& children_to_process =
                element->shadow_root() ? element->shadow_root()->children() : element->children();
            std::vector<std::shared_ptr<LayoutBox>> child_boxes;
            for (const auto& ch : children_to_process) {
                child_boxes.clear();
                if (ch->is_element() && ch->as_element()->tag_name() == "slot") {
                    auto slot_el = std::static_pointer_cast<Element>(ch);
                    auto assigned = ShadowRoot::assigned_nodes(slot_el);
                    if (!assigned.empty()) {
                        for (const auto& a : assigned) {
                            build_boxes(a, resolver, &style, depth + 1, child_boxes, split_inline_blocks);
                        }
                    } else {
                        for (const auto& slot_ch : slot_el->children()) {
                            build_boxes(slot_ch, resolver, &style, depth + 1, child_boxes, split_inline_blocks);
                        }
                    }
                } else {
                    build_boxes(ch, resolver, &style, depth + 1, child_boxes, split_inline_blocks);
                }
                for (auto& cb : child_boxes) {
                    if (cb) box->append_child(cb);
                }
            }
        }

        style_ptr.reset();

        if (box->box_type == BoxType::BlockNode) {
            normalize_block_children(box, box->style);
        }

        if (tag == "img" || tag == "canvas" || tag == "svg") {
            populate_special_element_box(element, box);
        }

        if (box->box_type == BoxType::InlineNode) {
            split_inline_box_around_blocks(element, box, out, split_inline_blocks);
        } else {
            out.push_back(box);
        }
        return;
    }

    if (node->is_document()) {
        auto doc = std::static_pointer_cast<Document>(node);
        auto body = doc->body();
        if (body) {
            build_boxes(body, resolver, nullptr, depth + 1, out, split_inline_blocks);
            return;
        }
        auto doc_el = doc->document_element();
        if (doc_el) {
            build_boxes(doc_el, resolver, nullptr, depth + 1, out, split_inline_blocks);
            return;
        }
    }
}

std::shared_ptr<LayoutBox> LayoutTree::build_box(
    const std::shared_ptr<Node>& node,
    const StyleResolver& resolver,
    const ComputedStyle* parent_style,
    int depth
) {
    std::vector<std::shared_ptr<LayoutBox>> boxes;
    std::vector<std::pair<std::shared_ptr<Element>, std::shared_ptr<LayoutBox>>> split_inline_blocks;
    build_boxes(node, resolver, parent_style, depth, boxes, split_inline_blocks);
    if (boxes.empty()) return nullptr;
    if (boxes.size() == 1) return boxes.front();

    ComputedStyle wrapper_style = parent_style ? inherited_only_style(*parent_style) : ComputedStyle{};
    wrapper_style.display = Display::Block;
    auto wrapper = LayoutBox::create_anonymous(wrapper_style);
    for (auto& b : boxes) {
        wrapper->append_child(b);
    }
    normalize_block_children(wrapper, wrapper_style);
    return wrapper;
}

std::shared_ptr<LayoutBox> LayoutTree::build(
    const std::shared_ptr<Node>& root_node,
    const StyleResolver& resolver,
    float viewport_width,
    float viewport_height
) {
    if (root_node) {
        if (root_node->is_element()) {
            static_cast<Element&>(*root_node).clear_layout_box();
        }
        for_each_descendant(*root_node, [](const Node& n) {
            if (n.is_element()) {
                const_cast<Element&>(static_cast<const Element&>(n)).clear_layout_box();
            }
            return true;
        });
    }

    std::vector<std::shared_ptr<LayoutBox>> root_boxes;
    std::vector<std::pair<std::shared_ptr<Element>, std::shared_ptr<LayoutBox>>> split_inline_blocks;
    build_boxes(root_node, resolver, nullptr, 0, root_boxes, split_inline_blocks);

    std::shared_ptr<LayoutBox> root_box;
    if (root_boxes.size() == 1 && is_block_level_box(*root_boxes.front())) {
        root_box = root_boxes.front();
    } else if (!root_boxes.empty()) {
        ComputedStyle root_style;
        root_style.display = Display::Block;
        root_box = LayoutBox::create_anonymous(root_style);
        for (auto& b : root_boxes) {
            root_box->append_child(b);
        }
        normalize_block_children(root_box, root_style);
    } else {
        ComputedStyle root_style;
        root_style.display = Display::Block;
        root_box = LayoutBox::create_block(nullptr, root_style);
    }

    root_box->dimensions.content.x = 0.0f;
    root_box->dimensions.content.y = 0.0f;
    root_box->layout(viewport_width, viewport_height);
    LayoutBox::resolve_out_of_flow_positions(root_box, viewport_width, viewport_height);

    // Non-recursive box-to-DOM geometry synchronization.
    std::vector<const LayoutBox*> stack{root_box.get()};
    while (!stack.empty()) {
        const LayoutBox* box = stack.back();
        stack.pop_back();
        if (!box) continue;

        if (box->box_type != BoxType::InlineNode && box->node && box->node->is_element()) {
            Element* el = box->node->as_element();
            if (!el->has_layout_box()) {
                el->set_layout_box(box->dimensions.border_box(), box->dimensions.border);
            } else {
                el->union_layout_box(box->dimensions.border_box());
            }
        }

        // Synchronize inline elements (`<span>`, `<a>`, `<b>`, ...) and inline images from IFC fragments.
        if (!box->line_boxes.empty()) {
            for (const auto& line : box->line_boxes) {
                for (const auto& frag : line.fragments) {
                    if (!frag.box || frag.is_space) continue;
                    if (frag.box->box_type == BoxType::ImageNode && frag.box->node && frag.box->node->is_element()) {
                        frag.box->node->as_element()->set_layout_box(frag.rect, frag.box->dimensions.border);
                    }
                    std::shared_ptr<LayoutBox> cur = frag.box;
                    int hops = 0;
                    while (cur && cur.get() != box && hops < 256) {
                        if (cur->box_type == BoxType::InlineNode) {
                            if (cur->node && cur->node->is_element()) {
                                cur->node->as_element()->union_layout_box(frag.rect);
                            }
                        }
                        cur = cur->parent.lock();
                        ++hops;
                    }
                }
            }
        }

        for (auto it = box->children.rbegin(); it != box->children.rend(); ++it) {
            if (*it) stack.push_back(it->get());
        }
    }

    // Union lifted block rects into any split inline elements (e.g. `<a><div>Card</div></a>`)
    // in reverse order so nested split inlines (`<span><a><div>...</div></a></span>`) propagate bottom-up.
    for (auto it = split_inline_blocks.rbegin(); it != split_inline_blocks.rend(); ++it) {
        if (it->first && it->second) {
            it->first->union_layout_box(it->second->dimensions.border_box());
        }
    }

    if (root_node && root_node->is_document()) {
        auto doc = std::static_pointer_cast<Document>(root_node);
        if (auto doc_el = doc->document_element()) {
            if (!doc_el->has_layout_box() && root_box) {
                const float h = std::max(viewport_height, root_box->dimensions.margin_box().height);
                doc_el->set_layout_box(Rect(0.0f, 0.0f, viewport_width, h));
            }
        }
        doc->mark_layout_clean();
    }

    return root_box;
}

} // namespace litebird
