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

} // namespace

// The walk keeps its place in a vector rather than on the native stack, so the
// depth of the tree does not matter. Each frame is a node whose children are
// being written, with the depth those children are written at; that is not
// always one more than the node's own depth (the children of a document or of
// a fragment are written at its depth, and the children of a template's
// contents two deeper than the template).
void dump_tree_webplatform(std::ostream& os, const Node& root) {
    struct Frame {
        const Node* node;
        std::size_t next;
        int child_depth;
    };
    std::vector<Frame> open;

    // Writes a node's own lines and opens a frame for what is inside it.
    auto write = [&](const Node& node, int depth) {
        switch (node.type()) {
            case NodeType::Document: {
                const auto& doc = static_cast<const Document&>(node);
                if (doc.has_doctype()) dump_doctype(os, doc, depth);
                open.push_back({&node, 0, depth});
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

                // The element's own children come after its template contents,
                // so their frame goes underneath.
                open.push_back({&node, 0, depth + 1});
                if (el.tag_name() == "template" && el.template_contents()) {
                    write_indent(os, depth + 1);
                    os << "content\n";
                    open.push_back({el.template_contents().get(), 0, depth + 2});
                }
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
                open.push_back({&node, 0, depth});
                break;
            case NodeType::DocumentType:
                break;      // never a child; the document's doctype is written below
        }
    };

    // A document node contributes its doctype and its children at depth 0,
    // the doctype where it was written: after the comments that came before
    // it. A fragment root contributes only its children.
    const Document* with_doctype = nullptr;
    if (root.type() == NodeType::Document) {
        const auto& document = static_cast<const Document&>(root);
        if (document.has_doctype()) with_doctype = &document;
    }
    open.push_back({&root, 0, 0});
    while (!open.empty()) {
        Frame& top = open.back();
        const auto& children = top.node->children();
        if (with_doctype && top.node == &root &&
            top.next >= std::min(with_doctype->doctype().preceding_children, children.size())) {
            dump_doctype(os, *with_doctype, 0);
            with_doctype = nullptr;
        }
        if (top.next >= children.size()) {
            open.pop_back();
            continue;
        }
        const Node* child = children[top.next++].get();
        const int depth = top.child_depth;
        // `top` is not used after this: write() may add frames.
        if (child) write(*child, depth);
    }
}

std::string dump_tree_webplatform(const Node& root) {
    std::ostringstream oss;
    dump_tree_webplatform(oss, root);
    return oss.str();
}

} // namespace litebird
