#include "litebird/css/StyleResolver.hpp"
#include <unordered_set>
#include <algorithm>
#include "litebird/css/CssParser.hpp"
#include "litebird/css/CssText.hpp"
#include "litebird/dom/Selector.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/FormState.hpp"
#include <sstream>
#include <bit>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstring>
#include <functional>

namespace litebird {

namespace {

void hash_combine(std::size_t& seed, std::size_t v) {
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

std::size_t hash_float(float f) {
    if (f == 0.0f) return 0u;
    if (f != f) return 0x7fc00000u;
    return static_cast<std::size_t>(std::bit_cast<uint32_t>(f));
}

void hash_edges(std::size_t& h, const EdgeSizes& e) {
    hash_combine(h, hash_float(e.top));
    hash_combine(h, hash_float(e.right));
    hash_combine(h, hash_float(e.bottom));
    hash_combine(h, hash_float(e.left));
}

void hash_color(std::size_t& h, const Color& c) {
    uint32_t packed = (static_cast<uint32_t>(c.r) << 24) |
                      (static_cast<uint32_t>(c.g) << 16) |
                      (static_cast<uint32_t>(c.b) << 8) |
                      static_cast<uint32_t>(c.a);
    hash_combine(h, packed);
}

bool equal_transform(const TransformMatrix2D& a, const TransformMatrix2D& b) {
    return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.e == b.e && a.f == b.f;
}

bool equal_filter(const FilterFunction& a, const FilterFunction& b) {
    return a.type == b.type && a.amount == b.amount && a.color == b.color &&
           a.dx == b.dx && a.dy == b.dy && a.blur_radius == b.blur_radius;
}

bool ascii_iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != static_cast<unsigned char>(b[i])) {
            return false;
        }
    }
    return true;
}

bool parse_text_align(std::string_view val_lower, TextAlign& out) {
    if (ascii_iequals(val_lower, "left")) { out = TextAlign::Left; return true; }
    if (ascii_iequals(val_lower, "center")) { out = TextAlign::Center; return true; }
    if (ascii_iequals(val_lower, "right")) { out = TextAlign::Right; return true; }
    if (ascii_iequals(val_lower, "justify")) { out = TextAlign::Justify; return true; }
    if (ascii_iequals(val_lower, "start")) { out = TextAlign::Start; return true; }
    if (ascii_iequals(val_lower, "end")) { out = TextAlign::End; return true; }
    return false;
}

bool parse_text_decoration(std::string_view val_lower, TextDecoration& out) {
    std::string_view trimmed = css_trim_view(val_lower);
    if (ascii_iequals(trimmed, "none")) {
        out = TextDecoration::None;
        return true;
    }
    TextDecoration mask = TextDecoration::None;
    bool any = false;
    std::size_t i = 0;
    while (i < trimmed.size()) {
        while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i]))) ++i;
        std::size_t start = i;
        while (i < trimmed.size() && !std::isspace(static_cast<unsigned char>(trimmed[i]))) ++i;
        if (i > start) {
            std::string_view tok = trimmed.substr(start, i - start);
            if (ascii_iequals(tok, "underline")) { mask |= TextDecoration::Underline; any = true; }
            else if (ascii_iequals(tok, "line-through")) { mask |= TextDecoration::LineThrough; any = true; }
            else if (ascii_iequals(tok, "overline")) { mask |= TextDecoration::Overline; any = true; }
            else if (ascii_iequals(tok, "none")) { any = true; }
        }
    }
    if (!any) return false;
    out = mask;
    return true;
}

bool parse_timing_function(std::string_view val, TimingFunction& out) {
    std::string_view trimmed = css_trim_view(val);
    if (ascii_iequals(trimmed, "ease")) { out = TimingFunction::Ease; return true; }
    if (ascii_iequals(trimmed, "linear")) { out = TimingFunction::Linear; return true; }
    if (ascii_iequals(trimmed, "ease-in")) { out = TimingFunction::EaseIn; return true; }
    if (ascii_iequals(trimmed, "ease-out")) { out = TimingFunction::EaseOut; return true; }
    if (ascii_iequals(trimmed, "ease-in-out")) { out = TimingFunction::EaseInOut; return true; }
    if (ascii_iequals(trimmed, "step-start")) { out = TimingFunction::StepStart; return true; }
    if (ascii_iequals(trimmed, "step") || ascii_iequals(trimmed, "step-end")) { out = TimingFunction::StepEnd; return true; }
    return false;
}

} // namespace

bool ComputedStyle::operator==(const ComputedStyle& other) const {
    if (display != other.display || color != other.color ||
        background_color != other.background_color || dark_color_scheme != other.dark_color_scheme ||
        width != other.width || height != other.height ||
        !(margin == other.margin) || !(padding == other.padding) ||
        width_percent != other.width_percent || height_percent != other.height_percent ||
        min_width_percent != other.min_width_percent || max_width_percent != other.max_width_percent ||
        min_height_percent != other.min_height_percent || max_height_percent != other.max_height_percent ||
        !(margin_percent == other.margin_percent) || !(padding_percent == other.padding_percent) ||
        !(border_width == other.border_width) || border_color != other.border_color ||
        border_radius != other.border_radius || border_radius_percent != other.border_radius_percent ||
        !(box_shadow == other.box_shadow) ||
        font_size != other.font_size || font_weight != other.font_weight ||
        font_style != other.font_style || font_family != other.font_family ||
        line_height != other.line_height || line_height_number != other.line_height_number ||
        text_align != other.text_align || text_decoration != other.text_decoration ||
        float_type != other.float_type || clear_type != other.clear_type || overflow != other.overflow ||
        border_collapse != other.border_collapse ||
        border_spacing_x != other.border_spacing_x || border_spacing_y != other.border_spacing_y ||
        table_layout != other.table_layout || empty_cells != other.empty_cells ||
        caption_side != other.caption_side ||
        position != other.position ||
        top != other.top || right != other.right || bottom != other.bottom || left != other.left ||
        has_top != other.has_top || has_right != other.has_right ||
        has_bottom != other.has_bottom || has_left != other.has_left ||
        z_index != other.z_index || has_z_index != other.has_z_index ||
        box_sizing != other.box_sizing ||
        min_width != other.min_width || max_width != other.max_width ||
        min_height != other.min_height || max_height != other.max_height ||
        margin_top_auto != other.margin_top_auto || margin_bottom_auto != other.margin_bottom_auto ||
        margin_left_auto != other.margin_left_auto || margin_right_auto != other.margin_right_auto ||
        flex_direction != other.flex_direction || flex_wrap != other.flex_wrap ||
        justify_content != other.justify_content || align_items != other.align_items ||
        align_content != other.align_content ||
        gap != other.gap || flex_grow != other.flex_grow ||
        flex_shrink != other.flex_shrink || flex_basis != other.flex_basis ||
        grid_column_gap != other.grid_column_gap || grid_row_gap != other.grid_row_gap ||
        grid_auto_fit_columns != other.grid_auto_fit_columns ||
        !(grid_auto_fit_def == other.grid_auto_fit_def) ||
        opacity != other.opacity || animation_duration != other.animation_duration ||
        animation_timing_function != other.animation_timing_function ||
        animation_iteration_count != other.animation_iteration_count ||
        transition_duration != other.transition_duration ||
        transition_timing_function != other.transition_timing_function ||
        transition_delay != other.transition_delay ||
        has_transform != other.has_transform ||
        grid_row_start != other.grid_row_start || grid_row_end != other.grid_row_end ||
        grid_column_start != other.grid_column_start || grid_column_end != other.grid_column_end ||
        containment != other.containment ||
        animation_name != other.animation_name ||
        transition_property != other.transition_property ||
        transform_str != other.transform_str ||
        transform_origin != other.transform_origin ||
        grid_area_name != other.grid_area_name ||
        grid_template_columns != other.grid_template_columns ||
        grid_template_rows != other.grid_template_rows ||
        grid_col_track_defs != other.grid_col_track_defs ||
        grid_row_track_defs != other.grid_row_track_defs) {
        return false;
    }
    if (!equal_transform(transform_matrix, other.transform_matrix)) {
        return false;
    }
    if (filters.size() != other.filters.size()) return false;
    for (std::size_t i = 0; i < filters.size(); ++i) {
        if (!equal_filter(filters[i], other.filters[i])) return false;
    }
    if (backdrop_filters.size() != other.backdrop_filters.size()) return false;
    for (std::size_t i = 0; i < backdrop_filters.size(); ++i) {
        if (!equal_filter(backdrop_filters[i], other.backdrop_filters[i])) return false;
    }
    if (custom_properties != other.custom_properties) return false;
    if (grid_named_areas != other.grid_named_areas) return false;
    return true;
}

std::size_t ComputedStyle::hash() const {
    std::size_t h = static_cast<std::size_t>(display);
    hash_color(h, color);
    hash_color(h, background_color);
    hash_combine(h, dark_color_scheme ? 1u : 0u);
    hash_combine(h, hash_float(width));
    hash_combine(h, hash_float(height));
    hash_edges(h, margin);
    hash_edges(h, padding);
    hash_combine(h, hash_float(width_percent));
    hash_combine(h, hash_float(height_percent));
    hash_combine(h, hash_float(min_width_percent));
    hash_combine(h, hash_float(max_width_percent));
    hash_combine(h, hash_float(min_height_percent));
    hash_combine(h, hash_float(max_height_percent));
    hash_edges(h, margin_percent);
    hash_edges(h, padding_percent);
    hash_edges(h, border_width);
    hash_color(h, border_color);
    hash_combine(h, hash_float(border_radius));
    hash_combine(h, hash_float(border_radius_percent));
    if (box_shadow.has_shadow) {
        hash_combine(h, 1u);
        hash_combine(h, hash_float(box_shadow.x_offset));
        hash_combine(h, hash_float(box_shadow.y_offset));
        hash_combine(h, hash_float(box_shadow.blur));
        hash_color(h, box_shadow.color);
    }
    hash_combine(h, hash_float(font_size));
    hash_combine(h, static_cast<std::size_t>(font_weight));
    hash_combine(h, static_cast<std::size_t>(font_style));
    if (!font_family.empty()) hash_combine(h, std::hash<std::string>{}(font_family));
    hash_combine(h, hash_float(line_height));
    hash_combine(h, hash_float(line_height_number));
    const uint32_t flags =
        (static_cast<uint32_t>(text_align)) |
        (static_cast<uint32_t>(text_decoration) << 4) |
        (static_cast<uint32_t>(float_type) << 8) |
        (static_cast<uint32_t>(clear_type) << 10) |
        (static_cast<uint32_t>(overflow) << 12) |
        (static_cast<uint32_t>(border_collapse) << 14) |
        (static_cast<uint32_t>(table_layout) << 15) |
        (static_cast<uint32_t>(empty_cells) << 16) |
        (static_cast<uint32_t>(caption_side) << 17) |
        (static_cast<uint32_t>(position) << 18) |
        (static_cast<uint32_t>(box_sizing) << 20) |
        (static_cast<uint32_t>(containment) << 21);
    hash_combine(h, flags);
    hash_combine(h, hash_float(border_spacing_x));
    hash_combine(h, hash_float(border_spacing_y));
    if (has_top || has_right || has_bottom || has_left || has_z_index) {
        hash_combine(h, (has_top ? 1u : 0u) | (has_right ? 2u : 0u) |
                        (has_bottom ? 4u : 0u) | (has_left ? 8u : 0u) |
                        (has_z_index ? 16u : 0u));
        hash_combine(h, hash_float(top));
        hash_combine(h, hash_float(right));
        hash_combine(h, hash_float(bottom));
        hash_combine(h, hash_float(left));
        hash_combine(h, static_cast<std::size_t>(z_index));
    }
    hash_combine(h, hash_float(min_width));
    hash_combine(h, hash_float(max_width));
    hash_combine(h, hash_float(min_height));
    hash_combine(h, hash_float(max_height));
    if (margin_top_auto || margin_bottom_auto || margin_left_auto || margin_right_auto) {
        hash_combine(h, (margin_top_auto ? 1u : 0u) | (margin_bottom_auto ? 2u : 0u) |
                        (margin_left_auto ? 4u : 0u) | (margin_right_auto ? 8u : 0u));
    }
    if (display == Display::Flex || flex_grow != 0.0f || flex_shrink != 1.0f ||
        flex_basis >= 0.0f || gap != 0.0f) {
        hash_combine(h, static_cast<std::size_t>(flex_direction));
        hash_combine(h, static_cast<std::size_t>(flex_wrap));
        hash_combine(h, static_cast<std::size_t>(justify_content));
        hash_combine(h, static_cast<std::size_t>(align_items));
        hash_combine(h, static_cast<std::size_t>(align_content));
        hash_combine(h, hash_float(gap));
        hash_combine(h, hash_float(flex_grow));
        hash_combine(h, hash_float(flex_shrink));
        hash_combine(h, hash_float(flex_basis));
    }
    if (!grid_template_columns.empty() || !grid_template_rows.empty() ||
        !grid_col_track_defs.empty() || !grid_row_track_defs.empty() ||
        grid_column_gap != 0.0f || grid_row_gap != 0.0f || grid_auto_fit_columns ||
        !grid_named_areas.empty() || !grid_area_name.empty() ||
        grid_row_start != 0 || grid_row_end != 0 ||
        grid_column_start != 0 || grid_column_end != 0) {
        hash_combine(h, grid_template_columns.size());
        for (float v : grid_template_columns) hash_combine(h, hash_float(v));
        hash_combine(h, grid_template_rows.size());
        for (float v : grid_template_rows) hash_combine(h, hash_float(v));
        hash_combine(h, hash_float(grid_column_gap));
        hash_combine(h, hash_float(grid_row_gap));
        hash_combine(h, grid_col_track_defs.size());
        for (const auto& d : grid_col_track_defs) {
            hash_combine(h, hash_float(d.min_px));
            hash_combine(h, hash_float(d.max_px));
            hash_combine(h, hash_float(d.max_fr));
        }
        hash_combine(h, grid_row_track_defs.size());
        for (const auto& d : grid_row_track_defs) {
            hash_combine(h, hash_float(d.min_px));
            hash_combine(h, hash_float(d.max_px));
            hash_combine(h, hash_float(d.max_fr));
        }
        hash_combine(h, grid_auto_fit_columns ? 1u : 0u);
        if (!grid_area_name.empty()) hash_combine(h, std::hash<std::string>{}(grid_area_name));
        hash_combine(h, static_cast<std::size_t>(grid_row_start));
        hash_combine(h, static_cast<std::size_t>(grid_row_end));
        hash_combine(h, static_cast<std::size_t>(grid_column_start));
        hash_combine(h, static_cast<std::size_t>(grid_column_end));
        hash_combine(h, grid_named_areas.size());
    }
    hash_combine(h, hash_float(opacity));
    if (!animation_name.empty() || animation_duration != 0.0f ||
        animation_timing_function != TimingFunction::Ease || animation_iteration_count != 1.0f) {
        if (!animation_name.empty()) hash_combine(h, std::hash<std::string>{}(animation_name));
        hash_combine(h, hash_float(animation_duration));
        hash_combine(h, static_cast<std::size_t>(animation_timing_function));
        hash_combine(h, hash_float(animation_iteration_count));
    }
    if (!transition_property.empty() || transition_duration != 0.0f ||
        transition_timing_function != TimingFunction::Ease || transition_delay != 0.0f) {
        if (!transition_property.empty()) hash_combine(h, std::hash<std::string>{}(transition_property));
        hash_combine(h, hash_float(transition_duration));
        hash_combine(h, static_cast<std::size_t>(transition_timing_function));
        hash_combine(h, hash_float(transition_delay));
    }
    if (has_transform || !transform_str.empty() || !transform_origin.empty()) {
        hash_combine(h, has_transform ? 1u : 0u);
        hash_combine(h, hash_float(transform_matrix.a));
        hash_combine(h, hash_float(transform_matrix.b));
        hash_combine(h, hash_float(transform_matrix.c));
        hash_combine(h, hash_float(transform_matrix.d));
        hash_combine(h, hash_float(transform_matrix.e));
        hash_combine(h, hash_float(transform_matrix.f));
        if (!transform_str.empty()) hash_combine(h, std::hash<std::string>{}(transform_str));
        if (!transform_origin.empty()) hash_combine(h, std::hash<std::string>{}(transform_origin));
    }
    if (!custom_properties.empty()) {
        // Order-independent hash for custom_properties map.
        std::size_t map_xor = custom_properties.size();
        for (const auto& [k, v] : custom_properties) {
            std::size_t kv = std::hash<std::string>{}(k);
            hash_combine(kv, std::hash<std::string>{}(v));
            map_xor ^= kv;
        }
        hash_combine(h, map_xor);
    }
    if (!filters.empty()) {
        hash_combine(h, filters.size());
        for (const auto& f : filters) {
            hash_combine(h, static_cast<std::size_t>(f.type));
            hash_combine(h, hash_float(f.amount));
        }
    }
    if (!backdrop_filters.empty()) {
        hash_combine(h, backdrop_filters.size());
        for (const auto& f : backdrop_filters) {
            hash_combine(h, static_cast<std::size_t>(f.type));
            hash_combine(h, hash_float(f.amount));
        }
    }
    return h;
}

