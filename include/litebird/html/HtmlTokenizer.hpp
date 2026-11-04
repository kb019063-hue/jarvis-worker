#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace litebird {

enum class HtmlTokenType {
    Doctype,
    StartTag,
    EndTag,
    Comment,
    Character,
    EndOfFile
};

struct HtmlToken {
    HtmlTokenType type{HtmlTokenType::EndOfFile};
    std::string tag_name;
    std::unordered_map<std::string, std::string> attributes;
    bool self_closing{false};
    std::string data; // for Character, Comment, Doctype

    static HtmlToken make_character(char c) {
        HtmlToken t;
        t.type = HtmlTokenType::Character;
        t.data = std::string(1, c);
        return t;
    }

    static HtmlToken make_character_string(std::string str) {
        HtmlToken t;
        t.type = HtmlTokenType::Character;
        t.data = std::move(str);
        return t;
    }

    static HtmlToken make_eof() {
        HtmlToken t;
        t.type = HtmlTokenType::EndOfFile;
        return t;
    }
};

class HtmlTokenizer {
public:
    explicit HtmlTokenizer(std::string_view input);

    HtmlToken next_token();
    bool has_more() const { return m_cursor < m_input.size(); }

    void switch_to_raw_text(std::string tag_name);

private:
    char peek(size_t offset = 0) const;
    char advance();
    void consume_whitespace();

    HtmlToken consume_tag();
    HtmlToken consume_comment();
    HtmlToken consume_doctype();
    HtmlToken consume_raw_text();

    std::string_view m_input;
    size_t m_cursor{0};
    bool m_raw_text_mode{false};
    std::string m_raw_text_tag;
};

} // namespace litebird
