#include "litebird/xml/XmlDom.hpp"

#include "litebird/dom/Comment.hpp"
#include "litebird/dom/ProcessingInstruction.hpp"
#include "litebird/dom/Text.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <utility>
#include <vector>

namespace litebird {
namespace xml {

// ---------------------------------------------------------------------------
// Namespaces of nodes
// ---------------------------------------------------------------------------

const std::string& element_namespace_uri(const Element& element) {
    static const std::string kHtml = kXhtmlNamespace;
    static const std::string kSvg = kSvgNamespace;
    static const std::string kMathml = kMathmlNamespace;
    switch (element.element_namespace()) {
        case ElementNamespace::HTML: return kHtml;
        case ElementNamespace::SVG: return kSvg;
        case ElementNamespace::MathML: return kMathml;
        case ElementNamespace::Other: break;
    }
    return element.other_namespace_uri();
}

std::shared_ptr<Element> make_element(const std::shared_ptr<Document>& document, const std::string& namespace_uri,
                                      const std::string& prefix, const std::string& local_name) {
    ElementNamespace kind = ElementNamespace::Other;
    if (namespace_uri == kXhtmlNamespace) kind = ElementNamespace::HTML;
    else if (namespace_uri == kSvgNamespace) kind = ElementNamespace::SVG;
    else if (namespace_uri == kMathmlNamespace) kind = ElementNamespace::MathML;
    auto element = std::make_shared<Element>(local_name, kind);
    // The constructor lower-cases an HTML name, which is right for markup
    // read by the HTML parser and wrong here: in XML, <DIV> is not <div>.
    element->set_tag_name_verbatim(local_name);
    element->set_prefix(prefix);
    if (kind == ElementNamespace::Other) element->set_other_namespace_uri(namespace_uri);
    element->set_owner_document(document);
    if (kind == ElementNamespace::HTML && local_name == "template" && document) {
        auto contents = std::make_shared<Node>(NodeType::DocumentFragment);
        contents->set_owner_document(document->template_document());
        element->set_template_contents(contents);
    }
    return element;
}

namespace {

const Element* element_for_lookup(const Node& node, std::shared_ptr<Node>& hold) {
    if (node.is_element()) return static_cast<const Element*>(&node);
    if (node.is_document()) {
        hold = static_cast<const Document&>(node).document_element();
        return static_cast<const Element*>(hold.get());
    }
    if (node.is_document_fragment() || node.is_document_type()) return nullptr;
    hold = node.parent();
    return hold && hold->is_element() ? static_cast<const Element*>(hold.get()) : nullptr;
}

} // namespace

std::string locate_namespace(const Node& node, const std::string& prefix) {
    std::shared_ptr<Node> hold;
    const Element* element = element_for_lookup(node, hold);
    // "xml" and "xmlns" mean what they always mean, at an element: a
    // fragment, a doctype, a document with no root and a text node outside
    // any element have no namespaces at all.
    if (!element) return std::string();
    if (prefix == "xml") return kXmlNamespace;
    if (prefix == "xmlns") return kXmlnsNamespace;
    const std::string declaration = prefix.empty() ? std::string("xmlns") : "xmlns:" + prefix;
    while (element) {
        const std::string& own = element_namespace_uri(*element);
        if (!own.empty() && element->prefix() == prefix) return own;
        if (const std::string* value = element->attribute(declaration)) {
            if (element->attribute_namespace(declaration) == kXmlnsNamespace) return *value;
        }
        auto parent = element->parent();
        if (!parent || !parent->is_element()) break;
        hold = parent;
        element = static_cast<const Element*>(hold.get());
    }
    return std::string();
}

bool locate_prefix(const Element& start, const std::string& namespace_uri, std::string& prefix) {
    const Element* element = &start;
    std::shared_ptr<Node> hold;
    while (element) {
        if (element_namespace_uri(*element) == namespace_uri && !element->prefix().empty()) {
            prefix = element->prefix();
            return true;
        }
        for (const auto& attribute : element->attributes()) {
            if (attribute.name.compare(0, 6, "xmlns:") != 0 || attribute.value != namespace_uri) continue;
            if (attribute.namespace_uri() != kXmlnsNamespace) continue;
            prefix = attribute.name.substr(6);
            return true;
        }
        auto parent = element->parent();
        if (!parent || !parent->is_element()) break;
        hold = parent;
        element = static_cast<const Element*>(hold.get());
    }
    return false;
}

// ---------------------------------------------------------------------------
// Text to tree
// ---------------------------------------------------------------------------

namespace {

class TreeBuilder : public XmlHandler {
public:
    TreeBuilder(std::shared_ptr<Document> document, std::shared_ptr<Node> root)
        : m_document(std::move(document)), m_root(std::move(root)) {
        m_open.push_back(m_root);
    }

