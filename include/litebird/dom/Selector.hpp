#pragma once

// Selectors for scripts: querySelector, querySelectorAll, matches, closest.
//
// The stylesheet cascade has its own selector type (css/StyleSheet.hpp), built
// for matching every rule against every element quickly and for a parser that
// must keep going after anything it does not understand. A script's selector
// is the opposite case: one selector, usually matched once, and a selector
// that does not parse is an error the script must be told about (a
// SyntaxError). The cascade's matcher also knows nothing about what only a
// live page has: whether a checkbox is checked now, which element has focus,
// what `:scope` is.
//
// So this is a second, complete implementation of Selectors Level 4 as pages
// use it: compound selectors with type, id, class and attribute parts
// (all seven operators and the `i` flag), the four combinators, selector
// lists, the structural pseudo-classes including `:nth-child(an+b of S)`,
// `:not()`, `:is()`, `:where()`, `:has()`, `:scope`, `:root`, `:empty`, the
// form states (`:checked`, `:disabled`, `:required`, `:valid`, ...), the link
// and focus states, and `:defined`. A pseudo-element parses and matches no
// element, as the standard says for these methods.
//
// Matching does not recurse over the tree and keeps no state between calls.

#include "litebird/dom/Element.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace litebird {

class SelectorList;

namespace selector_detail {

struct AttributePart {
    std::string name;           // as written; compared lowercased on HTML elements
    std::string name_lower;
    char op{0};                 // 0 presence, '=' exact, '~' word, '|' prefix-dash, '^', '$', '*'
    std::string value;
    bool ignore_case{false};
};

enum class Pseudo {
    FirstChild, LastChild, OnlyChild, NthChild, NthLastChild,
    FirstOfType, LastOfType, OnlyOfType, NthOfType, NthLastOfType,
    Not, Is, Has, Empty, Root, Scope,
    Checked, Disabled, Enabled, Required, Optional, ReadOnly, ReadWrite, PlaceholderShown,
    Valid, Invalid, Link, Hover, Active, Focus, FocusWithin, Target, Defined,
    Never                       // a pseudo-element, or a state nothing here is ever in
};

struct PseudoPart {
    Pseudo kind{Pseudo::Never};
    int a{0};                   // an+b
    int b{0};
    std::shared_ptr<SelectorList> list;     // :not, :is, :where, :has, and `of S`
    bool no_specificity{false};             // :where(): matches like :is(), counts for nothing
};

struct Compound {
    bool any_type{true};
    std::string type;           // as written
    std::string type_lower;
    std::vector<std::string> ids;
    std::vector<std::string> classes;
    std::vector<AttributePart> attributes;
    std::vector<PseudoPart> pseudos;
};

struct Complex {
    /// Left to right. combinators[i] stands between compounds[i] and
    /// compounds[i + 1]: ' ' descendant, '>' child, '+' next sibling,
    /// '~' later sibling.
    std::vector<Compound> compounds;
    std::vector<char> combinators;
    /// Inside :has() only: how the first compound relates to the element
    /// :has() is asked about. ' ' when none was written.
    char leading{' '};
};

} // namespace selector_detail

class SelectorList {
public:
    /// Parses a selector list. False when it is not a valid one; `out` is
    /// then unusable.
    static bool parse(std::string_view text, SelectorList& out);

    /// Whether `element` matches any selector of the list. `scope` is what
    /// `:scope` matches: the element a query was started on. With none,
    /// `:scope` matches the root element, like `:root`.
    bool matches(const Element& element, const Node* scope = nullptr) const;

    /// For the cascade: the specificity of the most specific selector of the
    /// list that `element` matches, as ids * 1,000,000 + classes * 1,000 +
    /// types (attribute selectors and pseudo-classes count as classes), or -1
    /// when it matches none.
    long matching_specificity(const Element& element, const Node* scope = nullptr) const;

    /// The descendants of `root` that match, in tree order; `root` itself is
    /// never among them. `first_only` stops at the first.
    std::vector<std::shared_ptr<Element>> select(const Node& root, bool first_only) const;

    bool empty() const { return m_selectors.empty(); }

private:
    friend struct SelectorParser;
    friend struct SelectorMatcher;
    friend struct SelectorSpecificity;
    std::vector<selector_detail::Complex> m_selectors;
    bool m_relative{false};     // the list of a :has()
};

/// CSS.escape(): `text` written as a CSS identifier.
std::string css_escape_identifier(std::string_view text);

} // namespace litebird
