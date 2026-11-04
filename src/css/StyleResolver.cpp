#include "litebird/css/StyleResolver.hpp"
#include "litebird/css/CssParser.hpp"
#include <sstream>
#include <cctype>

namespace litebird {

StyleSheet StyleResolver::default_user_agent_stylesheet() {
    const char* ua_css = R"(
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

void StyleResolver::add_stylesheet(const StyleSheet& sheet) {
    m_author_sheets.push_back(sheet);
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
    return true;
}

bool StyleResolver::matches_selector(const std::shared_ptr<Element>& element, const Selector& selector) const {
    if (selector.parts.empty()) return false;

    // Check last part (target element)
    if (!matches_simple_selector(element, selector.parts.back())) {
        return false;
    }

    if (selector.parts.size() == 1) {
        return true;
    }

    // Descendant matching for earlier parts
    int part_idx = static_cast<int>(selector.parts.size()) - 2;
    auto curr = element->parent();

    while (curr && part_idx >= 0) {
        if (auto el = std::dynamic_pointer_cast<Element>(curr)) {
            if (matches_simple_selector(el, selector.parts[part_idx])) {
                part_idx--;
            }
        }
        curr = curr->parent();
    }

    return part_idx < 0;
}

float StyleResolver::parse_length(std::string_view val, float default_val) const {
    while (!val.empty() && std::isspace(val.front())) val.remove_prefix(1);
    while (!val.empty() && std::isspace(val.back())) val.remove_suffix(1);
    if (val.empty() || val == "auto") return default_val;

    try {
        size_t idx = 0;
        float num = std::stof(std::string(val), &idx);
        return num;
    } catch (...) {
        return default_val;
    }
}

EdgeSizes StyleResolver::parse_box_edges(std::string_view val) const {
    std::vector<float> nums;
    std::stringstream ss{std::string(val)};
    std::string token;
    while (ss >> token) {
        nums.push_back(parse_length(token, 0.0f));
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

void StyleResolver::apply_declaration(ComputedStyle& style, const std::string& prop, const std::string& val) const {
    if (prop == "display") {
        if (val == "block") style.display = Display::Block;
        else if (val == "inline") style.display = Display::Inline;
        else if (val == "flex") style.display = Display::Flex;
        else if (val == "none") style.display = Display::None;
    } else if (prop == "flex-direction") {
        if (val == "row") style.flex_direction = FlexDirection::Row;
        else if (val == "column") style.flex_direction = FlexDirection::Column;
        else if (val == "row-reverse") style.flex_direction = FlexDirection::RowReverse;
        else if (val == "column-reverse") style.flex_direction = FlexDirection::ColumnReverse;
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
    } else if (prop == "gap") {
        style.gap = parse_length(val, 0.0f);
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
    } else if (prop == "color") {
        style.color = Color::parse(val);
    } else if (prop == "background-color" || prop == "background") {
        style.background_color = Color::parse(val);
    } else if (prop == "width") {
        style.width = parse_length(val, -1.0f);
    } else if (prop == "height") {
        style.height = parse_length(val, -1.0f);
    } else if (prop == "margin") {
        style.margin = parse_box_edges(val);
    } else if (prop == "margin-top") {
        style.margin.top = parse_length(val, 0.0f);
    } else if (prop == "margin-right") {
        style.margin.right = parse_length(val, 0.0f);
    } else if (prop == "margin-bottom") {
        style.margin.bottom = parse_length(val, 0.0f);
    } else if (prop == "margin-left") {
        style.margin.left = parse_length(val, 0.0f);
    } else if (prop == "padding") {
        style.padding = parse_box_edges(val);
    } else if (prop == "padding-top") {
        style.padding.top = parse_length(val, 0.0f);
    } else if (prop == "padding-right") {
        style.padding.right = parse_length(val, 0.0f);
    } else if (prop == "padding-bottom") {
        style.padding.bottom = parse_length(val, 0.0f);
    } else if (prop == "padding-left") {
        style.padding.left = parse_length(val, 0.0f);
    } else if (prop == "border-radius") {
        style.border_radius = parse_length(val, 0.0f);
    } else if (prop == "box-shadow") {
        std::stringstream ss(val);
        std::string p1, p2, p3, p4;
        if (ss >> p1 >> p2) {
            style.box_shadow.has_shadow = true;
            style.box_shadow.x_offset = parse_length(p1, 0.0f);
            style.box_shadow.y_offset = parse_length(p2, 0.0f);
            if (ss >> p3) {
                if (std::isdigit(p3[0]) || p3[0] == '-') {
                    style.box_shadow.blur = parse_length(p3, 0.0f);
                    if (ss >> p4) {
                        style.box_shadow.color = Color::parse(p4);
                    } else {
                        style.box_shadow.color = Color(0, 0, 0, 100);
                    }
                } else {
                    style.box_shadow.color = Color::parse(p3);
                }
            } else {
                style.box_shadow.color = Color(0, 0, 0, 100);
            }
        }
    } else if (prop == "border") {
        // e.g. "1px solid black" or "2px #333"
        std::stringstream ss(val);
        std::string part;
        while (ss >> part) {
            if (std::isdigit(part[0])) {
                style.border_width = EdgeSizes(parse_length(part, 0.0f));
            } else if (part != "solid" && part != "none" && part != "dashed") {
                style.border_color = Color::parse(part);
            }
        }
    } else if (prop == "border-width") {
        style.border_width = parse_box_edges(val);
    } else if (prop == "border-color") {
        style.border_color = Color::parse(val);
    } else if (prop == "font-size") {
        style.font_size = parse_length(val, 16.0f);
    } else if (prop == "font-weight") {
        if (val == "bold") style.font_weight = 700;
        else if (val == "normal") style.font_weight = 400;
        else {
            try { style.font_weight = std::stoi(val); } catch (...) {}
        }
    } else if (prop == "text-align") {
        style.text_align = val;
    } else if (prop == "text-decoration") {
        style.text_decoration = val;
    }
}

ComputedStyle StyleResolver::resolve_style(const std::shared_ptr<Element>& element, const ComputedStyle* parent_style) const {
    ComputedStyle style;

    // Inherit properties from parent
    if (parent_style) {
        style.color = parent_style->color;
        style.font_size = parent_style->font_size;
        style.font_weight = parent_style->font_weight;
        style.text_align = parent_style->text_align;
    }

    struct MatchedRule {
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
                    matched.push_back({Specificity{0, 0, 0}, order++, &decl});
                }
            }
        }
    }

    // 2. Author Stylesheets
    for (const auto& sheet : m_author_sheets) {
        for (const auto& rule : sheet.rules) {
            for (const auto& sel : rule.selectors) {
                if (matches_selector(element, sel)) {
                    auto spec = sel.specificity();
                    for (const auto& decl : rule.declarations) {
                        matched.push_back({spec, order++, &decl});
                    }
                }
            }
        }
    }

    // Stable sort by specificity, then source order
    std::stable_sort(matched.begin(), matched.end(), [](const MatchedRule& a, const MatchedRule& b) {
        if (a.spec == b.spec) return a.order < b.order;
        return a.spec < b.spec;
    });

    for (const auto& m : matched) {
        apply_declaration(style, m.decl->property, m.decl->value);
    }

    // 3. Inline style attribute (highest priority: specificty 1000, 0, 0)
    std::string inline_style = element->get_attribute("style");
    if (!inline_style.empty()) {
        std::string dummy_css = "* { " + inline_style + " }";
        auto sheet = CssParser::parse(dummy_css);
        for (const auto& r : sheet.rules) {
            for (const auto& d : r.declarations) {
                apply_declaration(style, d.property, d.value);
            }
        }
    }

    return style;
}

} // namespace litebird
