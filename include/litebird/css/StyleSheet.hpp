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

    Specificity specificity() const {
        Specificity s;
        if (!id.empty()) s.ids += 1;
        s.classes += static_cast<int>(classes.size());
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

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
};

struct StyleSheet {
    std::vector<Rule> rules;
};

} // namespace litebird
