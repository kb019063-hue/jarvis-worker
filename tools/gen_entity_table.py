#!/usr/bin/env python3
"""Generates src/html/EntityTable.cpp from Python's WHATWG named character reference table.

Python's ``html.entities.html5`` is a byte-for-byte port of the WHATWG
"named character references" table (2231 entries, including the legacy
no-semicolon forms), so it is a reliable source for generating the table
instead of hand-copying it out of the spec.

Usage:
    python3 tools/gen_entity_table.py > src/html/EntityTable.cpp
"""

import html.entities


def c_escape(value: str) -> str:
    out = []
    for byte in value.encode("utf-8"):
        if 32 <= byte < 127 and byte not in (34, 92):
            out.append(chr(byte))
        else:
            out.append("\\x%02x" % byte)
    return "".join(out)


def main() -> None:
    entries = sorted(html.entities.html5.items())
    print("// GENERATED FILE - do not edit by hand.")
    print("// Regenerate with: python3 tools/gen_entity_table.py > src/html/EntityTable.cpp")
    print("// Source: WHATWG HTML named character references (via Python html.entities.html5).")
    print("// clang-format off")
    print('#include "litebird/html/EntityTable.hpp"')
    print()
    print("namespace litebird {")
    print("namespace {")
    print()
    print("constexpr EntityEntry kEntities[] = {")
    for name, value in entries:
        print('    {"%s", "%s"},' % (name.replace('"', '\\"'), c_escape(value)))
    print("};")
    print()
    print("constexpr std::size_t kEntityCount = sizeof(kEntities) / sizeof(kEntities[0]);")
    print()
    print("""const EntityEntry* find_entity(std::string_view name) {
    std::size_t lo = 0;
    std::size_t hi = kEntityCount;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const std::string_view candidate(kEntities[mid].name);
        if (candidate < name) {
            lo = mid + 1;
        } else if (name < candidate) {
            hi = mid;
        } else {
            return &kEntities[mid];
        }
    }
    return nullptr;
}""")
    print()
    print("} // namespace")
    print()
    print("""std::string decode_named_character_reference(std::string_view input, std::size_t& consumed) {
    // Longest match wins: WHATWG requires the longest prefix that names an
    // entity, so walk down from the longest possible name.
    const std::size_t max_length = std::min<std::size_t>(input.size(), 32);
    for (std::size_t length = max_length; length >= 1; --length) {
        const EntityEntry* entry = find_entity(input.substr(0, length));
        if (entry) {
            consumed = length;
            return std::string(entry->value);
        }
    }
    consumed = 0;
    return std::string();
}

std::size_t named_character_reference_count() {
    return kEntityCount;
}""")
    print()
    print("} // namespace litebird")


if __name__ == "__main__":
    main()