const SharedComputedStyle& default_computed_style() {
    static const SharedComputedStyle* canonical =
        new SharedComputedStyle(std::make_shared<const ComputedStyle>());
    return *canonical;
}

ComputedStylePool::ComputedStylePool() {
    const SharedComputedStyle& def = default_computed_style();
    m_buckets[def->hash()].push_back(def);
}

SharedComputedStyle ComputedStylePool::intern(ComputedStyle&& style) {
    const std::size_t h = style.hash();
    auto& bucket = m_buckets[h];
    for (const auto& existing : bucket) {
        if (*existing == style) {
            return existing;
        }
    }
    auto created = std::make_shared<const ComputedStyle>(std::move(style));
    bucket.push_back(created);
    return created;
}

SharedComputedStyle ComputedStylePool::intern(const ComputedStyle& style) {
    const std::size_t h = style.hash();
    auto& bucket = m_buckets[h];
    for (const auto& existing : bucket) {
        if (*existing == style) {
            return existing;
        }
    }
    auto created = std::make_shared<const ComputedStyle>(style);
    bucket.push_back(created);
    return created;
}

SharedComputedStyle ComputedStylePool::inherited_only(const SharedComputedStyle& parent, Display display) {
    const ComputedStyle* key = parent ? parent.get() : default_computed_style().get();
    if (display == Display::Inline) {
        auto it = m_inherited_inline_cache.find(key);
        if (it != m_inherited_inline_cache.end()) return it->second;
    } else if (display == Display::Block) {
        auto it = m_inherited_block_cache.find(key);
        if (it != m_inherited_block_cache.end()) return it->second;
    }

    ComputedStyle s;
    s.display = display;
    s.color = key->color;
    s.font_size = key->font_size;
    s.font_weight = key->font_weight;
    if (!key->font_family.empty()) s.font_family = key->font_family;
    // And the slant, for the same reason and by the same argument: it inherits,
    // and this is the style a text box is measured and drawn with. An `<em>`
    // whose font-style stopped at the element would be upright in the box that
    // lays it out and upright in the painter, however italic the computed style
    // said it was.
    s.font_style = key->font_style;
    s.line_height = key->line_height;
    s.line_height_number = key->line_height_number;
    s.text_align = key->text_align;
    s.text_decoration = key->text_decoration;
    s.border_collapse = key->border_collapse;
    s.border_spacing_x = key->border_spacing_x;
    s.border_spacing_y = key->border_spacing_y;
    if (!key->custom_properties.empty()) s.custom_properties = key->custom_properties;

    SharedComputedStyle interned = intern(std::move(s));
    if (display == Display::Inline) {
        m_inherited_inline_cache.emplace(key, interned);
    } else if (display == Display::Block) {
        m_inherited_block_cache.emplace(key, interned);
    }
    return interned;
}

std::size_t ComputedStylePool::unique_count() const {
    std::size_t total = 0;
    for (const auto& [h, vec] : m_buckets) {
        total += vec.size();
    }
    return total;
}

StyleSheet StyleResolver::default_user_agent_stylesheet() {
    const char* ua_css = R"(
        table { display: table; border-collapse: separate; }
        caption { display: table-caption; }
        col, colgroup { display: none; }
        thead, tbody, tfoot { display: table-row-group; }
        tr { display: table-row; }
        td, th { display: table-cell; }
        th { font-weight: 700; text-align: center; }

        html, body, div, p, h1, h2, h3, h4, h5, h6, ul, ol, li,
        section, article, header, footer, nav, main, pre, blockquote {
            display: block;
        }
        span, a, strong, b, em, i, code {
            display: inline;
        }
        strong, b {
            font-weight: 700;
        }
        a {
            color: #0066cc;
            text-decoration: underline;
        }
        code {
            background-color: #f1f5f9;
        }
        /* HTML §Rendering: the elements whose text is italic. `cite`, `dfn`,
           `var` and `address` are in the same row of that table as `em` and
           `i`, and leaving them out is how `<cite>` ends up upright while the
           `<em>` beside it is not. */
        em, i, cite, dfn, var, address {
            font-style: italic;
        }
        /* HTML §Rendering: the elements whose text is monospaced. Without
           this the engine had a `monospace` generic family it could resolve
           and no element that ever asked for it, so `<pre>` -- the element
           whose whole purpose is that its columns line up -- was laid out
           proportional. */
        pre, xmp, plaintext, listing, code, kbd, samp, tt {
            font-family: monospace;
        }
        body {
            margin: 8px;
            color: #222222;
            background-color: #ffffff;
            font-size: 16px;
        }
        h1 {
            font-size: 28px;
            font-weight: 700;
            margin: 16px 0;
        }
        h2 {
            font-size: 22px;
            font-weight: 700;
            margin: 12px 0;
        }
        h3 {
            font-size: 18px;
            font-weight: 700;
            margin: 10px 0;
        }
        p {
            margin: 8px 0;
        }
    )";
    return CssParser::parse(ua_css);
}

StyleResolver::StyleResolver() : m_ua_sheet(default_user_agent_stylesheet()) {}

void StyleResolver::index_sheet(SelectorIndex& index, const StyleSheet& sheet,
                                std::size_t sheet_index, std::size_t& source_order) {
    for (std::size_t ri = 0; ri < sheet.rules.size(); ++ri) {
        const auto& rule = sheet.rules[ri];
        for (std::size_t si = 0; si < rule.selectors.size(); ++si) {
            const auto& selector = rule.selectors[si];
            if (selector.parts.empty()) continue;
            const auto& rightmost = selector.parts.back();
            IndexedSelector entry{sheet_index, ri, si, source_order++};
            if (!rightmost.id.empty()) {
                index.by_id[rightmost.id].push_back(entry);
            } else if (!rightmost.classes.empty()) {
                // Index each selector by one necessary condition only. The
                // full selector is still checked after candidate retrieval.
                index.by_class[rightmost.classes.front()].push_back(entry);
            } else if (!rightmost.tag_name.empty() && rightmost.tag_name != "*") {
                index.by_tag[rightmost.tag_name].push_back(entry);
            } else {
                // Attribute- and pseudo-only selectors, universal selectors,
                // and :host rules have no id/class/tag key to narrow by.
                index.universal.push_back(entry);
            }
        }
    }
}

void StyleResolver::add_stylesheet(const StyleSheet& sheet) {
    m_depth_cache.clear();
    m_desc_prefix_cache.clear();
    for (const auto& layer : sheet.layers) {
        if (!layer.empty() &&
            std::find(m_layer_order.begin(), m_layer_order.end(), layer) == m_layer_order.end()) {
            m_layer_order.push_back(layer);
        }
    }
    for (const auto& rule : sheet.rules) {
        if (!rule.layer_name.empty() &&
            std::find(m_layer_order.begin(), m_layer_order.end(), rule.layer_name) == m_layer_order.end()) {
            m_layer_order.push_back(rule.layer_name);
        }
    }
    m_author_sheets.push_back(sheet);
    index_sheet(m_author_selector_index, m_author_sheets.back(),
                m_author_sheets.size() - 1, m_next_author_selector_order);
}

int StyleResolver::layer_rank(const std::string& layer_name) const {
    if (layer_name.empty()) return -1;
    auto it = std::find(m_layer_order.begin(), m_layer_order.end(), layer_name);
    if (it == m_layer_order.end()) return -1;
    return static_cast<int>(it - m_layer_order.begin());
}

std::vector<StyleResolver::IndexedSelector> StyleResolver::indexed_candidates(
    const std::shared_ptr<Element>& element) const {
    std::vector<IndexedSelector> candidates = m_author_selector_index.universal;
    auto append = [&](const auto& map, const std::string& key) {
        auto it = map.find(key);
        if (it != map.end()) candidates.insert(candidates.end(), it->second.begin(), it->second.end());
    };

    const std::string id = element->id();
    if (!id.empty()) append(m_author_selector_index.by_id, id);
    append(m_author_selector_index.by_tag, element->tag_name());

    const std::string class_attr = element->get_attribute("class");
    // Any ASCII whitespace separates class names, not only a space: markup
    // formatted by a tool breaks a long list of classes across lines.
    const auto is_class_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    };
    std::size_t pos = 0;
    while (pos < class_attr.size()) {
        while (pos < class_attr.size() && is_class_space(class_attr[pos])) ++pos;
        const std::size_t start = pos;
        while (pos < class_attr.size() && !is_class_space(class_attr[pos])) ++pos;
        if (pos > start) append(m_author_selector_index.by_class, class_attr.substr(start, pos - start));
    }

    std::sort(candidates.begin(), candidates.end(), [](const IndexedSelector& a, const IndexedSelector& b) {
        return a.source_order < b.source_order;
    });
    candidates.erase(
        std::unique(candidates.begin(), candidates.end(), [](const IndexedSelector& a, const IndexedSelector& b) {
            return a.source_order == b.source_order;
        }),
        candidates.end());
    return candidates;
}

// Parse a strictly signed base-10 integer (optional single sign, then at
// least one digit). CSS tokenises the parts of an <an+b> formula separately,
// so this rejects anything that is not one token's worth of number: "+1" is
// valid, "1.5", "x", "" and "++1" are not (the caller decides what an empty
// part means).
static bool parse_b_integer(const std::string& text, int& out) {
    size_t i = 0;
    int sign = 1;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        sign = text[i] == '-' ? -1 : 1;
        ++i;
    }
    if (i >= text.size()) return false;
    long value = 0;
    for (; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<long>(c - '0');
        if (value > 0x7fffffffL) return false;
    }
    out = static_cast<int>(sign * value);
    return true;
}

// The <an+b> micro-syntax ignores whitespace tokens: `:nth-child(2n + 1)`
// and `:nth-child(2n+1)` are the same formula, and so are the spaces around
// a sign (`+ 1` is `+1`).
static void erase_css_whitespace(std::string& text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) {
                   return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
               }),
               text.end());
}

