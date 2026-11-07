#pragma once

#include "litebird/common/Color.hpp"
#include <string>
#include <vector>
#include <tuple>
#include <algorithm>

namespace litebird {

struct Specificity {
    int ids{0};
    int classes{0};
    int tags{0};

    auto to_tuple() const {
        return std::make_tuple(ids, classes, tags);
    }

    bool operator<(const Specificity& o) const {
        return to_tuple() < o.to_tuple();
    }
    bool operator>(const Specificity& o) const {
        return to_tuple() > o.to_tuple();
    }
    bool operator==(const Specificity& o) const {
        return to_tuple() == o.to_tuple();
    }
};

struct PseudoClass {
    std::string name; // "first-child", "last-child", "nth-child", "nth-of-type", "hover", "focus", "active", "disabled", "checked", "not"
    std::string argument;
};

struct SimpleSelector {
    std::string tag_name;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<PseudoClass> pseudo_classes;

    Specificity specificity() const {
        Specificity s;
        if (!id.empty()) s.ids += 1;
        s.classes += static_cast<int>(classes.size() + attributes.size() + pseudo_classes.size());
        if (!tag_name.empty() && tag_name != "*") s.tags += 1;
        return s;
    }
};

struct Selector {
    // Chain of simple selectors for descendant matching: e.g. "div .card p"
    std::vector<SimpleSelector> parts;

    Specificity specificity() const {
        Specificity total;
        for (const auto& part : parts) {
            auto s = part.specificity();
            total.ids += s.ids;
            total.classes += s.classes;
            total.tags += s.tags;
        }
        return total;
    }
};

struct Declaration {
    std::string property;
    std::string value;
    bool important{false};
};

struct MediaQuery {
    float max_width{-1.0f};
    float min_width{-1.0f};
    float max_height{-1.0f};
    float min_height{-1.0f};
    std::string media_type{"all"};

    bool matches(float viewport_w, float viewport_h = 0.0f, const std::string& type = "screen") const {
        if (!media_type.empty() && media_type != "all" && media_type != type) return false;
        if (max_width >= 0.0f && viewport_w > max_width) return false;
        if (min_width >= 0.0f && viewport_w < min_width) return false;
        if (max_height >= 0.0f && viewport_h > max_height) return false;
        if (min_height >= 0.0f && viewport_h < min_height) return false;
        return true;
    }
};

struct Keyframe {
    float offset{0.0f}; // 0.0 to 1.0
    std::vector<Declaration> declarations;
};

struct KeyframesRule {
    std::string name;
    std::vector<Keyframe> keyframes;
};

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    std::vector<MediaQuery> media_queries;
};

struct StyleSheet {
    std::vector<Rule> rules;
    std::vector<KeyframesRule> keyframes;
};

} // namespace litebird
