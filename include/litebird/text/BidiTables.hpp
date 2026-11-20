#pragma once

// Internal declarations for the Unicode 17.0 tables generated into
// src/text/BidiTables.cpp. Not part of the public engine API.

#include <cstddef>
#include <cstdint>

namespace litebird {

struct BidiClassRange {
    uint32_t first;
    uint32_t last;
    uint8_t cls;  // a litebird::BidiClass value
};

struct BidiBracketPair {
    uint32_t bracket;
    uint32_t paired;
    bool opening;
};

struct BidiMirrorPair {
    uint32_t from;
    uint32_t to;
};

const BidiClassRange* bidi_class_ranges(std::size_t& count);
const BidiBracketPair* bidi_bracket_pairs(std::size_t& count);
const BidiMirrorPair* bidi_mirror_pairs(std::size_t& count);

} // namespace litebird