    void doctype(const std::string& name, const std::string& public_id, const std::string& system_id) override {
        m_text = nullptr;
        if (!m_root->is_document()) return;
        Doctype doctype;
        doctype.present = true;
        doctype.name = name;
        doctype.public_id = public_id;
        doctype.system_id = system_id;
        doctype.preceding_children = m_root->children().size();
        static_cast<Document&>(*m_root).set_doctype(std::move(doctype));
    }

    void start_element(const XmlName& name, std::vector<XmlAttribute>& attributes) override {
        m_text = nullptr;
        auto element = make_element(m_document, name.namespace_uri, name.prefix, name.local);
        for (auto& attribute : attributes) {
            element->set_attribute_ns(attribute.name.namespace_uri, attribute.name.qualified, std::move(attribute.value));
        }
        m_open.back()->append_child(element);
        // What is inside a <template> of XHTML goes into its contents.
        if (element->template_contents()) m_open.push_back(element->template_contents());
        else m_open.push_back(element);
    }

    void end_element(const XmlName&) override {
        m_text = nullptr;
        m_open.pop_back();
    }

    void characters(std::string_view text) override {
        if (text.empty()) return;
        if (m_text) {
            m_text->append_text(std::string(text));
            return;
        }
        auto node = std::make_shared<Text>(std::string(text));
        node->set_owner_document(m_document);
        m_text = node.get();
        m_open.back()->append_child(node);
    }

    void cdata_section(std::string_view text) override {
        m_text = nullptr;
        auto node = std::make_shared<Text>(std::string(text));
        node->set_cdata_section(true);
        node->set_owner_document(m_document);
        m_open.back()->append_child(node);
    }

    void comment(std::string_view text) override {
        m_text = nullptr;
        auto node = std::make_shared<Comment>(std::string(text));
        node->set_owner_document(m_document);
        m_open.back()->append_child(node);
    }

