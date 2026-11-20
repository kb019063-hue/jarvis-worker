#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace litebird {

/// Every encoding defined by the WHATWG Encoding Standard
/// (https://encoding.spec.whatwg.org/#names-and-labels).
enum class EncodingId : uint8_t {
    Utf8 = 0,
    // Legacy single-byte encodings (28)
    Ibm866,
    Iso8859_2,
    Iso8859_3,
    Iso8859_4,
    Iso8859_5,
    Iso8859_6,
    Iso8859_7,
    Iso8859_8,
    Iso8859_8_I,
    Iso8859_10,
    Iso8859_13,
    Iso8859_14,
    Iso8859_15,
    Iso8859_16,
    Koi8R,
    Koi8U,
    Macintosh,
    Windows874,
    Windows1250,
    Windows1251,
    Windows1252,
    Windows1253,
    Windows1254,
    Windows1255,
    Windows1256,
    Windows1257,
    Windows1258,
    XMacCyrillic,
    // Legacy multi-byte Chinese (simplified)
    Gbk,
    Gb18030,
    // Legacy multi-byte Chinese (traditional)
    Big5,
    // Legacy multi-byte Japanese
    EucJp,
    Iso2022Jp,
    ShiftJis,
    // Legacy multi-byte Korean
    EucKr,
    // Legacy miscellaneous
    Replacement,
    Utf16BE,
    Utf16LE,
    XUserDefined,
};

/// Internal index tables generated from whatwg/encoding (`TextEncodingTables.cpp`).
namespace encoding_tables {

struct LabelEntry {
    const char* label;
    EncodingId id;
};

struct Gb18030Range {
    uint32_t pointer;
    uint32_t code_point;
};

extern const char* const kCanonicalNames[40];
extern const LabelEntry kLabels[228];
extern const uint16_t kSingleByte[27][128];
extern const uint16_t kIndexGb18030[23940];
extern const Gb18030Range kIndexGb18030Ranges[207];
extern const uint32_t kIndexBig5[19782];
extern const uint16_t kIndexJis0208[11280];
extern const uint16_t kIndexJis0212[8836];
extern const uint16_t kIndexEucKr[23940];

} // namespace encoding_tables

/// WHATWG Encoding Standard §5, the encoder half: the bytes `encoding` gives
/// `code_point`, or none when that encoding cannot carry it. The standard
/// encodes through an "output" of bytes and a numeric "code point" state; the
/// cases below are the ones that state machine has. A code point the encoding
/// has no entry for has no bytes, and this returns false: the caller decides
/// whether that is U+FFFD (the encoder interface's default) or a count of what
/// was written (`encodeInto`).
///
/// A few encodings are not one code point to a run of bytes:
/// ISO-2022-JP switches state (so its bytes depend on the ones before them and
/// are produced by encode_iso2022_jp, below), and `replacement` cannot encode
/// anything at all. `x-user-defined` carries U+0000..U+007F as themselves and
/// U+F780..U+F7FF as 0x80..0xFF.
bool encode_code_point(EncodingId encoding, uint32_t code_point, std::string& out);

/// The whole of `text` in `encoding`. Returns false at the first code point the
/// encoding cannot carry, with `out` holding what was encoded before it: what
/// `TextEncoder` reports as an error and `encodeInto` reports as `read`.
///
/// ISO-2022-JP is stateful, so it is encoded in one pass here rather than a
/// code point at a time: the escape sequence that switches back to ASCII is
/// written once, at the end, and not after every character.
bool encode_with_encoding(EncodingId encoding, std::string_view utf8_text, std::string& out);

/// WHATWG Encoding Standard §4.2 "get an encoding": strips leading/trailing
/// ASCII whitespace, ASCII-lowercases, and resolves any of the 228 standard
/// labels to its `EncodingId`. Returns false when `label` is not a recognized
/// encoding label.
bool get_encoding_from_label(std::string_view label, EncodingId& out);

/// The canonical WHATWG name for `id` (e.g. `"UTF-8"`, `"windows-1252"`,
/// `"Shift_JIS"`, `"EUC-JP"`, `"ISO-2022-JP"`, `"gb18030"`, `"replacement"`).
std::string_view canonical_encoding_name(EncodingId id);

/// WHATWG Encoding Standard §6 "BOM sniff": checks the first 2-3 bytes for a
/// UTF-8 (`EF BB BF`), UTF-16BE (`FE FF`), or UTF-16LE (`FF FE`) byte order mark.
/// When found, sets `out` and `bom_length` (2 or 3) and returns true.
bool sniff_bom_encoding(std::string_view bytes, EncodingId& out, std::size_t& bom_length);

/// Runs `encoding`'s WHATWG decoder on `bytes` without stripping a BOM.
/// When `fatal` is false ("replacement" error mode), invalid byte sequences
/// emit U+FFFD and the function returns true. When `fatal` is true ("fatal"
/// error mode, used by XML 1.0), returns false at the first decoding error.
bool decode_with_encoding(std::string_view bytes, EncodingId encoding, std::string& out_utf8, bool fatal = false);

/// WHATWG Encoding Standard §6 "decode": sniffs and strips at most one leading
/// BOM (`UTF-8`, `UTF-16BE`, `UTF-16LE`), otherwise uses `fallback_encoding`,
/// and decodes the remaining bytes in replacement mode into well-formed UTF-8.
/// Writes the chosen `EncodingId` to `*actual_encoding` when non-null.
std::string whatwg_decode(std::string_view bytes,
                          EncodingId fallback_encoding = EncodingId::Utf8,
                          EncodingId* actual_encoding = nullptr);

} // namespace litebird
