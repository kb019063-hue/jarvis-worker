#include "litebird/css/StyleResolver.hpp"
#include <unordered_set>
#include "litebird/css/CssParser.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include <sstream>
#include <cctype>
#include <cmath>

namespace litebird {

StyleSheet StyleResolver::default_user_agent_stylesheet() {
    const char* ua_css = R"(
        table { display: table; border-collapse: separate; }
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
    m_author_sheets.push_back(sheet);
    index_sheet(m_author_selector_index, m_author_sheets.back(),
                m_author_sheets.size() - 1, m_next_author_selector_order);
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
    return candidates;
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
        // resolve_calc_functions replaces every calc(), so this comes back here
        // at most once. It used to leave an unclosed `calc(` in place, and then
        // this function called itself until the stack ran out: one missing
        // parenthesis in a style attribute ended the process.
        if (resolved.find("calc(") != std::string::npos) return default_val;
        return parse_length(resolved, default_val);
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
        if (std::isspace(static_cast<unsigned char>(c))) continue;
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
    if (unit == "vw") return number * m_viewport_width / 100.0f;
    if (unit == "vh") return number * m_viewport_height / 100.0f;
    if (unit == "vmin") return number * std::min(m_viewport_width, m_viewport_height) / 100.0f;
    if (unit == "vmax") return number * std::max(m_viewport_width, m_viewport_height) / 100.0f;
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

static std::vector<FilterFunction> parse_css_filters(std::string_view val) {
    std::vector<FilterFunction> filters;
    std::string s(val);
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) i++;
        if (i >= s.size()) break;
        size_t open_p = s.find('(', i);
        if (open_p == std::string::npos) break;
        std::string name = s.substr(i, open_p - i);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();

        size_t close_p = s.find(')', open_p);
        if (close_p == std::string::npos) break;
        std::string arg = s.substr(open_p + 1, close_p - (open_p + 1));
        while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front()))) arg.erase(arg.begin());
        while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.back()))) arg.pop_back();

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
            std::stringstream ss(arg);
            std::string tok1, tok2, tok3;
            if (ss >> tok1 >> tok2) {
                try { fn.dx = std::stof(tok1); } catch (...) {}
                try { fn.dy = std::stof(tok2); } catch (...) {}
                if (ss >> tok3) {
                    if (std::isdigit(static_cast<unsigned char>(tok3[0])) || tok3[0] == '-') {
                        try { fn.blur_radius = std::stof(tok3); } catch (...) {}
                        std::string color_part;
                        std::getline(ss, color_part);
                        while (!color_part.empty() && std::isspace(static_cast<unsigned char>(color_part.front()))) color_part.erase(color_part.begin());
                        if (!color_part.empty()) {
                            fn.color = Color::parse(color_part);
                        } else {
                            fn.color = Color::black();
                        }
                    } else {
                        fn.color = Color::parse(tok3);
                    }
                }
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
    auto trim = [](std::string text) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
        return text;
    };
    auto split_tokens = [&](const std::string& text) {
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
        if (token.empty()) return false;
        if (token[0] == '#') return true;
        if (token.rfind("rgb", 0) == 0 || token.rfind("hsl", 0) == 0) return true;
        std::string lowered;
        for (char c : token) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        static const std::unordered_set<std::string> named = {
            "black", "white", "red", "green", "blue", "yellow", "orange", "purple",
            "gray", "grey", "silver", "maroon", "navy", "teal", "olive", "lime",
            "aqua", "fuchsia", "transparent", "currentcolor", "pink", "brown",
            "gold", "indigo", "violet", "cyan", "magenta", "beige", "ivory"};
        return named.count(lowered) > 0;
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
        for (const auto& raw_token : tokens) {
            const std::string token = trim(raw_token);
            if (token.empty()) continue;
            std::string lowered;
            for (char c : token) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lowered == "none" || lowered == "hidden" || lowered == "solid" || lowered == "dashed" ||
                lowered == "dotted" || lowered == "double" || lowered == "groove" ||
                lowered == "ridge" || lowered == "inset" || lowered == "outset") {
                continue;  // border styles are not rendered yet
            }
            if (is_color_token(token)) {
                out.emplace_back(prop == "border" ? "border-color" : prop + "-color", token);
            } else {
                out.emplace_back(prop == "border" ? "border-width" : prop + "-width", token);
            }
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

    // font: family defaults plus weight and size.
    if (prop == "font") {
        for (const auto& token : split_tokens(trim(val))) {
            std::string lowered;
            for (char c : token) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lowered == "bold" || lowered == "bolder") out.emplace_back("font-weight", "700");
            else if (lowered == "normal" || lowered == "lighter") out.emplace_back("font-weight", "400");
            else if (lowered == "italic" || lowered == "oblique") continue;
            else if (lowered.find_first_of("0123456789") != std::string::npos &&
                     (lowered.ends_with("px") || lowered.ends_with("em") || lowered.ends_with("rem") ||
                      lowered.ends_with("pt") || lowered.ends_with("%"))) {
                out.emplace_back("font-size", token);
            }
        }
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
    // CSS custom properties are not shorthands.
    std::vector<std::pair<std::string, std::string>> longhands;
    if (!prop.starts_with("--") && expand_shorthand(prop, raw_val, longhands)) {
        for (const auto& [longhand, longhand_value] : longhands) {
            apply_longhand(style, longhand, longhand_value);
        }
        return;
    }
    apply_longhand(style, prop, raw_val);
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
    if (val.find("calc(") != std::string::npos) {
        val = resolve_calc_functions(val, m_viewport_width);
    }


    if (prop == "display") {
        if (val == "block") style.display = Display::Block;
        else if (val == "inline") style.display = Display::Inline;
        else if (val == "flex") style.display = Display::Flex;
        else if (val == "grid") style.display = Display::Grid;
        else if (val == "none") style.display = Display::None;
        else if (val == "inline-block") style.display = Display::InlineBlock;
        else if (val == "inline-flex") style.display = Display::Flex;
        else if (val == "inline-grid") style.display = Display::Grid;
        else if (val == "table") style.display = Display::Table;
        else if (val == "table-row") style.display = Display::TableRow;
        else if (val == "table-cell") style.display = Display::TableCell;
        else if (val == "table-row-group" || val == "table-header-group" ||
                 val == "table-footer-group") style.display = Display::TableRowGroup;
    } else if (prop == "float") {
        if (val == "left") style.float_type = FloatType::Left;
        else if (val == "right") style.float_type = FloatType::Right;
        else style.float_type = FloatType::None;
    } else if (prop == "overflow" || prop == "overflow-x" || prop == "overflow-y") {
        if (val == "hidden") style.overflow = Overflow::Hidden;
        else if (val == "auto") style.overflow = Overflow::Auto;
        else if (val == "scroll") style.overflow = Overflow::Scroll;
        else style.overflow = Overflow::Visible;
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
    } else if (prop == "color") {
        style.color = Color::parse(val);
    } else if (prop == "background-color" || prop == "background") {
        style.background_color = Color::parse(val);
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
    } else if (prop == "border-top-width") {
        style.border_width.top = parse_length(val, 0.0f);
    } else if (prop == "border-right-width") {
        style.border_width.right = parse_length(val, 0.0f);
    } else if (prop == "border-bottom-width") {
        style.border_width.bottom = parse_length(val, 0.0f);
    } else if (prop == "border-left-width") {
        style.border_width.left = parse_length(val, 0.0f);
    } else if (prop == "border-color") {
        style.border_color = Color::parse(val);
    } else if (prop == "border-top-color" || prop == "border-right-color" ||
               prop == "border-bottom-color" || prop == "border-left-color") {
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
                while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
                while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
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

    // calc() inside calc() is a pair of parentheses.
    if (token == "calc" && lex.pos < lex.text.size() && lex.text[lex.pos] == '(') {
        lex.pos++;
        value = parse_calc_group(lex, ref_size);
        return negative ? -value : value;
    }

    try {
        if (token.ends_with("px")) {
            value = std::stof(token.substr(0, token.size() - 2));
        } else if (token.ends_with("%")) {
            value = (std::stof(token.substr(0, token.size() - 1)) / 100.0f) * ref_size;
        } else if (token.ends_with("em") || token.ends_with("rem")) {
            value = std::stof(token.substr(0, token.size() - (token.ends_with("rem") ? 3 : 2))) * 16.0f;
        } else if (token.ends_with("pt")) {
            value = std::stof(token.substr(0, token.size() - 2)) * 1.33333f;
        } else {
            value = std::stof(token);
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
    CalcLexer lex{expr, 0};
    const float value = parse_calc_expr(lex, reference_size);
    if (valid) *valid = !lex.invalid;
    return lex.invalid ? 0.0f : value;
}

// Replaces each calc(...) in `value` by its result in px. A calc() that is
// still open where the value ends is closed there, which is how CSS reads an
// unterminated function. One that nests too deeply to evaluate is replaced by
// a word no property accepts, so the declaration has no effect.
//
// The result is built by appending. (It used to be edited in place, and an
// unclosed `calc(` was skipped and searched for again from five characters
// further on, so a value made of N of them cost N*N steps.)
std::string StyleResolver::resolve_calc_functions(const std::string& value, float reference_size) const {
    std::string result;
    size_t at = 0;
    for (;;) {
        const size_t calc_pos = value.find("calc(", at);
        if (calc_pos == std::string::npos) {
            result.append(value, at, std::string::npos);
            break;
        }
        result.append(value, at, calc_pos - at);

        const size_t content_start = calc_pos + 5;
        int paren_depth = 1;
        size_t i = content_start;
        for (; i < value.size() && paren_depth > 0; ++i) {
            if (value[i] == '(') paren_depth++;
            else if (value[i] == ')') paren_depth--;
        }
        const bool closed = paren_depth == 0;
        const size_t content_end = closed ? i - 1 : value.size();

        bool valid = true;
        const float val = evaluate_calc_expression(
            std::string_view(value).substr(content_start, content_end - content_start), reference_size, &valid);
        result += valid ? std::to_string(val) + "px" : std::string("invalid");
        at = closed ? i : value.size();
    }
    return result;
}

ComputedStyle StyleResolver::resolve_style(const std::shared_ptr<Element>& element, const ComputedStyle* parent_style) const {
    ComputedStyle style;
    // Relative (em/ex/ch) lengths resolve against the inherited font size.
    m_em_base = parent_style ? parent_style->font_size : 16.0f;

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
            if (matches_selector(element, sel)) {
                auto spec = sel.specificity();
                for (const auto& decl : rule.declarations) {
                    matched.push_back({spec, order++, &decl});
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

    // The cascade, in ascending priority: author normal, inline normal,
    // author !important, inline !important. (CSS Cascade 4, "cascade sorting
    // order".) Within each tier the declarations are already sorted by
    // specificity and then source order.
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

    // 3. Inline style attribute (highest priority: specificity 1000, 0, 0)
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

    // Font size first: em/ex/ch lengths elsewhere on the element resolve
    // against its own computed font size, while font-size itself resolves
    // against the inherited one.
    auto sets_font_size = [](const std::string& prop) {
        return prop == "font-size" || prop == "font";
    };
    std::vector<const Declaration*> rest;
    for (const auto* declaration : cascade_order) {
        if (sets_font_size(declaration->property)) {
            apply_declaration(style, declaration->property, declaration->value);
        } else {
            rest.push_back(declaration);
        }
    }
    m_em_base = style.font_size;
    apply_all(rest);

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
