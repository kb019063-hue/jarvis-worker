#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace litebird {

enum class JsTokenType {
    Eof,
    Number,
    String,
    Identifier,
    Regex,          // text = pattern, flags = flags
    PrivateName,    // #name (text includes the '#')

    // Keywords. Contextual keywords (of, async, static, get, set) are lexed as
    // Identifier and recognised by the parser, so they stay usable as names.
    Let,
    Var,
    Const,
    Function,
    Return,
    If,
    Else,
    While,
    Do,
    For,
    In,
    New,
    True,
    False,
    Null,
    Undefined,
    Typeof,
    InstanceOf,
    Switch,
    Case,
    Default,
    Break,
    Continue,
    Class,
    Extends,
    Super,
    This,
    Await,
    Yield,
    Try,
    Catch,
    Finally,
    Throw,
    Delete,
    Void,
    Debugger,
    With,
    Import,
    Export,
    Enum,

    // Operators
    Plus,
    Minus,
    Star,
    Power,
    Slash,
    Percent,
    Equal,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    PowerEqual,
    ShiftLeftEqual,
    ShiftRightEqual,
    ShiftRightUnsignedEqual,
    AmpEqual,
    PipeEqual,
    CaretEqual,
    AmpAmpEqual,
    PipePipeEqual,
    NullishEqual,
    PlusPlus,
    MinusMinus,
    EqualEqual,
    StrictEqual,
    NotEqual,
    StrictNotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    ShiftLeft,
    ShiftRight,
    ShiftRightUnsigned,
    Amp,
    Pipe,
    Caret,
    Tilde,
    AmpAmp,
    PipePipe,
    Nullish,
    Bang,
    Arrow,
    Question,
    QuestionDot,
    Ellipsis,

    // Template Literals
    TemplateHead,
    TemplateMiddle,
    TemplateTail,
    NoSubstitutionTemplate,

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
    std::string text;            // identifier name, cooked string, operator text, regex pattern
    std::string raw;             // template literals: the uncooked source text
    std::string flags;           // regex flags
    double number_value{0.0};
    int line{1};
    bool newline_before{false};  // a line terminator separates this token from the previous one
    bool is_keyword{false};      // reserved word; still valid as a property name after '.'
};

/// A syntax error found while lexing or parsing. Carries the source line so the
/// message can point at it. Scripts with a syntax error never run, not even
/// partially.
class JsSyntaxError : public std::runtime_error {
public:
    JsSyntaxError(const std::string& message, int line_number)
        : std::runtime_error(message + " (line " + std::to_string(line_number) + ")"),
          line(line_number), bare_message(message) {}
    int line;
    std::string bare_message;
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
    bool regex_allowed() const;

    JsToken scan_number();
    JsToken scan_string(char quote);
    JsToken scan_identifier();
    JsToken scan_regex();
    JsToken scan_template(bool is_start);
    void scan_escape(std::string& cooked, bool in_template);
    void append_code_point(std::string& out, unsigned long cp);

    [[noreturn]] void fail(const std::string& message) const;

    std::string_view m_source;
    size_t m_cursor{0};
    int m_line{1};
    int m_brace_depth{0};
    bool m_newline_before{false};
    bool m_has_prev{false};
    JsTokenType m_prev_type{JsTokenType::Eof};
    bool m_prev_is_keyword{false};
    std::vector<int> m_template_brace_stack;
};

} // namespace litebird
