#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace litebird {

struct EntityEntry {
    const char* name;
    const char* value;
};

/// Decodes a WHATWG named character reference at the start of `input`.
/// `input` must begin with the name itself (the leading '&' already consumed).
/// On success, `consumed` receives the number of bytes of the name that were
/// used (longest match wins) and the decoded UTF-8 text is returned.
/// On failure, returns an empty string and sets `consumed` to 0.
std::string decode_named_character_reference(std::string_view input, std::size_t& consumed);

/// Number of named character references in the generated table (diagnostics).
std::size_t named_character_reference_count();

} // namespace litebird
