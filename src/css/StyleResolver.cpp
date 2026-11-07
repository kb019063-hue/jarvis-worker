#include "litebird/css/StyleResolver.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include <sstream>
#include <cctype>
#include <cmath>

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

static bool match_nth_formula(int idx, std::string formula) {
    std::transform(formula.begin(), formula.end(), formula.begin(), ::tolower);
    while (!formula.empty() && std::isspace(formula.front())) formula.erase(formula.begin());
    while (!formula.empty() && std::isspace(formula.back())) formula.pop_back();

    if (formula.empty()) return false;
    if (formula == "odd") return (idx % 2 != 0);
    if (formula == "even") return (idx % 2 == 0);

    size_t n_pos = formula.find('n');
    if (n_pos == std::string::npos) {
        try {
            int b = std::stoi(formula);
            return idx == b;
        } catch (...) {
            return false;
        }
    }

    int a = 1;
    if (n_pos == 0) {
        a = 1;
    } else if (n_pos == 1 && formula[0] == '-') {
        a = -1;
    } else if (n_pos == 1 && formula[0] == '+') {
        a = 1;
    } else {
        try {
            a = std::stoi(formula.substr(0, n_pos));
        } catch (...) {
            a = 1;
        }
    }

    int b = 0;
    std::string rest = formula.substr(n_pos + 1);
    while (!rest.empty() && std::isspace(rest.front())) rest.erase(rest.begin());
    if (!rest.empty()) {
        try {
            b = std::stoi(rest);
        } catch (...) {
            b = 0;
        }
    }

    if (a == 0) return idx == b;
    int diff = idx - b;
    if (diff % a != 0) return false;
    return (diff / a) >= 0;
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
            } else if (name == "nth-child") {
                int idx = 1;
                bool found = false;
                for (size_t i = 0; i < sibling_elements.size(); ++i) {
                    if (sibling_elements[i] == element) {
                        idx = static_cast<int>(i) + 1;
                        found = true;
                        break;
                    }
                }
                if (!found || !match_nth_formula(idx, arg)) return false;
            } else if (name == "nth-of-type") {
                int idx = 1;
                bool found = false;
                int count = 0;
                for (const auto& sib : sibling_elements) {
                    if (sib->tag_name() == element->tag_name()) {
                        count++;
                        if (sib == element) {
                            idx = count;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found || !match_nth_formula(idx, arg)) return false;
            } else if (name == "hover") {
                if (!element->is_hovered()) return false;
            } else if (name == "focus") {
                if (!element->is_focused()) return false;
            } else if (name == "active") {
                if (!element->is_active()) return false;
            } else if (name == "disabled") {
                if (!element->disabled()) return false;
            } else if (name == "checked") {
                if (!element->checked()) return false;
            } else if (name == "not") {
                // Negation pseudo-class: if inner selector matches, then :not fails
                SimpleSelector sub_sel;
                if (!arg.empty()) {
                    if (arg[0] == '.') sub_sel.classes.push_back(arg.substr(1));
                    else if (arg[0] == '#') sub_sel.id = arg.substr(1);
                    else sub_sel.tag_name = arg;
                    if (matches_simple_selector(element, sub_sel)) return false;
                }
            } else if (name == "host") {
                // Host pseudo-class matches if this element has a shadow root
                if (!element->shadow_root()) return false;
                if (!arg.empty()) {
                    SimpleSelector sub_sel;
                    if (arg[0] == '.') sub_sel.classes.push_back(arg.substr(1));
                    else if (arg[0] == '#') sub_sel.id = arg.substr(1);
                    else sub_sel.tag_name = arg;
                    if (!matches_simple_selector(element, sub_sel)) return false;
                }
            }
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

    if (val.find("calc(") != std::string_view::npos) {
        std::string resolved = resolve_calc_functions(std::string(val), m_viewport_width);
        return parse_length(resolved, default_val);
    }

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

static ComputedStyle::GridTrackDef parse_single_grid_track(const std::string& token) {
    ComputedStyle::GridTrackDef def;
    if (token.starts_with("minmax(") && token.ends_with(")")) {
        std::string inside = token.substr(7, token.size() - 8);
        size_t comma = inside.find(',');
        if (comma != std::string::npos) {
            std::string min_part = inside.substr(0, comma);
            std::string max_part = inside.substr(comma + 1);
            while (!min_part.empty() && std::isspace(static_cast<unsigned char>(min_part.front()))) min_part.erase(min_part.begin());
            while (!min_part.empty() && std::isspace(static_cast<unsigned char>(min_part.back()))) min_part.pop_back();
            while (!max_part.empty() && std::isspace(static_cast<unsigned char>(max_part.front()))) max_part.erase(max_part.begin());
            while (!max_part.empty() && std::isspace(static_cast<unsigned char>(max_part.back()))) max_part.pop_back();

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
                while (!count_str.empty() && std::isspace(static_cast<unsigned char>(count_str.front()))) count_str.erase(count_str.begin());
                while (!count_str.empty() && std::isspace(static_cast<unsigned char>(count_str.back()))) count_str.pop_back();
                while (!item_str.empty() && std::isspace(static_cast<unsigned char>(item_str.front()))) item_str.erase(item_str.begin());
                while (!item_str.empty() && std::isspace(static_cast<unsigned char>(item_str.back()))) item_str.pop_back();

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
        auto def = parse_single_grid_track(token);
        defs.push_back(def);

        if (def.max_fr > 0.0f) {
            legacy_tracks.push_back(-def.max_fr);
        } else {
            legacy_tracks.push_back(def.min_px);
        }
    }
}

void StyleResolver::apply_declaration(ComputedStyle& style, const std::string& prop, const std::string& raw_val) const {
    if (prop.starts_with("--")) {
        style.custom_properties[prop] = raw_val;
        return;
    }

    std::string val = raw_val;
    if (val.find("var(") != std::string::npos) {
        val = resolve_var_functions(val, style.custom_properties);
    }
    if (val.find("calc(") != std::string::npos) {
        val = resolve_calc_functions(val, m_viewport_width);
    }

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
    } else if (prop == "opacity") {
        try { style.opacity = std::clamp(std::stof(val), 0.0f, 1.0f); } catch (...) {}
    } else if (prop == "animation-name") {
        style.animation_name = val;
    } else if (prop == "animation-duration") {
        auto parse_sec = [](std::string_view s) -> float {
            std::string str(s);
            while (!str.empty() && std::isspace(str.front())) str.erase(str.begin());
            while (!str.empty() && std::isspace(str.back())) str.pop_back();
            if (str.size() > 2 && str.substr(str.size() - 2) == "ms") {
                try { return std::stof(str.substr(0, str.size() - 2)) / 1000.0f; } catch (...) { return 0.0f; }
            } else if (!str.empty() && str.back() == 's') {
                try { return std::stof(str.substr(0, str.size() - 1)); } catch (...) { return 0.0f; }
            }
            try { return std::stof(str); } catch (...) { return 0.0f; }
        };
        style.animation_duration = parse_sec(val);
    } else if (prop == "animation-timing-function") {
        style.animation_timing_function = val;
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
            } else if (token == "linear" || token == "ease" || token == "ease-in" || token == "ease-out" || token == "ease-in-out") {
                style.animation_timing_function = token;
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
            std::string str(s);
            while (!str.empty() && std::isspace(str.front())) str.erase(str.begin());
            while (!str.empty() && std::isspace(str.back())) str.pop_back();
            if (str.size() > 2 && str.substr(str.size() - 2) == "ms") {
                try { return std::stof(str.substr(0, str.size() - 2)) / 1000.0f; } catch (...) { return 0.0f; }
            } else if (!str.empty() && str.back() == 's') {
                try { return std::stof(str.substr(0, str.size() - 1)); } catch (...) { return 0.0f; }
            }
            try { return std::stof(str); } catch (...) { return 0.0f; }
        };
        style.transition_duration = parse_sec(val);
    } else if (prop == "transition-timing-function") {
        style.transition_timing_function = val;
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
                    style.transition_timing_function = token;
                }
            }
        }
    } else if (prop == "transform") {
        style.transform_str = val;
        style.has_transform = true;
    } else if (prop == "transform-origin") {
        style.transform_origin = val;
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
        while (!func.empty() && std::isspace(func.front())) func.erase(func.begin());
        while (!func.empty() && std::isspace(func.back())) func.pop_back();

        size_t close_paren = str.find(')', open_paren);
        if (close_paren == std::string::npos) break;
        std::string args_str = str.substr(open_paren + 1, close_paren - open_paren - 1);
        i = close_paren + 1;

        std::vector<std::string> args;
        std::stringstream ss(args_str);
        std::string arg;
        while (std::getline(ss, arg, ',')) {
            while (!arg.empty() && std::isspace(arg.front())) arg.erase(arg.begin());
            while (!arg.empty() && std::isspace(arg.back())) arg.pop_back();
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

    std::string result = value;
    size_t var_pos = 0;
    while ((var_pos = result.find("var(", var_pos)) != std::string::npos) {
        size_t content_start = var_pos + 4;
        int paren_depth = 1;
        size_t i = content_start;
        for (; i < result.size() && paren_depth > 0; ++i) {
            if (result[i] == '(') paren_depth++;
            else if (result[i] == ')') paren_depth--;
        }
        if (paren_depth != 0) {
            var_pos += 4;
            continue;
        }
        size_t content_end = i - 1;
        std::string inner = result.substr(content_start, content_end - content_start);

        std::string var_name;
        std::string fallback;
        bool has_fallback = false;
        int inner_depth = 0;
        size_t comma_idx = std::string::npos;
        for (size_t c = 0; c < inner.size(); ++c) {
            if (inner[c] == '(') inner_depth++;
            else if (inner[c] == ')') inner_depth--;
            else if (inner[c] == ',' && inner_depth == 0) {
                comma_idx = c;
                break;
            }
        }

        if (comma_idx != std::string::npos) {
            var_name = inner.substr(0, comma_idx);
            fallback = inner.substr(comma_idx + 1);
            has_fallback = true;
        } else {
            var_name = inner;
        }

        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        };
        trim(var_name);
        trim(fallback);

        std::string replacement;
        auto it = custom_props.find(var_name);
        if (it != custom_props.end()) {
            replacement = resolve_var_functions(it->second, custom_props, depth + 1);
        } else if (has_fallback) {
            replacement = resolve_var_functions(fallback, custom_props, depth + 1);
        } else {
            replacement = "";
        }

        result.replace(var_pos, content_end - var_pos + 1, replacement);
        var_pos += replacement.size();
    }
    return result;
}

