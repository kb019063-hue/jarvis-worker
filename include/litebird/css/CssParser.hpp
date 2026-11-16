#pragma once

#include "litebird/css/StyleSheet.hpp"
#include <string_view>

namespace litebird {

class CssParser {
public:
    static StyleSheet parse(std::string_view css);

private:
    explicit CssParser(std::string_view css);
    StyleSheet parse_stylesheet();

    void consume_whitespace_and_comments();
    char peek(size_t offset = 0) const;
    char advance();
    bool match(char expected);

    void parse_stylesheet_blocks(StyleSheet& sheet);
    void skip_at_rule_remainder();
    std::string read_prelude_before_brace_or_semicolon();
    bool evaluate_supports_condition(std::string_view cond, int depth = 0) const;
    bool supports_declaration(std::string_view decl) const;

    Selector parse_selector();
    SimpleSelector parse_simple_selector(std::string& raw_out, bool& has_host, bool& has_pseudo_element);
    Declaration parse_declaration();
    Rule parse_rule();
    MediaQuery parse_media_query(std::string_view str);
    MediaQuery parse_single_media_query(std::string_view str);
    KeyframesRule parse_keyframes();

    std::string_view m_css;
    size_t m_cursor{0};
    size_t m_anon_layer_counter{0};
};

} // namespace litebird
