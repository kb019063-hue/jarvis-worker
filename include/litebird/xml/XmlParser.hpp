#pragma once

// An XML 1.0 (Fifth Edition) parser with Namespaces in XML 1.0.
//
// What a browser's XML parser is: non-validating, namespace-aware, and it
// reads nothing but the text it is given. It checks every well-formedness
// constraint that can be checked without fetching anything, reads the
// internal DTD subset (entities and attribute defaults declared there are
// used; nothing is validated), and stops at the first error: XML has no error
// recovery, and a document that is not well formed has no tree.
//
// The parser knows nothing of the DOM. It reports what it reads to an
// XmlHandler; XmlDom.hpp has the handler that builds a tree. Nothing in it
// recurses over the input: elements nest on a vector, entity references
// inside entity values are expanded from a vector of sources, and a content
// model's parentheses are counted, so depth costs memory and never the
// native stack.
//
// See ARCHITECTURE.md, Phase 28.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace litebird {
namespace xml {

inline constexpr const char* kXmlNamespace = "http://www.w3.org/XML/1998/namespace";
inline constexpr const char* kXmlnsNamespace = "http://www.w3.org/2000/xmlns/";

/// The name of an element or an attribute after namespace processing.
struct XmlName {
    std::string qualified;      // as written: "svg:rect"
    std::string prefix;         // "svg"; empty for none
    std::string local;          // "rect"
    std::string namespace_uri;  // empty for no namespace
};

struct XmlAttribute {
    XmlName name;
    /// Normalized as section 3.3.3 says: references expanded, white space
    /// made spaces, and trimmed and collapsed when the DTD declares the
    /// attribute with a type other than CDATA.
    std::string value;
    /// False for a default the DTD supplied.
    bool specified{true};
};

class XmlHandler {
public:
    virtual ~XmlHandler() = default;
    /// `<!DOCTYPE name PUBLIC "public_id" "system_id">`, after its internal
    /// subset has been read.
    virtual void doctype(const std::string& name, const std::string& public_id, const std::string& system_id) {
        (void)name; (void)public_id; (void)system_id;
    }
    /// A notation declared in the internal subset. Nothing in the DOM shows
    /// it; the canonical form the conformance suite compares with does.
    virtual void notation(const std::string& name, const std::string* public_id, const std::string* system_id) {
        (void)name; (void)public_id; (void)system_id;
    }
    virtual void start_element(const XmlName& name, std::vector<XmlAttribute>& attributes) {
        (void)name; (void)attributes;
    }
    virtual void end_element(const XmlName& name) { (void)name; }
    /// Character data. Text between two tags can arrive in several calls
    /// (around a reference, at the edge of an entity); the receiver joins them.
    virtual void characters(std::string_view text) { (void)text; }
    /// The content of one `<![CDATA[ ]]>` section.
    virtual void cdata_section(std::string_view text) { (void)text; }
    virtual void comment(std::string_view text) { (void)text; }
    virtual void processing_instruction(const std::string& target, std::string_view data) {
        (void)target; (void)data;
    }
    /// A processing instruction inside the DOCTYPE. It is no part of the
    /// document's tree; the canonical form has it.
    virtual void dtd_processing_instruction(const std::string& target, std::string_view data) {
        (void)target; (void)data;
    }
};

struct XmlError {
    std::string message;
    /// Where in the text, counted from 1. Lines end at a line feed after
    /// line ends have been normalized; a column is a count of code points.
    std::size_t line{0};
    std::size_t column{0};
    /// decode_xml() only: the document names an encoding this build cannot
    /// read. That says nothing about whether the document is well formed.
    bool unsupported_encoding{false};

    /// "message at line L, column C".
    std::string describe() const;
};

struct XmlParseOptions {
    /// The text is the content of an element, not a document: what is
    /// assigned to innerHTML in an XML document. No prolog, any number of
    /// elements, text between them.
    bool fragment{false};
    /// For a fragment: the namespace prefixes in scope where it will go, the
    /// default namespace under the empty prefix.
    std::vector<std::pair<std::string, std::string>> namespaces;
    /// The entity names of HTML (`&nbsp;`, `&eacute;`) are declared when the
    /// DOCTYPE has one of the public identifiers of XHTML, as the HTML
    /// standard asks of a browser.
    bool xhtml_entities{true};
    /// A document may expand entities to this many times its own size once
    /// the expansion is over `expansion_threshold` bytes. Past that it is an
    /// attack (a "billion laughs"), and an error. The figures are Expat's.
    std::size_t expansion_factor{100};
    std::size_t expansion_threshold{8u * 1024u * 1024u};
};

/// Parses `text`, which is UTF-8, and reports to `handler`. False when the
/// text is not well formed: `error` then says why, and what the handler was
/// told up to that point describes no document.
bool parse_xml(std::string_view text, XmlHandler& handler, XmlError& error, const XmlParseOptions& options = {});

/// The bytes of an XML entity as UTF-8, by appendix F of XML 1.0: a byte
/// order mark decides, then the encoding declaration, then UTF-8.
/// `transport_charset` is the charset of a Content-Type header, which
/// overrides the declaration. `encoding` receives the name that was used.
/// False for an encoding this build cannot read, or one that contradicts
/// the bytes.
bool decode_xml(std::string_view bytes, std::string& utf8, std::string& encoding, XmlError& error,
                std::string_view transport_charset = {});

/// Whether `name` matches the Name production (the names of elements,
/// attributes, processing instruction targets), and the stricter QName of
/// namespaces: at most one colon, with a name on each side of it.
bool is_xml_name(std::string_view name);
bool is_xml_qname(std::string_view name);

} // namespace xml
} // namespace litebird
