#include "litebird/net/DataUrl.hpp"

#include <cstddef>

// MIME types, data: URLs and the decoding of bytes into text, each by the
// standard that defines it: MIME Sniffing ("parse a MIME type", "serialize a
// MIME type"), Fetch ("data: URL processor"), Infra ("forgiving-base64
// decode"), URL ("percent-decode") and Encoding ("decode"). Nothing here
// reads a file or opens a connection.

namespace litebird {

namespace {

bool is_http_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool is_ascii_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool is_alphanumeric(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_token_char(char c) {
    if (is_alphanumeric(c)) return true;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
        case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

bool is_token(std::string_view text) {
    if (text.empty()) return false;
    for (const char c : text) {
        if (!is_token_char(c)) return false;
    }
    return true;
}

// Tab, U+0020 to U+007E and U+0080 to U+00FF, in text that is UTF-8.
bool is_quoted_string_text(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\t' || (c >= 0x20 && c <= 0x7E)) continue;
        if ((c == 0xC2 || c == 0xC3) && i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
}

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = lower(c);
    return out;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && hex_value(text[i + 1]) >= 0 && hex_value(text[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool forgiving_base64_decode(std::string_view text, std::string& out) {
    std::string data;
    data.reserve(text.size());
    for (const char c : text) {
        if (!is_ascii_whitespace(c)) data += c;
    }
    if (data.size() % 4 == 0) {
        if (!data.empty() && data.back() == '=') data.pop_back();
        if (!data.empty() && data.back() == '=') data.pop_back();
    }
    if (data.size() % 4 == 1) return false;
    out.clear();
    out.reserve(data.size() / 4 * 3 + 2);
    unsigned buffer = 0;
    int bits = 0;
    for (const char c : data) {
        unsigned value = 0;
        if (c >= 'A' && c <= 'Z') value = static_cast<unsigned>(c - 'A');
        else if (c >= 'a' && c <= 'z') value = static_cast<unsigned>(c - 'a') + 26;
        else if (c >= '0' && c <= '9') value = static_cast<unsigned>(c - '0') + 52;
        else if (c == '+') value = 62;
        else if (c == '/') value = 63;
        else return false;
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    // What is left of the last group is less than a byte and is dropped.
    return true;
}

void put_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

const char kReplacement[] = "\xEF\xBF\xBD";

// The UTF-8 decoder of the Encoding Standard: one U+FFFD for each sequence
// that goes wrong, and the byte that ended it is read again.
std::string decode_utf8(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    const std::size_t n = bytes.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);
        if (lead < 0x80) {
            out += static_cast<char>(lead);
            ++i;
            continue;
        }
        std::size_t needed = 0;
        unsigned char low = 0x80, high = 0xBF;      // the range the next byte must be in
        if (lead >= 0xC2 && lead <= 0xDF) {
            needed = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            needed = 2;
            if (lead == 0xE0) low = 0xA0;           // no overlong forms
            if (lead == 0xED) high = 0x9F;          // no surrogates
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            needed = 3;
            if (lead == 0xF0) low = 0x90;
            if (lead == 0xF4) high = 0x8F;          // nothing above U+10FFFF
        }
        if (needed == 0) {
            out += kReplacement;
            ++i;
            continue;
        }
        std::size_t length = 1;
        bool ok = true;
        while (length <= needed) {
            if (i + length >= n) {
                ok = false;
                break;
            }
            const unsigned char next = static_cast<unsigned char>(bytes[i + length]);
            if (next < low || next > high) {
                ok = false;
                break;
            }
            low = 0x80;
            high = 0xBF;
            ++length;
        }
        if (ok) out.append(bytes.data() + i, length);
        else out += kReplacement;
        i += length;
    }
    return out;
}

std::string decode_utf16(std::string_view bytes, bool big_endian) {
    std::string out;
    out.reserve(bytes.size());
    const auto unit = [&](std::size_t i) -> char32_t {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        return big_endian ? static_cast<char32_t>((a << 8) | b) : static_cast<char32_t>((b << 8) | a);
    };
    std::size_t i = 0;
    while (i + 1 < bytes.size()) {
        const char32_t c = unit(i);
        i += 2;
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 < bytes.size()) {
                const char32_t next = unit(i);
                if (next >= 0xDC00 && next <= 0xDFFF) {
                    put_utf8(out, 0x10000 + ((c - 0xD800) << 10) + (next - 0xDC00));
                    i += 2;
                    continue;
                }
            } else {
                // The first half of a pair and then the end of the text,
                // with or without one more byte: one error for all of it.
                i = bytes.size();
            }
            out += kReplacement;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            out += kReplacement;
        } else {
            put_utf8(out, c);
        }
    }
    if (i < bytes.size()) out += kReplacement;      // half a code unit at the end
    return out;
}

std::string decode_windows_1252(std::string_view bytes) {
    static const char32_t kHigh[32] = {
        0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178,
    };
    std::string out;
    out.reserve(bytes.size());
    for (const char raw : bytes) {
        const unsigned char b = static_cast<unsigned char>(raw);
        if (b < 0x80) out += static_cast<char>(b);
        else put_utf8(out, b < 0xA0 ? kHigh[b - 0x80] : b);
    }
    return out;
}

enum class TextEncoding { Utf8, Utf16LE, Utf16BE, Windows1252 };

// The labels of the Encoding Standard for the four encodings read here.
TextEncoding encoding_of_label(std::string_view label) {
    std::size_t start = 0, end = label.size();
    while (start < end && is_ascii_whitespace(label[start])) ++start;
    while (end > start && is_ascii_whitespace(label[end - 1])) --end;
    const std::string name = lowered(label.substr(start, end - start));
    static const char* const kUtf16LE[] = {"utf-16le", "utf-16", "ucs-2", "unicode", "csunicode", "iso-10646-ucs-2", "unicodefeff"};
    static const char* const kUtf16BE[] = {"utf-16be", "unicodefffe"};
    static const char* const kWindows1252[] = {
        "ansi_x3.4-1968", "ascii", "cp1252", "cp819", "csisolatin1", "ibm819", "iso-8859-1", "iso-ir-100", "iso8859-1",
        "iso88591", "iso_8859-1", "iso_8859-1:1987", "l1", "latin1", "us-ascii", "windows-1252", "x-cp1252",
    };
    for (const char* candidate : kUtf16LE) {
        if (name == candidate) return TextEncoding::Utf16LE;
    }
    for (const char* candidate : kUtf16BE) {
        if (name == candidate) return TextEncoding::Utf16BE;
    }
    for (const char* candidate : kWindows1252) {
        if (name == candidate) return TextEncoding::Windows1252;
    }
    return TextEncoding::Utf8;
}

} // namespace

bool MimeType::parse(std::string_view text, MimeType& out) {
    std::size_t start = 0, end = text.size();
    while (start < end && is_http_whitespace(text[start])) ++start;
    while (end > start && is_http_whitespace(text[end - 1])) --end;
    const std::string_view input = text.substr(start, end - start);

    const auto slash = input.find('/');
    if (slash == std::string_view::npos) return false;
    const std::string_view type = input.substr(0, slash);
    if (!is_token(type)) return false;
    std::size_t at = slash + 1;
    auto stop = input.find(';', at);
    if (stop == std::string_view::npos) stop = input.size();
    std::string_view subtype = input.substr(at, stop - at);
    while (!subtype.empty() && is_http_whitespace(subtype.back())) subtype.remove_suffix(1);
    if (!is_token(subtype)) return false;

    MimeType result;
    result.type = lowered(type);
    result.subtype = lowered(subtype);
    at = stop;
    while (at < input.size()) {
        ++at;                                                           // the semicolon
        while (at < input.size() && is_http_whitespace(input[at])) ++at;
        const std::size_t name_start = at;
        while (at < input.size() && input[at] != ';' && input[at] != '=') ++at;
        const std::string name = lowered(input.substr(name_start, at - name_start));
        if (at < input.size()) {
            if (input[at] == ';') continue;                             // a name and no value
            ++at;                                                       // the equals sign
        }
        if (at >= input.size()) break;
        std::string value;
        if (input[at] == '"') {
            ++at;
            while (at < input.size()) {
                const char c = input[at++];
                if (c == '"') break;
                if (c == '\\') {
                    if (at >= input.size()) {
                        value += '\\';
                        break;
                    }
                    value += input[at++];
                } else {
                    value += c;
                }
            }
            while (at < input.size() && input[at] != ';') ++at;         // what follows the quotes is dropped
        } else {
            const std::size_t value_start = at;
            while (at < input.size() && input[at] != ';') ++at;
            std::string_view raw = input.substr(value_start, at - value_start);
            while (!raw.empty() && is_http_whitespace(raw.back())) raw.remove_suffix(1);
            if (raw.empty()) continue;
            value = std::string(raw);
        }
        if (is_token(name) && is_quoted_string_text(value) && !result.parameter(name)) {
            result.parameters.emplace_back(name, std::move(value));
        }
    }
    out = std::move(result);
    return true;
}

std::string MimeType::essence() const {
    return type + "/" + subtype;
}

std::string MimeType::serialize() const {
    std::string out = essence();
    for (const auto& [name, value] : parameters) {
        out += ';';
        out += name;
        out += '=';
        if (is_token(value)) {
            out += value;
        } else {
            out += '"';
            for (const char c : value) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            out += '"';
        }
    }
    return out;
}

const std::string* MimeType::parameter(std::string_view name) const {
    for (const auto& entry : parameters) {
        if (entry.first == name) return &entry.second;
    }
    return nullptr;
}

bool MimeType::is_xml() const {
    if (subtype.size() > 4 && subtype.compare(subtype.size() - 4, 4, "+xml") == 0) return true;
    return subtype == "xml" && (type == "text" || type == "application");
}

bool MimeType::is_html() const {
    return type == "text" && subtype == "html";
}

bool process_data_url(std::string_view url, MimeType& type, std::string& body) {
    if (url.size() < 5 || lowered(url.substr(0, 5)) != "data:") return false;
    const std::string_view input = url.substr(5);
    const auto comma = input.find(',');
    if (comma == std::string_view::npos) return false;
    std::string_view head = input.substr(0, comma);
    while (!head.empty() && is_ascii_whitespace(head.front())) head.remove_prefix(1);
    while (!head.empty() && is_ascii_whitespace(head.back())) head.remove_suffix(1);
    body = percent_decode(input.substr(comma + 1));

    // ";base64" at the end, in any case and with spaces before the word.
    if (head.size() >= 7 && lowered(head.substr(head.size() - 6)) == "base64") {
        std::string_view before = head.substr(0, head.size() - 6);
        while (!before.empty() && before.back() == ' ') before.remove_suffix(1);
        if (!before.empty() && before.back() == ';') {
            std::string decoded;
            if (!forgiving_base64_decode(body, decoded)) return false;
            body = std::move(decoded);
            head = before.substr(0, before.size() - 1);
        }
    }
    std::string name(head);
    if (!name.empty() && name.front() == ';') name.insert(0, "text/plain");
    if (!MimeType::parse(name, type)) {
        type = MimeType{};
        type.type = "text";
        type.subtype = "plain";
        type.parameters.emplace_back("charset", "US-ASCII");
    }
    return true;
}

bool is_http_token(std::string_view text) {
    return is_token(text);
}

std::string utf8_decode(std::string_view bytes) {
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.remove_prefix(3);
    return decode_utf8(bytes);
}

std::string decode_text(std::string_view bytes, std::string_view label) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    if (bytes.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) return decode_utf8(bytes.substr(3));
    if (bytes.size() >= 2 && byte(0) == 0xFE && byte(1) == 0xFF) return decode_utf16(bytes.substr(2), true);
    if (bytes.size() >= 2 && byte(0) == 0xFF && byte(1) == 0xFE) return decode_utf16(bytes.substr(2), false);
    switch (encoding_of_label(label)) {
        case TextEncoding::Utf16LE: return decode_utf16(bytes, false);
        case TextEncoding::Utf16BE: return decode_utf16(bytes, true);
        case TextEncoding::Windows1252: return decode_windows_1252(bytes);
        case TextEncoding::Utf8: break;
    }
    return decode_utf8(bytes);
}

} // namespace litebird
