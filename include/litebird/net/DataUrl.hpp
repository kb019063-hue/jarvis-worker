#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace litebird {

/// A MIME type as the MIME Sniffing Standard reads one ("parse a MIME type"):
/// `text/xml;charset=UTF-8` is the type `text`, the subtype `xml` and one
/// parameter. Type, subtype and parameter names are in lower case; a
/// parameter that is given twice counts the first time.
struct MimeType {
    std::string type;
    std::string subtype;
    std::vector<std::pair<std::string, std::string>> parameters;

    /// False when `text` is not a MIME type (no `/`, an empty half, a
    /// character that no token has).
    static bool parse(std::string_view text, MimeType& out);

    /// `type/subtype`.
    std::string essence() const;
    /// The essence and every parameter; a value that is not a token is
    /// written in quotes.
    std::string serialize() const;
    /// The value of the parameter `name` (lower case), or null.
    const std::string* parameter(std::string_view name) const;

    /// `text/xml`, `application/xml` and every subtype ending in `+xml`.
    bool is_xml() const;
    /// `text/html`.
    bool is_html() const;
};

/// What a `data:` URL holds, by the Fetch Standard's "data: URL processor".
/// `url` is the URL as the URL parser serializes it, without its fragment.
/// False when it is not a data: URL, has no comma, or says base64 and is
/// not. A type that cannot be read is `text/plain;charset=US-ASCII`, as the
/// standard has it.
bool process_data_url(std::string_view url, MimeType& type, std::string& body);

/// Whether `text` is a token of HTTP: the name of a method or of a header.
bool is_http_token(std::string_view text);

/// Bytes as text, read as UTF-8 whatever they say they are (the Encoding
/// Standard's "UTF-8 decode"): a UTF-8 byte order mark is dropped, and what
/// is not UTF-8 becomes U+FFFD.
std::string utf8_decode(std::string_view bytes);

/// Bytes as text (the Encoding Standard's "decode"): a byte order mark
/// decides, then `label`, the name of an encoding, and UTF-8 when the label
/// is empty or names an encoding this build cannot read. It reads UTF-8,
/// UTF-16LE, UTF-16BE and windows-1252 (which is what `iso-8859-1` and
/// `us-ascii` mean on the web). Bytes that are no text become U+FFFD; the
/// result is always UTF-8 that is well formed.
std::string decode_text(std::string_view bytes, std::string_view label);

} // namespace litebird
