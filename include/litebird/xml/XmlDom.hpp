#pragma once

// XML and the DOM: a tree from text, and text from a tree.
//
// Parsing is XmlParser's events made into nodes. Serialization is the
// algorithm of "DOM Parsing and Serialization" (now part of the HTML
// standard): what XMLSerializer writes for any tree, and what innerHTML
// reads back in an XML document. Both are loops; neither recurses over the
// tree.

#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/xml/XmlParser.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace litebird {
namespace xml {

inline constexpr const char* kXhtmlNamespace = "http://www.w3.org/1999/xhtml";
inline constexpr const char* kSvgNamespace = "http://www.w3.org/2000/svg";
inline constexpr const char* kMathmlNamespace = "http://www.w3.org/1998/Math/MathML";

/// The namespace of an element as its URI; empty for an element in none.
const std::string& element_namespace_uri(const Element& element);

/// A new element of `document` with this namespace, prefix and local name,
/// the name kept exactly as given. An empty namespace is none.
std::shared_ptr<Element> make_element(const std::shared_ptr<Document>& document, const std::string& namespace_uri,
                                      const std::string& prefix, const std::string& local_name);

/// The DOM standard's "locate a namespace": what `prefix` means at `node`,
/// the default namespace for the empty prefix. Empty when it means nothing.
std::string locate_namespace(const Node& node, const std::string& prefix);
/// "Locate a namespace prefix": a prefix bound to `namespace_uri` at
/// `element`. False when there is none.
bool locate_prefix(const Element& element, const std::string& namespace_uri, std::string& prefix);

/// Parses `text` into `document`, which has no children and whose content
/// type is an XML one. False when the text is not well formed; the document
/// then holds part of a tree, and the caller empties it.
bool parse_xml_document(const std::shared_ptr<Document>& document, std::string_view text, XmlError& error);

/// What `text` parses to as the content of `context`: a document fragment
/// owned by `document`, or null when the text is not well formed there. The
/// namespace prefixes in scope at `context` are in scope in the text.
std::shared_ptr<Node> parse_xml_fragment(const std::shared_ptr<Document>& document, const Element& context,
                                         std::string_view text, XmlError& error);

/// The XML serialization of `node` and what is below it. With
/// `require_well_formed` the result is checked to be something an XML parser
/// reads back, and false is returned when it cannot be (a comment with "--"
/// in it, a name that is no name).
bool serialize_xml(const Node& node, bool require_well_formed, std::string& out);
/// The same for the children of `node` and not `node` itself.
bool serialize_xml_children(const Node& node, bool require_well_formed, std::string& out);

} // namespace xml
} // namespace litebird