static bool match_nth_formula(int idx, std::string_view formula_text) {
    std::string formula(css_trim_view(formula_text));
    std::transform(formula.begin(), formula.end(), formula.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    erase_css_whitespace(formula);

    if (formula.empty()) return false;
    if (formula == "odd") return (idx % 2 != 0);
    if (formula == "even") return (idx % 2 == 0);

    const size_t n_pos = formula.find('n');
    if (n_pos == std::string::npos) {
        // A plain integer: matches only that position.
        int b = 0;
        return parse_b_integer(formula, b) && idx == b;
    }

    // The coefficient: empty, a bare sign, or a signed integer; anything
    // else ("2nn", "1.5n", "xn") makes the formula invalid.
    const std::string a_text = formula.substr(0, n_pos);
    int a = 1;
    if (a_text.empty() || a_text == "+") {
        a = 1;
    } else if (a_text == "-") {
        a = -1;
    } else if (!parse_b_integer(a_text, a)) {
        return false;
    }

    // The offset: empty (0) or a signed integer.
    const std::string b_text = formula.substr(n_pos + 1);
    int b = 0;
    if (!b_text.empty() && !parse_b_integer(b_text, b)) {
        return false;
    }

    if (a == 0) return idx == b;
    const int diff = idx - b;
    if (diff % a != 0) return false;
    return (diff / a) >= 0;
}

static bool match_attr_operator(const std::shared_ptr<Element>& element, const AttributeMatch& am) {
    if (!element->has_attribute(am.name)) return false;
    if (am.op.empty()) return true;
    std::string found = element->get_attribute(am.name);
    std::string wanted = am.value;
    if (am.ignore_case) {
        for (char& c : found) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (am.op == "=") return found == wanted;
    if (am.op == "~=") {
        if (wanted.empty()) return false;
        size_t i = 0;
        while (i < found.size()) {
            while (i < found.size() && css_is_space(found[i])) ++i;
            size_t start = i;
            while (i < found.size() && !css_is_space(found[i])) ++i;
            if (i > start && found.compare(start, i - start, wanted) == 0) return true;
        }
        return false;
    }
    if (am.op == "|=") {
        return found == wanted ||
               (found.size() > wanted.size() && found.compare(0, wanted.size(), wanted) == 0 &&
                found[wanted.size()] == '-');
    }
    if (am.op == "^=") return !wanted.empty() && found.compare(0, wanted.size(), wanted) == 0;
    if (am.op == "$=") {
        return !wanted.empty() && found.size() >= wanted.size() &&
               found.compare(found.size() - wanted.size(), wanted.size(), wanted) == 0;
    }
    if (am.op == "*=") return !wanted.empty() && found.find(wanted) != std::string::npos;
    return false;
}

bool StyleResolver::matches_simple_selector(const std::shared_ptr<Element>& element, const SimpleSelector& sel) const {
    if (!sel.tag_name.empty() && sel.tag_name != "*" && sel.tag_name != element->tag_name()) {
        return false;
    }
    if (!sel.id.empty() && sel.id != element->id()) {
        return false;
    }
    for (const auto& cls : sel.classes) {
        if (!element->has_class(cls)) {
            return false;
        }
    }
    if (!sel.attribute_matches.empty()) {
        for (const auto& am : sel.attribute_matches) {
            if (!match_attr_operator(element, am)) return false;
        }
    } else {
        for (const auto& [attr_name, attr_val] : sel.attributes) {
            if (!element->has_attribute(attr_name)) {
                return false;
            }
            if (!attr_val.empty() && element->get_attribute(attr_name) != attr_val) {
                return false;
            }
        }
    }

    // Match pseudo-classes
    if (!sel.pseudo_classes.empty()) {
        auto parent = element->parent();
        std::vector<std::shared_ptr<Element>> sibling_elements;
        if (parent) {
            for (const auto& child : parent->children()) {
                if (auto el_child = std::dynamic_pointer_cast<Element>(child)) {
                    sibling_elements.push_back(el_child);
                }
            }
        }

        for (const auto& pc : sel.pseudo_classes) {
            const std::string& name = pc.name;
            const std::string& arg = pc.argument;

            if (name == "first-child") {
                if (sibling_elements.empty() || sibling_elements.front() != element) return false;
            } else if (name == "last-child") {
                if (sibling_elements.empty() || sibling_elements.back() != element) return false;
            } else if (name == "only-child") {
                if (sibling_elements.size() != 1 || sibling_elements.front() != element) return false;
            } else if (name == "nth-child" || name == "nth-last-child") {
                int idx = 1;
                bool found = false;
                for (size_t i = 0; i < sibling_elements.size(); ++i) {
                    if (sibling_elements[i] == element) {
                        idx = (name == "nth-last-child")
                                  ? static_cast<int>(sibling_elements.size() - i)
                                  : static_cast<int>(i) + 1;
                        found = true;
                        break;
                    }
                }
                if (!found || !match_nth_formula(idx, arg)) return false;
            } else if (name == "first-of-type") {
                std::shared_ptr<Element> first;
                for (const auto& sib : sibling_elements) {
                    if (sib->tag_name() == element->tag_name()) { first = sib; break; }
                }
                if (first != element) return false;
            } else if (name == "last-of-type") {
                std::shared_ptr<Element> last;
                for (const auto& sib : sibling_elements) {
                    if (sib->tag_name() == element->tag_name()) last = sib;
                }
                if (last != element) return false;
            } else if (name == "only-of-type") {
                int count = 0;
                bool mine = false;
                for (const auto& sib : sibling_elements) {
                    if (sib->tag_name() == element->tag_name()) {
                        ++count;
                        if (sib == element) mine = true;
                    }
                }
                if (!mine || count != 1) return false;
            } else if (name == "nth-of-type" || name == "nth-last-of-type") {
                std::vector<std::shared_ptr<Element>> of_type;
                for (const auto& sib : sibling_elements) {
                    if (sib->tag_name() == element->tag_name()) of_type.push_back(sib);
                }
                int idx = 1;
                bool found = false;
                for (size_t i = 0; i < of_type.size(); ++i) {
                    if (of_type[i] == element) {
                        idx = (name == "nth-last-of-type")
                                  ? static_cast<int>(of_type.size() - i)
                                  : static_cast<int>(i) + 1;
                        found = true;
                        break;
                    }
                }
                if (!found || !match_nth_formula(idx, arg)) return false;
            } else if (name == "empty") {
                for (const auto& ch : element->children()) {
                    if (ch->is_element()) return false;
                    if (ch->is_text() && !ch->text_content().empty()) return false;
                }
            } else if (name == "root") {
                if (!parent || !parent->is_document()) return false;
            } else if (name == "hover") {
                if (!element->is_hovered()) return false;
            } else if (name == "focus") {
                if (!element->is_focused()) return false;
            } else if (name == "active") {
                if (!element->is_active()) return false;
            } else if (name == "disabled") {
                if (!forms::is_disabled(*element)) return false;
            } else if (name == "enabled") {
                if (!forms::can_be_disabled(*element) || forms::is_disabled(*element)) return false;
            } else if (name == "checked") {
                // The control's state now, not its `checked` attribute.
                if (!forms::matches_checked(*element)) return false;
            } else if (name == "not") {
                if (!arg.empty()) {
                    if (pc.compiled_arg) {
                        if (pc.compiled_arg->matches(*element)) return false;
                    } else {
                        bool matched_sub = false;
                        size_t comb_count = 0;
                        for (char c : arg) {
                            if (c == ' ' || c == '>' || c == '+' || c == '~') ++comb_count;
                        }
                        if (arg.size() <= 512 && comb_count <= 16) {
                            SelectorList list;
                            if (SelectorList::parse(arg, list)) {
                                if (list.matches(*element)) return false;
                                matched_sub = true;
                            }
                        }
                        if (!matched_sub) {
                            SimpleSelector sub_sel;
                            if (arg[0] == '.') sub_sel.classes.push_back(arg.substr(1));
                            else if (arg[0] == '#') sub_sel.id = arg.substr(1);
                            else sub_sel.tag_name = arg;
                            if (matches_simple_selector(element, sub_sel)) return false;
                        }
                    }
                }
            } else if (name == "is" || name == "where" || name == "matches") {
                if (pc.compiled_arg) {
                    if (!pc.compiled_arg->matches(*element)) return false;
                } else {
                    size_t comb_count = 0;
                    for (char c : arg) {
                        if (c == ' ' || c == '>' || c == '+' || c == '~') ++comb_count;
                    }
                    SelectorList list;
                    if (arg.size() > 512 || comb_count > 16 || !SelectorList::parse(arg, list) || !list.matches(*element)) {
                        return false;
                    }
                }
            } else if (name == "host") {
                // Host pseudo-class matches if this element has a shadow root
                if (!element->shadow_root()) return false;
                if (!arg.empty()) {
                    if (pc.compiled_arg) {
                        if (!pc.compiled_arg->matches(*element)) return false;
                    } else {
                        SelectorList list;
                        if (arg.size() <= 512 && SelectorList::parse(arg, list)) {
                            if (!list.matches(*element)) return false;
                        } else {
                            SimpleSelector sub_sel;
                            if (arg[0] == '.') sub_sel.classes.push_back(arg.substr(1));
                            else if (arg[0] == '#') sub_sel.id = arg.substr(1);
                            else sub_sel.tag_name = arg;
                            if (!matches_simple_selector(element, sub_sel)) return false;
                        }
                    }
                }
            } else if (name == "before" || name == "after" || name == "first-line" || name == "first-letter") {
                return false;
            }
        }
    }

    return true;
}

bool StyleResolver::matches_selector(const std::shared_ptr<Element>& element, const Selector& selector) const {
    return match_selector_impl(element, selector, nullptr);
}

bool StyleResolver::match_selector_impl(const std::shared_ptr<Element>& element, const Selector& selector, Specificity* out_spec) const {
    if (selector.parts.empty() || selector.has_pseudo_element || !element) return false;

    const size_t n = selector.parts.size();
    if (!matches_simple_selector(element, selector.parts.back())) return false;

    auto add_part_spec = [](Specificity& total, const SimpleSelector& part, const Element& el) {
        if (part.compiled) {
            long packed = part.compiled->matching_specificity(el);
            if (packed >= 0) {
                total.ids += static_cast<int>(packed / 1000000L);
                total.classes += static_cast<int>((packed / 1000L) % 1000L);
                total.tags += static_cast<int>(packed % 1000L);
                return;
            }
        }
        auto s = part.specificity();
        total.ids += s.ids;
        total.classes += s.classes;
        total.tags += s.tags;
    };

    bool any_dynamic = selector.any_dynamic_specificity;
    if (!selector.metadata_computed && out_spec) {
        any_dynamic = false;
        for (const auto& part : selector.parts) {
            if (part.has_dynamic_specificity) {
                any_dynamic = true;
                break;
            }
        }
    }
    if (out_spec && !any_dynamic) {
        *out_spec = selector.specificity();
    }

    if (n == 1) {
        if (out_spec && any_dynamic) {
            Specificity total;
            add_part_spec(total, selector.parts[0], *element);
            *out_spec = total;
        }
        return true;
    }

    CombinatorKind kind = selector.combinator_kind;
    size_t min_anc = selector.min_ancestors;
    if (!selector.metadata_computed) {
        bool all_desc = true;
        bool all_det = true;
        bool all_gsib = true;
        min_anc = 0;
        for (size_t i = 0; i + 1 < n; ++i) {
            Combinator c = (i < selector.combinators.size()) ? selector.combinators[i] : Combinator::Descendant;
            if (c == Combinator::Descendant || c == Combinator::Child) ++min_anc;
            if (c != Combinator::Descendant) all_desc = false;
            if (c != Combinator::Child && c != Combinator::AdjacentSibling) all_det = false;
            if (c != Combinator::GeneralSibling) all_gsib = false;
        }
        if (all_det) kind = CombinatorKind::AllDeterministic;
        else if (all_desc) kind = CombinatorKind::AllDescendant;
        else if (all_gsib) kind = CombinatorKind::AllGeneralSibling;
        else kind = CombinatorKind::Mixed;
    }

    if (min_anc > 0) {
        auto dit = m_depth_cache.find(element.get());
        if (dit != m_depth_cache.end() && dit->second < min_anc) {
            return false;
        }
    }

    auto parent_element = [](const Element& el) -> std::shared_ptr<Element> {
        auto p = el.parent();
        if (p && p->is_element()) return std::static_pointer_cast<Element>(p);
        return nullptr;
    };
    auto prev_element_sibling = [](const Element& el) -> std::shared_ptr<Element> {
        auto s = el.previous_sibling();
        while (s) {
            if (s->is_element()) return std::static_pointer_cast<Element>(s);
            s = s->previous_sibling();
        }
        return nullptr;
    };

    const bool track_spec = (out_spec != nullptr && any_dynamic);
    std::vector<std::shared_ptr<Element>> matched_els;
    if (track_spec) matched_els.resize(n);

    auto finalize_spec = [&]() {
        if (!track_spec) return;
        Specificity total;
        for (size_t i = 0; i < n; ++i) {
            add_part_spec(total, selector.parts[i], *matched_els[i]);
        }
        *out_spec = total;
    };

    if (kind == CombinatorKind::AllDeterministic) {
        std::shared_ptr<Element> curr = element;
        if (track_spec) matched_els[n - 1] = curr;
        for (size_t idx = n - 1; idx > 0; --idx) {
            Combinator comb = (idx - 1 < selector.combinators.size())
                                  ? selector.combinators[idx - 1]
                                  : Combinator::Descendant;
            curr = (comb == Combinator::Child) ? parent_element(*curr) : prev_element_sibling(*curr);
            if (!curr || !matches_simple_selector(curr, selector.parts[idx - 1])) return false;
            if (track_spec) matched_els[idx - 1] = curr;
        }
        finalize_spec();
        return true;
    }

    if (kind == CombinatorKind::AllDescendant) {
        if (n > 32 && !track_spec && m_desc_prefix_cache.size() < 32) {
            auto& pmap = m_desc_prefix_cache[&selector];
            auto par = parent_element(*element);
            size_t par_prefix = 0;
            if (par) {
                std::vector<std::shared_ptr<Element>> chain;
                std::shared_ptr<Element> cur = par;
                while (cur) {
                    auto it = pmap.find(cur.get());
                    if (it != pmap.end() && it->second.parent == cur->parent().get()) {
                        par_prefix = it->second.prefix;
                        break;
                    }
                    chain.push_back(cur);
                    cur = parent_element(*cur);
                }
                for (size_t i = chain.size(); i > 0; --i) {
                    const auto& node = chain[i - 1];
                    if (par_prefix < n - 1 && matches_simple_selector(node, selector.parts[par_prefix])) {
                        ++par_prefix;
                    }
                    pmap[node.get()] = {node->parent().get(), par_prefix};
                }
            }
            return par_prefix >= n - 1;
        }
        std::shared_ptr<Element> curr = parent_element(*element);
        if (track_spec) matched_els[n - 1] = element;
        size_t idx = n - 1;
        while (idx > 0 && curr) {
            if (matches_simple_selector(curr, selector.parts[idx - 1])) {
                if (track_spec) matched_els[idx - 1] = curr;
                --idx;
                if (idx == 0) {
                    finalize_spec();
                    return true;
                }
            }
            curr = parent_element(*curr);
        }
        return false;
    }

    if (kind == CombinatorKind::AllGeneralSibling) {
        std::shared_ptr<Element> curr = prev_element_sibling(*element);
        if (track_spec) matched_els[n - 1] = element;
        size_t idx = n - 1;
        while (idx > 0 && curr) {
            if (matches_simple_selector(curr, selector.parts[idx - 1])) {
                if (track_spec) matched_els[idx - 1] = curr;
                --idx;
                if (idx == 0) {
                    finalize_spec();
                    return true;
                }
            }
            curr = prev_element_sibling(*curr);
        }
        return false;
    }

    // General mixed-combinator matcher: iterative DFS with exact (Element*, idx) failure memoization.
    struct FailKey {
        const Element* el;
        size_t idx;
        bool operator==(const FailKey& o) const { return el == o.el && idx == o.idx; }
    };
    struct FailHash {
        size_t operator()(const FailKey& k) const {
            size_t h = reinterpret_cast<uintptr_t>(k.el);
            h ^= k.idx * 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct Frame {
        size_t idx;
        std::shared_ptr<Element> el;
        bool on_fail_mark;
    };

    std::unordered_set<FailKey, FailHash> failed;
    std::vector<Frame> stack;
    stack.push_back({n - 1, element, false});

    std::vector<std::shared_ptr<Element>> candidates;
    while (!stack.empty()) {
        Frame f = std::move(stack.back());
        stack.pop_back();

        if (f.on_fail_mark) {
            failed.insert({f.el.get(), f.idx});
            continue;
        }

        if (failed.count({f.el.get(), f.idx})) continue;
        if (track_spec) matched_els[f.idx] = f.el;
        if (f.idx == 0) {
            finalize_spec();
            return true;
        }

        const size_t next_idx = f.idx - 1;
        Combinator comb = (next_idx < selector.combinators.size())
                              ? selector.combinators[next_idx]
                              : Combinator::Descendant;

        switch (comb) {
            case Combinator::Child: {
                auto pel = parent_element(*f.el);
                if (pel && !failed.count({pel.get(), next_idx}) &&
                    matches_simple_selector(pel, selector.parts[next_idx])) {
                    stack.push_back({f.idx, std::move(f.el), true});
                    stack.push_back({next_idx, std::move(pel), false});
                } else {
                    failed.insert({f.el.get(), f.idx});
                }
                break;
            }
            case Combinator::AdjacentSibling: {
                auto prev = prev_element_sibling(*f.el);
                if (prev && !failed.count({prev.get(), next_idx}) &&
                    matches_simple_selector(prev, selector.parts[next_idx])) {
                    stack.push_back({f.idx, std::move(f.el), true});
                    stack.push_back({next_idx, std::move(prev), false});
                } else {
                    failed.insert({f.el.get(), f.idx});
                }
                break;
            }
            case Combinator::GeneralSibling: {
                candidates.clear();
                for (auto sib = prev_element_sibling(*f.el); sib; sib = prev_element_sibling(*sib)) {
                    if (!failed.count({sib.get(), next_idx})) {
                        if (matches_simple_selector(sib, selector.parts[next_idx])) {
                            candidates.push_back(std::move(sib));
                        } else {
                            failed.insert({sib.get(), next_idx});
                        }
                    }
                }
                if (!candidates.empty()) {
                    stack.push_back({f.idx, std::move(f.el), true});
                    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
                        stack.push_back({next_idx, std::move(*it), false});
                    }
                } else {
                    failed.insert({f.el.get(), f.idx});
                }
                break;
            }
            case Combinator::Descendant:
            default: {
                candidates.clear();
                for (auto anc = parent_element(*f.el); anc; anc = parent_element(*anc)) {
                    if (!failed.count({anc.get(), next_idx})) {
                        if (matches_simple_selector(anc, selector.parts[next_idx])) {
                            candidates.push_back(std::move(anc));
                        } else {
                            failed.insert({anc.get(), next_idx});
                        }
                    }
                }
                if (!candidates.empty()) {
                    stack.push_back({f.idx, std::move(f.el), true});
                    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
                        stack.push_back({next_idx, std::move(*it), false});
                    }
                } else {
                    failed.insert({f.el.get(), f.idx});
                }
                break;
            }
        }
    }

    return false;
}

float StyleResolver::parse_length(std::string_view val, float default_val) const {
    val = css_trim_view(val);
    if (val.empty() || val == "auto" || val == "none" || val == "normal") return default_val;

    {
        std::string low(val);
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low.find("calc(") != std::string::npos || low.find("min(") != std::string::npos ||
            low.find("max(") != std::string::npos || low.find("clamp(") != std::string::npos) {
            std::string resolved = resolve_calc_functions(std::string(val), m_viewport_width);
            std::string rlow = resolved;
            for (char& c : rlow) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (rlow == "invalid" || rlow.find("calc(") != std::string::npos ||
                rlow.find("min(") != std::string::npos || rlow.find("max(") != std::string::npos ||
                rlow.find("clamp(") != std::string::npos) {
                return default_val;
            }
            return parse_length(resolved, default_val);
        }
    }

    // Split the numeric part from its unit; the unit decides the scale factor.
    std::size_t consumed = 0;
    float number = 0.0f;
    try {
        number = std::stof(std::string(val), &consumed);
    } catch (...) {
        return default_val;
    }

    std::string unit;
    for (std::size_t i = consumed; i < val.size(); ++i) {
        const char c = val[i];
        if (css_is_space(c)) continue;
        if (c == '%') { unit = "%"; break; }
        unit.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (unit.empty() || unit == "px") return number;
    if (unit == "em") return number * m_em_base;
    if (unit == "rem") return number * m_root_font_size;
    if (unit == "ex" || unit == "ch") return number * m_em_base * 0.5f;
    if (unit == "pt") return number * 96.0f / 72.0f;
    if (unit == "pc") return number * 16.0f;
    if (unit == "in") return number * 96.0f;
    if (unit == "cm") return number * 96.0f / 2.54f;
    if (unit == "mm") return number * 96.0f / 25.4f;
    if (unit == "q") return number * 96.0f / 101.6f;
    if (unit == "vw" || unit == "dvw" || unit == "svw" || unit == "lvw") {
        return number * m_viewport_width / 100.0f;
    }
    if (unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh") {
        return number * m_viewport_height / 100.0f;
    }
    if (unit == "vmin" || unit == "dvmin" || unit == "svmin" || unit == "lvmin") {
        return number * std::min(m_viewport_width, m_viewport_height) / 100.0f;
    }
    if (unit == "vmax" || unit == "dvmax" || unit == "svmax" || unit == "lvmax") {
        return number * std::max(m_viewport_width, m_viewport_height) / 100.0f;
    }
    // Percentages need a containing block, which a length parser does not
    // have; they are resolved by the layout stage instead.
    return number;
}

/// True when the last non-space character is '%': the value is a percentage
/// and must be resolved against a containing block at layout time.
static bool is_percentage_value(std::string_view val) {
    for (auto it = val.rbegin(); it != val.rend(); ++it) {
        if (*it == ' ') continue;
        return *it == '%';
    }
    return false;
}

EdgeSizes StyleResolver::parse_box_edges(std::string_view val, EdgeSizes* percent_out) const {
    std::vector<float> nums;
    std::vector<bool> is_pct;
    std::stringstream ss{std::string(val)};
    std::string token;
    while (ss >> token) {
        const bool pct = is_percentage_value(token);
        is_pct.push_back(pct);
        // Percentages keep their raw number here; the pixel edge stays 0.
        nums.push_back(pct ? 0.0f : parse_length(token, 0.0f));
    }

    auto to_edges = [](const std::vector<float>& v, const std::vector<bool>& p, float fallback) {
        std::vector<float> out(4, fallback);
        const std::size_t n = v.size();
        if (n == 1) { out = {v[0], v[0], v[0], v[0]}; }
        else if (n == 2) { out = {v[0], v[1], v[0], v[1]}; }
        else if (n == 3) { out = {v[0], v[1], v[2], v[1]}; }
        else if (n >= 4) { out = {v[0], v[1], v[2], v[3]}; }
        (void)p;
        return out;
    };

    if (percent_out) {
        // Recover the raw percentage numbers for the edges that used one.
        std::vector<float> raw;
        std::stringstream raw_ss{std::string(val)};
        while (raw_ss >> token) {
            float number = 0.0f;
            try { number = std::stof(token); } catch (...) { number = 0.0f; }
            raw.push_back(is_percentage_value(token) ? number : -1.0f);
        }
        const auto edges = to_edges(raw, is_pct, -1.0f);
        *percent_out = EdgeSizes(edges[0], edges[1], edges[2], edges[3]);
    }

    if (nums.size() == 1) {
        return EdgeSizes(nums[0]);
    } else if (nums.size() == 2) {
        return EdgeSizes(nums[0], nums[1], nums[0], nums[1]);
    } else if (nums.size() == 3) {
        return EdgeSizes(nums[0], nums[1], nums[2], nums[1]);
    } else if (nums.size() >= 4) {
        return EdgeSizes(nums[0], nums[1], nums[2], nums[3]);
    }
    return EdgeSizes(0.0f);
}

static ComputedStyle::GridTrackDef parse_single_grid_track(const std::string& token) {
    ComputedStyle::GridTrackDef def;
    if (token.starts_with("minmax(") && token.ends_with(")")) {
        std::string inside = token.substr(7, token.size() - 8);
        size_t comma = inside.find(',');
        if (comma != std::string::npos) {
            std::string min_part = inside.substr(0, comma);
            std::string max_part = inside.substr(comma + 1);
            css_trim(min_part);
            css_trim(max_part);

            if (min_part.ends_with("px")) {
                try { def.min_px = std::stof(min_part.substr(0, min_part.size() - 2)); } catch (...) {}
            } else if (min_part != "0") {
                try { def.min_px = std::stof(min_part); } catch (...) {}
            }

            if (max_part.ends_with("fr")) {
                float fr = 1.0f;
                try { fr = std::stof(max_part.substr(0, max_part.size() - 2)); } catch (...) {}
                def.max_fr = std::max(0.1f, fr);
            } else if (max_part == "auto") {
                def.max_fr = 1.0f;
            } else if (max_part.ends_with("px")) {
                try { def.max_px = std::stof(max_part.substr(0, max_part.size() - 2)); } catch (...) {}
            } else {
                try { def.max_px = std::stof(max_part); } catch (...) {}
            }
        }
        return def;
    }

    if (token.ends_with("fr")) {
        std::string num_part = token.substr(0, token.size() - 2);
        float fr = 1.0f;
        try { fr = std::stof(num_part); } catch (...) {}
        def.max_fr = std::max(0.1f, fr);
    } else if (token == "auto") {
        def.max_fr = 1.0f;
    } else {
        float px = 0.0f;
        try { px = std::stof(token); } catch (...) {}
        def.min_px = px;
        def.max_px = px;
    }
    return def;
}

static void parse_grid_template(std::string_view val,
                                std::vector<float>& legacy_tracks,
                                std::vector<ComputedStyle::GridTrackDef>& defs,
                                bool& is_auto_fit,
                                ComputedStyle::GridTrackDef& auto_fit_def) {
    legacy_tracks.clear();
    defs.clear();
    is_auto_fit = false;

    std::string s(val);

    size_t rep_pos = s.find("repeat(");
    while (rep_pos != std::string::npos) {
        int depth = 0;
        size_t close_pos = std::string::npos;
        for (size_t i = rep_pos; i < s.size(); ++i) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') {
                depth--;
                if (depth == 0) {
                    close_pos = i;
                    break;
                }
            }
        }

        if (close_pos != std::string::npos) {
            std::string inside = s.substr(rep_pos + 7, close_pos - (rep_pos + 7));
            size_t comma = inside.find(',');
            if (comma != std::string::npos) {
                std::string count_str = inside.substr(0, comma);
                std::string item_str = inside.substr(comma + 1);
                css_trim(count_str);
                css_trim(item_str);

                if (count_str == "auto-fit" || count_str == "auto-fill") {
                    is_auto_fit = true;
                    auto_fit_def = parse_single_grid_track(item_str);
                    auto_fit_def.is_auto_fit = (count_str == "auto-fit");
                    auto_fit_def.is_auto_fill = (count_str == "auto-fill");
                    s.erase(rep_pos, close_pos - rep_pos + 1);
                    break;
                } else {
                    int count = 1;
                    try { count = std::stoi(count_str); } catch (...) {}
                    std::string rep_expanded;
                    for (int i = 0; i < count; ++i) {
                        if (i > 0) rep_expanded += " ";
                        rep_expanded += item_str;
                    }
                    s.replace(rep_pos, close_pos - rep_pos + 1, rep_expanded);
                }
            }
        }
        rep_pos = s.find("repeat(", rep_pos + 1);
    }

    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
        if (i >= s.size()) break;
        size_t token_start = i;
        int pdepth = 0;
        while (i < s.size()) {
            if (s[i] == '(') pdepth++;
            else if (s[i] == ')') pdepth--;
            else if (std::isspace(static_cast<unsigned char>(s[i])) && pdepth == 0) {
                break;
            }
            i++;
        }
        std::string token = s.substr(token_start, i - token_start);
        if (!token.empty() && token.front() == '[') continue; // a line name is not a track
        auto def = parse_single_grid_track(token);
        defs.push_back(def);

        if (def.max_fr > 0.0f) {
            legacy_tracks.push_back(-def.max_fr);
        } else {
            legacy_tracks.push_back(def.min_px);
        }
    }
}

