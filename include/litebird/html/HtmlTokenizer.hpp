#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace litebird {

enum class HtmlTokenType {
    Doctype,
    StartTag,
    EndTag,
    Comment,
    ProcessingInstruction,
    Character,
    EndOfFile
};

/// A tokenizer output token. Unlike the pre-Phase-15 tokenizer this keeps
/// attribute order (duplicates already dropped), separates doctype
/// identifiers, and retains comments as first-class tokens.
struct HtmlToken {
    HtmlTokenType type{HtmlTokenType::EndOfFile};
    std::string name;                              // tag name or doctype name
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string data;                              // character or comment data
    std::string public_id;
    std::string system_id;
    bool self_closing{false};
    bool force_quirks{false};

    bool is_start_tag(std::string_view tag) const {
        return type == HtmlTokenType::StartTag && name == tag;
    }

    bool is_end_tag(std::string_view tag) const {
        return type == HtmlTokenType::EndTag && name == tag;
    }

    bool is_character() const { return type == HtmlTokenType::Character; }

    static HtmlToken make_eof() { return HtmlToken{}; }
};

/// The tokenizer content model, which the tree builder switches as it
/// inserts elements such as <title>, <style> and <script>.
enum class ContentModel {
    Data,
    Rcdata,
    Rawtext,
    ScriptData,
    Plaintext
};

class HtmlTokenizer {
public:
    explicit HtmlTokenizer(std::string_view input);

    HtmlToken next_token();
    bool has_more() const { return m_cursor < m_input.size(); }

    /// Switches the content model (the spec's "tokenizer state" changes made
    /// by the tree builder, e.g. after inserting <title> or <script>).
    void set_content_model(ContentModel model, std::string tag_name);
    void switch_to_raw_text(std::string tag_name) { set_content_model(ContentModel::Rawtext, std::move(tag_name)); }
    ContentModel content_model() const { return m_content_model; }

    /// CDATA sections are only recognised inside foreign (SVG/MathML) content.
    void set_allow_cdata(bool allow) { m_allow_cdata = allow; }

private:
    bool eof(std::size_t offset = 0) const { return m_cursor + offset >= m_input.size(); }
    char peek(std::size_t offset = 0) const { return eof(offset) ? '\0' : m_input[m_cursor + offset]; }
    void advance() { if (!eof()) ++m_cursor; }
    bool starts_with_ci(std::string_view text) const;

    std::string consume_char_ref(bool in_attribute);
    /// Decodes a numeric character reference. When `ok` comes back false the
    /// reference had no digits and the returned string is the literal
    /// continuation ("x" for a hex prefix) that must be emitted as text.
    std::string consume_numeric_char_ref(bool& ok);
    /// Returns false when EOF is reached inside the tag, in which case the
    /// tag token is dropped (the spec emits only EOF).
    bool consume_tag(HtmlToken& token);
    void consume_comment(HtmlToken& token);
    void consume_bogus_comment(HtmlToken& token, std::string_view prefix);
    enum class PiOutcome { NotAnInstruction, Emitted, Dropped };
    PiOutcome consume_processing_instruction(HtmlToken& token);
    void consume_doctype(HtmlToken& token);
    void consume_cdata(HtmlToken& token);
    bool scan_text_run(HtmlToken& token);
    bool scan_script_data(HtmlToken& token);
    void skip_whitespace();

    std::string m_storage;      // holds the CRLF-normalised input
    std::string_view m_input;
    std::size_t m_cursor{0};
    ContentModel m_content_model{ContentModel::Data};
    std::string m_text_tag;     // tag name for RCDATA/RAWTEXT/ScriptData
    bool m_allow_cdata{false};
    bool m_emitted_eof{false};
};

/// Appends the UTF-8 encoding of `codepoint` to `out`.
void append_utf8(std::string& out, char32_t codepoint);

} // namespace litebird
