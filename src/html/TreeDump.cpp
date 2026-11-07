#include "litebird/html/TreeDump.hpp"
#include "litebird/dom/Comment.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <vector>

namespace litebird {

namespace {

const char* namespace_prefix(ElementNamespace ns) {
    switch (ns) {
        case ElementNamespace::SVG: return "svg ";
        case ElementNamespace::MathML: return "math ";
        case ElementNamespace::HTML: break;
    }
    return "";
}

void write_indent(std::ostream& os, int depth) {
    os << "| ";
    for (int i = 0; i < depth; ++i) os << "  ";
}

void dump_doctype(std::ostream& os, const Document& doc, int depth) {
    write_indent(os, depth);
    os << "<!DOCTYPE ";
    const auto& dt = doc.doctype();
    os << dt.name;
    if (!dt.public_id.empty() || !dt.system_id.empty()) {
        os << " \"" << dt.public_id << "\" \"" << dt.system_id << "\"";
    }
    os << ">\n";
}

void dump_node(std::ostream& os, const Node& node, int depth);

void dump_children(std::ostream& os, const Node& parent, int depth) {
    for (const auto& child : parent.children()) {
        dump_node(os, *child, depth);
    }
}

void dump_node(std::ostream& os, const Node& node, int depth) {
    switch (node.type()) {
        case NodeType::Document: {
            const auto& doc = static_cast<const Document&>(node);
            if (doc.has_doctype()) dump_doctype(os, doc, depth);
            dump_children(os, node, depth);
            break;
        }
        case NodeType::Element: {
            const auto& el = static_cast<const Element&>(node);
            write_indent(os, depth);
            os << "<" << namespace_prefix(el.element_namespace()) << el.tag_name() << ">\n";

            // Attributes are emitted sorted by name, each on its own line.
            std::vector<std::pair<std::string, std::string>> attrs(
                el.attributes().begin(), el.attributes().end());
            std::sort(attrs.begin(), attrs.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; })
            ;
            for (const auto& [name, value] : attrs) {
                write_indent(os, depth + 1);
                os << name << "=\"" << value << "\"\n";
            }

            if (el.tag_name() == "template" && el.template_contents()) {
                write_indent(os, depth + 1);
                os << "content\n";
                dump_children(os, *el.template_contents(), depth + 2);
            }
            dump_children(os, node, depth + 1);
            break;
        }
        case NodeType::Text: {
            const auto& text = static_cast<const Text&>(node);
            write_indent(os, depth);
            os << "\"" << text.text() << "\"\n";
            break;
        }
        case NodeType::Comment: {
            const auto& comment = static_cast<const Comment&>(node);
            write_indent(os, depth);
            os << "<!-- " << comment.data() << " -->\n";
            break;
        }
        case NodeType::DocumentFragment:
            dump_children(os, node, depth);
            break;
    }
}

} // namespace

void dump_tree_webplatform(std::ostream& os, const Node& root) {
    // A document node contributes its doctype, then its children at depth 0;
    // a fragment root contributes only its children.
    if (root.type() == NodeType::Document) {
        const auto& document = static_cast<const Document&>(root);
        if (document.has_doctype()) dump_doctype(os, document, 0);
    }
    dump_children(os, root, 0);
}

std::string dump_tree_webplatform(const Node& root) {
    std::ostringstream oss;
    dump_tree_webplatform(oss, root);
    return oss.str();
}

} // namespace litebird
