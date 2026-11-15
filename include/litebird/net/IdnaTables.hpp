#pragma once

// Internal declarations for the UTS #46 tables generated into
// src/net/IdnaTables.cpp. Not part of the public engine API.

#include <cstddef>
#include <cstdint>

namespace litebird {

// UTS #46 map range. lo..hi inclusive. status is one of
// kUts46Valid / kUts46Mapped / kUts46Deviation / kUts46DisallowedStd3 /
// kUts46Disallowed / kUts46Ignored. map is the byte offset into mapping_pool()
// and map_len its byte length, or 0xffffffff / 0 when the range has no mapping.
struct Uts46Range {
    uint32_t lo;
    uint32_t hi;
    uint8_t status;
    uint32_t map;
    uint16_t map_len;
};

constexpr uint8_t kUts46Valid = 0;
constexpr uint8_t kUts46Mapped = 1;
constexpr uint8_t kUts46Deviation = 2;
constexpr uint8_t kUts46DisallowedStd3 = 3;
constexpr uint8_t kUts46Disallowed = 4;
constexpr uint8_t kUts46Ignored = 5;

// Simple inclusive range with a small payload.
struct Uts46Range2 {
    uint32_t lo;
    uint32_t hi;
};

struct Uts46Range2U8 {
    uint32_t lo;
    uint32_t hi;
    uint8_t payload;
};

// Canonical decomposition entry: the code points at
// decomp_pool()[offset .. offset+length) form cp's full recursive
// canonical decomposition.
struct Uts46Decomp {
    uint32_t cp;
    uint32_t offset;
    uint32_t length;
};

// Canonical composition pair. key = ((uint64_t) starter << 21) | combining.
struct Uts46Composition {
    uint64_t key;
    uint32_t composite;
};

const Uts46Range* uts46_ranges();
std::size_t uts46_range_count();
const unsigned char* mapping_pool();
std::size_t mapping_pool_size();

const Uts46Range2* mark_ranges();
std::size_t mark_range_count();
const Uts46Range2U8* combining_class_ranges();
std::size_t combining_class_range_count();

const uint32_t* decomp_pool();
std::size_t decomp_pool_size();
const Uts46Decomp* decomp_entries();
std::size_t decomp_entry_count();
const Uts46Composition* composition_pairs();
std::size_t composition_pair_count();

const Uts46Range2U8* bidi_ranges();
std::size_t bidi_range_count();
const Uts46Range2U8* joining_type_ranges();
std::size_t joining_type_range_count();

constexpr uint8_t kBidiL = 1, kBidiR = 2, kBidiAL = 3, kBidiEN = 4, kBidiAN = 5, kBidiES = 6,
                  kBidiCS = 7, kBidiET = 8, kBidiON = 9, kBidiBN = 10, kBidiNSM = 11, kBidiWS = 12;

constexpr uint8_t kJoiningL = 1, kJoiningR = 2, kJoiningD = 3, kJoiningT = 4;

// Mapping-table and joining-type data version packed as major*10000+minor*100+patch
// (150100 = 15.1.0). Note the mapping table and joining types are Unicode 15.1 while
// the bidi, decomposition and composition tables derive from the generator host's
// unicodedata (Unicode 14.0.0 with Python 3.11); code points assigned after Unicode
// 14 lack bidi data.
std::size_t idna_tables_unicode_version();

} // namespace litebird
