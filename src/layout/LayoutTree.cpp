#include "litebird/layout/LayoutTree.hpp"
#include "litebird/css/DocumentStyles.hpp"
#include "litebird/dom/FormState.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/render/BitmapRenderer.hpp"
#include "litebird/render/Canvas2D.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/PaintCommand.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include <cctype>
#include <unordered_map>

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

std::string normalize_preserved_newlines(std::string_view s) {
    std::string res;
    res.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
            res += '\n';
        } else {
            res += s[i];
        }
    }
    return res;
}

std::string collapse_spaces_preserve_newlines(std::string_view s) {
    std::string norm = normalize_preserved_newlines(s);
    std::string res;
    res.reserve(norm.size());
    bool in_space = false;
    for (char c : norm) {
        if (c == '\n') {
            // CSS Text 3 §4.1.1: discard collapsible spaces immediately before a forced break.
            while (!res.empty() && res.back() == ' ') res.pop_back();
            res += '\n';
            in_space = true; // also discard collapsible spaces immediately after a forced break
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_space) {
                res += ' ';
                in_space = true;
            }
        } else {
            res += c;
            in_space = false;
        }
    }
    return res;
}

bool preserves_whitespace_text(const LayoutBox& box) {
    if (!box.style) return false;
    if (box.style->preserves_spaces()) return true;
    return box.style->preserves_newlines() && box.text.find('\n') != std::string::npos;
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

// Whether a child takes a line of its own, in CSS 2.1 §9.2.1.1's terms: only a
// block-level box interrupts an inline formatting context.
//
// `display: inline-block` is *not* one, though it is built as a block box --
// §9.2.2.1 makes it an atomic **inline-level** box, which sits on its parent's
// line exactly like a replaced element and wraps only at a soft wrap
// opportunity beside it. Asking the box type alone therefore lifted every
// inline-block out of its line and split the line in three, so `x <kbd
// style="display:inline-block">Tab</kbd> y` became three lines. `LayoutBox`
// already lays such a child out atomically and `establishes_ifc()` already
// treats it as inline content; only this predicate disagreed.
//
// The guard names `inline-block` rather than deferring to
// `ComputedStyle::is_block_level()`, because that predicate also leaves out
// `table-caption`/`table-column`/`table-column-group`, which §17.4 does define
// as block-level; sweeping them in would change how a caption interrupts a
// line, which is not this phase's subject.
bool is_block_level_box(const LayoutBox& box) {
    if (box.style && box.style->is_inline_block()) return false;
    return box.box_type == BoxType::BlockNode || box.box_type == BoxType::FlexNode ||
           box.box_type == BoxType::GridNode || box.box_type == BoxType::AnonymousBlock ||
           box.box_type == BoxType::TableNode || box.box_type == BoxType::TableRowNode ||
           box.box_type == BoxType::TableCellNode;
}

// Auto margins must be zero while calculating an auto width. Longhand
// `margin-left:auto` can otherwise retain the user-agent body's old 8px in
// the style's pixel field, so resolve just this root box's auto edges before
// layout. The final horizontal auto margins depend on the used width below.
void prepare_document_body(LayoutBox& box) {
    const auto& s = *box.style;
    if (!s.margin_top_auto && !s.margin_right_auto && !s.margin_bottom_auto && !s.margin_left_auto) return;
    ComputedStyle used = s;
    if (s.margin_top_auto) { used.margin.top = 0.0f; used.margin_percent.top = -1.0f; }
    if (s.margin_right_auto) { used.margin.right = 0.0f; used.margin_percent.right = -1.0f; }
    if (s.margin_bottom_auto) { used.margin.bottom = 0.0f; used.margin_percent.bottom = -1.0f; }
    if (s.margin_left_auto) { used.margin.left = 0.0f; used.margin_percent.left = -1.0f; }
    box.style = std::make_shared<const ComputedStyle>(std::move(used));
}

// The document tree currently starts at <body>, not <html>. Its coordinates
// are content-box coordinates, so laying it out at (0, 0) without placing it
// leaves the margin, border and padding above/to the left of the viewport.
// Do this *after* layout: percentages and auto margins need the used width.
void place_document_body(LayoutBox& box, float viewport_width, float viewport_height,
                         const EdgeSizes* root_margin = nullptr) {
    auto& d = box.dimensions;
    const auto& s = *box.style;

    if (s.margin_top_auto) d.margin.top = 0.0f;
    if (s.margin_bottom_auto) d.margin.bottom = 0.0f;
    if (!s.is_out_of_flow() && (s.margin_left_auto || s.margin_right_auto)) {
        const float fixed = d.content.width + d.border.left + d.border.right +
                            d.padding.left + d.padding.right +
                            (s.margin_left_auto ? 0.0f : d.margin.left) +
                            (s.margin_right_auto ? 0.0f : d.margin.right);
        const float remaining = viewport_width - fixed;
        if (s.margin_left_auto && s.margin_right_auto) {
            // CSS 2.1 10.3.3, for the engine's left-to-right block direction.
            d.margin.left = std::max(0.0f, remaining / 2.0f);
            d.margin.right = remaining - d.margin.left;
        } else if (s.margin_left_auto) {
            d.margin.left = remaining;
        } else {
            d.margin.right = remaining;
        }
    }

    // `layout_block()` already collapsed the body's top margin through any
    // leading empty blocks and first-child chain (and adjusted descendant
    // positions and auto heights accordingly). Use its collapsed top strut
    // directly for the body's vertical placement inside the viewport.
    const float body_top = box.has_collapsed_margins
                               ? box.collapsed_margin_top.value()
                               : d.margin.top;

    float x = d.margin.left + d.border.left + d.padding.left;
    float y = body_top + d.border.top + d.padding.top;

    // The `<html>` root has no box of its own here, so its margin used to be
    // ignored entirely: `<html style="margin:30px">` moved nothing. CSS 2.1
    // 8.3 puts the body inside the root's content box, and a margin on the root
    // is outside it, so both edges move by the root's used margin. A root that
    // states no margin contributes nothing and this is the previous behaviour.
    if (root_margin) {
        x += root_margin->left;
        y += root_margin->top;
    }
    const auto inset = s.used_insets(viewport_width, viewport_height);
    if (s.position == Position::Relative) {
        if (s.has_left) x += inset.left;
        else if (s.has_right) x -= inset.right;
        if (s.has_top) y += inset.top;
        else if (s.has_bottom) y -= inset.bottom;
    } else if (s.is_out_of_flow()) {
        if (s.has_left) x += inset.left;
        else if (s.has_right) x = viewport_width - inset.right - d.margin.right -
                                  d.border.right - d.padding.right - d.content.width;
        if (s.has_top) y += inset.top;
        else if (s.has_bottom) y = viewport_height - inset.bottom - d.margin.bottom -
                                   d.border.bottom - d.padding.bottom - d.content.height;
    }
    const float dx = x - d.content.x;
    const float dy = y - d.content.y;
    if (dx == 0.0f && dy == 0.0f) return;
    std::vector<LayoutBox*> stack{&box};
    while (!stack.empty()) {
        LayoutBox* current = stack.back();
        stack.pop_back();
        current->dimensions.content.x += dx;
        current->dimensions.content.y += dy;
        for (auto& line : current->line_boxes) {
            line.rect.x += dx;
            line.rect.y += dy;
            for (auto& frag : line.fragments) {
                frag.rect.x += dx;
                frag.rect.y += dy;
            }
        }
        for (auto& child : current->children) {
            if (!child->style->is_out_of_flow()) {
                stack.push_back(child.get());
            }
        }
    }
}

SharedComputedStyle resolve_element_style(
    const std::shared_ptr<Element>& element,
    const StyleResolver& resolver,
    const SharedComputedStyle& parent_style,
    ComputedStylePool& pool
) {
    const std::string& tag = element->tag_name();
    if (!element->is_foreign() && tag == "input" && forms::input_type(*element) == "hidden") {
        return nullptr;
    }

    ComputedStyle computed = resolver.resolve_style(element, parent_style.get());
    if (!element->is_foreign()) {
        if ((tag == "button" || tag == "input" || tag == "select" || tag == "textarea") &&
            computed.display == Display::Inline) {
            computed.display = Display::InlineBlock;
        }
        if (tag == "input") {
            const std::string itype = forms::input_type(*element);
            if (itype != "checkbox" && itype != "radio" && itype != "submit" &&
                itype != "button" && itype != "reset") {
                if (computed.width < 0.0f && computed.width_percent < 0.0f) {
                    computed.width = 180.0f;
                }
                if (computed.height < 0.0f && computed.height_percent < 0.0f) {
                    computed.min_height = std::max(computed.min_height, computed.font_size * 1.4f);
                }
                if (computed.background_color.is_transparent()) {
                    computed.background_color = Color(255, 255, 255, 255);
                }
            }
        }
    }
    // CSS Overflow: with visible overflow on the HTML root, the first
    // body's overflow applies to the viewport, not to its own padding box.
    // An absolutely positioned canvas can fill the viewport even if almost
    // nothing contributes to the body's in-flow height. Keep ordinary clips
    // and containment intact; the framebuffer itself bounds viewport paint.
    if (tag == "body" && !element->is_foreign() && computed.overflow != Overflow::Visible && !computed.containment) {
        auto doc = element->owner_document();
        auto html = doc ? doc->document_element() : nullptr;
        if (html && !html->is_foreign() && html->tag_name() == "html" && element->parent() == html && doc->body() == element) {
            const auto root_style = resolver.resolve_style(html, nullptr);
            if (root_style.overflow == Overflow::Visible && !root_style.containment) computed.overflow = Overflow::Visible;
        }
    }
    if (computed.is_none()) return nullptr;
    return pool.intern(std::move(computed));
}

void normalize_block_children(
    const std::shared_ptr<LayoutBox>& box,
    const SharedComputedStyle& parent_style,
    ComputedStylePool& pool
) {
    if (!box) return;
    if (box->box_type != BoxType::BlockNode && box->box_type != BoxType::AnonymousBlock &&
        box->box_type != BoxType::TableCellNode) return;
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
    SharedComputedStyle anon_style;

    for (const auto& ch : box->children) {
        bool is_block_level = is_block_level_box(*ch);
        if (is_block_level) {
            current_anon = nullptr;
            normalized.push_back(ch);
        } else {
            if (ch->box_type == BoxType::TextNode && is_all_whitespace(ch->text) &&
                !preserves_whitespace_text(*ch)) {
                continue;
            }
            if (!current_anon) {
                if (!anon_style) {
                    anon_style = pool.inherited_only(parent_style, Display::Block);
                }
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

// CSS 2.1 §17.2.1. Ordinary children of a CSS table still need to enter
// its grid. Generate layout-only rows/cells for consecutive improper boxes;
// never invent a DOM node or split adjacent content into separate columns.
void normalize_table_children(const std::shared_ptr<LayoutBox>& box, ComputedStylePool& pool) {
    const bool table = box->style->is_table();
    const bool row = box->style->is_table_row();
    if (!table && !row && !box->style->is_table_row_group()) return;
    auto children = std::move(box->children);
    box->children.clear();
    std::shared_ptr<LayoutBox> implicit_row, implicit_cell;
    for (const auto& child : children) {
        if (!row && (child->style->is_table_row() ||
                (table && (child->style->is_table_caption() || child->style->is_table_row_group() ||
                           child->style->is_table_column() || child->style->is_table_column_group())))) {
            implicit_row.reset();
            implicit_cell.reset();
            box->append_child(child);
            continue;
        }
        if (!row && !implicit_row) {
            implicit_row = LayoutBox::create_table_row(nullptr, pool.inherited_only(box->style, Display::TableRow));
            box->append_child(implicit_row);
        }
        const auto& parent = row ? box : implicit_row;
        if (child->style->is_table_cell()) {
            implicit_cell.reset();
            parent->append_child(child);
        } else {
            if (!implicit_cell) {
                implicit_cell = LayoutBox::create_table_cell(nullptr, pool.inherited_only(parent->style, Display::TableCell));
                parent->append_child(implicit_cell);
            }
            implicit_cell->append_child(child);
        }
    }
    // A generated cell wasn't visited by build_boxes; normalize its mixed
    // block/inline content just as for a DOM-backed cell.
    auto finish_cells = [&](const auto& r) {
        for (const auto& c : r->children) {
            if (!c->node && c->box_type == BoxType::TableCellNode) normalize_block_children(c, c->style, pool);
        }
    };
    if (row) finish_cells(box);
    else for (const auto& r : box->children) if (!r->node && r->box_type == BoxType::TableRowNode) finish_cells(r);
}

// Origin-safe `<img src>` loader with per-document subresource caching.
// Kept out of `build_boxes` so recursive stack frames stay tiny.

void populate_special_element_box(
    const std::shared_ptr<Element>& element,
    const std::shared_ptr<LayoutBox>& box,
    ComputedStylePool& pool
) {
    const std::string& tag = element->tag_name();
    if (tag == "img") {
        box->box_type = BoxType::ImageNode;
        std::shared_ptr<ImageData> img_data = load_image_for_element(*element);
        float resolved_w = box->style->width;
        float resolved_h = box->style->height;

        if (resolved_w < 0.0f) {
            if (element->has_attribute("width")) {
                try { resolved_w = std::stof(element->get_attribute("width")); } catch (...) {}
            } else if (img_data) {
                resolved_w = static_cast<float>(img_data->width);
            } else {
                resolved_w = 0.0f;
            }
        }
        if (resolved_h < 0.0f) {
            if (element->has_attribute("height")) {
                try { resolved_h = std::stof(element->get_attribute("height")); } catch (...) {}
            } else if (img_data) {
                resolved_h = static_cast<float>(img_data->height);
            } else {
                resolved_h = 0.0f;
            }
        }
        if (resolved_w != box->style->width || resolved_h != box->style->height) {
            ComputedStyle updated = *box->style;
            updated.width = resolved_w;
            updated.height = resolved_h;
            box->style = pool.intern(std::move(updated));
        }
        box->image = img_data;
    } else if (tag == "canvas") {
        box->box_type = BoxType::ImageNode;
        auto ctx = element->get_or_create_canvas_context_2d();
        float resolved_w = box->style->width;
        float resolved_h = box->style->height;
        if (resolved_w < 0.0f) {
            if (element->has_attribute("width")) {
                try { resolved_w = std::stof(element->get_attribute("width")); } catch (...) {}
            } else if (ctx) {
                resolved_w = static_cast<float>(ctx->width());
            } else {
                resolved_w = 300.0f;
            }
        }
        if (resolved_h < 0.0f) {
            if (element->has_attribute("height")) {
                try { resolved_h = std::stof(element->get_attribute("height")); } catch (...) {}
            } else if (ctx) {
                resolved_h = static_cast<float>(ctx->height());
            } else {
                resolved_h = 150.0f;
            }
        }
        if (resolved_w != box->style->width || resolved_h != box->style->height) {
            ComputedStyle updated = *box->style;
            updated.width = resolved_w;
            updated.height = resolved_h;
            box->style = pool.intern(std::move(updated));
        }
        if (ctx) {
            int target_w = static_cast<int>(resolved_w);
            int target_h = static_cast<int>(resolved_h);
            if (target_w > 0 && target_h > 0 && (ctx->width() != target_w || ctx->height() != target_h)) {
                ctx->resize(target_w, target_h);
            }
            box->image = ctx->image_data();
        }
    } else if (tag == "svg") {
        box->box_type = BoxType::ImageNode;
        float svg_w = box->style->width;
        float svg_h = box->style->height;
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
        if (svg_w != box->style->width || svg_h != box->style->height) {
            ComputedStyle updated = *box->style;
            updated.width = svg_w;
            updated.height = svg_h;
            box->style = pool.intern(std::move(updated));
        }
        box->image = SvgRenderer::render(element, svg_w, svg_h);
    } else if (!element->is_foreign() && tag == "iframe") {
        box->box_type = BoxType::ImageNode;
        float frame_w = box->style->width;
        float frame_h = box->style->height;
        if (frame_w < 0.0f) {
            if (element->has_attribute("width")) {
                try { frame_w = std::stof(element->get_attribute("width")); } catch (...) {}
            }
            if (frame_w < 0.0f) frame_w = 300.0f;
        }
        if (frame_h < 0.0f) {
            if (element->has_attribute("height")) {
                try { frame_h = std::stof(element->get_attribute("height")); } catch (...) {}
            }
            if (frame_h < 0.0f) frame_h = 150.0f;
        }
        if (frame_w != box->style->width || frame_h != box->style->height) {
            ComputedStyle updated = *box->style;
            updated.width = frame_w;
            updated.height = frame_h;
            box->style = pool.intern(std::move(updated));
        }

        static thread_local int t_iframe_layout_depth = 0;
        if (auto subdoc = element->content_document(); subdoc && t_iframe_layout_depth < 8 && !stack_is_exhausted()) {
            const int rw = std::clamp(static_cast<int>(frame_w), 0, 2048);
            const int rh = std::clamp(static_cast<int>(frame_h), 0, 2048);
            if (rw > 0 && rh > 0 && RasterBudget::allows(rw, rh)) {
                auto img = std::make_shared<ImageData>();
                if (img->budget.resize(static_cast<std::int64_t>(rw) * static_cast<std::int64_t>(rh))) {
                    ++t_iframe_layout_depth;
                    struct DepthGuard { ~DepthGuard() { --t_iframe_layout_depth; } } guard;
                    subdoc->set_viewport_size(static_cast<float>(rw), static_cast<float>(rh));
                    StyleResolver sub_resolver = DocumentStyles::build_resolver(
                        *subdoc, &subdoc->stylesheet_cache(), static_cast<float>(rw), static_cast<float>(rh)
                    );
                    auto sub_root = LayoutTree::build(
                        subdoc, sub_resolver, static_cast<float>(rw), static_cast<float>(rh)
                    );
                    subdoc->mark_layout_clean();
                    if (sub_root) {
                        DisplayList sub_list = DisplayList::build(sub_root);
                        BitmapRenderer bmp(rw, rh);
                        // A child browsing context is transparent where its
                        // document paints nothing. Flattening it onto white
                        // here hid the embedding page and changed translucent
                        // canvas colors before the iframe was composited.
                        bmp.clear(Color::transparent());
                        bmp.render(sub_list);
                        img->width = rw;
                        img->height = rh;
                        img->pixels.resize(static_cast<std::size_t>(rw) * static_cast<std::size_t>(rh));
                        for (int y = 0; y < rh; ++y) {
                            for (int x = 0; x < rw; ++x) {
                                const Color c = bmp.get_pixel(x, y);
                                img->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(rw) +
                                            static_cast<std::size_t>(x)] =
                                    (static_cast<uint32_t>(c.a) << 24) |
                                    (static_cast<uint32_t>(c.r) << 16) |
                                    (static_cast<uint32_t>(c.g) << 8) |
                                    static_cast<uint32_t>(c.b);
                            }
                        }
                        box->image = std::move(img);
                    }
                }
            }
        }
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
        if (is_block_level_box(*ch) && !ch->style->is_out_of_flow() && !ch->style->is_floated()) {
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
        const bool lift_block = is_block_level_box(*ch) && !ch->style->is_out_of_flow() && !ch->style->is_floated();
        if (lift_block) {
            current_inline = nullptr;
            out.push_back(ch);
            split_inline_blocks.emplace_back(element, ch);
        } else {
            if (ch->box_type == BoxType::TextNode && is_all_whitespace(ch->text) &&
                !preserves_whitespace_text(*ch) && !current_inline) {
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
        // Whether these pixels may be read back by a script is decided here,
        // once, and carried on the element: a fetch that was not
        // CORS-same-origin taints any canvas it is drawn on (§4.12.5.1), and
        // `cross_origin` reports that the final hop, after any redirects, was
        // not the document's own origin under a CORS request.
        element.set_image_source_cross_origin(resp.cross_origin);
    }

    if (doc) {
        doc->store_cached_image(resolved, decoded);
    }
    return decoded;
}

void LayoutTree::build_boxes(
    const std::shared_ptr<Node>& node,
    const StyleResolver& resolver,
    const SharedComputedStyle& parent_style,
    ComputedStylePool& pool,
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
        const bool preserve_sp = parent_style && parent_style->preserves_spaces();
        const bool preserve_nl = parent_style && parent_style->preserves_newlines();
        if (preserve_sp) {
            if (raw.empty()) return;
            SharedComputedStyle text_style = pool.inherited_only(parent_style, Display::Inline);
            out.push_back(LayoutBox::create_text(node, normalize_preserved_newlines(raw), std::move(text_style)));
            return;
        }
        if (preserve_nl && (raw.find('\n') != std::string::npos || raw.find('\r') != std::string::npos)) {
            SharedComputedStyle text_style = pool.inherited_only(parent_style, Display::Inline);
            out.push_back(LayoutBox::create_text(node, collapse_spaces_preserve_newlines(raw), std::move(text_style)));
            return;
        }
        if (is_all_whitespace(raw)) {
            if (parent_style && parent_style->is_inline()) {
                SharedComputedStyle text_style = pool.inherited_only(parent_style, Display::Inline);
                out.push_back(LayoutBox::create_text(node, " ", std::move(text_style)));
            }
            return;
        }

        SharedComputedStyle text_style = parent_style
            ? pool.inherited_only(parent_style, Display::Inline)
            : default_computed_style();
        std::string collapsed = collapse_whitespace(raw);
        out.push_back(LayoutBox::create_text(node, std::move(collapsed), std::move(text_style)));
        return;
    }

    if (node->is_element()) {
        auto element = std::static_pointer_cast<Element>(node);
        const std::string& tag = element->tag_name();
        if (tag == "noscript" && !element->is_foreign()) {
            auto doc = element->owner_document();
            if (doc && doc->scripting_enabled()) return;
        }
        if (tag == "head" || tag == "style" || tag == "script" || tag == "meta" ||
            tag == "link" || tag == "base" || tag == "title" || tag == "template") {
            return;
        }
        if (tag == "br") {
            // Represent <br> as a newline/line-break inline box if needed, or preserve existing behavior
        }

        SharedComputedStyle style = resolve_element_style(element, resolver, parent_style, pool);
        if (!style) return;

        // CSS Display 3 §2.7 `display: contents`: the element generates no box
        // of its own and its children take part in its parent's formatting
        // context, as if the element had been replaced by them. `None` would
        // throw the children away as well, and the engine used to fall through
        // to `block` because the value was never parsed -- which is the row's
        // documented regression (a boxless body wrongly coloured the canvas).
        //
        // The children are built against the *parent's* style, not this
        // element's: with no box there is nothing for them to inherit through
        // in layout, and the element's own inherited properties have already
        // been passed down by `resolve_element_style` when it resolved them.
        if (style->is_contents()) {
            const auto& contents_children =
                element->shadow_root() ? element->shadow_root()->children() : element->children();
            for (const auto& ch : contents_children) {
                build_boxes(ch, resolver, parent_style, pool, depth + 1, out, split_inline_blocks);
            }
            return;
        }

        std::shared_ptr<LayoutBox> box;
        if (style->is_table()) {
            box = LayoutBox::create_table(element, style);
        } else if (style->is_table_row()) {
            box = LayoutBox::create_table_row(element, style);
        } else if (style->is_table_cell()) {
            box = LayoutBox::create_table_cell(element, style);
        } else if (style->is_flex()) {
            box = LayoutBox::create_flex(element, style);
        } else if (style->is_grid()) {
            box = LayoutBox::create_grid(element, style);
        } else if (style->is_block() || style->is_block_level() || style->is_inline_block() ||
                   style->is_table_caption() || style->is_table_column() ||
                   style->is_table_column_group()) {
            box = LayoutBox::create_block(element, style);
        } else {
            box = LayoutBox::create_inline(element, style);
        }

        // Synthesize visual content for void/form controls (<input>, <select>, dirty <textarea>).
        if (!element->is_foreign() && tag == "input") {
            const std::string itype = forms::input_type(*element);
            SharedComputedStyle ctrl_style = pool.inherited_only(style, Display::Inline);
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
                    ComputedStyle ph_style = *ctrl_style;
                    ph_style.color = Color(100, 116, 139, 255);
                    ctrl_style = pool.intern(std::move(ph_style));
                }
            }
            if (!label.empty()) {
                box->append_child(LayoutBox::create_text(nullptr, std::move(label), std::move(ctrl_style)));
            }
        } else if (!element->is_foreign() && tag == "select") {
            auto sel_opts = forms::selected_options(*element);
            if (!sel_opts.empty() && sel_opts.front()) {
                SharedComputedStyle ctrl_style = pool.inherited_only(style, Display::Inline);
                std::string label = collapse_whitespace(forms::option_label(*sel_opts.front()));
                if (!label.empty()) {
                    box->append_child(LayoutBox::create_text(nullptr, std::move(label), std::move(ctrl_style)));
                }
            }
        } else if (!element->is_foreign() && tag == "textarea") {
            std::string cur = forms::value(*element);
            if (!cur.empty() || element->has_attribute("placeholder")) {
                SharedComputedStyle ctrl_style = pool.inherited_only(style, Display::Inline);
                std::string label = !cur.empty()
                                        ? collapse_whitespace(cur)
                                        : collapse_whitespace(element->get_attribute("placeholder"));
                if (cur.empty()) {
                    ComputedStyle ph_style = *ctrl_style;
                    ph_style.color = Color(100, 116, 139, 255);
                    ctrl_style = pool.intern(std::move(ph_style));
                }
                if (!label.empty()) {
                    box->append_child(LayoutBox::create_text(nullptr, std::move(label), std::move(ctrl_style)));
                }
            }
        } else if (tag != "svg" && (element->is_foreign() || tag != "iframe")) {
            // CSS Generated Content 3 `::before` and `::after` (Phase 215): an
            // element with a string-valued `content` on one of them generates
            // an inline run, in the DOM order the pseudo-elements take -- the
            // before box first, then the children, then the after box.
            //
            // The run is laid out as a TextBox with no node of its own. Every
            // object in it is made for this layout and dies with the tree, so
            // nothing is inserted into the document: `textContent`, the
            // children list and every live collection stay exactly as the page
            // made them, which is what CSSOM says a pseudo-element is.
            auto generated_run = [&](int which) -> std::shared_ptr<LayoutBox> {
                const ComputedStyle pseudo =
                    resolver.resolve_pseudo_element(element, which, *style);
                if (pseudo.content.empty()) return nullptr;
                if (pseudo.is_none() || pseudo.is_contents()) return nullptr;
                SharedComputedStyle run_style = pool.intern(pseudo);
                return LayoutBox::create_text(nullptr, collapse_whitespace(pseudo.content),
                                              std::move(run_style));
            };
            if (auto before = generated_run(1)) box->append_child(std::move(before));
            const auto& children_to_process =
                element->shadow_root() ? element->shadow_root()->children() : element->children();
            std::vector<std::shared_ptr<LayoutBox>> child_boxes;
            // With an explicit summary, a closed disclosure lays out only
            // its first summary child. The DOM and its listeners stay intact.
            const Node* closed_summary = nullptr;
            if (!element->is_foreign() && tag == "details" && !element->has_attribute("open")) {
                for (const auto& ch : children_to_process) {
                    if (ch->is_element() && !ch->as_element()->is_foreign() && ch->as_element()->tag_name() == "summary") {
                        closed_summary = ch.get();
                        break;
                    }
                }
            }
            for (const auto& ch : children_to_process) {
                if (closed_summary && ch.get() != closed_summary) continue;
                child_boxes.clear();
                if (ch->is_element() && ch->as_element()->tag_name() == "slot") {
                    auto slot_el = std::static_pointer_cast<Element>(ch);
                    auto assigned = ShadowRoot::assigned_nodes(slot_el);
                    if (!assigned.empty()) {
                        for (const auto& a : assigned) {
                            build_boxes(a, resolver, style, pool, depth + 1, child_boxes, split_inline_blocks);
                        }
                    } else {
                        for (const auto& slot_ch : slot_el->children()) {
                            build_boxes(slot_ch, resolver, style, pool, depth + 1, child_boxes, split_inline_blocks);
                        }
                    }
                } else {
                    build_boxes(ch, resolver, style, pool, depth + 1, child_boxes, split_inline_blocks);
                }
                for (auto& cb : child_boxes) {
                    if (cb) box->append_child(cb);
                }
            }
            if (auto after = generated_run(2)) box->append_child(std::move(after));
        }

        normalize_table_children(box, pool);
        normalize_block_children(box, box->style, pool);

        if (tag == "img" || tag == "canvas" || tag == "svg" || (!element->is_foreign() && tag == "iframe")) {
            populate_special_element_box(element, box, pool);
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
            build_boxes(body, resolver, nullptr, pool, depth + 1, out, split_inline_blocks);
            return;
        }
        auto doc_el = doc->document_element();
        if (doc_el) {
            build_boxes(doc_el, resolver, nullptr, pool, depth + 1, out, split_inline_blocks);
            return;
        }
    }
}

std::shared_ptr<LayoutBox> LayoutTree::build_box(
    const std::shared_ptr<Node>& node,
    const StyleResolver& resolver,
    const SharedComputedStyle& parent_style,
    ComputedStylePool& pool,
    int depth
) {
    std::vector<std::shared_ptr<LayoutBox>> boxes;
    std::vector<std::pair<std::shared_ptr<Element>, std::shared_ptr<LayoutBox>>> split_inline_blocks;
    build_boxes(node, resolver, parent_style, pool, depth, boxes, split_inline_blocks);
    if (boxes.empty()) return nullptr;
    if (boxes.size() == 1) return boxes.front();

    SharedComputedStyle wrapper_style = pool.inherited_only(parent_style, Display::Block);
    auto wrapper = LayoutBox::create_anonymous(wrapper_style);
    for (auto& b : boxes) {
        wrapper->append_child(b);
    }
    normalize_block_children(wrapper, wrapper_style, pool);
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

    // Only a whole HTML document owns a canvas. In particular, building a
    // layout for an arbitrary <body> or <div> subtree must not turn that
    // subtree's background into a viewport-wide paint.
    std::shared_ptr<Document> document;
    std::shared_ptr<Element> html;
    std::optional<ComputedStyle> html_style;
    if (root_node && root_node->is_document()) {
        document = std::static_pointer_cast<Document>(root_node);
        auto element = document->document_element();
        if (element && !element->is_foreign() && element->tag_name() == "html") {
            html = element;
            html_style = resolver.resolve_style(html, nullptr);
        }
    }

    ComputedStylePool pool;
    std::vector<std::shared_ptr<LayoutBox>> root_boxes;
    std::vector<std::pair<std::shared_ptr<Element>, std::shared_ptr<LayoutBox>>> split_inline_blocks;
    // The tree normally starts at body, but a display:none HTML ancestor hides
    // that whole tree, including both candidates for canvas propagation.
    if (!html_style || !html_style->is_none()) {
        build_boxes(root_node, resolver, nullptr, pool, 0, root_boxes, split_inline_blocks);
    }

    std::shared_ptr<LayoutBox> root_box;
    if (root_boxes.size() == 1 && is_block_level_box(*root_boxes.front())) {
        root_box = root_boxes.front();
    } else if (!root_boxes.empty()) {
        SharedComputedStyle root_style = pool.inherited_only(default_computed_style(), Display::Block);
        root_box = LayoutBox::create_anonymous(root_style);
        for (auto& b : root_boxes) {
            root_box->append_child(b);
        }
        normalize_block_children(root_box, root_style, pool);
    } else {
        SharedComputedStyle root_style = pool.inherited_only(default_computed_style(), Display::Block);
        root_box = LayoutBox::create_block(nullptr, std::move(root_style));
    }

    // Even an uncolored HTML document owns a transparent canvas: embedded
    // documents preserve its alpha, while a top-level terminal needs to know
    // that it should show the browser's white backdrop rather than its default
    // dark grid. Subtree and non-HTML layout roots do not own one.
    root_box->has_document_canvas = html_style.has_value();

    // CSS Backgrounds 3 §2.11.2: the HTML root supplies the canvas unless its
    // background-color is transparent AND its background-image is none, in
    // which case the first HTML body child supplies it. The used background on
    // the source box is then transparent; keep the computed style intact and
    // mark its boxes instead. Root opacity applies to the entire document
    // paint including the canvas (CSS Color 4 §3.2).
    std::shared_ptr<Element> canvas_source;
    if (html_style && !html_style->is_none()) {
        root_box->root_opacity = html_style->opacity;
        const bool html_has_bg = !html_style->background_color.is_transparent() ||
                                 html_style->has_background_image;
        if (html_has_bg) {
            root_box->canvas_background = html_style->background_color;
            if (html_style->background_gradient) {
                root_box->canvas_gradient = std::make_shared<LinearGradient>(*html_style->background_gradient);
            }
            canvas_source = html;
        } else if (auto body = document->body();
                   body && !body->is_foreign() && body->parent() == html) {
            const ComputedStyle body_style = resolver.resolve_style(body, &*html_style);
            // CSS Display 3 §2.7: an element with `display: contents` generates
            // no box, and CSS Backgrounds 3 propagates the canvas color from the
            // *body element's* background only when it is a box that paints it.
            // Chromium leaves the canvas white for a boxless body; before this
            // check the resolver had no `contents` value at all, so the body
            // stayed `block` and red leaked onto the canvas (finding 194).
            const bool body_has_bg = !body_style.background_color.is_transparent() ||
                                     body_style.has_background_image;
            if (!body_style.is_none() && !body_style.is_contents() && body_has_bg) {
                root_box->canvas_background = body_style.background_color;
                if (body_style.background_gradient) {
                    root_box->canvas_gradient = std::make_shared<LinearGradient>(*body_style.background_gradient);
                }
                canvas_source = body;
            }
        }
    }
    if (canvas_source) {
        // Usually the body is the root box. An inline body can be split into
        // multiple boxes around blocks; mark each before layout constructs its
        // line fragments so the inline painter can preserve child backgrounds.
        std::vector<LayoutBox*> boxes{root_box.get()};
        while (!boxes.empty()) {
            LayoutBox* box = boxes.back();
            boxes.pop_back();
            if (box->node == canvas_source) box->background_propagated = true;
            for (auto& child : box->children) boxes.push_back(child.get());
        }
    }

    bool document_body_root = false;
    if (document) {
        if (auto body = document->body(); body && root_box->node == body) document_body_root = true;
    }
    root_box->dimensions.content.x = 0.0f;
    root_box->dimensions.content.y = 0.0f;
    if (document_body_root) prepare_document_body(*root_box);
    root_box->layout(viewport_width, viewport_height);
    if (document_body_root) {
        // The root's own used margin, for the offset place_document_body adds.
        // Only a root that states one contributes; percentages resolve against
        // the viewport, which is the root's containing block.
        EdgeSizes root_margin{0.0f};
        bool has_root_margin = false;
        if (root_node && root_node->is_document()) {
            auto doc = std::static_pointer_cast<Document>(root_node);
            if (auto html = doc->document_element()) {
                if (html->tag_name() == "html") {
                    const ComputedStyle root_style = resolver.resolve_style(html, nullptr);
                    root_margin.top = root_style.margin_percent.top >= 0.0f
                                          ? root_style.margin_percent.top * viewport_width / 100.0f
                                          : root_style.margin.top;
                    root_margin.left = root_style.margin_percent.left >= 0.0f
                                           ? root_style.margin_percent.left * viewport_width / 100.0f
                                           : root_style.margin.left;
                    has_root_margin = root_margin.top != 0.0f || root_margin.left != 0.0f;
                }
            }
        }
        place_document_body(*root_box, viewport_width, viewport_height,
                            has_root_margin ? &root_margin : nullptr);
    }
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
                el->set_layout_box(box->dimensions.border_box(), box->dimensions.border, box->dimensions.padding);
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
                        frag.box->node->as_element()->set_layout_box(frag.rect, frag.box->dimensions.border, frag.box->dimensions.padding);
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
            if ((!html_style || !html_style->is_none()) && !doc_el->has_layout_box() && root_box) {
                const float h = std::max(viewport_height, root_box->dimensions.margin_box().height);
                doc_el->set_layout_box(Rect(0.0f, 0.0f, viewport_width, h));
            }
        }
        doc->mark_layout_clean();
    }

    return root_box;
}

} // namespace litebird