    void processing_instruction(const std::string& target, std::string_view data) override {
        m_text = nullptr;
        auto node = std::make_shared<ProcessingInstruction>(target, std::string(data));
        node->set_owner_document(m_document);
        m_open.back()->append_child(node);
    }

private:
    std::shared_ptr<Document> m_document;
    std::shared_ptr<Node> m_root;
    std::vector<std::shared_ptr<Node>> m_open;
    /// The text node that character data is being added to; null after
    /// anything that is not character data.
    Text* m_text{nullptr};
};

} // namespace

bool parse_xml_document(const std::shared_ptr<Document>& document, std::string_view text, XmlError& error) {
    TreeBuilder builder(document, document);
    return parse_xml(text, builder, error);
}

std::shared_ptr<Node> parse_xml_fragment(const std::shared_ptr<Document>& document, const Element& context,
                                         std::string_view text, XmlError& error) {
    XmlParseOptions options;
    options.fragment = true;
    // Every prefix that means something at the context, the nearest
    // declaration of each, and the default namespace.
    std::unordered_set<std::string> seen;
    auto add = [&](const std::string& prefix, const std::string& uri) {
        if (prefix == "xml" || prefix == "xmlns") return;
        if (!seen.insert(prefix).second) return;
        if (!uri.empty()) options.namespaces.emplace_back(prefix, uri);
    };
    std::shared_ptr<Node> hold;
    for (const Element* at = &context; at;) {
        if (!element_namespace_uri(*at).empty()) add(at->prefix(), element_namespace_uri(*at));
        for (const auto& attribute : at->attributes()) {
            if (attribute.namespace_uri() != kXmlnsNamespace) continue;
            if (attribute.name == "xmlns") add(std::string(), attribute.value);
            else if (attribute.name.compare(0, 6, "xmlns:") == 0) add(attribute.name.substr(6), attribute.value);
        }
        auto parent = at->parent();
        if (!parent || !parent->is_element()) break;
        hold = parent;
        at = static_cast<const Element*>(hold.get());
    }
    auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
    fragment->set_owner_document(document);
    TreeBuilder builder(document, fragment);
    if (!parse_xml(text, builder, error, options)) return nullptr;
    return fragment;
}

// ---------------------------------------------------------------------------
// Tree to text
// ---------------------------------------------------------------------------

namespace {

struct CannotSerialize {};

// Every character is one XML allows ([2] Char). The text is the engine's
// UTF-8; a lone surrogate, which a script can make, is not a character.
bool is_xml_text(const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
        if (c == 0xED && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0xA0) return false;
        if (c == 0xEF && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xBF &&
            static_cast<unsigned char>(text[i + 2]) >= 0xBE) {
            return false;
        }
    }
    return true;
}

// "Serializing an attribute value". Tab, line feed and carriage return are
// written as references because an XML parser turns the characters
// themselves into spaces. ">" is replaced as well: the draft's list lost it
// in the rewrite of September 2026, while its own note still says it is
// replaced, the test written a month earlier asserts it, and every browser
// does it.
void append_attribute_value(const std::string& value, bool require_well_formed, std::string& out) {
    if (require_well_formed && !is_xml_text(value)) throw CannotSerialize{};
    for (const char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\t': out += "&#9;"; break;
            case '\n': out += "&#xA;"; break;
            case '\r': out += "&#xD;"; break;
            default: out += c;
        }
    }
}

void append_text(const std::string& text, bool require_well_formed, std::string& out) {
    if (require_well_formed && !is_xml_text(text)) throw CannotSerialize{};
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
}

// The "namespace prefix map" of the standard: which prefixes stand for which
// namespace at the element being written.
//
// The standard copies the map at every element and never takes anything out
// of it. Here there is one map for the whole walk, and what an element
// declares is taken back when the walk leaves it: a copy per element costs
// the size of the map each time, and 50,000 declarations on a root with
// 50,000 children would cost their product.
//
// And a prefix stands for one namespace at a time. The standard's map keeps
// "p" among the prefixes of namespace A after a nearer element has declared
// p to be B, and then writes p: for something in A, which a parser reads as
// B (issues 45 and 75 of w3c/DOM-Parsing). Declaring a prefix here takes it
// away from the namespace it stood for, until the walk leaves the element.
class Scope {
public:
    /// The namespace `prefix` stands for; null when it stands for none.
    const std::string* bound(const std::string& prefix) const {
        const auto found = m_bound.find(prefix);
        return found == m_bound.end() ? nullptr : &found->second.uri;
    }

    /// "Found in map": `prefix` stands for `uri`.
    bool has(const std::string& uri, const std::string& prefix) const {
        const std::string* now = bound(prefix);
        return now && *now == uri;
    }

    /// "Retrieve a preferred prefix string": `wanted` when it stands for
    /// `uri`, else the prefix declared for it last. False when there is none.
    bool preferred(const std::string& uri, const std::string& wanted, std::string& out) const {
        if (!wanted.empty() && has(uri, wanted)) {
            out = wanted;
            return true;
        }
        const auto list = m_prefixes.find(uri);
        if (list == m_prefixes.end() || list->second.empty()) return false;
        out = list->second.rbegin()->second;
        return true;
    }

    /// "Add": `prefix` stands for `uri` from here on.
    void add(const std::string& prefix, const std::string& uri) {
        Undo undo{prefix, false, {}};
        const auto found = m_bound.find(prefix);
        if (found != m_bound.end()) {
            if (found->second.uri == uri) return;
            undo.had = true;
            undo.old = found->second;
            m_prefixes[undo.old.uri].erase(undo.old.order);
        }
        const Binding binding{uri, m_next_order++};
        m_prefixes[uri].emplace(binding.order, prefix);
        m_bound[prefix] = binding;
        m_log.push_back(std::move(undo));
    }

    std::size_t mark() const { return m_log.size(); }

