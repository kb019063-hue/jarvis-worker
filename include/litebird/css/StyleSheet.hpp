#pragma once

#include "litebird/common/Color.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <tuple>
#include <algorithm>

namespace litebird {

class SelectorList;

enum class Combinator : uint8_t {
    Descendant = 0,
    Child,
    AdjacentSibling,
    GeneralSibling
};

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
    std::shared_ptr<SelectorList> compiled_arg;
};

struct AttributeMatch {
    std::string name;
    std::string op; // "", "=", "~=", "|=", "^=", "$=", "*="
    std::string value;
    bool ignore_case{false};
};

struct SimpleSelector {
    std::string tag_name;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<PseudoClass> pseudo_classes;
    std::vector<AttributeMatch> attribute_matches;
    std::shared_ptr<SelectorList> compiled;
    bool has_dynamic_specificity{false};

    Specificity specificity() const {
        Specificity s;
        if (!id.empty()) s.ids += 1;
        const std::size_t attr_count = !attribute_matches.empty() ? attribute_matches.size() : attributes.size();
        s.classes += static_cast<int>(classes.size() + attr_count);
        for (const auto& pc : pseudo_classes) {
            if (pc.name != "where") s.classes += 1;
        }
        if (!tag_name.empty() && tag_name != "*") s.tags += 1;
        return s;
    }
};

enum class CombinatorKind : uint8_t {
    Single = 0,
    AllDescendant,
    AllDeterministic,
    AllGeneralSibling,
    Mixed
};

struct Selector {
    // Chain of simple selectors with combinators: e.g. "div > .card + p"
    std::vector<SimpleSelector> parts;
    std::vector<Combinator> combinators;
    std::shared_ptr<SelectorList> compiled;
    bool has_host{false};
    bool has_pseudo_element{false};
    bool metadata_computed{false};
    bool any_dynamic_specificity{false};
    CombinatorKind combinator_kind{CombinatorKind::Single};
    std::size_t min_ancestors{0};
    Specificity cached_specificity;

    void finalize_metadata() {
        metadata_computed = true;
        any_dynamic_specificity = false;
        min_ancestors = 0;
        cached_specificity = Specificity{};
        for (const auto& part : parts) {
            if (part.has_dynamic_specificity) any_dynamic_specificity = true;
            auto s = part.specificity();
            cached_specificity.ids += s.ids;
            cached_specificity.classes += s.classes;
            cached_specificity.tags += s.tags;
        }
        if (parts.size() <= 1) {
            combinator_kind = CombinatorKind::Single;
            return;
        }
        bool all_desc = true;
        bool all_det = true;
        bool all_gsib = true;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            Combinator c = (i < combinators.size()) ? combinators[i] : Combinator::Descendant;
            if (c == Combinator::Descendant || c == Combinator::Child) ++min_ancestors;
            if (c != Combinator::Descendant) all_desc = false;
            if (c != Combinator::Child && c != Combinator::AdjacentSibling) all_det = false;
            if (c != Combinator::GeneralSibling) all_gsib = false;
        }
        if (all_det) combinator_kind = CombinatorKind::AllDeterministic;
        else if (all_desc) combinator_kind = CombinatorKind::AllDescendant;
        else if (all_gsib) combinator_kind = CombinatorKind::AllGeneralSibling;
        else combinator_kind = CombinatorKind::Mixed;
    }

    Specificity specificity() const {
        if (metadata_computed) return cached_specificity;
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
    bool min_width_exclusive{false};
    bool max_width_exclusive{false};
    bool min_height_exclusive{false};
    bool max_height_exclusive{false};
    bool negated{false};
    bool always_false{false};
    int orientation{0}; // 0 = any, 1 = portrait, 2 = landscape
    std::vector<MediaQuery> alternatives;

    bool matches_single(float viewport_w, float viewport_h = 0.0f, const std::string& type = "screen") const {
        bool ok = true;
        if (always_false) ok = false;
        if (ok && !media_type.empty() && media_type != "all" && media_type != type) ok = false;
        if (ok && max_width >= 0.0f && (max_width_exclusive ? (viewport_w >= max_width) : (viewport_w > max_width))) ok = false;
        if (ok && min_width >= 0.0f && (min_width_exclusive ? (viewport_w <= min_width) : (viewport_w < min_width))) ok = false;
        if (ok && max_height >= 0.0f && (max_height_exclusive ? (viewport_h >= max_height) : (viewport_h > max_height))) ok = false;
        if (ok && min_height >= 0.0f && (min_height_exclusive ? (viewport_h <= min_height) : (viewport_h < min_height))) ok = false;
        if (ok && orientation == 1 && viewport_h < viewport_w) ok = false;
        if (ok && orientation == 2 && viewport_w < viewport_h) ok = false;
        return negated ? !ok : ok;
    }

    bool matches(float viewport_w, float viewport_h = 0.0f, const std::string& type = "screen") const {
        if (!alternatives.empty()) {
            for (const auto& alt : alternatives) {
                if (alt.matches_single(viewport_w, viewport_h, type)) return true;
            }
            return false;
        }
        return matches_single(viewport_w, viewport_h, type);
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
    std::string layer_name;
};

/// One entry of an `@font-face` `src` list.
///
/// A source is either a URL to fetch or `local()`, which names a font already
/// on the host. `format()` says what the bytes are *claimed* to be, and lets a
/// face the engine cannot parse be skipped before anything is fetched for it.
struct FontFaceSrc {
    std::string url;      ///< empty when this is a `local()`
    std::string local;    ///< the family name `local()` asks for; empty for a URL
    std::string format;   ///< lower-cased `format()` hint: "truetype", "woff", "" when none
};

/// One `@font-face` rule.
///
/// The family is the name the *rule* declares and not the name inside the file:
/// a page is entitled to call a font anything it likes, and two rules may serve
/// two weights of the same family from two different files. That is the whole
/// difference between a webfont and a face found on disk, where the file's own
/// `name` table is the only name there is.
struct FontFaceRule {
    std::string family;
    std::vector<FontFaceSrc> sources;
    int weight_min{400};   ///< both ends inclusive; `font-weight: 100 400` is a range
    int weight_max{400};
    bool italic{false};
};

struct StyleSheet {
    std::vector<Rule> rules;
    std::vector<KeyframesRule> keyframes;
    std::vector<FontFaceRule> font_faces;
    std::vector<std::string> layers;
};

} // namespace litebird