static std::vector<FilterFunction> parse_css_filters(std::string_view val) {
    std::vector<FilterFunction> filters;
    auto split_paren_tokens = [](std::string_view text) {
        std::vector<std::string> tokens;
        std::string current;
        int depth = 0;
        for (char c : text) {
            if (c == '(') ++depth;
            else if (c == ')' && depth > 0) --depth;
            if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
                if (!current.empty()) { tokens.push_back(std::move(current)); current.clear(); }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) tokens.push_back(std::move(current));
        return tokens;
    };

    std::string s(val);
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) i++;
        if (i >= s.size()) break;
        size_t open_p = s.find('(', i);
        if (open_p == std::string::npos) break;
        std::string name = s.substr(i, open_p - i);
        css_trim(name);

        // Match the closing ')' accounting for nested function calls such as
        // drop-shadow(2px 4px oklch(0.6 0.2 30)) or rgba(0, 0, 0, 0.5).
        size_t close_p = std::string::npos;
        int depth = 0;
        for (size_t k = open_p; k < s.size(); ++k) {
            if (s[k] == '(') ++depth;
            else if (s[k] == ')') {
                --depth;
                if (depth == 0) { close_p = k; break; }
            }
        }
        if (close_p == std::string::npos) break;
        std::string arg = s.substr(open_p + 1, close_p - (open_p + 1));
        css_trim(arg);

        FilterFunction fn;
        if (name == "blur") {
            fn.type = FilterType::Blur;
            try { fn.blur_radius = std::stof(arg); } catch (...) {}
            fn.amount = fn.blur_radius;
        } else if (name == "brightness") {
            fn.type = FilterType::Brightness;
            float v = 1.0f;
            if (!arg.empty() && arg.back() == '%') {
                try { v = std::stof(arg.substr(0, arg.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                try { v = std::stof(arg); } catch (...) {}
            }
            fn.amount = v;
        } else if (name == "contrast") {
            fn.type = FilterType::Contrast;
            float v = 1.0f;
            if (!arg.empty() && arg.back() == '%') {
                try { v = std::stof(arg.substr(0, arg.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                try { v = std::stof(arg); } catch (...) {}
            }
            fn.amount = v;
        } else if (name == "grayscale") {
            fn.type = FilterType::Grayscale;
            float v = 1.0f;
            if (!arg.empty() && arg.back() == '%') {
                try { v = std::stof(arg.substr(0, arg.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                try { v = std::stof(arg); } catch (...) {}
            }
            fn.amount = v;
        } else if (name == "sepia") {
            fn.type = FilterType::Sepia;
            float v = 1.0f;
            if (!arg.empty() && arg.back() == '%') {
                try { v = std::stof(arg.substr(0, arg.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                try { v = std::stof(arg); } catch (...) {}
            }
            fn.amount = v;
        } else if (name == "invert") {
            fn.type = FilterType::Invert;
            float v = 1.0f;
            if (!arg.empty() && arg.back() == '%') {
                try { v = std::stof(arg.substr(0, arg.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                try { v = std::stof(arg); } catch (...) {}
            }
            fn.amount = v;
        } else if (name == "opacity") {
            fn.type = FilterType::Opacity;
            float v = 1.0f;
            if (!arg.empty() && arg.back() == '%') {
                try { v = std::stof(arg.substr(0, arg.size() - 1)) / 100.0f; } catch (...) {}
            } else {
                try { v = std::stof(arg); } catch (...) {}
            }
            fn.amount = v;
        } else if (name == "drop-shadow") {
            fn.type = FilterType::DropShadow;
            fn.color = Color::black();
            const auto toks = split_paren_tokens(arg);
            std::vector<float> lengths;
            for (const auto& tok : toks) {
                Color parsed;
                if (Color::looks_like_color(tok) && Color::try_parse(tok, parsed)) {
                    fn.color = parsed;
                } else {
                    try { lengths.push_back(std::stof(tok)); } catch (...) {}
                }
            }
            if (lengths.size() >= 2) {
                fn.dx = lengths[0];
                fn.dy = lengths[1];
                if (lengths.size() >= 3) fn.blur_radius = lengths[2];
            }
        }
        if (fn.type != FilterType::None) {
            filters.push_back(fn);
        }
        i = close_p + 1;
    }
    return filters;
}

static void parse_grid_template_areas(std::string_view val, std::unordered_map<std::string, ComputedStyle::GridAreaRect>& areas) {
    areas.clear();
    std::string s(val);
    int current_row = 1;
    size_t i = 0;
    while (i < s.size()) {
        size_t q_start = s.find('"', i);
        if (q_start == std::string::npos) {
            q_start = s.find('\'', i);
        }
        if (q_start == std::string::npos) break;
        char quote = s[q_start];
        size_t q_end = s.find(quote, q_start + 1);
        if (q_end == std::string::npos) break;

        std::string row_str = s.substr(q_start + 1, q_end - (q_start + 1));
        std::stringstream ss(row_str);
        std::string cell;
        int current_col = 1;
        while (ss >> cell) {
            if (cell != "." && cell != "...") {
                auto it = areas.find(cell);
                if (it == areas.end()) {
                    ComputedStyle::GridAreaRect r;
                    r.row_start = current_row;
                    r.row_end = current_row + 1;
                    r.col_start = current_col;
                    r.col_end = current_col + 1;
                    areas[cell] = r;
                } else {
                    it->second.row_start = std::min(it->second.row_start, current_row);
                    it->second.row_end = std::max(it->second.row_end, current_row + 1);
                    it->second.col_start = std::min(it->second.col_start, current_col);
                    it->second.col_end = std::max(it->second.col_end, current_col + 1);
                }
            }
            current_col++;
        }
        current_row++;
        i = q_end + 1;
    }
}

bool StyleResolver::expand_shorthand(const std::string& prop, const std::string& val,
                                      std::vector<std::pair<std::string, std::string>>& out) const {
    auto trim = [](std::string_view text) { return css_trim_view(text); };
    auto split_tokens = [&](std::string_view text) {
        std::vector<std::string> tokens;
        std::string current;
        int depth = 0;
        for (char c : text) {
            if (c == '(') ++depth;
            if (c == ')') --depth;
            if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
                if (!current.empty()) { tokens.push_back(current); current.clear(); }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) tokens.push_back(current);
        return tokens;
    };
    auto is_color_token = [](const std::string& token) {
        return Color::looks_like_color(token);
    };

    // One-to-four value box shorthands: top, right, bottom, left.
    if (prop == "margin" || prop == "padding" || prop == "border-width" || prop == "border-color") {
        auto tokens = split_tokens(trim(val));
        if (tokens.empty()) return true;
        std::string top, right, bottom, left;
        if (prop == "margin" || prop == "padding") {
            top = prop + "-top"; right = prop + "-right";
            bottom = prop + "-bottom"; left = prop + "-left";
        } else if (prop == "border-width") {
            top = "border-top-width"; right = "border-right-width";
            bottom = "border-bottom-width"; left = "border-left-width";
        } else {
            top = "border-top-color"; right = "border-right-color";
            bottom = "border-bottom-color"; left = "border-left-color";
        }
        switch (tokens.size()) {
            case 1:
                out.emplace_back(top, tokens[0]); out.emplace_back(right, tokens[0]);
                out.emplace_back(bottom, tokens[0]); out.emplace_back(left, tokens[0]);
                break;
            case 2:
                out.emplace_back(top, tokens[0]); out.emplace_back(right, tokens[1]);
                out.emplace_back(bottom, tokens[0]); out.emplace_back(left, tokens[1]);
                break;
            case 3:
                out.emplace_back(top, tokens[0]); out.emplace_back(right, tokens[1]);
                out.emplace_back(bottom, tokens[2]); out.emplace_back(left, tokens[1]);
                break;
            default:
                out.emplace_back(top, tokens[0]); out.emplace_back(right, tokens[1]);
                out.emplace_back(bottom, tokens[2]); out.emplace_back(left, tokens[3]);
                break;
        }
        return true;
    }

    // border-radius collapses to the single radius the renderer supports.
    if (prop == "border-radius") {
        auto tokens = split_tokens(trim(val));
        if (!tokens.empty()) out.emplace_back("border-radius", tokens[0]);
        return true;
    }

    // inset / scroll-* have no renderer support yet.
    if (prop == "inset" || prop == "scroll-margin" || prop == "scroll-padding") {
        return true;
    }

    // border / border-top / ... : width, style and colour in any order.
    static const std::unordered_set<std::string> border_sides = {
        "border", "border-top", "border-right", "border-bottom", "border-left"};
    if (border_sides.count(prop)) {
        auto tokens = split_tokens(trim(val));
        bool saw_none_or_hidden = false;
        bool saw_width = false;
        for (const auto& raw_token : tokens) {
            const std::string token(trim(raw_token));
            if (token.empty()) continue;
            std::string lowered;
            for (char c : token) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lowered == "none" || lowered == "hidden") {
                saw_none_or_hidden = true;
                continue;
            }
            if (lowered == "solid" || lowered == "dashed" || lowered == "dotted" ||
                lowered == "double" || lowered == "groove" || lowered == "ridge" ||
                lowered == "inset" || lowered == "outset") {
                continue;  // border styles are not rendered yet
            }
            if (is_color_token(token)) {
                out.emplace_back(prop == "border" ? "border-color" : prop + "-color", token);
            } else if (lowered == "thin") {
                saw_width = true;
                out.emplace_back(prop == "border" ? "border-width" : prop + "-width", "1px");
            } else if (lowered == "medium") {
                saw_width = true;
                out.emplace_back(prop == "border" ? "border-width" : prop + "-width", "3px");
            } else if (lowered == "thick") {
                saw_width = true;
                out.emplace_back(prop == "border" ? "border-width" : prop + "-width", "5px");
            } else {
                saw_width = true;
                out.emplace_back(prop == "border" ? "border-width" : prop + "-width", token);
            }
        }
        if (saw_none_or_hidden && !saw_width) {
            out.emplace_back(prop == "border" ? "border-width" : prop + "-width", "0px");
        }
        return true;
    }

    // background: only the colour component is honoured today.
    if (prop == "background") {
        for (const auto& token : split_tokens(trim(val))) {
            if (token.rfind("url(", 0) == 0 || token == "no-repeat" || token == "repeat" ||
                token.rfind("linear-gradient(", 0) == 0) {
                continue;
            }
            if (is_color_token(token)) { out.emplace_back("background-color", token); break; }
        }
        return true;
    }

    // font: weight, style, variant, size, optional /line-height, then the
    // family list -- which is the one component CSS puts last and the one this
    // used to drop on the floor: `font: 16px monospace` set a size and left the
    // family empty, so the element was laid out in the default face however
    // emphatically the shorthand had asked for something else.
    if (prop == "font") {
        std::string family;
        bool saw_size = false;
        for (const auto& token : split_tokens(trim(val))) {
            std::string lowered;
            for (char c : token) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lowered == "bold" || lowered == "bolder") out.emplace_back("font-weight", "700");
            else if (lowered == "normal" || lowered == "lighter") out.emplace_back("font-weight", "400");
            // The shorthand used to throw this token away, so `font: italic
            // 16px serif` set a size and a family and no slant at all -- the
            // one property in the shorthand with no place to be kept. It is
            // kept now, in the same longhand the expanded form sets.
            else if (lowered == "italic") out.emplace_back("font-style", "italic");
            else if (lowered == "oblique") out.emplace_back("font-style", "oblique");
            else if (lowered == "small-caps") continue;
            else {
                std::string size_part = token;
                std::string lh_part;
                auto slash = token.find('/');
                if (slash != std::string::npos) {
                    size_part = token.substr(0, slash);
                    lh_part = token.substr(slash + 1);
                }
                std::string size_low;
                for (char c : size_part) size_low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (!size_low.empty() && size_low.find_first_of("0123456789") != std::string::npos &&
                    (size_low.ends_with("px") || size_low.ends_with("em") || size_low.ends_with("rem") ||
                     size_low.ends_with("pt") || size_low.ends_with("%") || size_low.ends_with("vw") ||
                     size_low.ends_with("vh") || size_low.ends_with("dvh") || size_low.ends_with("dvw"))) {
                    out.emplace_back("font-size", size_part);
                    if (!lh_part.empty()) out.emplace_back("line-height", lh_part);
                    saw_size = true;
                } else if (saw_size) {
                    // Everything after the size is the family list. It is put
                    // back together with single spaces, which is not the same
                    // string that was written and is the same list: matching a
                    // family ignores that spacing anyway.
                    if (!family.empty()) family += ' ';
                    family += token;
                }
            }
        }
        if (!family.empty()) out.emplace_back("font-family", family);
        return true;
    }

    if (prop == "flex-flow") {
        for (const auto& token : split_tokens(trim(val))) {
            const std::string lowered = [&] {
                std::string out_token;
                for (char c : token) out_token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                return out_token;
            }();
            if (lowered == "wrap" || lowered == "nowrap" || lowered == "wrap-reverse") {
                out.emplace_back("flex-wrap", lowered);
            } else if (lowered == "row" || lowered == "column") {
                out.emplace_back("flex-direction", lowered);
            } else if (lowered == "row-reverse" || lowered == "column-reverse") {
                out.emplace_back("flex-direction", lowered);
            }
        }
        return true;
    }

    return false;
}

void StyleResolver::apply_declaration(ComputedStyle& style, const std::string& prop, const std::string& raw_val) const {
    if (prop.starts_with("--")) {
        style.custom_properties[prop] = raw_val;
        return;
    }
    std::string resolved_val = raw_val;
    if (resolved_val.find("var(") != std::string::npos) {
        resolved_val = resolve_var_functions(resolved_val, style.custom_properties);
    }
    // CSS custom properties are not shorthands.
    std::vector<std::pair<std::string, std::string>> longhands;
    if (expand_shorthand(prop, resolved_val, longhands)) {
        for (const auto& [longhand, longhand_value] : longhands) {
            apply_longhand(style, longhand, longhand_value);
        }
        return;
    }
    apply_longhand(style, prop, resolved_val);
}

void StyleResolver::apply_longhand(ComputedStyle& style, const std::string& prop, const std::string& raw_val) const {
    if (prop.starts_with("--")) {
        style.custom_properties[prop] = raw_val;
        return;
    }

    std::string val = raw_val;
    if (val.find("var(") != std::string::npos) {
        val = resolve_var_functions(val, style.custom_properties);
    }
    if (val.find('(') != std::string::npos) {
        val = resolve_calc_functions(val, m_viewport_width);
    }
    css_trim(val);
    std::string val_lower = val;
    for (char& c : val_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (prop == "display") {
        if (val_lower == "block" || val_lower == "list-item") style.display = Display::Block;
        else if (val_lower == "inline") style.display = Display::Inline;
        else if (val_lower == "flex" || val_lower == "inline-flex") style.display = Display::Flex;
        else if (val_lower == "grid" || val_lower == "inline-grid") style.display = Display::Grid;
        else if (val_lower == "none") style.display = Display::None;
        else if (val_lower == "inline-block") style.display = Display::InlineBlock;
        else if (val_lower == "table") style.display = Display::Table;
        else if (val_lower == "table-row") style.display = Display::TableRow;
        else if (val_lower == "table-cell") style.display = Display::TableCell;
        else if (val_lower == "table-row-group" || val_lower == "table-header-group" ||
                 val_lower == "table-footer-group") style.display = Display::TableRowGroup;
        else if (val_lower == "table-caption") style.display = Display::TableCaption;
        else if (val_lower == "table-column" || val_lower == "table-column-group") style.display = Display::None;
    } else if (prop == "float") {
        if (val == "left") style.float_type = FloatType::Left;
        else if (val == "right") style.float_type = FloatType::Right;
        else style.float_type = FloatType::None;
    } else if (prop == "overflow" || prop == "overflow-x" || prop == "overflow-y") {
        if (val == "hidden") style.overflow = Overflow::Hidden;
        else if (val == "auto") style.overflow = Overflow::Auto;
        else if (val == "scroll") style.overflow = Overflow::Scroll;
        else style.overflow = Overflow::Visible;
    } else if (prop == "table-layout") {
        style.table_layout = (val == "fixed") ? TableLayout::Fixed : TableLayout::Auto;
    } else if (prop == "empty-cells") {
        style.empty_cells = (val == "hide") ? EmptyCells::Hide : EmptyCells::Show;
    } else if (prop == "caption-side") {
        style.caption_side = (val == "bottom") ? CaptionSide::Bottom : CaptionSide::Top;
    } else if (prop == "border-collapse") {
        style.border_collapse = (val == "collapse") ? BorderCollapse::Collapse : BorderCollapse::Separate;
    } else if (prop == "border-spacing") {
        std::stringstream ss(val);
        std::string first, second;
        if (ss >> first) {
            style.border_spacing_x = parse_length(first, 0.0f);
            style.border_spacing_y = style.border_spacing_x;
            if (ss >> second) style.border_spacing_y = parse_length(second, 0.0f);
        }
    } else if (prop == "clear") {
        if (val == "left") style.clear_type = ClearType::Left;
        else if (val == "right") style.clear_type = ClearType::Right;
        else if (val == "both") style.clear_type = ClearType::Both;
        else style.clear_type = ClearType::None;
    } else if (prop == "flex-direction") {
        if (val == "row") style.flex_direction = FlexDirection::Row;
        else if (val == "column") style.flex_direction = FlexDirection::Column;
        else if (val == "row-reverse") style.flex_direction = FlexDirection::RowReverse;
        else if (val == "column-reverse") style.flex_direction = FlexDirection::ColumnReverse;
    } else if (prop == "flex-wrap") {
        if (val == "wrap") style.flex_wrap = FlexWrap::Wrap;
        else if (val == "wrap-reverse") style.flex_wrap = FlexWrap::WrapReverse;
        else if (val == "nowrap") style.flex_wrap = FlexWrap::NoWrap;
    } else if (prop == "justify-content") {
        if (val == "flex-start" || val == "start") style.justify_content = JustifyContent::FlexStart;
        else if (val == "flex-end" || val == "end") style.justify_content = JustifyContent::FlexEnd;
        else if (val == "center") style.justify_content = JustifyContent::Center;
        else if (val == "space-between") style.justify_content = JustifyContent::SpaceBetween;
        else if (val == "space-around") style.justify_content = JustifyContent::SpaceAround;
        else if (val == "space-evenly") style.justify_content = JustifyContent::SpaceEvenly;
    } else if (prop == "align-items") {
        if (val == "stretch") style.align_items = AlignItems::Stretch;
        else if (val == "flex-start" || val == "start") style.align_items = AlignItems::FlexStart;
        else if (val == "flex-end" || val == "end") style.align_items = AlignItems::FlexEnd;
        else if (val == "center") style.align_items = AlignItems::Center;
    } else if (prop == "align-content") {
        if (val == "stretch") style.align_content = AlignContent::Stretch;
        else if (val == "flex-start" || val == "start") style.align_content = AlignContent::FlexStart;
        else if (val == "flex-end" || val == "end") style.align_content = AlignContent::FlexEnd;
        else if (val == "center") style.align_content = AlignContent::Center;
        else if (val == "space-between") style.align_content = AlignContent::SpaceBetween;
        else if (val == "space-around") style.align_content = AlignContent::SpaceAround;
        else if (val == "space-evenly") style.align_content = AlignContent::SpaceEvenly;
    } else if (prop == "grid-template-columns") {
        parse_grid_template(val, style.grid_template_columns, style.grid_col_track_defs,
                            style.grid_auto_fit_columns, style.grid_auto_fit_def);
    } else if (prop == "grid-template-rows") {
        bool dummy_af = false;
        ComputedStyle::GridTrackDef dummy_def;
        parse_grid_template(val, style.grid_template_rows, style.grid_row_track_defs,
                            dummy_af, dummy_def);
    } else if (prop == "grid-gap" || prop == "gap") {
        std::stringstream ss(val);
        std::string part1, part2;
        ss >> part1;
        if (ss >> part2) {
            style.grid_row_gap = parse_length(part1, 0.0f);
            style.grid_column_gap = parse_length(part2, 0.0f);
            style.gap = style.grid_row_gap;
        } else {
            style.gap = style.grid_column_gap = style.grid_row_gap = parse_length(val, 0.0f);
        }
    } else if (prop == "column-gap" || prop == "grid-column-gap") {
        style.grid_column_gap = parse_length(val, 0.0f);
    } else if (prop == "row-gap" || prop == "grid-row-gap") {
        style.grid_row_gap = parse_length(val, 0.0f);
    } else if (prop == "flex-grow") {
        try { style.flex_grow = std::stof(val); } catch (...) {}
    } else if (prop == "flex-shrink") {
        try { style.flex_shrink = std::stof(val); } catch (...) {}
    } else if (prop == "flex-basis") {
        style.flex_basis = parse_length(val, -1.0f);
    } else if (prop == "flex") {
        std::stringstream ss(val);
        std::string p1, p2, p3;
        if (ss >> p1) {
            try { style.flex_grow = std::stof(p1); } catch (...) {}
            if (ss >> p2) {
                try { style.flex_shrink = std::stof(p2); } catch (...) {}
                if (ss >> p3) {
                    style.flex_basis = parse_length(p3, -1.0f);
                }
            }
        }
    } else if (prop == "color-scheme") {
        if (val_lower == "dark" || val_lower == "only dark") {
            style.dark_color_scheme = true;
        } else if (val_lower != "inherit") {
            style.dark_color_scheme = false;
        }
    } else if (prop == "color") {
        if (val_lower == "initial") {
            style.color = Color::black();
        } else if (val_lower != "inherit" && val_lower != "currentcolor" && val_lower != "unset") {
            Color parsed;
            if (Color::try_parse(val, parsed, style.color, style.dark_color_scheme)) {
                style.color = parsed;
            }
        }
    } else if (prop == "background-color" || prop == "background") {
        Color parsed;
        if (Color::try_parse(val, parsed, style.color, style.dark_color_scheme)) {
            style.background_color = parsed;
        }
    } else if (prop == "width" || prop == "height" || prop == "min-width" ||
               prop == "max-width" || prop == "min-height" || prop == "max-height") {
        // A percentage is recorded as a percentage and left "auto" in pixels;
        // layout resolves it against the containing block.
        const bool pct = is_percentage_value(val);
        const float length = parse_length(val, -1.0f);
        float* pixels = &style.width;
        float* percent = &style.width_percent;
        if (prop == "height") { pixels = &style.height; percent = &style.height_percent; }
        else if (prop == "min-width") { pixels = &style.min_width; percent = &style.min_width_percent; }
        else if (prop == "max-width") { pixels = &style.max_width; percent = &style.max_width_percent; }
        else if (prop == "min-height") { pixels = &style.min_height; percent = &style.min_height_percent; }
        else if (prop == "max-height") { pixels = &style.max_height; percent = &style.max_height_percent; }
        if (pct) {
            *percent = length;
            *pixels = -1.0f;
        } else {
            *pixels = length;
            *percent = -1.0f;
        }
    } else if (prop == "box-sizing") {
        if (val == "border-box") style.box_sizing = BoxSizing::BorderBox;
        else if (val == "content-box") style.box_sizing = BoxSizing::ContentBox;
    } else if (prop == "position") {
        if (val == "static") style.position = Position::Static;
        else if (val == "relative") style.position = Position::Relative;
        else if (val == "absolute") style.position = Position::Absolute;
        else if (val == "fixed") style.position = Position::Fixed;
    } else if (prop == "top") {
        style.top = parse_length(val, 0.0f);
        style.has_top = true;
    } else if (prop == "right") {
        style.right = parse_length(val, 0.0f);
        style.has_right = true;
    } else if (prop == "bottom") {
        style.bottom = parse_length(val, 0.0f);
        style.has_bottom = true;
    } else if (prop == "left") {
        style.left = parse_length(val, 0.0f);
        style.has_left = true;
    } else if (prop == "z-index") {
        try {
            style.z_index = std::stoi(val);
            style.has_z_index = true;
        } catch (...) {}
    } else if (prop == "margin") {
        std::stringstream mss(val);
        std::vector<std::string> tokens;
        std::string tok;
        while (mss >> tok) tokens.push_back(tok);
        style.margin = parse_box_edges(val, &style.margin_percent);
        if (tokens.size() == 1 && tokens[0] == "auto") {
            style.margin_top_auto = style.margin_right_auto = style.margin_bottom_auto = style.margin_left_auto = true;
        } else if (tokens.size() == 2) {
            if (tokens[0] == "auto") style.margin_top_auto = style.margin_bottom_auto = true;
            if (tokens[1] == "auto") style.margin_left_auto = style.margin_right_auto = true;
        } else if (tokens.size() >= 4) {
            if (tokens[0] == "auto") style.margin_top_auto = true;
            if (tokens[1] == "auto") style.margin_right_auto = true;
            if (tokens[2] == "auto") style.margin_bottom_auto = true;
            if (tokens[3] == "auto") style.margin_left_auto = true;
        }
    } else if (prop == "margin-top" || prop == "margin-right" ||
               prop == "margin-bottom" || prop == "margin-left") {
        bool* is_auto = nullptr;
        float* pixels = nullptr;
        float* percent = nullptr;
        if (prop == "margin-top") { is_auto = &style.margin_top_auto; pixels = &style.margin.top; percent = &style.margin_percent.top; }
        else if (prop == "margin-right") { is_auto = &style.margin_right_auto; pixels = &style.margin.right; percent = &style.margin_percent.right; }
        else if (prop == "margin-bottom") { is_auto = &style.margin_bottom_auto; pixels = &style.margin.bottom; percent = &style.margin_percent.bottom; }
        else { is_auto = &style.margin_left_auto; pixels = &style.margin.left; percent = &style.margin_percent.left; }

        if (val == "auto") {
            *is_auto = true;
        } else if (is_percentage_value(val)) {
            *percent = parse_length(val, 0.0f);   // resolved by layout
            *pixels = 0.0f;
        } else {
            *pixels = parse_length(val, 0.0f);
            *percent = -1.0f;
        }
    } else if (prop == "padding") {
        style.padding = parse_box_edges(val, &style.padding_percent);
    } else if (prop == "padding-top" || prop == "padding-right" ||
               prop == "padding-bottom" || prop == "padding-left") {
        float* pixels = nullptr;
        float* percent = nullptr;
        if (prop == "padding-top") { pixels = &style.padding.top; percent = &style.padding_percent.top; }
        else if (prop == "padding-right") { pixels = &style.padding.right; percent = &style.padding_percent.right; }
        else if (prop == "padding-bottom") { pixels = &style.padding.bottom; percent = &style.padding_percent.bottom; }
        else { pixels = &style.padding.left; percent = &style.padding_percent.left; }

        if (is_percentage_value(val)) {
            *percent = parse_length(val, 0.0f);   // resolved by layout
            *pixels = 0.0f;
        } else {
            *pixels = parse_length(val, 0.0f);
            *percent = -1.0f;
        }
    } else if (prop == "border-radius") {
        if (is_percentage_value(val)) {
            style.border_radius_percent = parse_length(val, 0.0f);
            style.border_radius = 0.0f;
        } else {
            style.border_radius_percent = -1.0f;
            style.border_radius = parse_length(val, 0.0f);
        }
    } else if (prop == "box-shadow") {
        // Parenthesis-aware token split so `rgba(...)`, `oklch(...)`,
        // `color-mix(...)`, and relative colors stay intact as single tokens.
        std::vector<std::string> toks;
        {
            std::string cur;
            int depth = 0;
            for (char c : val) {
                if (c == '(') ++depth;
                else if (c == ')' && depth > 0) --depth;
                if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
                    if (!cur.empty()) { toks.push_back(std::move(cur)); cur.clear(); }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) toks.push_back(std::move(cur));
        }
        std::vector<float> lengths;
        Color shadow_color(0, 0, 0, 100);
        for (const auto& tok : toks) {
            std::string low;
            for (char c : tok) low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (low == "inset" || low == "none") continue;
            Color parsed;
            if (Color::looks_like_color(tok) &&
                Color::try_parse(tok, parsed, style.color, style.dark_color_scheme)) {
                shadow_color = parsed;
            } else {
                lengths.push_back(parse_length(tok, 0.0f));
            }
        }
        if (lengths.size() >= 2) {
            style.box_shadow.has_shadow = true;
            style.box_shadow.x_offset = lengths[0];
            style.box_shadow.y_offset = lengths[1];
            style.box_shadow.blur = lengths.size() >= 3 ? lengths[2] : 0.0f;
            style.box_shadow.color = shadow_color;
        }
    } else if (prop == "border") {
        // e.g. "1px solid black" or "2px solid oklch(0.6 0.2 30)"
        std::string cur;
        int depth = 0;
        auto process_part = [&](const std::string& part) {
            if (part.empty()) return;
            std::string low;
            for (char c : part) low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (low == "solid" || low == "none" || low == "dashed" || low == "dotted" ||
                low == "double" || low == "groove" || low == "ridge" ||
                low == "inset" || low == "outset" || low == "hidden") {
                return;
            }
            Color parsed;
            if (Color::looks_like_color(part) &&
                Color::try_parse(part, parsed, style.color, style.dark_color_scheme)) {
                style.border_color = parsed;
            } else if (std::isdigit(static_cast<unsigned char>(part[0]))) {
                style.border_width = EdgeSizes(parse_length(part, 0.0f));
            }
        };
        for (char c : val) {
            if (c == '(') ++depth;
            else if (c == ')' && depth > 0) --depth;
            if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
                process_part(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        process_part(cur);
    } else if (prop == "border-width") {
        style.border_width = parse_box_edges(val);
    } else if (prop == "border-top-width") {
        style.border_width.top = parse_length(val, 0.0f);
    } else if (prop == "border-right-width") {
        style.border_width.right = parse_length(val, 0.0f);
    } else if (prop == "border-bottom-width") {
        style.border_width.bottom = parse_length(val, 0.0f);
    } else if (prop == "border-left-width") {
        style.border_width.left = parse_length(val, 0.0f);
    } else if (prop == "border-color" || prop == "border-top-color" || prop == "border-right-color" ||
               prop == "border-bottom-color" || prop == "border-left-color") {
        Color parsed;
        if (Color::try_parse(val, parsed, style.color, style.dark_color_scheme)) {
            style.border_color = parsed;
        }
    } else if (prop == "font-size") {
        if (val_lower == "inherit" || val_lower == "unset") {
            // Inherited from parent via initial copy
        } else if (val_lower == "initial" || val_lower == "medium") {
            style.font_size = 16.0f;
        } else if (val_lower == "xx-small") {
            style.font_size = 9.0f;
        } else if (val_lower == "x-small") {
            style.font_size = 10.0f;
        } else if (val_lower == "small") {
            style.font_size = 13.0f;
        } else if (val_lower == "large") {
            style.font_size = 18.0f;
        } else if (val_lower == "x-large") {
            style.font_size = 24.0f;
        } else if (val_lower == "xx-large") {
            style.font_size = 32.0f;
        } else if (val_lower == "xxx-large") {
            style.font_size = 48.0f;
        } else if (val_lower == "smaller") {
            style.font_size = m_em_base / 1.2f;
        } else if (val_lower == "larger") {
            style.font_size = m_em_base * 1.2f;
        } else if (is_percentage_value(val)) {
            float pct = parse_length(val, 100.0f);
            style.font_size = std::max(0.0f, m_em_base * (pct / 100.0f));
        } else {
            style.font_size = std::max(0.0f, parse_length(val, 16.0f));
        }
    } else if (prop == "line-height") {
        const std::string text{css_trim_view(val)};
        bool is_number = false;
        float multiplier = 0.0f;
        if (val_lower != "normal" && !is_percentage_value(text)) {
            std::size_t consumed = 0;
            try {
                multiplier = std::stof(text, &consumed);
                is_number = consumed == text.size();
            } catch (...) {}
        }
        if (val_lower == "normal") {
            style.line_height = -1.0f;
            style.line_height_number = -1.0f;
        } else if (is_percentage_value(text)) {
            // A percentage is of the element's own font size.
            style.line_height = style.font_size * parse_length(text, 0.0f) / 100.0f;
            style.line_height_number = -1.0f;
        } else if (is_number) {
            if (multiplier >= 0.0f) {
                style.line_height_number = multiplier;
                style.line_height = -1.0f;
            } // a negative multiplier is not a legal line-height; drop the declaration
        } else {
            const float len = parse_length(text, -1.0f);
            if (len >= 0.0f) {
                style.line_height = len;
                style.line_height_number = -1.0f;
            } // negative lengths are not a legal line-height; drop the declaration
        }
    } else if (prop == "font-weight") {
        if (val_lower == "bold" || val_lower == "bolder") style.font_weight = 700;
        else if (val_lower == "normal" || val_lower == "lighter") style.font_weight = 400;
        else {
            try { style.font_weight = std::stoi(val); } catch (...) {}
        }
    } else if (prop == "font-style") {
        // `oblique` is kept apart from `italic` because a face declares which
        // it is, and `@font-face { font-style: oblique }` has to find the
        // oblique face. When neither can be had the two are answered alike:
        // both are synthesised by the same shear.
        if (val_lower == "italic") style.font_style = FontStyle::Italic;
        else if (val_lower.starts_with("oblique")) style.font_style = FontStyle::Oblique;
        else style.font_style = FontStyle::Normal;
    } else if (prop == "font-family") {
        // Stored as written. `font-family` was on the list of known properties
        // for a long time and nothing ever read the value back — it is read
        // now, by FontRenderer, at the point a string is measured and drawn.
        style.font_family = val;
    } else if (prop == "text-align") {
        parse_text_align(val_lower, style.text_align);
    } else if (prop == "text-decoration") {
        parse_text_decoration(val_lower, style.text_decoration);
    } else if (prop == "opacity") {
        try { style.opacity = std::clamp(std::stof(val), 0.0f, 1.0f); } catch (...) {}
    } else if (prop == "animation-name") {
        style.animation_name = val;
    } else if (prop == "animation-duration") {
        auto parse_sec = [](std::string_view s) -> float {
            std::string str(css_trim_view(s));
            if (str.size() > 2 && str.substr(str.size() - 2) == "ms") {
                try { return std::stof(str.substr(0, str.size() - 2)) / 1000.0f; } catch (...) { return 0.0f; }
            } else if (!str.empty() && str.back() == 's') {
                try { return std::stof(str.substr(0, str.size() - 1)); } catch (...) { return 0.0f; }
            }
            try { return std::stof(str); } catch (...) { return 0.0f; }
        };
        style.animation_duration = parse_sec(val);
    } else if (prop == "animation-timing-function") {
        parse_timing_function(val_lower, style.animation_timing_function);
    } else if (prop == "animation-iteration-count") {
        if (val == "infinite") style.animation_iteration_count = -1.0f;
        else { try { style.animation_iteration_count = std::stof(val); } catch (...) {} }
    } else if (prop == "animation") {
        // e.g. "slidein 2s ease infinite" or "fade 1s"
        std::stringstream ss(val);
        std::string token;
        int idx = 0;
        while (ss >> token) {
            if (idx == 0) {
                style.animation_name = token;
            } else if (token.find('s') != std::string::npos || std::isdigit(token[0])) {
                std::string s = token;
                if (s.size() > 2 && s.substr(s.size() - 2) == "ms") {
                    try { style.animation_duration = std::stof(s.substr(0, s.size() - 2)) / 1000.0f; } catch (...) {}
                } else if (s.back() == 's') {
                    try { style.animation_duration = std::stof(s.substr(0, s.size() - 1)); } catch (...) {}
                }
            } else if (parse_timing_function(token, style.animation_timing_function)) {
                // parsed timing function
            } else if (token == "infinite") {
                style.animation_iteration_count = -1.0f;
            } else if (std::isdigit(token[0])) {
                try { style.animation_iteration_count = std::stof(token); } catch (...) {}
            }
            idx++;
        }
    } else if (prop == "transition-property") {
        style.transition_property = val;
    } else if (prop == "transition-duration") {
        auto parse_sec = [](std::string_view s) -> float {
            std::string str(css_trim_view(s));
            if (str.size() > 2 && str.substr(str.size() - 2) == "ms") {
                try { return std::stof(str.substr(0, str.size() - 2)) / 1000.0f; } catch (...) { return 0.0f; }
            } else if (!str.empty() && str.back() == 's') {
                try { return std::stof(str.substr(0, str.size() - 1)); } catch (...) { return 0.0f; }
            }
            try { return std::stof(str); } catch (...) { return 0.0f; }
        };
        style.transition_duration = parse_sec(val);
    } else if (prop == "transition-timing-function") {
        parse_timing_function(val_lower, style.transition_timing_function);
    } else if (prop == "transition") {
        // e.g. "opacity 0.5s ease"
        std::stringstream ss(val);
        std::string token;
        if (ss >> token) {
            style.transition_property = token;
            if (ss >> token) {
                if (token.size() > 2 && token.substr(token.size() - 2) == "ms") {
                    try { style.transition_duration = std::stof(token.substr(0, token.size() - 2)) / 1000.0f; } catch (...) {}
                } else if (!token.empty() && token.back() == 's') {
                    try { style.transition_duration = std::stof(token.substr(0, token.size() - 1)); } catch (...) {}
                }
                if (ss >> token) {
                    parse_timing_function(token, style.transition_timing_function);
                }
            }
        }
    } else if (prop == "transform") {
        style.transform_str = val;
        style.has_transform = true;
    } else if (prop == "transform-origin") {
        style.transform_origin = val;
    } else if (prop == "filter") {
        if (val == "none") {
            style.filters.clear();
        } else {
            style.filters = parse_css_filters(val);
        }
    } else if (prop == "backdrop-filter" || prop == "-webkit-backdrop-filter") {
        if (val == "none") {
            style.backdrop_filters.clear();
        } else {
            style.backdrop_filters = parse_css_filters(val);
        }
    } else if (prop == "grid-template-areas") {
        parse_grid_template_areas(val, style.grid_named_areas);
    } else if (prop == "grid-area") {
        if (val.find('/') != std::string::npos) {
            std::stringstream ss(val);
            std::string part;
            std::vector<int> lines;
            while (std::getline(ss, part, '/')) {
                css_trim(part);
                try { lines.push_back(std::stoi(part)); } catch (...) { lines.push_back(0); }
            }
            if (lines.size() >= 1) style.grid_row_start = lines[0];
            if (lines.size() >= 2) style.grid_column_start = lines[1];
            if (lines.size() >= 3) style.grid_row_end = lines[2];
            if (lines.size() >= 4) style.grid_column_end = lines[3];
        } else {
            style.grid_area_name = val;
        }
    } else if (prop == "contain") {
        uint8_t flags = 0;
        std::stringstream ss(val);
        std::string tok;
        while (ss >> tok) {
            if (tok == "none") {
                flags = 0;
                break;
            } else if (tok == "strict") {
                flags = static_cast<uint8_t>(Containment::Strict);
            } else if (tok == "content") {
                flags = static_cast<uint8_t>(Containment::Content);
            } else if (tok == "layout") {
                flags |= static_cast<uint8_t>(Containment::Layout);
            } else if (tok == "paint") {
                flags |= static_cast<uint8_t>(Containment::Paint);
            } else if (tok == "size") {
                flags |= static_cast<uint8_t>(Containment::Size);
            } else if (tok == "style") {
                flags |= static_cast<uint8_t>(Containment::Style);
            }
        }
        style.containment = flags;
    }
}

static TransformMatrix2D parse_transform_matrix(const std::string& str) {
    TransformMatrix2D result = TransformMatrix2D::identity();
    size_t i = 0;
    while (i < str.size()) {
        while (i < str.size() && (std::isspace(str[i]) || str[i] == ',')) i++;
        if (i >= str.size()) break;

        size_t open_paren = str.find('(', i);
        if (open_paren == std::string::npos) break;
        std::string func = str.substr(i, open_paren - i);
        css_trim(func);

        size_t close_paren = str.find(')', open_paren);
        if (close_paren == std::string::npos) break;
        std::string args_str = str.substr(open_paren + 1, close_paren - open_paren - 1);
        i = close_paren + 1;

        std::vector<std::string> args;
        std::stringstream ss(args_str);
        std::string arg;
        while (std::getline(ss, arg, ',')) {
            css_trim(arg);
            if (!arg.empty()) args.push_back(arg);
        }
        if (args.empty()) {
            std::stringstream ss2(args_str);
            while (ss2 >> arg) args.push_back(arg);
        }

        auto parse_f = [](const std::string& s) -> float {
            try { return std::stof(s); } catch (...) { return 0.0f; }
        };

        auto parse_angle = [&](const std::string& s) -> float {
            float v = parse_f(s);
            if (s.find("deg") != std::string::npos) {
                return v * 3.14159265358979323846f / 180.0f;
            }
            if (s.find("rad") != std::string::npos) {
                return v;
            }
            return v * 3.14159265358979323846f / 180.0f;
        };

        TransformMatrix2D op = TransformMatrix2D::identity();
        if (func == "translate") {
            float tx = !args.empty() ? parse_f(args[0]) : 0.0f;
            float ty = args.size() > 1 ? parse_f(args[1]) : 0.0f;
            op = TransformMatrix2D::translation(tx, ty);
        } else if (func == "translateX") {
            float tx = !args.empty() ? parse_f(args[0]) : 0.0f;
            op = TransformMatrix2D::translation(tx, 0.0f);
        } else if (func == "translateY") {
            float ty = !args.empty() ? parse_f(args[0]) : 0.0f;
            op = TransformMatrix2D::translation(0.0f, ty);
        } else if (func == "scale") {
            float sx = !args.empty() ? parse_f(args[0]) : 1.0f;
            float sy = args.size() > 1 ? parse_f(args[1]) : sx;
            op = TransformMatrix2D::scale(sx, sy);
        } else if (func == "scaleX") {
            float sx = !args.empty() ? parse_f(args[0]) : 1.0f;
            op = TransformMatrix2D::scale(sx, 1.0f);
        } else if (func == "scaleY") {
            float sy = !args.empty() ? parse_f(args[0]) : 1.0f;
            op = TransformMatrix2D::scale(1.0f, sy);
        } else if (func == "rotate") {
            float rad = !args.empty() ? parse_angle(args[0]) : 0.0f;
            op = TransformMatrix2D::rotation(rad);
        } else if (func == "skew") {
            float ax = !args.empty() ? parse_angle(args[0]) : 0.0f;
            float ay = args.size() > 1 ? parse_angle(args[1]) : 0.0f;
            op = TransformMatrix2D::skew(ax, ay);
        } else if (func == "skewX") {
            float ax = !args.empty() ? parse_angle(args[0]) : 0.0f;
            op = TransformMatrix2D::skew(ax, 0.0f);
        } else if (func == "skewY") {
            float ay = !args.empty() ? parse_angle(args[0]) : 0.0f;
            op = TransformMatrix2D::skew(0.0f, ay);
        } else if (func == "matrix" && args.size() >= 6) {
            op = TransformMatrix2D(parse_f(args[0]), parse_f(args[1]), parse_f(args[2]),
                                  parse_f(args[3]), parse_f(args[4]), parse_f(args[5]));
        }

        result = result.multiply(op);
    }
    return result;
}

std::string StyleResolver::resolve_var_functions(
    const std::string& value,
    const std::unordered_map<std::string, std::string>& custom_props,
    int depth
) const {
    if (depth > 12) return "";
    if (value.find("var(") == std::string::npos) return value;

    // Where each "(" closes, found in one pass. Looking for the end of every
    // var( by scanning forward from it reads the rest of the value once per
    // var(: a value of 200,000 var( that are never closed took minutes.
    constexpr std::size_t kNotClosed = std::string::npos;
    std::vector<std::size_t> closes_at(value.size(), kNotClosed);
    {
        std::vector<std::size_t> open;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '(') {
                open.push_back(i);
            } else if (value[i] == ')' && !open.empty()) {
                closes_at[open.back()] = i;
                open.pop_back();
            }
        }
    }

    auto trimmed = [](const std::string& text, std::size_t from, std::size_t to) {
        while (from < to && std::isspace(static_cast<unsigned char>(text[from]))) ++from;
        while (to > from && std::isspace(static_cast<unsigned char>(text[to - 1]))) --to;
        return text.substr(from, to - from);
    };

    // The value is read once, left to right, and the result is built by
    // appending: text up to a var(, then what the var( stands for.
    std::string result;
    std::size_t pos = 0;
    while (true) {
        const std::size_t var_pos = value.find("var(", pos);
        if (var_pos == std::string::npos) {
            result.append(value, pos, std::string::npos);
            break;
        }
        result.append(value, pos, var_pos - pos);
        const std::size_t content_start = var_pos + 4;
        const std::size_t content_end = closes_at[var_pos + 3];
        if (content_end == kNotClosed) {
            // Never closed: the text stays, and a var( inside it may still
            // be a complete one.
            result.append("var(");
            pos = content_start;
            continue;
        }

        // The name ends at the first comma that is not inside parentheses;
        // the rest is the fallback.
        std::size_t comma = kNotClosed;
        for (std::size_t c = content_start; c < content_end; ++c) {
            if (value[c] == '(') {
                // Matched inside the var(, so it closes inside it too.
                c = closes_at[c] == kNotClosed ? c : closes_at[c];
            } else if (value[c] == ',') {
                comma = c;
                break;
            }
        }
        const bool has_fallback = comma != kNotClosed;
        const std::string var_name = trimmed(value, content_start, has_fallback ? comma : content_end);

        auto it = custom_props.find(var_name);
        if (it != custom_props.end()) {
            result += resolve_var_functions(it->second, custom_props, depth + 1);
        } else if (has_fallback) {
            result += resolve_var_functions(trimmed(value, comma + 1, content_end), custom_props, depth + 1);
        }
        pos = content_end + 1;
    }
    return result;
}

namespace {

// How deeply a calc() expression may nest parentheses and calc() functions.
// The parser below calls itself once per level, so the nesting of the input
// must not decide how much native stack it uses. Blink has the same limit and
// the same number (kMaxExpressionDepth in css_math_expression_node.h); a
// deeper expression is invalid there, and here.
constexpr int kMaxCalcDepth = 100;

struct CalcLexer {
    std::string_view text;
    size_t pos{0};
    int depth{0};
    bool invalid{false};
    float em_base{16.0f};
    float root_font_size{16.0f};
    float viewport_w{800.0f};
    float viewport_h{600.0f};

    void skip_whitespace() {
        while (pos < text.size() && css_is_space(text[pos])) {
            pos++;
        }
    }

    bool has_more() {
        skip_whitespace();
        return pos < text.size();
    }

    char peek() {
        skip_whitespace();
        if (pos >= text.size()) return '\0';
        return text[pos];
    }

    char get() {
        skip_whitespace();
        if (pos >= text.size()) return '\0';
        return text[pos++];
    }

    // Gives up on the expression: nothing is left to read, so every loop in
    // the parser ends.
    void fail() {
        invalid = true;
        pos = text.size();
    }
};

static float parse_calc_primary(CalcLexer& lex, float ref_size);
static float parse_calc_term(CalcLexer& lex, float ref_size);
static float parse_calc_expr(CalcLexer& lex, float ref_size);

// `(` has been read: the expression inside, and the `)` if there is one. A
// parenthesis still open at the end of the value is closed by the end.
static float parse_calc_group(CalcLexer& lex, float ref_size) {
    if (lex.depth >= kMaxCalcDepth) {
        lex.fail();
        return 0.0f;
    }
    lex.depth++;
    float val = parse_calc_expr(lex, ref_size);
    lex.depth--;
    if (lex.peek() == ')') {
        lex.get(); // consume ')'
    }
    return val;
}

static float parse_calc_math_fn(CalcLexer& lex, float ref_size, const std::string& fn) {
    if (lex.depth >= kMaxCalcDepth) {
        lex.fail();
        return 0.0f;
    }
    lex.depth++;
    std::vector<float> args;
    for (;;) {
        args.push_back(parse_calc_expr(lex, ref_size));
        if (lex.invalid) break;
        if (lex.peek() == ',') {
            lex.get();
            continue;
        }
        if (lex.peek() == ')') {
            lex.get();
        }
        break;
    }
    lex.depth--;
    if (args.empty()) return 0.0f;
    if (fn == "min") {
        float m = args[0];
        for (size_t i = 1; i < args.size(); ++i) m = std::min(m, args[i]);
        return m;
    }
    if (fn == "max") {
        float m = args[0];
        for (size_t i = 1; i < args.size(); ++i) m = std::max(m, args[i]);
        return m;
    }
    if (fn == "clamp") {
        if (args.size() >= 3) {
            return std::max(args[0], std::min(args[1], args[2]));
        }
        return args[0];
    }
    return args[0];
}

static float parse_calc_primary(CalcLexer& lex, float ref_size) {
    // A run of signs is read in a loop. (It used to be one call per sign, so a
    // long enough run of `-` overflowed the stack.)
    bool negative = false;
    for (;;) {
        const char sign = lex.peek();
        if (sign == '+') {
            lex.get();
        } else if (sign == '-') {
            lex.get();
            negative = !negative;
        } else {
            break;
        }
    }
    if (!lex.has_more()) return 0.0f;

    float value = 0.0f;
    if (lex.peek() == '(') {
        lex.get(); // consume '('
        value = parse_calc_group(lex, ref_size);
        return negative ? -value : value;
    }

    size_t start = lex.pos;
    while (lex.pos < lex.text.size()) {
        char ch = lex.text[lex.pos];
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' ||
            std::isalpha(static_cast<unsigned char>(ch)) || ch == '%') {
            lex.pos++;
        } else {
            break;
        }
    }

    std::string token(lex.text.substr(start, lex.pos - start));
    if (token.empty()) return 0.0f;
    std::string token_lower = token;
    for (char& c : token_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lex.pos < lex.text.size() && lex.text[lex.pos] == '(') {
        if (token_lower == "calc") {
            lex.pos++;
            value = parse_calc_group(lex, ref_size);
            return negative ? -value : value;
        }
        if (token_lower == "min" || token_lower == "max" || token_lower == "clamp") {
            lex.pos++;
            value = parse_calc_math_fn(lex, ref_size, token_lower);
            return negative ? -value : value;
        }
    }

    try {
        std::size_t consumed = 0;
        const float num = std::stof(token_lower, &consumed);
        const std::string_view unit = std::string_view(token_lower).substr(consumed);
        if (unit.empty() || unit == "px") value = num;
        else if (unit == "%") value = (num / 100.0f) * ref_size;
        else if (unit == "em") value = num * lex.em_base;
        else if (unit == "rem") value = num * lex.root_font_size;
        else if (unit == "ex" || unit == "ch") value = num * lex.em_base * 0.5f;
        else if (unit == "pt") value = num * (96.0f / 72.0f);
        else if (unit == "pc") value = num * 16.0f;
        else if (unit == "in") value = num * 96.0f;
        else if (unit == "cm") value = num * (96.0f / 2.54f);
        else if (unit == "mm") value = num * (96.0f / 25.4f);
        else if (unit == "q") value = num * (96.0f / 101.6f);
        else if (unit == "vw" || unit == "dvw" || unit == "svw" || unit == "lvw") value = num * lex.viewport_w / 100.0f;
        else if (unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh") value = num * lex.viewport_h / 100.0f;
        else if (unit == "vmin" || unit == "dvmin" || unit == "svmin" || unit == "lvmin") {
            value = num * std::min(lex.viewport_w, lex.viewport_h) / 100.0f;
        } else if (unit == "vmax" || unit == "dvmax" || unit == "svmax" || unit == "lvmax") {
            value = num * std::max(lex.viewport_w, lex.viewport_h) / 100.0f;
        } else {
            value = num;
        }
    } catch (...) {
        value = 0.0f;
    }
    return negative ? -value : value;
}

static float parse_calc_term(CalcLexer& lex, float ref_size) {
    float left = parse_calc_primary(lex, ref_size);
    while (lex.has_more()) {
        char op = lex.peek();
        if (op != '*' && op != '/') break;
        lex.get();
        float right = parse_calc_primary(lex, ref_size);
        if (op == '*') left *= right;
        else if (op == '/' && right != 0.0f) left /= right;
    }
    return left;
}

static float parse_calc_expr(CalcLexer& lex, float ref_size) {
    float left = parse_calc_term(lex, ref_size);
    while (lex.has_more()) {
        char op = lex.peek();
        if (op != '+' && op != '-') break;
        lex.get();
        float right = parse_calc_term(lex, ref_size);
        if (op == '+') left += right;
        else if (op == '-') left -= right;
    }
    return left;
}
} // namespace

float StyleResolver::evaluate_calc_expression(std::string_view expr, float reference_size, bool* valid) const {
    CalcLexer lex{expr, 0, 0, false, m_em_base, m_root_font_size, m_viewport_width, m_viewport_height};
    const float value = parse_calc_expr(lex, reference_size);
    if (valid) *valid = !lex.invalid;
    return lex.invalid ? 0.0f : value;
}

// Replaces each calc(...), min(...), max(...), clamp(...) in `value` by its result in px.
std::string StyleResolver::resolve_calc_functions(const std::string& value, float reference_size) const {
    auto is_ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    };
    auto find_math_fn = [&](size_t from, size_t& fn_len) -> size_t {
        static const char* const fns[] = {"calc(", "clamp(", "min(", "max("};
        size_t best = std::string::npos;
        size_t best_len = 0;
        for (size_t idx = from; idx < value.size(); ++idx) {
            if (idx > 0 && is_ident_char(value[idx - 1])) continue;
            for (const char* name : fns) {
                const size_t nlen = std::char_traits<char>::length(name);
                if (idx + nlen <= value.size()) {
                    bool match = true;
                    for (size_t k = 0; k < nlen; ++k) {
                        char vc = value[idx + k];
                        char fc = name[k];
                        if (fc != '(') vc = static_cast<char>(std::tolower(static_cast<unsigned char>(vc)));
                        if (vc != fc) { match = false; break; }
                    }
                    if (match) {
                        best = idx;
                        best_len = nlen;
                        fn_len = best_len;
                        return best;
                    }
                }
            }
        }
        return std::string::npos;
    };

    std::string result;
    size_t at = 0;
    for (;;) {
        size_t fn_len = 0;
        const size_t fn_pos = find_math_fn(at, fn_len);
        if (fn_pos == std::string::npos) {
            result.append(value, at, std::string::npos);
            break;
        }
        result.append(value, at, fn_pos - at);

        const size_t content_start = fn_pos + fn_len;
        int paren_depth = 1;
        size_t i = content_start;
        for (; i < value.size() && paren_depth > 0; ++i) {
            if (value[i] == '(') paren_depth++;
            else if (value[i] == ')') paren_depth--;
        }
        const bool closed = paren_depth == 0;
        const size_t content_end = closed ? i - 1 : value.size();

        bool valid = true;
        // For min(/max(/clamp(, evaluate the whole function call expression; for calc(, evaluate inside.
        std::string_view expr_slice = (fn_len == 5 && (value[fn_pos] | 0x20) == 'c')
                                          ? std::string_view(value).substr(content_start, content_end - content_start)
                                          : std::string_view(value).substr(fn_pos, (closed ? i : value.size()) - fn_pos);
        const float val = evaluate_calc_expression(expr_slice, reference_size, &valid);
        result += valid ? std::to_string(val) + "px" : std::string("invalid");
        at = closed ? i : value.size();
    }
    return result;
}

ComputedStyle StyleResolver::resolve_style(const std::shared_ptr<Element>& element, const ComputedStyle* parent_style) const {
    if (!parent_style && element) {
        auto p = element->parent();
        if (p && p->is_element()) {
            std::vector<std::shared_ptr<Element>> ancestors;
            while (p && p->is_element()) {
                ancestors.push_back(std::static_pointer_cast<Element>(p));
                p = p->parent();
            }
            ComputedStyle inherited;
            for (std::size_t i = ancestors.size(); i > 0; --i) {
                inherited = resolve_style(ancestors[i - 1], i == ancestors.size() ? nullptr : &inherited);
            }
            return resolve_style(element, &inherited);
        }
        m_depth_cache.clear();
        m_desc_prefix_cache.clear();
        m_depth_cache[element.get()] = 0;
    } else if (parent_style && element) {
        auto p = element->parent();
        if (p && p->is_element()) {
            auto dit = m_depth_cache.find(static_cast<const Element*>(p.get()));
            if (dit != m_depth_cache.end()) {
                m_depth_cache[element.get()] = dit->second + 1;
            }
        }
    }

    ComputedStyle style;
    // Relative (em/ex/ch) lengths resolve against the inherited font size.
    m_em_base = parent_style ? parent_style->font_size : 16.0f;

    // Inherit properties from parent
    if (parent_style) {
        style.color = parent_style->color;
        style.dark_color_scheme = parent_style->dark_color_scheme;
        style.font_size = parent_style->font_size;
        style.font_weight = parent_style->font_weight;
        style.font_style = parent_style->font_style;
        if (!parent_style->font_family.empty()) style.font_family = parent_style->font_family;
        style.text_align = parent_style->text_align;
        if (!parent_style->custom_properties.empty()) style.custom_properties = parent_style->custom_properties;
        // empty-cells and caption-side inherit per CSS 2.1 17.5.1.2 and 17.4.1.
        style.empty_cells = parent_style->empty_cells;
        style.caption_side = parent_style->caption_side;
        if (parent_style->line_height_number >= 0.0f) {
            // A declared multiplier is inherited as a multiplier.
            style.line_height_number = parent_style->line_height_number;
        } else if (parent_style->line_height >= 0.0f) {
            // A declared length or percentage is inherited as a length.
            style.line_height = parent_style->line_height;
        }
    }

    struct MatchedRule {
        int tier;       // 0: UA, 1: layered normal, 2: unlayered normal, 4: unlayered !important, 5: layered !important
        int layer_key;  // normal: rank ascending; !important: -rank ascending (earlier layers win)
        Specificity spec;
        int order;
        const Declaration* decl;
    };
    std::vector<MatchedRule> matched;

    // 1. User Agent Stylesheet
    int order = 0;
    for (const auto& rule : m_ua_sheet.rules) {
        for (const auto& sel : rule.selectors) {
            if (matches_selector(element, sel)) {
                for (const auto& decl : rule.declarations) {
                    matched.push_back({0, 0, Specificity{0, 0, 0}, order++, &decl});
                }
            }
        }
    }

    // 2. Author Stylesheets or Scoped Shadow Root Stylesheets
    ShadowRoot* shadow_scope = nullptr;
    auto curr_p = element->parent();
    while (curr_p) {
        if (curr_p->is_document_fragment()) {
            shadow_scope = dynamic_cast<ShadowRoot*>(curr_p.get());
            if (shadow_scope) break;
        }
        curr_p = curr_p->parent();
    }

    if (shadow_scope) {
        // Scoped styling: Only stylesheets defined within this ShadowRoot match
        for (const auto& sheet : shadow_scope->parsed_stylesheets()) {
            for (const auto& rule : sheet.rules) {
                for (const auto& sel : rule.selectors) {
                    Specificity spec;
                    if (match_selector_impl(element, sel, &spec)) {
                        for (const auto& decl : rule.declarations) {
                            matched.push_back({decl.important ? 4 : 2, 0, spec, order++, &decl});
                        }
                    }
                }
            }
        }
    } else {
        // Light DOM: retrieve possible author selectors by their rightmost
        // simple selector, then run the full selector matcher on that smaller
        // candidate set. Candidate sorting preserves stylesheet source order.
        for (const auto& candidate : indexed_candidates(element)) {
            const auto& rule = m_author_sheets[candidate.sheet_index].rules[candidate.rule_index];
            bool media_matches = true;
            for (const auto& mq : rule.media_queries) {
                if (!mq.matches(m_viewport_width, m_viewport_height)) {
                    media_matches = false;
                    break;
                }
            }
            if (!media_matches) continue;

            const auto& sel = rule.selectors[candidate.selector_index];
            Specificity spec;
            if (match_selector_impl(element, sel, &spec)) {
                const int rank = layer_rank(rule.layer_name);
                for (const auto& decl : rule.declarations) {
                    int tier = 0;
                    int lkey = 0;
                    if (!decl.important) {
                        tier = (rank >= 0) ? 1 : 2;
                        lkey = (rank >= 0) ? rank : 0;
                    } else {
                        tier = (rank < 0) ? 4 : 5;
                        lkey = (rank < 0) ? 0 : -rank;
                    }
                    matched.push_back({tier, lkey, spec, order++, &decl});
                }
            }
        }

        // Host element styling: If element has a shadow root, match :host rules from shadow stylesheets
        if (element->shadow_root()) {
            for (const auto& sheet : element->shadow_root()->parsed_stylesheets()) {
                for (const auto& rule : sheet.rules) {
                    for (const auto& sel : rule.selectors) {
                        Specificity spec;
                        if (match_selector_impl(element, sel, &spec)) {
                            for (const auto& decl : rule.declarations) {
                                matched.push_back({decl.important ? 4 : 2, 0, spec, order++, &decl});
                            }
                        }
                    }
                }
            }
        }
    }

    // Stable sort by cascade tier, layer order, specificity, then source order
    std::stable_sort(matched.begin(), matched.end(), [](const MatchedRule& a, const MatchedRule& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.layer_key != b.layer_key) return a.layer_key < b.layer_key;
        if (a.spec == b.spec) return a.order < b.order;
        return a.spec < b.spec;
    });

    auto apply_all = [&](const std::vector<const Declaration*>& declarations) {
        for (const auto* declaration : declarations) {
            apply_declaration(style, declaration->property, declaration->value);
        }
    };

    std::vector<const Declaration*> normal_author;
    std::vector<const Declaration*> important_author;
    for (const auto& m : matched) {
        (m.decl->important ? important_author : normal_author).push_back(m.decl);
    }

    // 3. Inline style attribute
    std::vector<Declaration> inline_declarations;
    std::string inline_style = element->get_attribute("style");
    if (!inline_style.empty()) {
        std::string dummy_css = "* { " + inline_style + " }";
        auto sheet = CssParser::parse(dummy_css);
        for (const auto& r : sheet.rules) {
            for (const auto& d : r.declarations) inline_declarations.push_back(d);
        }
    }
    std::vector<const Declaration*> normal_inline;
    std::vector<const Declaration*> important_inline;
    for (const auto& d : inline_declarations) {
        (d.important ? important_inline : normal_inline).push_back(&d);
    }

    std::vector<const Declaration*> cascade_order;
    cascade_order.insert(cascade_order.end(), normal_author.begin(), normal_author.end());
    cascade_order.insert(cascade_order.end(), normal_inline.begin(), normal_inline.end());
    cascade_order.insert(cascade_order.end(), important_author.begin(), important_author.end());
    cascade_order.insert(cascade_order.end(), important_inline.begin(), important_inline.end());

    // Custom properties first so font-size: var(--...) works, then font-size
    // so em/ex/ch lengths elsewhere on the element resolve against its own
    // computed font size while font-size resolves against the inherited one.
    auto sets_font_size = [](const std::string& prop) {
        return prop == "font-size" || prop == "font";
    };
    std::vector<const Declaration*> scheme_decls;
    std::vector<const Declaration*> color_decls;
    std::vector<const Declaration*> font_decls;
    std::vector<const Declaration*> rest;
    for (const auto* declaration : cascade_order) {
        if (declaration->property.rfind("--", 0) == 0) {
            apply_declaration(style, declaration->property, declaration->value);
        } else if (declaration->property == "color-scheme") {
            scheme_decls.push_back(declaration);
        } else if (declaration->property == "color") {
            color_decls.push_back(declaration);
        } else if (sets_font_size(declaration->property)) {
            font_decls.push_back(declaration);
        } else {
            rest.push_back(declaration);
        }
    }
    apply_all(scheme_decls);
    apply_all(color_decls);
    apply_all(font_decls);
    if (element->tag_name() == "html") {
        m_root_font_size = style.font_size;
    }
    m_em_base = style.font_size;
    apply_all(rest);

    if (style.border_radius_percent >= 0.0f && style.width > 0.0f) {
        float ref = style.height > 0.0f ? std::min(style.width, style.height) : style.width;
        style.border_radius = ref * (style.border_radius_percent / 100.0f);
    }

    if (style.has_transform && !style.transform_str.empty()) {
        style.transform_matrix = parse_transform_matrix(style.transform_str);
    }

    return style;
}

const KeyframesRule* StyleResolver::find_keyframes(const std::string& name) const {
    if (name.empty()) return nullptr;
    for (const auto& sheet : m_author_sheets) {
        for (const auto& kf : sheet.keyframes) {
            if (kf.name == name) return &kf;
        }
    }
    for (const auto& kf : m_ua_sheet.keyframes) {
        if (kf.name == name) return &kf;
    }
    return nullptr;
}

static float lerp_f(float a, float b, float t) {
    return a + (b - a) * t;
}

static Color lerp_color(const Color& a, const Color& b, float t) {
    uint8_t nr = static_cast<uint8_t>(std::clamp(std::round(lerp_f(a.r, b.r, t)), 0.0f, 255.0f));
    uint8_t ng = static_cast<uint8_t>(std::clamp(std::round(lerp_f(a.g, b.g, t)), 0.0f, 255.0f));
    uint8_t nb = static_cast<uint8_t>(std::clamp(std::round(lerp_f(a.b, b.b, t)), 0.0f, 255.0f));
    uint8_t na = static_cast<uint8_t>(std::clamp(std::round(lerp_f(a.a, b.a, t)), 0.0f, 255.0f));
    return Color(nr, ng, nb, na);
}

void StyleResolver::interpolate_styles(ComputedStyle& target, const ComputedStyle& from, const ComputedStyle& to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    target = from;

    target.opacity = lerp_f(from.opacity, to.opacity, t);
    target.color = lerp_color(from.color, to.color, t);
    target.background_color = lerp_color(from.background_color, to.background_color, t);
    target.border_color = lerp_color(from.border_color, to.border_color, t);

    if (from.width >= 0.0f && to.width >= 0.0f) target.width = lerp_f(from.width, to.width, t);
    if (from.height >= 0.0f && to.height >= 0.0f) target.height = lerp_f(from.height, to.height, t);
    if (from.font_size > 0.0f && to.font_size > 0.0f) target.font_size = lerp_f(from.font_size, to.font_size, t);

    target.margin.top = lerp_f(from.margin.top, to.margin.top, t);
    target.margin.right = lerp_f(from.margin.right, to.margin.right, t);
    target.margin.bottom = lerp_f(from.margin.bottom, to.margin.bottom, t);
    target.margin.left = lerp_f(from.margin.left, to.margin.left, t);

    target.padding.top = lerp_f(from.padding.top, to.padding.top, t);
    target.padding.right = lerp_f(from.padding.right, to.padding.right, t);
    target.padding.bottom = lerp_f(from.padding.bottom, to.padding.bottom, t);
    target.padding.left = lerp_f(from.padding.left, to.padding.left, t);

    target.border_width.top = lerp_f(from.border_width.top, to.border_width.top, t);
    target.border_width.right = lerp_f(from.border_width.right, to.border_width.right, t);
    target.border_width.bottom = lerp_f(from.border_width.bottom, to.border_width.bottom, t);
    target.border_width.left = lerp_f(from.border_width.left, to.border_width.left, t);

    if (from.has_top && to.has_top) target.top = lerp_f(from.top, to.top, t);
    if (from.has_left && to.has_left) target.left = lerp_f(from.left, to.left, t);
    if (from.has_right && to.has_right) target.right = lerp_f(from.right, to.right, t);
    if (from.has_bottom && to.has_bottom) target.bottom = lerp_f(from.bottom, to.bottom, t);
}

void StyleResolver::apply_animation(ComputedStyle& style, float elapsed_seconds) const {
    if (style.animation_name.empty() || style.animation_duration <= 0.0f) return;

    const KeyframesRule* kfr = find_keyframes(style.animation_name);
    if (!kfr || kfr->keyframes.empty()) return;

    float t = 0.0f;
    if (style.animation_iteration_count > 0.0f && elapsed_seconds >= style.animation_duration * style.animation_iteration_count) {
        t = 1.0f;
    } else {
        t = std::fmod(elapsed_seconds, style.animation_duration) / style.animation_duration;
    }

    // Apply timing function
    switch (style.animation_timing_function) {
        case TimingFunction::Ease:
        case TimingFunction::EaseInOut:
            t = t * t * (3.0f - 2.0f * t);
            break;
        case TimingFunction::EaseIn:
            t = t * t;
            break;
        case TimingFunction::EaseOut:
            t = t * (2.0f - t);
            break;
        case TimingFunction::StepStart:
            t = (t > 0.0f) ? 1.0f : 0.0f;
            break;
        case TimingFunction::StepEnd:
            t = (t >= 1.0f) ? 1.0f : 0.0f;
            break;
        case TimingFunction::Linear:
            break;
    }

    if (t <= kfr->keyframes.front().offset) {
        for (const auto& decl : kfr->keyframes.front().declarations) {
            apply_declaration(style, decl.property, decl.value);
        }
        return;
    }
    if (t >= kfr->keyframes.back().offset) {
        for (const auto& decl : kfr->keyframes.back().declarations) {
            apply_declaration(style, decl.property, decl.value);
        }
        return;
    }

    // Find bounding keyframes
    for (size_t i = 0; i + 1 < kfr->keyframes.size(); ++i) {
        const auto& k1 = kfr->keyframes[i];
        const auto& k2 = kfr->keyframes[i + 1];
        if (t >= k1.offset && t <= k2.offset) {
            float seg_range = k2.offset - k1.offset;
            float seg_t = (seg_range > 0.0001f) ? ((t - k1.offset) / seg_range) : 0.0f;

            ComputedStyle style1 = style;
            for (const auto& d : k1.declarations) apply_declaration(style1, d.property, d.value);

            ComputedStyle style2 = style;
            for (const auto& d : k2.declarations) apply_declaration(style2, d.property, d.value);

            interpolate_styles(style, style1, style2, seg_t);
            break;
        }
    }
}

} // namespace litebird
