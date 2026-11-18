#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace litebird {

/// Bounds-checked RFC 7932 Brotli (`br`) decompressor with zero external runtime
/// dependencies. Enforces `max_output_bytes` before writing to `out` so
/// decompression bombs fail deterministically.
class Brotli {
public:
    static constexpr std::size_t DefaultMaxOutputBytes = 64u * 1024u * 1024u;

    static bool decompress(
        const uint8_t* data,
        std::size_t size,
        std::vector<uint8_t>& out,
        std::size_t max_output_bytes = DefaultMaxOutputBytes,
        std::size_t* bytes_consumed = nullptr
    );
};

namespace detail {

struct BrotliTransform {
    uint8_t prefix_id;
    uint8_t type;
    uint8_t suffix_id;
};

constexpr std::size_t kBrotliDictionarySize = 122784;
constexpr std::size_t kBrotliMaxWordLength = 24;
constexpr std::size_t kBrotliNumTransforms = 121;
constexpr std::size_t kBrotliNumPrefixSuffix = 50;

extern const uint8_t kBrotliDictionaryData[kBrotliDictionarySize];
extern const uint8_t kBrotliDictSizeBitsByLength[kBrotliMaxWordLength + 1];
extern const uint32_t kBrotliDictOffsetsByLength[kBrotliMaxWordLength + 1];

extern const char* const kBrotliPrefixSuffix[kBrotliNumPrefixSuffix];
extern const uint8_t kBrotliPrefixSuffixLen[kBrotliNumPrefixSuffix];
extern const BrotliTransform kBrotliTransforms[kBrotliNumTransforms];

extern const uint8_t kBrotliContextLookup[2048];

} // namespace detail

} // namespace litebird
