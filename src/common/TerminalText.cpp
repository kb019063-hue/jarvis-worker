#include "litebird/common/TerminalText.hpp"

namespace litebird {

namespace {

// A byte a terminal acts on whatever comes with it.
bool is_c0_or_del(unsigned char c) {
    return c < 0x20 || c == 0x7f;
}

// A UTF-8 lead byte for a sequence of `length` bytes (2, 3 or 4).
bool is_lead(unsigned char c, int length) {
    switch (length) {
        case 2: return (c & 0xe0) == 0xc0;
        case 3: return (c & 0xf0) == 0xe0;
        case 4: return (c & 0xf8) == 0xf0;
        default: return false;
    }
}

bool is_continuation(unsigned char c) { return (c & 0xc0) == 0x80; }

// U+FFFD, the replacement character: one character for one byte, so the length
// of a dump line says nothing about where the byte was, and nothing of the
// page's text is dropped without a mark.
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
        if (c == 0x9b || c == 0x9d || c == 0x90 || c == 0x9e || c == 0x9f) {
            // The 8-bit introducers, which only a single-byte encoding can put
            // in text. They are replaced whether or not a UTF-8 sequence is
            // being read, because they are never part of one.
            out += kReplacement;
            ++i;
            continue;
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));      // printable ASCII
            ++i;
            continue;
        }
        // A multi-byte character: kept whole when it is well formed, because
        // its bytes are the page's text and a terminal prints them. A sequence
        // that is not, is a byte at a time, so a byte of it can be replaced
        // without eating the bytes that follow.
        int length = 0;
        if (is_lead(c, 2)) length = 2;
        else if (is_lead(c, 3)) length = 3;
        else if (is_lead(c, 4)) length = 4;
        bool whole = length > 0 && i + static_cast<std::size_t>(length) <= text.size();
        for (int k = 1; whole && k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if (!is_continuation(next)) whole = false;
        }
        if (!whole) {
            // Not a UTF-8 lead byte, or not a whole sequence. A single 0x9B is
            // caught above; every other high byte here is not text this engine
            // can name, and is replaced so that no byte of it survives.
            out += kReplacement;
            ++i;
            continue;
        }
        out.append(text, i, static_cast<std::size_t>(length));
        i += static_cast<std::size_t>(length);
    }
    return out;
}

} // namespace litebird
