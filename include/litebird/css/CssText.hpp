#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace litebird {

// The whitespace set the CSS trim sites already used: std::isspace in the
// process locale (the engine never calls setlocale, so this is the C locale).
// Casting first keeps a byte above 0x7f out of the undefined negative-char
// range. This is deliberately not a change to CSS tokenization: the C set
// includes VT, which the CSS whitespace production does not.
inline bool css_is_space(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

// A view of the text between its outer whitespace runs. Each input character
// is inspected at most once; nothing is copied or shifted.
inline std::string_view css_trim_view(std::string_view text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && css_is_space(text[first])) ++first;
    while (last > first && css_is_space(text[last - 1])) --last;
    return text.substr(first, last - first);
}

// Trim an owned string with at most one shift of the surviving text. Erasing
// the first character in a loop shifted the whole remainder on every space:
// 300,000 leading spaces cost quadratic time before reaching the value.
inline void css_trim(std::string& text) {
    const std::string_view trimmed = css_trim_view(text);
    const std::size_t leading = static_cast<std::size_t>(trimmed.data() - text.data());
    if (leading != 0) text.erase(0, leading);
    text.resize(trimmed.size());
}

} // namespace litebird
