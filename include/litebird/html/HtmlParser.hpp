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

    /// Parses a page into `document`, which must be empty, and calls
    /// `on_script` for each <script> when its end tag has been read, before
    /// anything after it is parsed (see HtmlTreeBuilder::set_script_handler).
    /// This is how a page with scripts is loaded:
    /// JsInterpreter::load_page() is the caller.
    static void parse_into(const std::shared_ptr<Document>& document, std::string_view html, bool scripting,
                           HtmlTreeBuilder::ScriptHandler on_script);

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

    /// WHATWG HTML §13.2.3.2 "prescan a byte stream to determine its encoding":
    /// inspects up to the first 1024 bytes of `bytes` for `<meta charset>` or
    /// `<meta http-equiv="Content-Type" content="...">`, mapping UTF-16BE/LE
    /// to `"UTF-8"` and `x-user-defined` to `"windows-1252"`. Returns empty
    /// string when no recognized `<meta>` encoding is found.
    static std::string prescan_meta_charset(std::string_view bytes);

    /// WHATWG HTML §13.2.3.1 character encoding determination + decoding:
    /// 1. BOM sniff (`UTF-8`, `UTF-16BE`, `UTF-16LE`) — strips one BOM.
    /// 2. `transport_charset_label` (HTTP `Content-Type: ...; charset=...`).
    /// 3. `<meta>` prescan of the first 1024 bytes.
    /// 4. `fallback_encoding_label` (`"UTF-8"` by default, or same-origin parent).
    /// Writes the canonical WHATWG encoding name to `*resolved_encoding_out` when non-null.
    static std::string decode_bytes(
        std::string_view bytes,
        std::string_view transport_charset_label = {},
        std::string_view fallback_encoding_label = "UTF-8",
        std::string* resolved_encoding_out = nullptr);
};

} // namespace litebird
