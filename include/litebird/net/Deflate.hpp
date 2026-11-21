#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace litebird {

/// Bounds-checked RFC 1951 (DEFLATE), RFC 1950 (zlib), and RFC 1952 (gzip)
/// decompression and compression primitives with zero external header
/// dependencies. Every decoder enforces `max_output_bytes` before growing
/// `out`, so decompression bombs fail deterministically.
class Deflate {
public:
    static constexpr std::size_t DefaultMaxOutputBytes = 64u * 1024u * 1024u;

    enum class InflateStatus {
        Ok,
        CorruptData,
        OutputLimitExceeded,
    };

    /// Stateful RFC 1951 sliding-window inflater for RFC 7692 permessage-deflate.
    /// Preserves up to (1 << window_bits) bytes of history across messages unless
    /// reset() is called (server_no_context_takeover). Automatically appends the
    /// RFC 7692 sync-flush tail (0x00, 0x00, 0xFF, 0xFF) and enforces both the
    /// negotiated window bound and max_output_bytes before growing `out`.
    class InflateStream {
    public:
        explicit InflateStream(int window_bits = 15);
        void reset();
        int window_bits() const { return m_window_bits; }

        InflateStatus inflate_message(
            const uint8_t* data,
            std::size_t size,
            std::vector<uint8_t>& out,
            std::size_t max_output_bytes = DefaultMaxOutputBytes
        );

    private:
        int m_window_bits{15};
        std::size_t m_window_size{32768};
        std::vector<uint8_t> m_history;
    };

    /// Stateful RFC 1951 LZ77 + fixed-Huffman streaming compressor for RFC 7692
    /// permessage-deflate. Emits sync-flushed blocks with the trailing 4-byte
    /// 0x00 0x00 0xFF 0xFF removed, preserving up to (1 << window_bits) bytes of
    /// sliding-window history across messages unless reset() is called
    /// (client_no_context_takeover). Falls back to RFC 1951 stored blocks when
    /// fixed Huffman would expand incompressible payloads.
    class DeflateStream {
    public:
        explicit DeflateStream(int window_bits = 15);
        void reset();
        int window_bits() const { return m_window_bits; }

        void compress_message(
            const uint8_t* data,
            std::size_t size,
            std::vector<uint8_t>& out
        );

    private:
        int m_window_bits{15};
        std::size_t m_window_size{32768};
        std::vector<uint8_t> m_history;
    };

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
