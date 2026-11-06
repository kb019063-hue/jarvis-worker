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
    for (const auto& [attr_name, attr_val] : sel.attributes) {
        if (!element->has_attribute(attr_name)) {
            return false;
        }
        if (!attr_val.empty() && element->get_attribute(attr_name) != attr_val) {
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

static std::vector<float> parse_grid_tracks(std::string_view val) {
    std::vector<float> tracks;
    std::string s(val);
    size_t rep_pos = s.find("repeat(");
    while (rep_pos != std::string::npos) {
        size_t close_pos = s.find(")", rep_pos);
        if (close_pos != std::string::npos) {
            std::string inside = s.substr(rep_pos + 7, close_pos - (rep_pos + 7));
            size_t comma = inside.find(",");
            if (comma != std::string::npos) {
                int count = 1;
                try { count = std::stoi(inside.substr(0, comma)); } catch (...) {}
                std::string item = inside.substr(comma + 1);
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
                std::string rep_expanded;
                for (int i = 0; i < count; ++i) {
                    if (i > 0) rep_expanded += " ";
                    rep_expanded += item;
                }
                s.replace(rep_pos, close_pos - rep_pos + 1, rep_expanded);
            }
        }
        rep_pos = s.find("repeat(", rep_pos + 1);
    }

    std::stringstream ss(s);
    std::string token;
    while (ss >> token) {
        if (token.ends_with("fr")) {
            std::string num_part = token.substr(0, token.size() - 2);
            float fr = 1.0f;
            try { fr = std::stof(num_part); } catch (...) {}
            tracks.push_back(-std::max(0.1f, fr));
        } else if (token == "auto") {
            tracks.push_back(-1.0f);
        } else {
            float px = 0.0f;
            try { px = std::stof(token); } catch (...) {}
            tracks.push_back(px);
        }
    }
    return tracks;
}

void StyleResolver::apply_declaration(ComputedStyle& style, const std::string& prop, const std::string& val) const {
    if (prop == "display") {
        if (val == "block") style.display = Display::Block;
        else if (val == "inline") style.display = Display::Inline;
        else if (val == "flex") style.display = Display::Flex;
        else if (val == "grid") style.display = Display::Grid;
        else if (val == "none") style.display = Display::None;
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
        style.grid_template_columns = parse_grid_tracks(val);
    } else if (prop == "grid-template-rows") {
        style.grid_template_rows = parse_grid_tracks(val);
    } else if (prop == "grid-gap" || prop == "gap") {
        style.gap = style.grid_column_gap = style.grid_row_gap = parse_length(val, 0.0f);
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
    } else if (prop == "color") {
        style.color = Color::parse(val);
    } else if (prop == "background-color" || prop == "background") {
        style.background_color = Color::parse(val);
    } else if (prop == "width") {
        style.width = parse_length(val, -1.0f);
    } else if (prop == "height") {
        style.height = parse_length(val, -1.0f);
    } else if (prop == "min-width") {
        style.min_width = parse_length(val, -1.0f);
    } else if (prop == "max-width") {
        style.max_width = parse_length(val, -1.0f);
    } else if (prop == "min-height") {
        style.min_height = parse_length(val, -1.0f);
    } else if (prop == "max-height") {
        style.max_height = parse_length(val, -1.0f);
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
        style.margin = parse_box_edges(val);
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
    } else if (prop == "margin-top") {
        if (val == "auto") style.margin_top_auto = true;
        else style.margin.top = parse_length(val, 0.0f);
    } else if (prop == "margin-right") {
        if (val == "auto") style.margin_right_auto = true;
        else style.margin.right = parse_length(val, 0.0f);
    } else if (prop == "margin-bottom") {
        if (val == "auto") style.margin_bottom_auto = true;
        else style.margin.bottom = parse_length(val, 0.0f);
    } else if (prop == "margin-left") {
        if (val == "auto") style.margin_left_auto = true;
        else style.margin.left = parse_length(val, 0.0f);
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
            bool media_matches = true;
            for (const auto& mq : rule.media_queries) {
                if (!mq.matches(m_viewport_width, m_viewport_height)) {
                    media_matches = false;
                    break;
                }
            }
            if (!media_matches) continue;

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
