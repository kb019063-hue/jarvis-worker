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

    Selector parse_selector();
    SimpleSelector parse_simple_selector();
    Declaration parse_declaration();
    Rule parse_rule();

    std::string_view m_css;
    size_t m_cursor{0};
};

} // namespace litebird