    /// Takes back everything added since `mark`.
    void rewind(std::size_t mark) {
        while (m_log.size() > mark) {
            Undo& undo = m_log.back();
            const auto found = m_bound.find(undo.prefix);
            m_prefixes[found->second.uri].erase(found->second.order);
            if (undo.had) {
                m_prefixes[undo.old.uri].emplace(undo.old.order, undo.prefix);
                found->second = undo.old;
            } else {
                m_bound.erase(found);
            }
            m_log.pop_back();
        }
    }

private:
    struct Binding {
        std::string uri;
        /// When it was declared: the latest for a namespace is preferred.
        std::size_t order;
    };
    struct Undo {
        std::string prefix;
        bool had;
        Binding old;
    };
    std::unordered_map<std::string, Binding> m_bound;
    std::unordered_map<std::string, std::map<std::size_t, std::string>> m_prefixes;
    std::vector<Undo> m_log;
    std::size_t m_next_order{0};
};

bool is_void_html(const std::string& name) {
    static const char* const kVoid[] = {"area", "base", "basefont", "bgsound", "br", "col", "embed", "frame", "hr",
                                        "img", "input", "keygen", "link", "menuitem", "meta", "param", "source",
                                        "track", "wbr"};
    for (const char* candidate : kVoid) {
        if (name == candidate) return true;
    }
    return false;
}

// "The serialization of the ID": the other quote when the identifier holds
// a double one.
void append_quoted_id(const std::string& id, std::string& out) {
    const char quote = id.find('"') != std::string::npos ? '\'' : '"';
    out += quote;
    out += id;
    out += quote;
}

class Serializer {
public:
    Serializer(bool require_well_formed, std::string& out) : m_strict(require_well_formed), m_out(out) {
        m_scope.add("xml", kXmlNamespace);
    }

    void run(const Node& node, bool with_node) {
        static const std::string kNone;
        if (with_node) write_node(node, kNone);
        else push_children(node, std::string(), kNone);
        while (!m_frames.empty()) {
            Frame& frame = m_frames.back();
            if (frame.doctype && frame.next == frame.doctype_at && !frame.doctype_written) {
                frame.doctype_written = true;
                write_doctype(frame.doctype->name, frame.doctype->public_id, frame.doctype->system_id);
                continue;
            }
            if (frame.next >= frame.children->size()) {
                if (!frame.end_tag.empty()) {
                    m_out += "</";
                    m_out += frame.end_tag;
                    m_out += '>';
                }
                m_scope.rewind(frame.scope_mark);
                m_frames.pop_back();
                continue;
            }
            const Node& child = *(*frame.children)[frame.next++];
            // Writing an element may push a frame, which moves this one.
            const std::string* inherited = frame.inherited;
            write_node(child, *inherited);
        }
    }

private:
    struct Frame {
        const std::vector<std::shared_ptr<Node>>* children;
        std::size_t next;
        std::string end_tag;
        /// The default namespace in force for the children. It points at a
        /// string of the tree, which nothing changes while this runs.
        const std::string* inherited;
        /// Where the scope stood before the element: it goes back there.
        std::size_t scope_mark;
        const Doctype* doctype;
        std::size_t doctype_at;
        bool doctype_written;
    };

    void push_children(const Node& node, std::string end_tag, const std::string& inherited) {
        push_children(node, std::move(end_tag), inherited, m_scope.mark());
    }

    void push_children(const Node& node, std::string end_tag, const std::string& inherited, std::size_t scope_mark) {
        const Node* source = &node;
        if (node.is_element()) {
            const auto& element = static_cast<const Element&>(node);
            if (element.template_contents() && element.element_namespace() == ElementNamespace::HTML &&
                element.tag_name() == "template") {
                source = element.template_contents().get();
            }
        }
        Frame frame{&source->children(), 0, std::move(end_tag), &inherited, scope_mark, nullptr, 0, false};
        if (node.is_document()) {
            const Doctype& doctype = static_cast<const Document&>(node).doctype();
            if (doctype.present) {
                frame.doctype = &doctype;
                frame.doctype_at = std::min(doctype.preceding_children, source->children().size());
            }
        }
        m_frames.push_back(std::move(frame));
    }

    void write_doctype(const std::string& name, const std::string& public_id, const std::string& system_id) {
        if (m_strict) {
            for (const char c : public_id) {
                const bool allowed = c == ' ' || c == '\n' || c == '\r' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                     (c >= '0' && c <= '9') || std::string_view("-'()+,./:=?;!*#@$_%").find(c) != std::string_view::npos;
                if (!allowed) throw CannotSerialize{};
            }
            if (!is_xml_text(system_id)) throw CannotSerialize{};
            if (system_id.find('"') != std::string::npos && system_id.find('\'') != std::string::npos) throw CannotSerialize{};
        }
        m_out += "<!DOCTYPE ";
        m_out += name;
        if (!public_id.empty()) {
            m_out += " PUBLIC ";
            append_quoted_id(public_id, m_out);
        }
        if (!system_id.empty() && public_id.empty()) m_out += " SYSTEM";
        if (!system_id.empty()) {
            m_out += ' ';
            append_quoted_id(system_id, m_out);
        }
        m_out += '>';
    }

