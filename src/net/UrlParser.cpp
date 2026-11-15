// The URL parser of the WHATWG URL Standard (https://url.spec.whatwg.org/).
//
// The parser is the standard's state machine, state for state, including the
// "state override" that the setters of the URL API use to parse one part in
// place. It works on the bytes of the UTF-8 text: every character the syntax
// cares about is ASCII, and every byte outside ASCII is percent-encoded in
// every part, so nothing is lost by not decoding.

#include "litebird/net/UrlParser.hpp"
#include "litebird/net/Idna.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace litebird {

namespace {

bool is_special_scheme(std::string_view scheme) {
    return scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "ws" || scheme == "wss" ||
           scheme == "file";
}

int default_port(std::string_view scheme) {
    if (scheme == "http" || scheme == "ws") return 80;
    if (scheme == "https" || scheme == "wss") return 443;
    if (scheme == "ftp") return 21;
    return -1;
}

bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int hex_value(char c) { return is_digit(c) ? c - '0' : (c | 0x20) - 'a' + 10; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// --- Percent-encoding -------------------------------------------------------------

// Which characters a part of a URL writes as %XX. Each set contains the one
// before it.
enum class EncodeSet { C0Control, Fragment, Query, SpecialQuery, Path, Userinfo, Component, Form };

bool in_set(unsigned char c, EncodeSet set) {
    if (c < 0x20 || c > 0x7E) return true;
    switch (set) {
        case EncodeSet::C0Control:
            return false;
        case EncodeSet::Fragment:
            return c == ' ' || c == '"' || c == '<' || c == '>' || c == '`';
        case EncodeSet::Query:
            return c == ' ' || c == '"' || c == '#' || c == '<' || c == '>';
        case EncodeSet::SpecialQuery:
            return c == '\'' || in_set(c, EncodeSet::Query);
        case EncodeSet::Path:
            return c == '?' || c == '^' || c == '`' || c == '{' || c == '}' || in_set(c, EncodeSet::Query);
        case EncodeSet::Userinfo:
            return c == '/' || c == ':' || c == ';' || c == '=' || c == '@' || c == '[' || c == '\\' || c == ']' ||
                   c == '|' || in_set(c, EncodeSet::Path);
        case EncodeSet::Component:
            return (c >= '$' && c <= '&') || c == '+' || c == ',' || in_set(c, EncodeSet::Userinfo);
        case EncodeSet::Form:
            return c == '!' || (c >= '\'' && c <= ')') || c == '~' || in_set(c, EncodeSet::Component);
    }
    return false;
}

void append_encoded(std::string& out, unsigned char c, EncodeSet set) {
    if (!in_set(c, set)) {
        out += static_cast<char>(c);
        return;
    }
    static const char kHex[] = "0123456789ABCDEF";
    out += '%';
    out += kHex[c >> 4];
    out += kHex[c & 15];
}

std::string encode(std::string_view text, EncodeSet set) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) append_encoded(out, c, set);
    return out;
}

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && is_hex(text[i + 1]) && is_hex(text[i + 2])) {
            out += static_cast<char>((hex_value(text[i + 1]) << 4) | hex_value(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

// Bytes as text: what is not UTF-8 becomes U+FFFD, one for each sequence
// that goes wrong, the way a browser decodes a page.
std::string utf8_or_replacement(std::string_view bytes) {
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
        unsigned char low = 0x80, high = 0xBF;          // the range the next byte must be in
        if (lead >= 0xC2 && lead <= 0xDF) needed = 1;
        else if (lead >= 0xE0 && lead <= 0xEF) {
            needed = 2;
            if (lead == 0xE0) low = 0xA0;               // no overlong forms
            if (lead == 0xED) high = 0x9F;              // no surrogates
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            needed = 3;
            if (lead == 0xF0) low = 0x90;
            if (lead == 0xF4) high = 0x8F;              // nothing above U+10FFFF
        }
        std::size_t length = 1;
        bool ok = needed != 0;
        while (ok && length <= needed) {
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
        if (ok) out.append(bytes.substr(i, length));
        else out += "\xEF\xBF\xBD";
        i += length;        // the bytes that were part of the sequence, right or wrong
    }
    return out;
}

// --- Hosts ------------------------------------------------------------------------

bool is_forbidden_host_char(unsigned char c) {
    switch (c) {
        case 0x00: case '\t': case '\n': case '\r': case ' ': case '#': case '/': case ':': case '<': case '>':
        case '?': case '@': case '[': case '\\': case ']': case '^': case '|':
            return true;
        default:
            return false;
    }
}

bool is_forbidden_domain_char(unsigned char c) {
    return is_forbidden_host_char(c) || c <= 0x1F || c == '%' || c == 0x7F;
}

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

// One part of an IPv4 address: decimal, 0x hexadecimal or 0 octal.
bool parse_ipv4_number(std::string_view input, uint64_t& out) {
    if (input.empty()) return false;
    unsigned radix = 10;
    if (input.size() >= 2 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) {
        input.remove_prefix(2);
        radix = 16;
    } else if (input.size() >= 2 && input[0] == '0') {
        input.remove_prefix(1);
        radix = 8;
    }
    uint64_t value = 0;
    for (char c : input) {
        if (!is_hex(c)) return false;
        const unsigned digit = static_cast<unsigned>(hex_value(c));
        if (digit >= radix) return false;
        value = value * radix + digit;
        if (value > 0xFFFFFFFFFFull) value = 0xFFFFFFFFFFull;      // far too big already; do not overflow
    }
    out = value;
    return true;
}

// "Does this host name end in a number?" If it does it is an IPv4 address,
// or an error: `http://1.2.3/` is 1.2.0.3, `http://a.1/` is no URL.
bool ends_in_a_number(std::string_view domain) {
    auto parts = split(domain, '.');
    if (parts.back().empty()) {
        if (parts.size() == 1) return false;
        parts.pop_back();
    }
    const std::string_view last = parts.back();
    if (!last.empty() && std::all_of(last.begin(), last.end(), is_digit)) return true;
    uint64_t ignored = 0;
    return parse_ipv4_number(last, ignored);
}

bool parse_ipv4(std::string_view input, std::string& out) {
    auto parts = split(input, '.');
    if (parts.back().empty() && parts.size() > 1) parts.pop_back();
    if (parts.size() > 4) return false;
    std::vector<uint64_t> numbers;
    for (std::string_view part : parts) {
        uint64_t number = 0;
        if (!parse_ipv4_number(part, number)) return false;
        numbers.push_back(number);
    }
    for (std::size_t i = 0; i + 1 < numbers.size(); ++i) {
        if (numbers[i] > 255) return false;
    }
    uint64_t limit = 1;
    for (std::size_t i = 0; i < 5 - numbers.size(); ++i) limit *= 256;
    if (numbers.back() >= limit) return false;
    uint64_t address = numbers.back();
    for (std::size_t i = 0; i + 1 < numbers.size(); ++i) {
        uint64_t scale = 1;
        for (std::size_t k = 0; k < 3 - i; ++k) scale *= 256;
        address += numbers[i] * scale;
    }
    out.clear();
    for (int shift = 24; shift >= 0; shift -= 8) {
        out += std::to_string((address >> shift) & 0xFF);
        if (shift) out += '.';
    }
    return true;
}

bool parse_ipv6(std::string_view input, std::string& out) {
    std::array<uint32_t, 8> address{};
    int piece = 0;
    int compress = -1;
    std::size_t p = 0;
    const std::size_t n = input.size();
    auto at = [&](std::size_t i) { return i < n ? input[i] : '\0'; };

    if (at(p) == ':') {
        if (at(p + 1) != ':') return false;
        p += 2;
        compress = ++piece;
    }
    while (p < n) {
        if (piece == 8) return false;
        if (at(p) == ':') {
            if (compress != -1) return false;
            ++p;
            compress = ++piece;
            continue;
        }
        uint32_t value = 0;
        int length = 0;
        while (length < 4 && p < n && is_hex(at(p))) {
            value = value * 16 + static_cast<uint32_t>(hex_value(at(p)));
            ++p;
            ++length;
        }
        if (p < n && at(p) == '.') {
            // The last 32 bits written as an IPv4 address.
            if (length == 0) return false;
            p -= static_cast<std::size_t>(length);
            if (piece > 6) return false;
            int numbers_seen = 0;
            while (p < n) {
                int ipv4_piece = -1;
                if (numbers_seen > 0) {
                    if (at(p) == '.' && numbers_seen < 4) ++p;
                    else return false;
                }
                if (p >= n || !is_digit(at(p))) return false;
                while (p < n && is_digit(at(p))) {
                    const int number = at(p) - '0';
                    if (ipv4_piece == -1) ipv4_piece = number;
                    else if (ipv4_piece == 0) return false;
                    else ipv4_piece = ipv4_piece * 10 + number;
                    if (ipv4_piece > 255) return false;
                    ++p;
                }
                address[static_cast<std::size_t>(piece)] = address[static_cast<std::size_t>(piece)] * 0x100 +
                                                           static_cast<uint32_t>(ipv4_piece);
                ++numbers_seen;
                if (numbers_seen == 2 || numbers_seen == 4) ++piece;
            }
            if (numbers_seen != 4) return false;
            break;
        }
        if (p < n && at(p) == ':') {
            ++p;
            if (p >= n) return false;
        } else if (p < n) {
            return false;
        }
        address[static_cast<std::size_t>(piece)] = value;
        ++piece;
    }
    if (compress != -1) {
        int swaps = piece - compress;
        piece = 7;
        while (piece != 0 && swaps > 0) {
            std::swap(address[static_cast<std::size_t>(piece)], address[static_cast<std::size_t>(compress + swaps - 1)]);
            --piece;
            --swaps;
        }
    } else if (piece != 8) {
        return false;
    }

    // Written with its longest run of zero pieces (two or more) as "::".
    int best_start = -1, best_length = 0;
    for (int i = 0; i < 8;) {
        if (address[static_cast<std::size_t>(i)] != 0) {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && address[static_cast<std::size_t>(j)] == 0) ++j;
        if (j - i > best_length) {
            best_start = i;
            best_length = j - i;
        }
        i = j;
    }
    if (best_length < 2) best_start = -1;
    static const char kHex[] = "0123456789abcdef";
    out = "[";
    bool ignore_zero = false;
    for (int i = 0; i < 8; ++i) {
        const uint32_t value = address[static_cast<std::size_t>(i)];
        if (ignore_zero && value == 0) continue;
        ignore_zero = false;
        if (best_start == i) {
            out += i == 0 ? "::" : ":";
            ignore_zero = true;
            continue;
        }
        bool started = false;
        for (int shift = 12; shift >= 0; shift -= 4) {
            const unsigned digit = (value >> shift) & 15;
            if (digit || started || shift == 0) {
                out += kHex[digit];
                started = true;
            }
        }
        if (i != 7) out += ':';
    }
    out += ']';
    return true;
}

// The host of a URL from the text between "//" and the path. `opaque` is
// for the schemes that are not special: their host is kept as written.
bool parse_host(std::string_view input, bool opaque, std::string& out) {
    if (!input.empty() && input.front() == '[') {
        if (input.back() != ']') return false;
        return parse_ipv6(input.substr(1, input.size() - 2), out);
    }
    if (opaque) {
        for (unsigned char c : input) {
            if (is_forbidden_host_char(c)) return false;
        }
        out = encode(input, EncodeSet::C0Control);
        return true;
    }
    // WHATWG domain to ASCII (UTS #46, nontransitional) then the forbidden
    // domain code point check on the result.
    std::string domain = utf8_or_replacement(percent_decode(input));
    std::string ascii;
    if (!domain_to_ascii(domain, ascii)) return false;
    for (unsigned char c : ascii) {
        if (is_forbidden_domain_char(c)) return false;
    }
    if (ends_in_a_number(ascii)) return parse_ipv4(ascii, out);
    out = std::move(ascii);
    return true;
}

// --- Paths ------------------------------------------------------------------------

bool is_windows_drive_letter(std::string_view text) {
    return text.size() == 2 && is_alpha(text[0]) && (text[1] == ':' || text[1] == '|');
}

bool is_normalized_drive_letter(std::string_view text) {
    return text.size() == 2 && is_alpha(text[0]) && text[1] == ':';
}

bool starts_with_drive_letter(std::string_view text) {
    if (text.size() < 2 || !is_windows_drive_letter(text.substr(0, 2))) return false;
    return text.size() == 2 || text[2] == '/' || text[2] == '\\' || text[2] == '?' || text[2] == '#';
}

bool equals_ignoring_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

bool is_single_dot(std::string_view segment) {
    return segment == "." || equals_ignoring_case(segment, "%2e");
}

bool is_double_dot(std::string_view segment) {
    return segment == ".." || equals_ignoring_case(segment, ".%2e") || equals_ignoring_case(segment, "%2e.") ||
           equals_ignoring_case(segment, "%2e%2e");
}

void shorten_path(Url& url) {
    if (url.scheme == "file" && url.path.size() == 1 && is_normalized_drive_letter(url.path[0])) return;
    if (!url.path.empty()) url.path.pop_back();
}

// --- The parser -------------------------------------------------------------------

enum class State {
    SchemeStart, Scheme, NoScheme, SpecialRelativeOrAuthority, PathOrAuthority, Relative, RelativeSlash,
    SpecialAuthoritySlashes, SpecialAuthorityIgnoreSlashes, Authority, Host, Hostname, Port, File, FileSlash,
    FileHost, PathStart, Path, OpaquePath, Query, Fragment
};

// The standard's "basic URL parser". With `override` it parses one part of
// `url` in place, starting in that state. True unless the input is no URL
// (or, with an override, cannot be that part).
bool run(std::string_view raw, const Url* base, Url& url, std::optional<State> override) {
    // Leading and trailing spaces and control characters do not belong to a
    // URL, and tabs and line breaks are dropped wherever they are.
    if (!override) {
        while (!raw.empty() && static_cast<unsigned char>(raw.front()) <= 0x20) raw.remove_prefix(1);
        while (!raw.empty() && static_cast<unsigned char>(raw.back()) <= 0x20) raw.remove_suffix(1);
    }
    std::string input;
    input.reserve(raw.size());
    for (char c : raw) {
        if (c != '\t' && c != '\n' && c != '\r') input += c;
    }

    State state = override ? *override : State::SchemeStart;
    std::string buffer;
    bool at_sign_seen = false;
    bool inside_brackets = false;
    bool password_token_seen = false;
    const long n = static_cast<long>(input.size());

    auto rest_from = [&](long position) {
        return position >= n ? std::string_view() : std::string_view(input).substr(static_cast<std::size_t>(position));
    };

    for (long p = 0;; ++p) {
        const bool eof = p >= n;
        const char c = eof ? '\0' : input[static_cast<std::size_t>(p)];
        const bool special = is_special_scheme(url.scheme);
        const bool slash = !eof && (c == '/' || (special && c == '\\'));
        const bool ends_authority = eof || c == '/' || c == '?' || c == '#' || (special && c == '\\');

        switch (state) {
            case State::SchemeStart:
                if (!eof && is_alpha(c)) {
                    buffer += lower(c);
                    state = State::Scheme;
                } else if (!override) {
                    state = State::NoScheme;
                    --p;
                } else {
                    return false;
                }
                break;

            case State::Scheme:
                if (!eof && (is_alpha(c) || is_digit(c) || c == '+' || c == '-' || c == '.')) {
                    buffer += lower(c);
                } else if (!eof && c == ':') {
                    if (override) {
                        // A URL keeps its kind: http cannot become mailto.
                        if (special != is_special_scheme(buffer)) return true;
                        if ((!url.username.empty() || !url.password.empty() || url.port != -1) && buffer == "file") return true;
                        if (url.scheme == "file" && url.has_host && url.host.empty()) return true;
                    }
                    url.scheme = buffer;
                    if (override) {
                        if (url.port == default_port(url.scheme)) url.port = -1;
                        return true;
                    }
                    buffer.clear();
                    if (url.scheme == "file") {
                        state = State::File;
                    } else if (is_special_scheme(url.scheme) && base && base->scheme == url.scheme) {
                        state = State::SpecialRelativeOrAuthority;
                    } else if (is_special_scheme(url.scheme)) {
                        state = State::SpecialAuthoritySlashes;
                    } else if (!rest_from(p + 1).empty() && rest_from(p + 1).front() == '/') {
                        state = State::PathOrAuthority;
                        ++p;
                    } else {
                        url.opaque_path = true;
                        url.opaque.clear();
                        state = State::OpaquePath;
                    }
                } else if (!override) {
                    // Not a scheme after all: read it again as a relative reference.
                    buffer.clear();
                    state = State::NoScheme;
                    p = -1;
                } else {
                    return false;
                }
                break;

            case State::NoScheme:
                if (!base || (base->opaque_path && (eof || c != '#'))) return false;
                if (base->opaque_path) {
                    // Only a fragment can be added to "mailto:x" or "about:blank".
                    url.scheme = base->scheme;
                    url.opaque_path = true;
                    url.opaque = base->opaque;
                    url.has_query = base->has_query;
                    url.query = base->query;
                    url.has_fragment = true;
                    url.fragment.clear();
                    state = State::Fragment;
                } else if (base->scheme != "file") {
                    state = State::Relative;
                    --p;
                } else {
                    state = State::File;
                    --p;
                }
                break;

            case State::SpecialRelativeOrAuthority:
                if (!eof && c == '/' && !rest_from(p + 1).empty() && rest_from(p + 1).front() == '/') {
                    state = State::SpecialAuthorityIgnoreSlashes;
                    ++p;
                } else {
                    state = State::Relative;
                    --p;
                }
                break;

            case State::PathOrAuthority:
                if (!eof && c == '/') {
                    state = State::Authority;
                } else {
                    state = State::Path;
                    --p;
                }
                break;

            case State::Relative: {
                url.scheme = base->scheme;
                const bool now_special = is_special_scheme(url.scheme);
                if (!eof && (c == '/' || (now_special && c == '\\'))) {
                    state = State::RelativeSlash;
                    break;
                }
                url.username = base->username;
                url.password = base->password;
                url.has_host = base->has_host;
                url.host = base->host;
                url.port = base->port;
                url.path = base->path;
                url.has_query = base->has_query;
                url.query = base->query;
                if (!eof && c == '?') {
                    url.has_query = true;
                    url.query.clear();
                    state = State::Query;
                } else if (!eof && c == '#') {
                    url.has_fragment = true;
                    url.fragment.clear();
                    state = State::Fragment;
                } else if (!eof) {
                    url.has_query = false;
                    url.query.clear();
                    shorten_path(url);
                    state = State::Path;
                    --p;
                }
                break;
            }

            case State::RelativeSlash:
                if (special && !eof && (c == '/' || c == '\\')) {
                    state = State::SpecialAuthorityIgnoreSlashes;
                } else if (!eof && c == '/') {
                    state = State::Authority;
                } else {
                    url.username = base->username;
                    url.password = base->password;
                    url.has_host = base->has_host;
                    url.host = base->host;
                    url.port = base->port;
                    state = State::Path;
                    --p;
                }
                break;

            case State::SpecialAuthoritySlashes:
                if (!eof && c == '/' && !rest_from(p + 1).empty() && rest_from(p + 1).front() == '/') {
                    state = State::SpecialAuthorityIgnoreSlashes;
                    ++p;
                } else {
                    state = State::SpecialAuthorityIgnoreSlashes;
                    --p;
                }
                break;

            case State::SpecialAuthorityIgnoreSlashes:
                if (eof || (c != '/' && c != '\\')) {
                    state = State::Authority;
                    --p;
                }
                break;

            case State::Authority:
                if (!eof && c == '@') {
                    // Everything so far was "user:password".
                    if (at_sign_seen) buffer.insert(0, "%40");
                    at_sign_seen = true;
                    for (char unit : buffer) {
                        if (unit == ':' && !password_token_seen) {
                            password_token_seen = true;
                            continue;
                        }
                        append_encoded(password_token_seen ? url.password : url.username,
                                       static_cast<unsigned char>(unit), EncodeSet::Userinfo);
                    }
                    buffer.clear();
                } else if (ends_authority) {
                    if (at_sign_seen && buffer.empty()) return false;
                    p -= static_cast<long>(buffer.size()) + 1;
                    buffer.clear();
                    state = State::Host;
                } else {
                    buffer += c;
                }
                break;

            case State::Host:
            case State::Hostname:
                if (override && url.scheme == "file") {
                    --p;
                    state = State::FileHost;
                } else if (!eof && c == ':' && !inside_brackets) {
                    if (buffer.empty()) return false;
                    if (override && *override == State::Hostname) return false;
                    std::string host;
                    if (!parse_host(buffer, !special, host)) return false;
                    url.has_host = true;
                    url.host = std::move(host);
                    buffer.clear();
                    state = State::Port;
                } else if (ends_authority) {
                    --p;
                    if (special && buffer.empty()) return false;
                    if (override && buffer.empty() &&
                        (!url.username.empty() || !url.password.empty() || url.port != -1)) {
                        return false;
                    }
                    std::string host;
                    if (!parse_host(buffer, !special, host)) return false;
                    url.has_host = true;
                    url.host = std::move(host);
                    buffer.clear();
                    state = State::PathStart;
                    if (override) return true;
                } else {
                    if (c == '[') inside_brackets = true;
                    if (c == ']') inside_brackets = false;
                    buffer += c;
                }
                break;

            case State::Port:
                if (!eof && is_digit(c)) {
                    buffer += c;
                } else if (ends_authority || override) {
                    if (!buffer.empty()) {
                        long port = 0;
                        for (char digit : buffer) {
                            port = port * 10 + (digit - '0');
                            if (port > 65535) return false;
                        }
                        url.port = port == default_port(url.scheme) ? -1 : static_cast<int>(port);
                        buffer.clear();
                        if (override) return true;
                    }
                    if (override) return false;
                    state = State::PathStart;
                    --p;
                } else {
                    return false;
                }
                break;

            case State::File:
                url.scheme = "file";
                url.has_host = true;
                url.host.clear();
                if (!eof && (c == '/' || c == '\\')) {
                    state = State::FileSlash;
                } else if (base && base->scheme == "file") {
                    url.host = base->host;
                    url.path = base->path;
                    url.has_query = base->has_query;
                    url.query = base->query;
                    if (!eof && c == '?') {
                        url.has_query = true;
                        url.query.clear();
                        state = State::Query;
                    } else if (!eof && c == '#') {
                        url.has_fragment = true;
                        url.fragment.clear();
                        state = State::Fragment;
                    } else if (!eof) {
                        url.has_query = false;
                        url.query.clear();
                        if (!starts_with_drive_letter(rest_from(p))) shorten_path(url);
                        else url.path.clear();
                        state = State::Path;
                        --p;
                    }
                } else {
                    state = State::Path;
                    --p;
                }
                break;

            case State::FileSlash:
                if (!eof && (c == '/' || c == '\\')) {
                    state = State::FileHost;
                } else {
                    if (base && base->scheme == "file") {
                        url.host = base->host;
                        if (!starts_with_drive_letter(rest_from(p)) && !base->path.empty() &&
                            is_normalized_drive_letter(base->path[0])) {
                            url.path.push_back(base->path[0]);
                        }
                    }
                    state = State::Path;
                    --p;
                }
                break;

            case State::FileHost:
                if (eof || c == '/' || c == '\\' || c == '?' || c == '#') {
                    --p;
                    if (!override && is_windows_drive_letter(buffer)) {
                        // "file://C:/x": that was no host, it is the path.
                        state = State::Path;
                    } else if (buffer.empty()) {
                        url.has_host = true;
                        url.host.clear();
                        if (override) return true;
                        state = State::PathStart;
                    } else {
                        std::string host;
                        if (!parse_host(buffer, false, host)) return false;
                        if (host == "localhost") host.clear();
                        url.has_host = true;
                        url.host = std::move(host);
                        if (override) return true;
                        buffer.clear();
                        state = State::PathStart;
                    }
                } else {
                    buffer += c;
                }
                break;

            case State::PathStart:
                if (special) {
                    state = State::Path;
                    if (eof || (c != '/' && c != '\\')) --p;
                } else if (!override && !eof && c == '?') {
                    url.has_query = true;
                    url.query.clear();
                    state = State::Query;
                } else if (!override && !eof && c == '#') {
                    url.has_fragment = true;
                    url.fragment.clear();
                    state = State::Fragment;
                } else if (!eof) {
                    state = State::Path;
                    if (c != '/') --p;
                } else if (override && !url.has_host) {
                    url.path.emplace_back();
                }
                break;

            case State::Path:
                if (eof || slash || (!override && (c == '?' || c == '#'))) {
                    if (is_double_dot(buffer)) {
                        shorten_path(url);
                        if (!slash) url.path.emplace_back();
                    } else if (is_single_dot(buffer)) {
                        if (!slash) url.path.emplace_back();
                    } else {
                        if (url.scheme == "file" && url.path.empty() && is_windows_drive_letter(buffer)) buffer[1] = ':';
                        url.path.push_back(buffer);
                    }
                    buffer.clear();
                    if (!eof && c == '?') {
                        url.has_query = true;
                        url.query.clear();
                        state = State::Query;
                    } else if (!eof && c == '#') {
                        url.has_fragment = true;
                        url.fragment.clear();
                        state = State::Fragment;
                    }
                } else {
                    append_encoded(buffer, static_cast<unsigned char>(c), EncodeSet::Path);
                }
                break;

            case State::OpaquePath:
                if (!eof && c == '?') {
                    url.has_query = true;
                    url.query.clear();
                    state = State::Query;
                } else if (!eof && c == '#') {
                    url.has_fragment = true;
                    url.fragment.clear();
                    state = State::Fragment;
                } else if (!eof && c == ' ') {
                    // A space that ends the path would not survive being
                    // written out and read back.
                    const std::string_view rest = rest_from(p + 1);
                    if (!rest.empty() && (rest.front() == '?' || rest.front() == '#')) url.opaque += "%20";
                    else url.opaque += ' ';
                } else if (!eof) {
                    append_encoded(url.opaque, static_cast<unsigned char>(c), EncodeSet::C0Control);
                }
                break;

            case State::Query:
                if (eof || (!override && c == '#')) {
                    url.query += encode(buffer, special ? EncodeSet::SpecialQuery : EncodeSet::Query);
                    buffer.clear();
                    if (!eof) {
                        url.has_fragment = true;
                        url.fragment.clear();
                        state = State::Fragment;
                    }
                } else {
                    buffer += c;
                }
                break;

            case State::Fragment:
                if (!eof) append_encoded(url.fragment, static_cast<unsigned char>(c), EncodeSet::Fragment);
                break;
        }

        // The end of the input is a character like any other: a state that
        // stepped back from it sees it again.
        if (p >= n) break;
    }
    return true;
}

bool cannot_have_credentials_or_port(const Url& url) {
    return !url.has_host || url.host.empty() || url.scheme == "file";
}

} // namespace

bool Url::parse(std::string_view input, const Url* base, Url& out) {
    out = Url();
    return run(input, base, out, std::nullopt);
}

bool Url::parse(std::string_view input, std::string_view base, Url& out) {
    if (base.empty()) return parse(input, nullptr, out);
    Url parsed_base;
    if (!parse(base, nullptr, parsed_base)) return false;
    return parse(input, &parsed_base, out);
}

bool Url::is_special() const {
    return is_special_scheme(scheme);
}

std::string Url::pathname() const {
    if (opaque_path) return opaque;
    std::string out;
    for (const std::string& segment : path) {
        out += '/';
        out += segment;
    }
    return out;
}

std::string Url::host_and_port() const {
    if (!has_host) return std::string();
    return port == -1 ? host : host + ":" + std::to_string(port);
}

std::string Url::hostname() const {
    return has_host ? host : std::string();
}

std::string Url::port_text() const {
    return port == -1 ? std::string() : std::to_string(port);
}

std::string Url::protocol() const {
    return scheme + ":";
}

std::string Url::search() const {
    return has_query && !query.empty() ? "?" + query : std::string();
}

std::string Url::hash() const {
    return has_fragment && !fragment.empty() ? "#" + fragment : std::string();
}

std::string Url::href(bool without_fragment) const {
    std::string out = scheme + ":";
    if (has_host) {
        out += "//";
        if (!username.empty() || !password.empty()) {
            out += username;
            if (!password.empty()) out += ":" + password;
            out += '@';
        }
        out += host_and_port();
    } else if (!opaque_path && path.size() > 1 && path[0].empty()) {
        // Otherwise "web+demo:/.//not-a-host/" would read back with a host.
        out += "/.";
    }
    out += pathname();
    if (has_query) out += "?" + query;
    if (has_fragment && !without_fragment) out += "#" + fragment;
    return out;
}

std::string Url::origin() const {
    if (scheme == "blob") {
        // A blob: URL belongs to the page that made it.
        Url inner;
        if (parse(pathname(), nullptr, inner) && (inner.scheme == "http" || inner.scheme == "https")) return inner.origin();
        return "null";
    }
    if (scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss" || scheme == "ftp") {
        return scheme + "://" + host_and_port();
    }
    return "null";
}

void Url::set_protocol(std::string_view value) {
    run(std::string(value) + ":", nullptr, *this, State::SchemeStart);
}

void Url::set_username(std::string_view value) {
    if (cannot_have_credentials_or_port(*this)) return;
    username = encode(value, EncodeSet::Userinfo);
}

void Url::set_password(std::string_view value) {
    if (cannot_have_credentials_or_port(*this)) return;
    password = encode(value, EncodeSet::Userinfo);
}

void Url::set_host(std::string_view value) {
    if (opaque_path) return;
    run(value, nullptr, *this, State::Host);
}

void Url::set_hostname(std::string_view value) {
    if (opaque_path) return;
    run(value, nullptr, *this, State::Hostname);
}

void Url::set_port(std::string_view value) {
    if (cannot_have_credentials_or_port(*this)) return;
    if (value.empty()) port = -1;
    else run(value, nullptr, *this, State::Port);
}

void Url::set_pathname(std::string_view value) {
    if (opaque_path) return;
    path.clear();
    run(value, nullptr, *this, State::PathStart);
}

void Url::set_search(std::string_view value) {
    query.clear();
    has_query = !value.empty();
    if (value.empty()) return;
    if (value.front() == '?') value.remove_prefix(1);
    run(value, nullptr, *this, State::Query);
}

void Url::set_hash(std::string_view value) {
    fragment.clear();
    has_fragment = !value.empty();
    if (value.empty()) return;
    if (value.front() == '#') value.remove_prefix(1);
    run(value, nullptr, *this, State::Fragment);
}

bool Url::same_site_parts(const Url& other) const {
    return scheme == other.scheme && username == other.username && password == other.password &&
           has_host == other.has_host && host == other.host && port == other.port;
}

std::string file_url_from_path(std::string_view absolute_path) {
    std::string out = "file://";
    if (absolute_path.empty() || absolute_path.front() != '/') out += '/';
    for (unsigned char c : absolute_path) {
        // In a path "%" starts an escape, "\\" is a slash and "?" and "#"
        // end it; in a file name they are ordinary characters.
        if (c == '%' || c == '\\') {
            static const char kHex[] = "0123456789ABCDEF";
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        } else {
            append_encoded(out, c, EncodeSet::Path);
        }
    }
    return out;
}

// --- application/x-www-form-urlencoded ---------------------------------------------

std::vector<std::pair<std::string, std::string>> parse_urlencoded(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> pairs;
    auto decode = [](std::string_view part) {
        std::string spaced(part);
        std::replace(spaced.begin(), spaced.end(), '+', ' ');
        return utf8_or_replacement(percent_decode(spaced));
    };
    for (std::string_view piece : split(text, '&')) {
        if (piece.empty()) continue;
        const std::size_t equals = piece.find('=');
        if (equals == std::string_view::npos) pairs.emplace_back(decode(piece), std::string());
        else pairs.emplace_back(decode(piece.substr(0, equals)), decode(piece.substr(equals + 1)));
    }
    return pairs;
}

std::string serialize_urlencoded(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::string out;
    auto append = [&](const std::string& text) {
        for (unsigned char c : text) {
            if (c == ' ') out += '+';
            else append_encoded(out, c, EncodeSet::Form);
        }
    };
    for (const auto& [name, value] : pairs) {
        if (!out.empty()) out += '&';
        append(name);
        out += '=';
        append(value);
    }
    return out;
}

} // namespace litebird
