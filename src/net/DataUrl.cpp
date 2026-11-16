#include "litebird/net/DataUrl.hpp"
#include "litebird/net/TextEncoding.hpp"

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

const std::string* MimeType::parameter(std::string_view name) const& {
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
    std::string out;
    decode_with_encoding(bytes, EncodingId::Utf8, out, false);
    return out;
}

std::string decode_text(std::string_view bytes, std::string_view label) {
    EncodingId fallback = EncodingId::Utf8;
    if (!get_encoding_from_label(label, fallback)) {
        fallback = EncodingId::Utf8;
    }
    return whatwg_decode(bytes, fallback);
}

std::string encoding_name_for_label(std::string_view label) {
    EncodingId encoding = EncodingId::Utf8;
    if (!get_encoding_from_label(label, encoding)) return std::string();
    return std::string(canonical_encoding_name(encoding));
}

} // namespace litebird
