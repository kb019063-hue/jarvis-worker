#include "litebird/html/HtmlParser.hpp"

#include "litebird/dom/Text.hpp"

namespace litebird {

std::shared_ptr<Document> HtmlParser::parse(std::string_view html, bool scripting) {
    HtmlTreeBuilder builder;
    builder.set_scripting(scripting);
    return builder.parse(html);
}

std::shared_ptr<Node> HtmlParser::parse_fragment_with_context(std::string_view html,
                                                              const std::string& context_tag,
                                                              ElementNamespace context_namespace,
                                                              bool scripting) {
    HtmlTreeBuilder builder;
    builder.set_scripting(scripting);
    return builder.parse_fragment(html, context_tag, context_namespace);
}

std::vector<std::shared_ptr<Node>> HtmlParser::parse_inner_html(const Element& context, std::string_view html) {
    HtmlTreeBuilder builder;
    return builder.parse_fragment(html, context)->take_children();
}

std::vector<std::shared_ptr<Node>> HtmlParser::parse_fragment(std::string_view html,
                                                              std::vector<std::string>* out_stylesheets) {
    auto fragment = parse_fragment_with_context(html, "body");
    // Detach so callers can re-parent the nodes freely. All at once: taking
    // them off the front one by one moved every later sibling each time, and
    // `innerHTML` with 100,000 elements in it took 7 seconds.
    std::vector<std::shared_ptr<Node>> nodes = fragment->take_children();
    if (out_stylesheets) {
        for (const auto& node : nodes) {
            if (node->is_element() && static_cast<Element*>(node.get())->tag_name() == "style") {
                out_stylesheets->push_back(node->text_content());
            }
        }
    }
    return nodes;
}

} // namespace litebird
