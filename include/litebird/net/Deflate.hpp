#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace litebird {

/// Bounds-checked RFC 1951 (DEFLATE), RFC 1950 (zlib), and RFC 1952 (gzip)
/// decompression and stored-block compression primitives with zero external
/// header dependencies. Every decoder enforces `max_output_bytes` before
/// growing `out`, so decompression bombs fail deterministically.
class Deflate {
public:
    static constexpr std::size_t DefaultMaxOutputBytes = 64u * 1024u * 1024u;

    static uint32_t crc32(const uint8_t* data, std::size_t size);
    static uint32_t adler32(const uint8_t* data, std::size_t size);

    /// Raw RFC 1951 stream. If `bytes_consumed` is non-null, stores the byte
    /// offset immediately after the final block (aligned to the next byte).
    static bool inflate_raw(
        const uint8_t* data,
        std::size_t size,
        std::vector<uint8_t>& out,
        std::size_t max_output_bytes = DefaultMaxOutputBytes,
        std::size_t* bytes_consumed = nullptr
    );

    /// RFC 1950 zlib wrapper (2-byte header + raw DEFLATE + big-endian Adler-32).
    static bool inflate_zlib(
        const uint8_t* data,
        std::size_t size,
        std::vector<uint8_t>& out,
        std::size_t max_output_bytes = DefaultMaxOutputBytes
    );

    /// RFC 1952 gzip wrapper (supports optional header fields and concatenated
    /// gzip members, verifying CRC-32 and ISIZE per member).
    static bool inflate_gzip(
        const uint8_t* data,
        std::size_t size,
        std::vector<uint8_t>& out,
        std::size_t max_output_bytes = DefaultMaxOutputBytes
    );

    /// Wraps `data[0..size)` into a valid RFC 1950 zlib stream using stored
    /// blocks + Adler-32 (used for lossless PNG encoding).
    static void compress_zlib_stored(
        const uint8_t* data,
        std::size_t size,
        std::vector<uint8_t>& out
    );
};

} // namespace litebird
