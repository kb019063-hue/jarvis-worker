#include "litebird/common/TerminalText.hpp"

namespace litebird {

namespace {

// A byte a terminal acts on whatever comes with it.
bool is_c0_or_del(unsigned char c) {
    return c < 0x20 || c == 0x7f;
}

// Decode one non-ASCII scalar strictly. On an error consume only the first
// byte, so a malformed prefix cannot hide a following terminal instruction.
std::size_t decode_scalar(const std::string& text, std::size_t at, unsigned& cp) {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t length;
    unsigned minimum;
    if (lead >= 0xc2 && lead <= 0xdf) { length = 2; cp = lead & 0x1f; minimum = 0x80; }
    else if (lead >= 0xe0 && lead <= 0xef) { length = 3; cp = lead & 0x0f; minimum = 0x800; }
    else if (lead >= 0xf0 && lead <= 0xf4) { length = 4; cp = lead & 0x07; minimum = 0x10000; }
    else return 0;
    if (length > text.size() - at) return 0;
    for (std::size_t i = 1; i < length; ++i) {
        const auto c = static_cast<unsigned char>(text[at + i]);
        if ((c & 0xc0) != 0x80) return 0;
        cp = (cp << 6) | (c & 0x3f);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    return length;
}

// U+FFFD marks each rejected control or malformed byte in the output.
constexpr const char* kReplacement = "\xef\xbf\xbd";

} // namespace

std::string terminal_safe(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        // A tab and a newline are layout the dump writer uses itself; they are
        // not instructions, and every line of a dump has them.
        if (c == '\t' || c == '\n') {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        if (is_c0_or_del(c)) {
            out += kReplacement;
            ++i;
            continue;
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));      // printable ASCII
            ++i;
            continue;
        }
        unsigned cp = 0;
        const std::size_t length = decode_scalar(text, i, cp);
        if (length == 0) {
            out += kReplacement;
            ++i;
        } else if (cp >= 0x80 && cp <= 0x9f) {
            // C1 controls can also arrive as valid UTF-8, not only raw bytes.
            out += kReplacement;
            i += length;
        } else {
            out.append(text, i, length);
            i += length;
        }
    }
    return out;
}

} // namespace litebird
