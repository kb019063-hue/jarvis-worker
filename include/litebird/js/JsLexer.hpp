#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace litebird {

enum class JsTokenType {
    Eof,
    Number,
    String,
    Identifier,

    // Keywords
    Let,
    Var,
    Const,
    Function,
    Return,
    If,
    Else,
    While,
    True,
    False,
    Null,
    Undefined,

    // Operators
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Equal,
    EqualEqual,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AmpAmp,
    PipePipe,
    Bang,

    // Punctuation
    OpenParen,
    CloseParen,
    OpenBrace,
    CloseBrace,
    OpenBracket,
    CloseBracket,
    Semicolon,
    Colon,
    Comma,
    Dot
};

struct JsToken {
    JsTokenType type{JsTokenType::Eof};
    std::string text;
    double number_value{0.0};
    int line{1};
};

class JsLexer {
public:
    explicit JsLexer(std::string_view source);

    JsToken next_token();
    std::vector<JsToken> tokenize_all();

private:
    char peek(size_t offset = 0) const;
    char advance();
    bool match(char expected);
    void skip_whitespace_and_comments();

    JsToken scan_number();
    JsToken scan_string(char quote);
    JsToken scan_identifier();

    std::string_view m_source;
    size_t m_cursor{0};
    int m_line{1};
};

} // namespace litebird
