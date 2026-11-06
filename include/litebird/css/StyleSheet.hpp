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

struct SimpleSelector {
    std::string tag_name;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attributes;

    Specificity specificity() const {
        Specificity s;
        if (!id.empty()) s.ids += 1;
        s.classes += static_cast<int>(classes.size() + attributes.size());
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
};

struct MediaQuery {
    float max_width{-1.0f};
    float min_width{-1.0f};
    std::string media_type{"all"};

    bool matches(float viewport_w, float /*viewport_h*/ = 0.0f, const std::string& type = "screen") const {
        if (!media_type.empty() && media_type != "all" && media_type != type) return false;
        if (max_width >= 0.0f && viewport_w > max_width) return false;
        if (min_width >= 0.0f && viewport_w < min_width) return false;
        return true;
    }
};

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    std::vector<MediaQuery> media_queries;
};

struct StyleSheet {
    std::vector<Rule> rules;
};

} // namespace litebird