    void write_node(const Node& node, const std::string& inherited) {
        switch (node.type()) {
            case NodeType::Element:
                write_element(static_cast<const Element&>(node), inherited);
                return;
            case NodeType::Document:
                if (m_strict && !static_cast<const Document&>(node).document_element()) throw CannotSerialize{};
                push_children(node, std::string(), inherited);
                return;
            case NodeType::DocumentFragment:
                push_children(node, std::string(), inherited);
                return;
            case NodeType::Text: {
                const std::string& data = static_cast<const Text&>(node).text();
                if (node.is_cdata_section()) {
                    // The standard writes it as it is. With "]]>" inside,
                    // or a character XML has no place for, what comes out
                    // is not what went in, and when the result has to be
                    // well formed that is an error here.
                    if (m_strict && (data.find("]]>") != std::string::npos || !is_xml_text(data))) throw CannotSerialize{};
                    m_out += "<![CDATA[";
                    m_out += data;
                    m_out += "]]>";
                } else {
                    append_text(data, m_strict, m_out);
                }
                return;
            }
            case NodeType::Comment: {
                const std::string& data = static_cast<const Comment&>(node).data();
                if (m_strict && (!is_xml_text(data) || data.find("--") != std::string::npos ||
                                 (!data.empty() && data.back() == '-'))) {
                    throw CannotSerialize{};
                }
                m_out += "<!--";
                m_out += data;
                m_out += "-->";
                return;
            }
            case NodeType::ProcessingInstruction: {
                const auto& instruction = static_cast<const ProcessingInstruction&>(node);
                if (m_strict) {
                    std::string lower;
                    for (const char c : instruction.target()) lower += c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
                    if (instruction.target().find(':') != std::string::npos || lower == "xml") throw CannotSerialize{};
                    if (!is_xml_text(instruction.data()) || instruction.data().find("?>") != std::string::npos) {
                        throw CannotSerialize{};
                    }
                }
                m_out += "<?";
                m_out += instruction.target();
                m_out += ' ';
                m_out += instruction.data();
                m_out += "?>";
                return;
            }
            case NodeType::DocumentType: {
                const auto& doctype = static_cast<const DocumentTypeNode&>(node);
                write_doctype(doctype.name(), doctype.public_id(), doctype.system_id());
                return;
            }
        }
    }

    // "Generate a prefix": ns1, ns2, ... The standard takes the next number
    // whatever it is, and so can write xmlns:ns1 twice on an element that
    // declares ns1 itself (issue 44); a name that already stands for
    // something here is passed over.
    std::string generate_prefix(const std::string& uri) {
        std::string prefix;
        do {
            prefix = "ns" + std::to_string(m_prefix_index++);
        } while (m_scope.bound(prefix));
        m_scope.add(prefix, uri);
        return prefix;
    }

    void append_declaration(const std::string& prefix, const std::string& uri) {
        m_out += prefix.empty() ? " xmlns" : " xmlns:";
        m_out += prefix;
        m_out += "=\"";
        append_attribute_value(uri, m_strict, m_out);
        m_out += '"';
    }

