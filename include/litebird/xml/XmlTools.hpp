#pragma once

// Two ways of writing down what the XML parser read, for checking it against
// something else. Neither is used by the browser.

#include "litebird/xml/XmlParser.hpp"

#include <string>
#include <string_view>

namespace litebird {
namespace xml {

/// The document in James Clark's canonical form, with the notations of the
/// DTD in front when it declares any ("second canonical form"): the form the
/// W3C XML Conformance Test Suite gives its expected output in. Attributes
/// sorted by name, text and values escaped one way, no comments.
bool xml_canonical_form(std::string_view utf8, std::string& out, XmlError& error);

/// One line for each thing the parser reported, names with their
/// namespaces, adjacent text joined: something two other parsers can be
/// made to print too (tools/fuzz/diff_xml.py).
bool xml_event_dump(std::string_view utf8, std::string& out, XmlError& error);

} // namespace xml
} // namespace litebird
