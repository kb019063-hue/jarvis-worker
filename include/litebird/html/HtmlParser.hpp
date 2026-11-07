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

    /// Fragment parsing with an explicit context element, returning a
    /// DocumentFragment. This is what `Element.innerHTML` uses.
    static std::shared_ptr<Node> parse_fragment_with_context(
        std::string_view html,
        const std::string& context_tag,
        ElementNamespace context_namespace = ElementNamespace::HTML,
        bool scripting = true);

    /// Legacy fragment helper: parses in a <body> context and hands back the
    /// child nodes directly.
    static std::vector<std::shared_ptr<Node>> parse_fragment(
        std::string_view html,
        std::vector<std::string>* out_stylesheets = nullptr);
};

} // namespace litebird
