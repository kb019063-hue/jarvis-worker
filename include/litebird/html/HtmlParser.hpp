#pragma once

#include "litebird/dom/Document.hpp"
#include "litebird/html/HtmlTreeBuilder.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace litebird {

/// Thin facade over HtmlTreeBuilder, which implements the WHATWG HTML
/// parsing algorithm.
class HtmlParser {
public:
    static std::shared_ptr<Document> parse(std::string_view html, bool scripting = true);

    /// Fragment parsing with a context element given by name, returning a
    /// DocumentFragment. The corpus runner and the tests use this.
    static std::shared_ptr<Node> parse_fragment_with_context(
        std::string_view html,
        const std::string& context_tag,
        ElementNamespace context_namespace = ElementNamespace::HTML,
        bool scripting = true);

    /// What `context.innerHTML = html` puts into the element: `html` parsed
    /// as the content of `context`. The same markup is different nodes in
    /// different elements: "<td>x" is a cell in a <tr> and the text "x" in a
    /// <div>, "<b>" is an element in a <p> and three characters in a
    /// <textarea>, and "<circle>" is an SVG element only inside an <svg>.
    static std::vector<std::shared_ptr<Node>> parse_inner_html(const Element& context, std::string_view html);

    /// Fragment helper for a shadow root: parses in a <body> context and
    /// hands back the child nodes directly. Every element that may host a
    /// shadow root parses like a <body>; a form above the host is not
    /// looked for, so a <form> in the markup is always kept.
    static std::vector<std::shared_ptr<Node>> parse_fragment(
        std::string_view html,
        std::vector<std::string>* out_stylesheets = nullptr);
};

} // namespace litebird