    // "XML serializing an Element node", with "recording the namespace
    // information" and "XML serialization of the attributes" in it.
    void write_element(const Element& element, const std::string& context_namespace) {
        const std::string& local = element.tag_name();
        if (m_strict && (local.find(':') != std::string::npos || !is_xml_name(local))) throw CannotSerialize{};
        const std::size_t scope_mark = m_scope.mark();
        const AttributeList& attributes = element.attributes();

        // Recording the namespace information: what this element declares.
        // `declared` is the standard's "local prefixes map".
        std::unordered_map<std::string, std::string> declared;
        const std::string* local_default = nullptr;
        for (const auto& attribute : attributes) {
            const std::string& name = attribute.name;
            const std::string& value = attribute.value;
            if (name.size() < 5 || name.compare(0, 5, "xmlns") != 0) continue;
            const std::string& uri = attribute.namespace_uri();
            if (name.size() == 5) {
                // The default namespace declaration; one a script set with
                // setAttribute("xmlns", ...) stands in no namespace and is
                // taken for one all the same, as the browsers take it
                // (issue 83): written out it would be one.
                if (uri.empty() || uri == kXmlnsNamespace) local_default = &value;
                continue;
            }
            if (name[5] != ':' || uri != kXmlnsNamespace) continue;
            if (value == kXmlNamespace) continue;
            const std::string prefix = name.substr(6);
            if (m_scope.has(value, prefix)) continue;
            m_scope.add(prefix, value);
            declared[prefix] = value;
        }

        const std::string* inherited = &context_namespace;
        const std::string& ns = element_namespace_uri(element);
        std::string qualified;
        bool ignore_default_declaration = false;

        m_out += '<';
        // Every prefix written in this start tag: one of them cannot be
        // given another meaning further along the same tag.
        std::unordered_set<std::string> used;

        if (*inherited == ns) {
            // The element is in the default namespace in force: no prefix
            // (even if it has one), and a default declaration of its own is
            // left out, whatever it says -- with two exceptions, both of them
            // `xmlns=""`:
            //
            //  * it is not redundant when the default in force is *not* empty,
            //    because then it is the declaration that keeps the element out
            //    of it (`<root xmlns="u1"><child xmlns=""/></root>`);
            //  * it is not redundant when the element also declares a prefix
            //    (`<root xmlns="" xmlns:foo=""/>`), where both are one
            //    statement about what this element puts in scope.
            //
            // Anywhere else it says nothing and is dropped, as the standard's
            // redundant-`xmlns` rule asks -- and that is the half this row's
            // `serializer-not-well-formed` neighbours would be wrong to lose.
            // The declaration under test must *itself* be the empty one: a
            // non-empty `xmlns="urn:a"` on an element that is in no namespace
            // contradicts the element and must stay suppressed, or the element
            // reads back as a member of `urn:a` it never was. (Getting this
            // wrong is what `make diff-xml` caught: 19 of 400 trees stopped
            // round-tripping.)
            const bool empty_default_is_a_declaration =
                local_default && local_default->empty() && ns.empty() &&
                (!context_namespace.empty() || !declared.empty());
            if (local_default && !empty_default_is_a_declaration) {
                ignore_default_declaration = true;
            }
            if (ns == kXmlNamespace) qualified = "xml:";
            qualified += local;
            m_out += qualified;
        } else {
            std::string prefix = element.prefix();
            std::string candidate;
            bool has_candidate = m_scope.preferred(ns, prefix, candidate);
            if (prefix == "xmlns") {
                if (m_strict) throw CannotSerialize{};
                candidate = prefix;
                has_candidate = true;
            }
            if (has_candidate) {
                used.insert(candidate);
                qualified = candidate + ":" + local;
                m_out += qualified;
                if (local_default && *local_default != kXmlNamespace) inherited = local_default;
            } else if (!prefix.empty()) {
                // No prefix stands for the namespace: the element's own is
                // declared here, or a new one if this element declares its
                // own to be something else.
                if (declared.count(prefix)) {
                    prefix = generate_prefix(ns);
                } else {
                    m_scope.add(prefix, ns);
                }
                declared[prefix] = ns;
                used.insert(prefix);
                qualified = prefix + ":" + local;
                m_out += qualified;
                append_declaration(prefix, ns);
                if (local_default) inherited = local_default;
            } else if (!local_default || *local_default != ns) {
                // No prefix at all: the default namespace is declared here,
                // in place of whatever declaration the element had.
                ignore_default_declaration = true;
                qualified = local;
                inherited = &ns;
                m_out += qualified;
                append_declaration(std::string(), ns);
            } else {
                qualified = local;
                inherited = &ns;
                m_out += qualified;
            }
        }

        // The attributes. Two with one namespace and local name cannot be
        // written; only attributes with a namespace can be such a pair.
        std::vector<std::pair<const std::string*, std::string>> seen_few;
        std::unordered_set<std::string> seen_many;
        for (const auto& attribute : attributes) {
            const std::string& name = attribute.name;
            const std::string& value = attribute.value;
            const std::string& uri = attribute.namespace_uri();
            if (uri.empty()) {
                if (name == "xmlns") {
                    if (ignore_default_declaration) continue;
                    if (m_strict) throw CannotSerialize{};
                } else if (m_strict && (name.find(':') != std::string::npos || !is_xml_name(name))) {
                    throw CannotSerialize{};
                }
                m_out += ' ';
                m_out += name;
                m_out += "=\"";
                append_attribute_value(value, m_strict, m_out);
                m_out += '"';
                continue;
            }

            // With a namespace, what is before the first colon is a prefix.
            const auto colon = name.find(':');
            const std::string prefix = colon == std::string::npos ? std::string() : name.substr(0, colon);
            const std::string attribute_local = colon == std::string::npos ? name : name.substr(colon + 1);
            if (m_strict) {
                bool repeated = false;
                if (seen_many.empty() && seen_few.size() < 16) {
                    for (const auto& earlier : seen_few) {
                        if (*earlier.first == uri && earlier.second == attribute_local) repeated = true;
                    }
                    seen_few.emplace_back(&uri, attribute_local);
                } else {
                    for (const auto& earlier : seen_few) seen_many.insert(*earlier.first + '\x01' + earlier.second);
                    seen_few.clear();
                    repeated = !seen_many.insert(uri + '\x01' + attribute_local).second;
                }
                if (repeated) throw CannotSerialize{};
            }

            std::string candidate;
            bool has_candidate = m_scope.preferred(uri, prefix, candidate);
            if (uri == kXmlnsNamespace) {
                if (value == kXmlNamespace) continue;
                if (prefix.empty()) {
                    if (ignore_default_declaration) continue;
                } else {
                    // A declaration is written when it is the one this
                    // element makes: not when an ancestor has made it, and
                    // not when this element gave the prefix another meaning.
                    const auto here = declared.find(attribute_local);
                    if (here == declared.end() || here->second != value) continue;
                    if (m_strict && value.empty()) throw CannotSerialize{};
                }
                if (m_strict && value == kXmlnsNamespace) throw CannotSerialize{};
                has_candidate = prefix == "xmlns";
                if (has_candidate) candidate = "xmlns";
            } else if (!has_candidate) {
                // No prefix stands for the namespace. The attribute keeps
                // its own, declared here, unless this element declares that
                // prefix to be something else; a prefix an ancestor declared
                // is declared again. (The standard stops there. A prefix
                // already written in this tag with the ancestor's meaning,
                // the element's own name say, is not taken either: declared
                // again, it would change what was written.)
                if (!prefix.empty() && !declared.count(prefix) && !used.count(prefix)) {
                    candidate = prefix;
                    m_scope.add(candidate, uri);
                } else {
                    candidate = generate_prefix(uri);
                }
                has_candidate = true;
                declared[candidate] = uri;
                append_declaration(candidate, uri);
            }
            m_out += ' ';
            if (has_candidate) {
                used.insert(candidate);
                m_out += candidate;
                m_out += ':';
            }
            if (m_strict && (attribute_local.find(':') != std::string::npos || !is_xml_name(attribute_local))) {
                throw CannotSerialize{};
            }
            m_out += attribute_local;
            m_out += "=\"";
            append_attribute_value(value, m_strict, m_out);
            m_out += '"';
        }

        const bool html = element.element_namespace() == ElementNamespace::HTML;
        const bool is_template = html && local == "template" && element.template_contents();
        const bool childless = is_template ? element.template_contents()->children().empty() : element.children().empty();
        if (childless) {
            if (html && is_void_html(local)) m_out += " />";
            else if (!html) m_out += "/>";
            else m_out += "></" + qualified + ">";
            m_scope.rewind(scope_mark);
            return;
        }
        m_out += '>';
        push_children(element, std::move(qualified), *inherited, scope_mark);
    }

    bool m_strict;
    std::string& m_out;
    Scope m_scope;
    int m_prefix_index{1};
    std::vector<Frame> m_frames;
};

bool serialize(const Node& node, bool with_node, bool require_well_formed, std::string& out) {
    std::string buffer;
    try {
        Serializer(require_well_formed, buffer).run(node, with_node);
    } catch (const CannotSerialize&) {
        return false;
    }
    out = std::move(buffer);
    return true;
}

} // namespace

bool serialize_xml(const Node& node, bool require_well_formed, std::string& out) {
    return serialize(node, true, require_well_formed, out);
}

bool serialize_xml_children(const Node& node, bool require_well_formed, std::string& out) {
    return serialize(node, false, require_well_formed, out);
}

} // namespace xml
} // namespace litebird
