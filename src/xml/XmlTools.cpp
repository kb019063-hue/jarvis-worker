#include "litebird/xml/XmlTools.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace litebird {
namespace xml {

namespace {

// Datachar of the canonical form.
void append_canonical(std::string_view text, std::string& out) {
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\t': out += "&#9;"; break;
            case '\n': out += "&#10;"; break;
            case '\r': out += "&#13;"; break;
            default: out += c;
        }
    }
}

class CanonicalWriter : public XmlHandler {
public:
    std::string body;
    std::string doctype_name;
    /// How much of `body` was written before the DOCTYPE: processing
    /// instructions that come ahead of it stay ahead of it.
    std::size_t doctype_at{0};
    std::vector<std::pair<std::string, std::string>> notations;     // name, declaration

    void doctype(const std::string& name, const std::string&, const std::string&) override {
        doctype_name = name;
        doctype_at = body.size();
    }
    void notation(const std::string& name, const std::string* public_id, const std::string* system_id) override {
        std::string text = "<!NOTATION " + name;
        if (public_id) text += " PUBLIC '" + *public_id + "'";
        if (system_id) text += std::string(public_id ? " '" : " SYSTEM '") + *system_id + "'";
        text += ">\n";
        notations.emplace_back(name, text);
    }
    void start_element(const XmlName& name, std::vector<XmlAttribute>& attributes) override {
        std::vector<const XmlAttribute*> sorted;
        for (const auto& attribute : attributes) sorted.push_back(&attribute);
        std::sort(sorted.begin(), sorted.end(), [](const XmlAttribute* a, const XmlAttribute* b) {
            return a->name.qualified < b->name.qualified;
        });
        body += '<';
        body += name.qualified;
        for (const XmlAttribute* attribute : sorted) {
            body += ' ';
            body += attribute->name.qualified;
            body += "=\"";
            append_canonical(attribute->value, body);
            body += '"';
        }
        body += '>';
    }
    void end_element(const XmlName& name) override {
        body += "</";
        body += name.qualified;
        body += '>';
    }
    void characters(std::string_view text) override { append_canonical(text, body); }
    void cdata_section(std::string_view text) override { append_canonical(text, body); }
    void processing_instruction(const std::string& target, std::string_view data) override {
        body += "<?";
        body += target;
        body += ' ';
        body += data;
        body += "?>";
    }
    void dtd_processing_instruction(const std::string& target, std::string_view data) override {
        processing_instruction(target, data);
    }
};

void append_quoted(std::string_view text, std::string& out) {
    out += '"';
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    out += '"';
}

std::string expanded(const XmlName& name) {
    return name.namespace_uri.empty() ? name.local : "{" + name.namespace_uri + "}" + name.local;
}

class EventWriter : public XmlHandler {
public:
    std::string out;

    void doctype(const std::string& name, const std::string& public_id, const std::string& system_id) override {
        out += "doctype " + name + "|" + public_id + "|" + system_id + "\n";
    }
    void start_element(const XmlName& name, std::vector<XmlAttribute>& attributes) override {
        flush();
        out += "element " + expanded(name) + "\n";
        std::vector<std::pair<std::string, const std::string*>> sorted;
        for (const auto& attribute : attributes) {
            if (attribute.name.namespace_uri == kXmlnsNamespace) continue;
            sorted.emplace_back(expanded(attribute.name), &attribute.value);
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [key, value] : sorted) {
            out += "attr " + key + "=";
            append_quoted(*value, out);
            out += '\n';
        }
    }
    void end_element(const XmlName&) override {
        flush();
        out += "end\n";
    }
    void characters(std::string_view text) override { m_text.append(text); }
    void cdata_section(std::string_view text) override { m_text.append(text); }
    void comment(std::string_view text) override {
        flush();
        out += "comment ";
        append_quoted(text, out);
        out += '\n';
    }
    void processing_instruction(const std::string& target, std::string_view data) override {
        flush();
        out += "pi " + target + " ";
        append_quoted(data, out);
        out += '\n';
    }
    void flush() {
        if (m_text.empty()) return;
        out += "text ";
        append_quoted(m_text, out);
        out += '\n';
        m_text.clear();
    }

private:
    std::string m_text;
};

} // namespace

bool xml_canonical_form(std::string_view utf8, std::string& out, XmlError& error) {
    CanonicalWriter writer;
    if (!parse_xml(utf8, writer, error)) return false;
    out.assign(writer.body, 0, writer.doctype_at);
    if (!writer.notations.empty()) {
        std::sort(writer.notations.begin(), writer.notations.end());
        out += "<!DOCTYPE " + writer.doctype_name + " [\n";
        for (const auto& [name, text] : writer.notations) out += text;
        out += "]>\n";
    }
    out.append(writer.body, writer.doctype_at, std::string::npos);
    return true;
}

bool xml_event_dump(std::string_view utf8, std::string& out, XmlError& error) {
    EventWriter writer;
    if (!parse_xml(utf8, writer, error)) return false;
    writer.flush();
    out = std::move(writer.out);
    return true;
}

} // namespace xml
} // namespace litebird