namespace {
struct CalcLexer {
    std::string_view text;
    size_t pos{0};

    void skip_whitespace() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
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
};

static float parse_calc_primary(CalcLexer& lex, float ref_size);
static float parse_calc_term(CalcLexer& lex, float ref_size);
static float parse_calc_expr(CalcLexer& lex, float ref_size);

static float parse_calc_primary(CalcLexer& lex, float ref_size) {
    lex.skip_whitespace();
    if (!lex.has_more()) return 0.0f;

    char c = lex.peek();
    if (c == '+') {
        lex.get();
        return parse_calc_primary(lex, ref_size);
    }
    if (c == '-') {
        lex.get();
        return -parse_calc_primary(lex, ref_size);
    }
    if (c == '(') {
        lex.get(); // consume '('
        float val = parse_calc_expr(lex, ref_size);
        if (lex.peek() == ')') {
            lex.get(); // consume ')'
        }
        return val;
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

    if (token.ends_with("px")) {
        try { return std::stof(token.substr(0, token.size() - 2)); } catch (...) { return 0.0f; }
    }
    if (token.ends_with("%")) {
        try { return (std::stof(token.substr(0, token.size() - 1)) / 100.0f) * ref_size; } catch (...) { return 0.0f; }
    }
    if (token.ends_with("em") || token.ends_with("rem")) {
        try { return std::stof(token.substr(0, token.size() - (token.ends_with("rem") ? 3 : 2))) * 16.0f; } catch (...) { return 0.0f; }
    }
    if (token.ends_with("pt")) {
        try { return std::stof(token.substr(0, token.size() - 2)) * 1.33333f; } catch (...) { return 0.0f; }
    }

    try { return std::stof(token); } catch (...) { return 0.0f; }
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

float StyleResolver::evaluate_calc_expression(std::string_view expr, float reference_size) const {
    CalcLexer lex{expr, 0};
    return parse_calc_expr(lex, reference_size);
}

std::string StyleResolver::resolve_calc_functions(const std::string& value, float reference_size) const {
    std::string result = value;
    size_t calc_pos = 0;
    while ((calc_pos = result.find("calc(", calc_pos)) != std::string::npos) {
        size_t content_start = calc_pos + 5;
        int paren_depth = 1;
        size_t i = content_start;
        for (; i < result.size() && paren_depth > 0; ++i) {
            if (result[i] == '(') paren_depth++;
            else if (result[i] == ')') paren_depth--;
        }
        if (paren_depth != 0) {
            calc_pos += 5;
            continue;
        }
        size_t content_end = i - 1;
        std::string inner = result.substr(content_start, content_end - content_start);

        float val = evaluate_calc_expression(inner, reference_size);
        std::string replacement = std::to_string(val) + "px";

        result.replace(calc_pos, content_end - calc_pos + 1, replacement);
        calc_pos += replacement.size();
    }
    return result;
}

ComputedStyle StyleResolver::resolve_style(const std::shared_ptr<Element>& element, const ComputedStyle* parent_style) const {
    ComputedStyle style;

    // Inherit properties from parent
    if (parent_style) {
        style.color = parent_style->color;
        style.font_size = parent_style->font_size;
        style.font_weight = parent_style->font_weight;
        style.text_align = parent_style->text_align;
        style.custom_properties = parent_style->custom_properties;
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
                    if (matches_selector(element, sel)) {
                        auto spec = sel.specificity();
                        for (const auto& decl : rule.declarations) {
                            matched.push_back({spec, order++, &decl});
                        }
                    }
                }
            }
        }
    } else {
        // Light DOM: Author Stylesheets apply
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

        // Host element styling: If element has a shadow root, match :host rules from shadow stylesheets
        if (element->shadow_root()) {
            for (const auto& sheet : element->shadow_root()->parsed_stylesheets()) {
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
    if (style.animation_timing_function == "ease" || style.animation_timing_function == "ease-in-out") {
        t = t * t * (3.0f - 2.0f * t);
    } else if (style.animation_timing_function == "ease-in") {
        t = t * t;
    } else if (style.animation_timing_function == "ease-out") {
        t = t * (2.0f - t);
    } else if (style.animation_timing_function == "step" || style.animation_timing_function == "step-end") {
        t = (t >= 1.0f) ? 1.0f : 0.0f;
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
