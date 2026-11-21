#include "litebird/net/TextEncoding.hpp"

#include <algorithm>
#include <cstring>

namespace litebird {

namespace {

constexpr int kEndOfQueue = -1;

bool is_ascii_whitespace(char c) {
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c + ('a' - 'A'));
    }
    return c;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

void append_replacement(std::string& out) {
    out.append("\xEF\xBF\xBD", 3);
}

class ByteQueue {
public:
    explicit ByteQueue(std::string_view input) : input_(input) {}

    int read() {
        if (pushed_count_ > 0) {
            return pushed_[--pushed_count_];
        }
        if (pos_ < input_.size()) {
            return static_cast<unsigned char>(input_[pos_++]);
        }
        return kEndOfQueue;
    }

    void restore1(int b1) {
        if (b1 != kEndOfQueue) {
            pushed_[pushed_count_++] = static_cast<uint8_t>(b1);
        }
    }

    void restore2(int b1, int b2) {
        restore1(b2);
        restore1(b1);
    }

    void restore3(int b1, int b2, int b3) {
        restore1(b3);
        restore1(b2);
        restore1(b1);
    }

private:
    std::string_view input_;
    std::size_t pos_ = 0;
    uint8_t pushed_[8]{};
    std::size_t pushed_count_ = 0;
};

uint32_t lookup_gb18030_ranges_code_point(uint32_t pointer) {
    if ((pointer > 39419u && pointer < 189000u) || pointer > 1237575u) {
        return 0;
    }
    if (pointer == 7457u) {
        return 0xE7C7u;
    }
    const auto* begin = encoding_tables::kIndexGb18030Ranges;
    const auto* end = begin + 207;
    const auto* it = std::upper_bound(
        begin, end, pointer,
        [](uint32_t p, const encoding_tables::Gb18030Range& r) { return p < r.pointer; });
    if (it == begin) {
        return 0;
    }
    --it;
    return it->code_point + pointer - it->pointer;
}

const uint16_t* single_byte_table(EncodingId id) {
    int idx = static_cast<int>(id);
    if (id == EncodingId::Iso8859_8_I) {
        return encoding_tables::kSingleByte[7];
    }
    if (id >= EncodingId::Iso8859_10 && id <= EncodingId::XMacCyrillic) {
        return encoding_tables::kSingleByte[idx - 2];
    }
    return encoding_tables::kSingleByte[idx - 1];
}

bool decode_utf8(std::string_view bytes, std::string& out, bool fatal) {
    ByteQueue q(bytes);
    uint32_t code_point = 0;
    uint8_t bytes_seen = 0;
    uint8_t bytes_needed = 0;
    uint8_t lower_boundary = 0x80u;
    uint8_t upper_boundary = 0xBFu;

    for (;;) {
        int byte = q.read();
        if (byte == kEndOfQueue) {
            if (bytes_needed != 0) {
                bytes_needed = 0;
                if (fatal) {
                    return false;
                }
                append_replacement(out);
                continue;
            }
            return true;
        }

        uint8_t b = static_cast<uint8_t>(byte);
        if (bytes_needed == 0) {
            if (b <= 0x7Fu) {
                out.push_back(static_cast<char>(b));
            } else if (b >= 0xC2u && b <= 0xDFu) {
                bytes_needed = 1;
                code_point = b & 0x1Fu;
            } else if (b >= 0xE0u && b <= 0xEFu) {
                if (b == 0xE0u) {
                    lower_boundary = 0xA0u;
                } else if (b == 0xEDu) {
                    upper_boundary = 0x9Fu;
                }
                bytes_needed = 2;
                code_point = b & 0x0Fu;
            } else if (b >= 0xF0u && b <= 0xF4u) {
                if (b == 0xF0u) {
                    lower_boundary = 0x90u;
                } else if (b == 0xF4u) {
                    upper_boundary = 0x8Fu;
                }
                bytes_needed = 3;
                code_point = b & 0x07u;
            } else {
                if (fatal) {
                    return false;
                }
                append_replacement(out);
            }
            continue;
        }

        if (b < lower_boundary || b > upper_boundary) {
            code_point = 0;
            bytes_needed = 0;
            bytes_seen = 0;
            lower_boundary = 0x80u;
            upper_boundary = 0xBFu;
            q.restore1(b);
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        lower_boundary = 0x80u;
        upper_boundary = 0xBFu;
        code_point = (code_point << 6) | (b & 0x3Fu);
        ++bytes_seen;
        if (bytes_seen != bytes_needed) {
            continue;
        }
        uint32_t cp = code_point;
        code_point = 0;
        bytes_needed = 0;
        bytes_seen = 0;
        append_utf8(out, cp);
    }
}

bool decode_single_byte(std::string_view bytes, const uint16_t* table, std::string& out, bool fatal) {
    for (unsigned char b : bytes) {
        if (b < 0x80u) {
            out.push_back(static_cast<char>(b));
            continue;
        }
        uint16_t cp = table[b - 0x80u];
        if (cp == 0) {
            if (fatal) {
                return false;
            }
            append_replacement(out);
        } else {
            append_utf8(out, cp);
        }
    }
    return true;
}

bool decode_gb18030(std::string_view bytes, std::string& out, bool fatal) {
    ByteQueue q(bytes);
    uint8_t first = 0;
    uint8_t second = 0;
    uint8_t third = 0;

    for (;;) {
        int byte = q.read();
        if (byte == kEndOfQueue) {
            if (first == 0 && second == 0 && third == 0) {
                return true;
            }
            first = second = third = 0;
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        uint8_t b = static_cast<uint8_t>(byte);
        if (third != 0) {
            if (b < 0x30u || b > 0x39u) {
                q.restore3(second, third, b);
                first = second = third = 0;
                if (fatal) {
                    return false;
                }
                append_replacement(out);
                continue;
            }
            uint32_t pointer = (static_cast<uint32_t>(first - 0x81u) * (10u * 126u * 10u)) +
                               (static_cast<uint32_t>(second - 0x30u) * (10u * 126u)) +
                               (static_cast<uint32_t>(third - 0x81u) * 10u) +
                               static_cast<uint32_t>(b - 0x30u);
            first = second = third = 0;
            uint32_t cp = lookup_gb18030_ranges_code_point(pointer);
            if (cp == 0) {
                if (fatal) {
                    return false;
                }
                append_replacement(out);
            } else {
                append_utf8(out, cp);
            }
            continue;
        }

        if (second != 0) {
            if (b >= 0x81u && b <= 0xFEu) {
                third = b;
                continue;
            }
            q.restore2(second, b);
            first = second = 0;
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        if (first != 0) {
            if (b >= 0x30u && b <= 0x39u) {
                second = b;
                continue;
            }
            uint8_t leading = first;
            first = 0;
            uint16_t cp = 0;
            if ((b >= 0x40u && b <= 0x7Eu) || (b >= 0x80u && b <= 0xFEu)) {
                uint8_t offset = (b < 0x7Fu) ? 0x40u : 0x41u;
                uint32_t pointer = static_cast<uint32_t>(leading - 0x81u) * 190u + (b - offset);
                if (pointer < 23940u) {
                    cp = encoding_tables::kIndexGb18030[pointer];
                }
            }
            if (cp != 0) {
                append_utf8(out, cp);
                continue;
            }
            if (b <= 0x7Fu) {
                q.restore1(b);
            }
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        if (b <= 0x7Fu) {
            out.push_back(static_cast<char>(b));
        } else if (b == 0x80u) {
            append_utf8(out, 0x20ACu);
        } else if (b >= 0x81u && b <= 0xFEu) {
            first = b;
        } else {
            if (fatal) {
                return false;
            }
            append_replacement(out);
        }
    }
}

bool decode_big5(std::string_view bytes, std::string& out, bool fatal) {
    ByteQueue q(bytes);
    uint8_t leading = 0;

    for (;;) {
        int byte = q.read();
        if (byte == kEndOfQueue) {
            if (leading != 0) {
                leading = 0;
                if (fatal) {
                    return false;
                }
                append_replacement(out);
                continue;
            }
            return true;
        }

        uint8_t b = static_cast<uint8_t>(byte);
        if (leading != 0) {
            uint8_t lead = leading;
            leading = 0;
            if ((b >= 0x40u && b <= 0x7Eu) || (b >= 0xA1u && b <= 0xFEu)) {
                uint8_t offset = (b < 0x7Fu) ? 0x40u : 0x62u;
                uint32_t pointer = static_cast<uint32_t>(lead - 0x81u) * 157u + (b - offset);
                if (pointer == 1133u) {
                    append_utf8(out, 0x00CAu);
                    append_utf8(out, 0x0304u);
                    continue;
                }
                if (pointer == 1135u) {
                    append_utf8(out, 0x00CAu);
                    append_utf8(out, 0x030Cu);
                    continue;
                }
                if (pointer == 1164u) {
                    append_utf8(out, 0x00EAu);
                    append_utf8(out, 0x0304u);
                    continue;
                }
                if (pointer == 1166u) {
                    append_utf8(out, 0x00EAu);
                    append_utf8(out, 0x030Cu);
                    continue;
                }
                if (pointer < 19782u) {
                    uint32_t cp = encoding_tables::kIndexBig5[pointer];
                    if (cp != 0) {
                        append_utf8(out, cp);
                        continue;
                    }
                }
            }
            if (b <= 0x7Fu) {
                q.restore1(b);
            }
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        if (b <= 0x7Fu) {
            out.push_back(static_cast<char>(b));
        } else if (b >= 0x81u && b <= 0xFEu) {
            leading = b;
        } else {
            if (fatal) {
                return false;
            }
            append_replacement(out);
        }
    }
}

bool decode_euc_jp(std::string_view bytes, std::string& out, bool fatal) {
    ByteQueue q(bytes);
    bool jis0212 = false;
    uint8_t leading = 0;

    for (;;) {
        int byte = q.read();
        if (byte == kEndOfQueue) {
            if (leading != 0) {
                leading = 0;
                jis0212 = false;
                if (fatal) {
                    return false;
                }
                append_replacement(out);
                continue;
            }
            return true;
        }

        uint8_t b = static_cast<uint8_t>(byte);
        if (leading == 0x8Eu && b >= 0xA1u && b <= 0xDFu) {
            leading = 0;
            append_utf8(out, 0xFF61u - 0xA1u + b);
            continue;
        }
        if (leading == 0x8Fu && b >= 0xA1u && b <= 0xFEu) {
            jis0212 = true;
            leading = b;
            continue;
        }
        if (leading != 0) {
            uint8_t lead = leading;
            leading = 0;
            uint16_t cp = 0;
            if (lead >= 0xA1u && lead <= 0xFEu && b >= 0xA1u && b <= 0xFEu) {
                uint32_t pointer = static_cast<uint32_t>(lead - 0xA1u) * 94u + (b - 0xA1u);
                if (!jis0212) {
                    if (pointer < 11280u) {
                        cp = encoding_tables::kIndexJis0208[pointer];
                    }
                } else {
                    if (pointer < 8836u) {
                        cp = encoding_tables::kIndexJis0212[pointer];
                    }
                }
            }
            jis0212 = false;
            if (cp != 0) {
                append_utf8(out, cp);
                continue;
            }
            if (b <= 0x7Fu) {
                q.restore1(b);
            }
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        if (b <= 0x7Fu) {
            out.push_back(static_cast<char>(b));
        } else if (b == 0x8Eu || b == 0x8Fu || (b >= 0xA1u && b <= 0xFEu)) {
            leading = b;
        } else {
            if (fatal) {
                return false;
            }
            append_replacement(out);
        }
    }
}

// ISO-2022-JP is the one decoder here whose state outlives its input: an
// escape sequence decides which of four modes the bytes that follow are read
// in, and a `TextDecoder` with `{stream: true}` (§11.1.1) feeds it in chunks
// that can end between two of them. `mode_in` is the mode the previous chunk
// left it in (0 ASCII, 1 Roman, 2 Katakana, 3 double-byte), `mode_out` the one
// this chunk leaves. A one-shot call passes 0 for both.
bool decode_iso2022_jp(std::string_view bytes, std::string& out, bool fatal,
                       int mode_in = 0, int* mode_out = nullptr) {
    enum class State : uint8_t {
        Ascii,
        Roman,
        Katakana,
        LeadingByte,
        TrailingByte,
        EscapeStart,
        Escape,
    };
    auto mode_of = [](int mode) {
        switch (mode) {
        case 1: return State::Roman;
        case 2: return State::Katakana;
        case 3: return State::LeadingByte;
        default: return State::Ascii;
        }
    };

    ByteQueue q(bytes);
    State state = mode_of(mode_in);
    State output_state = state;
    if (mode_out) *mode_out = mode_in;
    uint8_t leading = 0;
    bool output = false;

    auto emit_error = [&]() -> bool {
        if (fatal) {
            return false;
        }
        append_replacement(out);
        return true;
    };

    for (;;) {
        int byte = q.read();
        switch (state) {
        case State::Ascii:
            if (byte == 0x1B) {
                state = State::EscapeStart;
            } else if (byte >= 0x00 && byte <= 0x7F && byte != 0x0E && byte != 0x0F && byte != 0x1B) {
                output = false;
                out.push_back(static_cast<char>(byte));
            } else if (byte == kEndOfQueue) {
                if (mode_out) *mode_out = 0;
                return true;
            } else {
                output = false;
                if (!emit_error()) return false;
            }
            break;

        case State::Roman:
            if (byte == 0x1B) {
                state = State::EscapeStart;
            } else if (byte == 0x5C) {
                output = false;
                append_utf8(out, 0x00A5u);
            } else if (byte == 0x7E) {
                output = false;
                append_utf8(out, 0x203Eu);
            } else if (byte >= 0x00 && byte <= 0x7F && byte != 0x0E && byte != 0x0F &&
                       byte != 0x1B && byte != 0x5C && byte != 0x7E) {
                output = false;
                out.push_back(static_cast<char>(byte));
            } else if (byte == kEndOfQueue) {
                if (mode_out) {
                    *mode_out = state == State::Roman ? 1 : state == State::Katakana ? 2
                             : state == State::LeadingByte ? 3 : 0;
                }
                return true;
            } else {
                output = false;
                if (!emit_error()) return false;
            }
            break;

        case State::Katakana:
            if (byte == 0x1B) {
                state = State::EscapeStart;
            } else if (byte >= 0x21 && byte <= 0x5F) {
                output = false;
                append_utf8(out, static_cast<uint32_t>(0xFF61 - 0x21 + byte));
            } else if (byte == kEndOfQueue) {
                if (mode_out) {
                    *mode_out = state == State::Roman ? 1 : state == State::Katakana ? 2
                             : state == State::LeadingByte ? 3 : 0;
                }
                return true;
            } else {
                output = false;
                if (!emit_error()) return false;
            }
            break;

        case State::LeadingByte:
            if (byte == 0x1B) {
                state = State::EscapeStart;
            } else if (byte >= 0x21 && byte <= 0x7E) {
                output = false;
                leading = static_cast<uint8_t>(byte);
                state = State::TrailingByte;
            } else if (byte == kEndOfQueue) {
                if (mode_out) {
                    *mode_out = state == State::Roman ? 1 : state == State::Katakana ? 2
                             : state == State::LeadingByte ? 3 : 0;
                }
                return true;
            } else {
                output = false;
                if (!emit_error()) return false;
            }
            break;

        case State::TrailingByte:
            if (byte == 0x1B) {
                state = State::EscapeStart;
                if (!emit_error()) return false;
            } else if (byte >= 0x21 && byte <= 0x7E) {
                state = State::LeadingByte;
                uint32_t pointer = static_cast<uint32_t>(leading - 0x21u) * 94u +
                                   static_cast<uint32_t>(byte - 0x21);
                uint16_t cp = (pointer < 11280u) ? encoding_tables::kIndexJis0208[pointer] : 0;
                if (cp == 0) {
                    if (!emit_error()) return false;
                } else {
                    append_utf8(out, cp);
                }
            } else {
                state = State::LeadingByte;
                if (!emit_error()) return false;
            }
            break;

        case State::EscapeStart:
            if (byte == 0x24 || byte == 0x28) {
                leading = static_cast<uint8_t>(byte);
                state = State::Escape;
            } else {
                if (byte != kEndOfQueue) {
                    q.restore1(byte);
                }
                output = false;
                state = output_state;
                if (!emit_error()) return false;
            }
            break;

        case State::Escape: {
            uint8_t lead = leading;
            leading = 0;
            bool matched = false;
            State next_state = State::Ascii;
            if (lead == 0x28u && byte == 0x42) {
                matched = true;
                next_state = State::Ascii;
            } else if (lead == 0x28u && byte == 0x4A) {
                matched = true;
                next_state = State::Roman;
            } else if (lead == 0x28u && byte == 0x49) {
                matched = true;
                next_state = State::Katakana;
            } else if (lead == 0x24u && (byte == 0x40 || byte == 0x42)) {
                matched = true;
                next_state = State::LeadingByte;
            }
            if (matched) {
                state = next_state;
                output_state = next_state;
                bool prev_output = output;
                output = true;
                if (prev_output) {
                    if (!emit_error()) return false;
                }
            } else {
                if (byte == kEndOfQueue) {
                    q.restore1(lead);
                } else {
                    q.restore2(lead, byte);
                }
                output = false;
                state = output_state;
                if (!emit_error()) return false;
            }
            break;
        }
        }
    }
}

bool decode_shift_jis(std::string_view bytes, std::string& out, bool fatal) {
    ByteQueue q(bytes);
    uint8_t leading = 0;

    for (;;) {
        int byte = q.read();
        if (byte == kEndOfQueue) {
            if (leading != 0) {
                leading = 0;
                if (fatal) {
                    return false;
                }
                append_replacement(out);
                continue;
            }
            return true;
        }

        uint8_t b = static_cast<uint8_t>(byte);
        if (leading != 0) {
            uint8_t lead = leading;
            leading = 0;
            if ((b >= 0x40u && b <= 0x7Eu) || (b >= 0x80u && b <= 0xFCu)) {
                uint8_t offset = (b < 0x7Fu) ? 0x40u : 0x41u;
                uint8_t lead_offset = (lead < 0xA0u) ? 0x81u : 0xC1u;
                uint32_t pointer = static_cast<uint32_t>(lead - lead_offset) * 188u + (b - offset);
                if (pointer >= 8836u && pointer <= 10715u) {
                    append_utf8(out, 0xE000u - 8836u + pointer);
                    continue;
                }
                if (pointer < 11280u) {
                    uint16_t cp = encoding_tables::kIndexJis0208[pointer];
                    if (cp != 0) {
                        append_utf8(out, cp);
                        continue;
                    }
                }
            }
            if (b <= 0x7Fu) {
                q.restore1(b);
            }
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        if (b <= 0x80u) {
            append_utf8(out, b);
        } else if (b >= 0xA1u && b <= 0xDFu) {
            append_utf8(out, 0xFF61u - 0xA1u + b);
        } else if ((b >= 0x81u && b <= 0x9Fu) || (b >= 0xE0u && b <= 0xFCu)) {
            leading = b;
        } else {
            if (fatal) {
                return false;
            }
            append_replacement(out);
        }
    }
}

bool decode_euc_kr(std::string_view bytes, std::string& out, bool fatal) {
    ByteQueue q(bytes);
    uint8_t leading = 0;

    for (;;) {
        int byte = q.read();
        if (byte == kEndOfQueue) {
            if (leading != 0) {
                leading = 0;
                if (fatal) {
                    return false;
                }
                append_replacement(out);
                continue;
            }
            return true;
        }

        uint8_t b = static_cast<uint8_t>(byte);
        if (leading != 0) {
            uint8_t lead = leading;
            leading = 0;
            if (b >= 0x41u && b <= 0xFEu) {
                uint32_t pointer = static_cast<uint32_t>(lead - 0x81u) * 190u + (b - 0x41u);
                if (pointer < 23940u) {
                    uint16_t cp = encoding_tables::kIndexEucKr[pointer];
                    if (cp != 0) {
                        append_utf8(out, cp);
                        continue;
                    }
                }
            }
            if (b <= 0x7Fu) {
                q.restore1(b);
            }
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        if (b <= 0x7Fu) {
            out.push_back(static_cast<char>(b));
        } else if (b >= 0x81u && b <= 0xFEu) {
            leading = b;
        } else {
            if (fatal) {
                return false;
            }
            append_replacement(out);
        }
    }
}

bool decode_utf16(std::string_view bytes, bool big_endian, std::string& out, bool fatal) {
    ByteQueue q(bytes);
    int lead_byte = -1;
    int lead_surrogate = -1;

    for (;;) {
        int byte = q.read();
        if (byte == kEndOfQueue) {
            if (lead_byte != -1 || lead_surrogate != -1) {
                lead_byte = -1;
                lead_surrogate = -1;
                if (fatal) {
                    return false;
                }
                append_replacement(out);
                continue;
            }
            return true;
        }

        if (lead_byte == -1) {
            lead_byte = byte;
            continue;
        }

        uint16_t code_unit = big_endian
            ? static_cast<uint16_t>((lead_byte << 8) | byte)
            : static_cast<uint16_t>((byte << 8) | lead_byte);
        lead_byte = -1;

        if (lead_surrogate != -1) {
            uint16_t hi = static_cast<uint16_t>(lead_surrogate);
            lead_surrogate = -1;
            if (code_unit >= 0xDC00u && code_unit <= 0xDFFFu) {
                uint32_t cp = 0x10000u + ((static_cast<uint32_t>(hi - 0xD800u) << 10) |
                                          static_cast<uint32_t>(code_unit - 0xDC00u));
                append_utf8(out, cp);
                continue;
            }
            uint8_t b1 = static_cast<uint8_t>(code_unit >> 8);
            uint8_t b2 = static_cast<uint8_t>(code_unit & 0xFFu);
            if (big_endian) {
                q.restore2(b1, b2);
            } else {
                q.restore2(b2, b1);
            }
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }

        if (code_unit >= 0xD800u && code_unit <= 0xDBFFu) {
            lead_surrogate = code_unit;
            continue;
        }
        if (code_unit >= 0xDC00u && code_unit <= 0xDFFFu) {
            if (fatal) {
                return false;
            }
            append_replacement(out);
            continue;
        }
        append_utf8(out, code_unit);
    }
}

// --- The encoder half, WHATWG Encoding Standard §5 ---------------------------
//
// Every index table above is code-point-valued and indexed by *pointer*, and
// the standard's decoders all turn bytes into a pointer and then read that
// table. The encoders do the same walk backwards: "index pointer" is the
// first position in the table that holds the code point. Doing it by search
// rather than by a reverse map is what the generated tables already support,
// and it keeps the two halves provably the same table.

// index ISO-2022-JP katakana (https://encoding.spec.whatwg.org/#index-iso-2022-jp-katakana):
// halfwidth katakana to the fullwidth form the encoder writes, which is NFKC
// except that U+FF9E/U+FF9F are U+309B/U+309C rather than the combining marks.
constexpr uint16_t kIso2022JpKatakana[63] = {
    0x3002u, 0x300Cu, 0x300Du, 0x3001u, 0x30FBu, 0x30F2u, 0x30A1u,
    0x30A3u, 0x30A5u, 0x30A7u, 0x30A9u, 0x30E3u, 0x30E5u, 0x30E7u,
    0x30C3u, 0x30FCu, 0x30A2u, 0x30A4u, 0x30A6u, 0x30A8u, 0x30AAu,
    0x30ABu, 0x30ADu, 0x30AFu, 0x30B1u, 0x30B3u, 0x30B5u, 0x30B7u,
    0x30B9u, 0x30BBu, 0x30BDu, 0x30BFu, 0x30C1u, 0x30C4u, 0x30C6u,
    0x30C8u, 0x30CAu, 0x30CBu, 0x30CCu, 0x30CDu, 0x30CEu, 0x30CFu,
    0x30D2u, 0x30D5u, 0x30D8u, 0x30DBu, 0x30DEu, 0x30DFu, 0x30E0u,
    0x30E1u, 0x30E2u, 0x30E4u, 0x30E6u, 0x30E8u, 0x30E9u, 0x30EAu,
    0x30EBu, 0x30ECu, 0x30EDu, 0x30EFu, 0x30F3u, 0x309Bu, 0x309Cu,
};

// The GB18030-2005 code points the gb18030 encoder sends down a fixed pair of
// bytes instead of through index gb18030, so that a decoder reading them back
// still sees what it used to. Asymmetric on purpose: index gb18030 holds the
// older pointers for these, and only the encoder is redirected.
struct Gb18030SideEntry {
    uint32_t code_point;
    uint8_t first;
    uint8_t second;
};

constexpr Gb18030SideEntry kGb18030EncoderSideTable[] = {
    {0xE78Du, 0xA6u, 0xD9u}, {0xE78Eu, 0xA6u, 0xDAu}, {0xE78Fu, 0xA6u, 0xDBu},
    {0xE790u, 0xA6u, 0xDCu}, {0xE791u, 0xA6u, 0xDDu}, {0xE792u, 0xA6u, 0xDEu},
    {0xE793u, 0xA6u, 0xDFu}, {0xE794u, 0xA6u, 0xECu}, {0xE795u, 0xA6u, 0xEDu},
    {0xE796u, 0xA6u, 0xF3u}, {0xE81Eu, 0xFEu, 0x59u}, {0xE826u, 0xFEu, 0x61u},
    {0xE82Bu, 0xFEu, 0x66u}, {0xE82Cu, 0xFEu, 0x67u}, {0xE832u, 0xFEu, 0x6Du},
    {0xE843u, 0xFEu, 0x7Eu}, {0xE854u, 0xFEu, 0x90u}, {0xE864u, 0xFEu, 0xA0u},
};

// "index pointer": the first position holding `code_point`, or `not_found`.
// The tables hold 0 for a hole, so 0 is never a code point here.
constexpr uint32_t kNoPointer = 0xFFFFFFFFu;

uint32_t index_pointer(const uint16_t* table, std::size_t length, uint32_t code_point) {
    for (std::size_t i = 0; i < length; ++i) {
        if (table[i] == code_point) {
            return static_cast<uint32_t>(i);
        }
    }
    return kNoPointer;
}

uint32_t index_pointer(const uint32_t* table, std::size_t length, uint32_t code_point,
                       std::size_t skip_below = 0) {
    for (std::size_t i = skip_below; i < length; ++i) {
        if (table[i] == code_point) {
            return static_cast<uint32_t>(i);
        }
    }
    return kNoPointer;
}

// index gb18030 ranges pointer: the last range whose code point is <=
// `code_point`, plus the distance between them.
uint32_t gb18030_ranges_pointer(uint32_t code_point) {
    if (code_point == 0xE7C7u) {
        return 7457u;
    }
    const auto* begin = encoding_tables::kIndexGb18030Ranges;
    const auto* end = begin + 207;
    const auto* it = std::upper_bound(
        begin, end, code_point,
        [](uint32_t cp, const encoding_tables::Gb18030Range& r) { return cp < r.code_point; });
    if (it == begin) {
        return kNoPointer;
    }
    --it;
    return it->pointer + code_point - it->code_point;
}

// index Shift_JIS pointer: index jis0208 without pointers 8272..8835, so that
// the duplicate code points in that run are encoded through a later pointer.
uint32_t shift_jis_pointer(uint32_t code_point) {
    for (std::size_t i = 0; i < 11280u; ++i) {
        if (i >= 8272u && i <= 8835u) {
            continue;
        }
        if (encoding_tables::kIndexJis0208[i] == code_point) {
            return static_cast<uint32_t>(i);
        }
    }
    return kNoPointer;
}

// index Big5 pointer: index Big5 without the Hong Kong Supplementary Character
// Set pointers below (0xA1 - 0x81) x 157, and six code points that are encoded
// through their *last* pointer rather than their first.
bool big5_pointer_uses_last(uint32_t code_point) {
    switch (code_point) {
    case 0x2550u:
    case 0x255Eu:
    case 0x2561u:
    case 0x256Au:
    case 0x5341u:
    case 0x5345u:
        return true;
    default:
        return false;
    }
}

uint32_t big5_pointer(uint32_t code_point) {
    constexpr std::size_t kFirstPointer = (0xA1u - 0x81u) * 157u;
    uint32_t found = kNoPointer;
    for (std::size_t i = kFirstPointer; i < 19782u; ++i) {
        if (encoding_tables::kIndexBig5[i] == code_point) {
            found = static_cast<uint32_t>(i);
            if (!big5_pointer_uses_last(code_point)) {
                return found;
            }
        }
    }
    return found;
}

void push_byte(std::string& out, uint32_t byte) {
    out.push_back(static_cast<char>(byte & 0xFFu));
}

std::string encode_utf8_of(uint32_t code_point) {
    std::string one;
    append_utf8(one, code_point);
    return one;
}

// The next code point of `utf8_text`, or kNoPointer at the end. A malformed
// sequence (which cannot come from a script string, but can from a caller
// that hands in bytes) yields U+FFFD and consumes one byte, which is the same
// answer the decoder gives the same bytes.
uint32_t next_code_point(std::string_view utf8_text, std::size_t& pos) {
    if (pos >= utf8_text.size()) {
        return kNoPointer;
    }
    const auto b0 = static_cast<unsigned char>(utf8_text[pos]);
    std::size_t need = 0;
    uint32_t cp = 0;
    if (b0 <= 0x7Fu) {
        ++pos;
        return b0;
    } else if ((b0 & 0xE0u) == 0xC0u) {
        need = 1;
        cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0u) == 0xE0u) {
        need = 2;
        cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8u) == 0xF0u) {
        need = 3;
        cp = b0 & 0x07u;
    } else {
        ++pos;
        return 0xFFFDu;
    }
    if (pos + need >= utf8_text.size()) {
        ++pos;
        return 0xFFFDu;
    }
    for (std::size_t i = 1; i <= need; ++i) {
        const auto b = static_cast<unsigned char>(utf8_text[pos + i]);
        if ((b & 0xC0u) != 0x80u) {
            ++pos;
            return 0xFFFDu;
        }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    pos += need + 1;
    return cp;
}

// ISO-2022-JP's encoder state. It is the one encoder whose output depends on
// what came before, because switching back is a sequence of its own.
enum class Iso2022JpState : uint8_t { Ascii, Roman, Jis0208 };

bool encode_iso2022_jp(std::string_view utf8_text, std::string& out) {
    Iso2022JpState state = Iso2022JpState::Ascii;
    std::size_t pos = 0;
    // "restore codePoint to ioQueue" in the algorithm is this: the switch is
    // written, the state changes, and the same code point is encoded again.
    for (;;) {
        uint32_t code_point = next_code_point(utf8_text, pos);
        if (code_point == kNoPointer) {
            if (state != Iso2022JpState::Ascii) {
                push_byte(out, 0x1B);
                push_byte(out, 0x28);
                push_byte(out, 0x42);
            }
            return true;
        }
        for (;;) {
            if ((state == Iso2022JpState::Ascii || state == Iso2022JpState::Roman) &&
                (code_point == 0x000Eu || code_point == 0x000Fu || code_point == 0x001Bu)) {
                return false;
            }
            if (state == Iso2022JpState::Ascii && code_point <= 0x7Fu) {
                push_byte(out, code_point);
                break;
            }
            if (state == Iso2022JpState::Roman && code_point <= 0x7Fu && code_point != 0x5Cu &&
                code_point != 0x7Eu) {
                push_byte(out, code_point);
                break;
            }
            if (state == Iso2022JpState::Roman && (code_point == 0x00A5u || code_point == 0x203Eu)) {
                push_byte(out, code_point == 0x00A5u ? 0x5Cu : 0x7Eu);
                break;
            }
            if (code_point <= 0x7Fu) {
                // An ASCII code point in the jis0208 state: back to ASCII and
                // come round again.
                push_byte(out, 0x1B);
                push_byte(out, 0x28);
                push_byte(out, 0x42);
                state = Iso2022JpState::Ascii;
                continue;
            }
            if (code_point == 0x00A5u || code_point == 0x203Eu) {
                push_byte(out, 0x1B);
                push_byte(out, 0x28);
                push_byte(out, 0x4A);
                state = Iso2022JpState::Roman;
                continue;
            }
            if (code_point == 0x2212u) {
                code_point = 0xFF0Du;
            }
            if (code_point >= 0xFF61u && code_point <= 0xFF9Fu) {
                code_point = kIso2022JpKatakana[code_point - 0xFF61u];
            }
            const uint32_t pointer =
                index_pointer(encoding_tables::kIndexJis0208, 11280u, code_point);
            if (pointer == kNoPointer) {
                if (state == Iso2022JpState::Jis0208) {
                    push_byte(out, 0x1B);
                    push_byte(out, 0x28);
                    push_byte(out, 0x42);
                    state = Iso2022JpState::Ascii;
                    continue;
                }
                return false;
            }
            if (state != Iso2022JpState::Jis0208) {
                push_byte(out, 0x1B);
                push_byte(out, 0x24);
                push_byte(out, 0x42);
                state = Iso2022JpState::Jis0208;
                continue;
            }
            push_byte(out, pointer / 94u + 0x21u);
            push_byte(out, pointer % 94u + 0x21u);
            break;
        }
    }
}

bool encode_gb18030(std::string_view utf8_text, std::string& out) {
    std::size_t pos = 0;
    for (;;) {
        uint32_t code_point = next_code_point(utf8_text, pos);
        if (code_point == kNoPointer) {
            return true;
        }
        if (code_point <= 0x7Fu) {
            push_byte(out, code_point);
            continue;
        }
        if (code_point == 0xE5E5u) {
            return false;
        }
        bool side_entry = false;
        for (const auto& entry : kGb18030EncoderSideTable) {
            if (entry.code_point == code_point) {
                push_byte(out, entry.first);
                push_byte(out, entry.second);
                side_entry = true;
                break;
            }
        }
        if (side_entry) {
            continue;
        }
        const uint32_t pointer =
            index_pointer(encoding_tables::kIndexGb18030, 23940u, code_point);
        if (pointer != kNoPointer) {
            const uint32_t leading = pointer / 190u + 0x81u;
            const uint32_t trailing = pointer % 190u;
            const uint32_t offset = trailing < 0x3Fu ? 0x40u : 0x41u;
            push_byte(out, leading);
            push_byte(out, trailing + offset);
            continue;
        }
        const uint32_t ranges = gb18030_ranges_pointer(code_point);
        if (ranges == kNoPointer) {
            return false;
        }
        uint32_t rest = ranges;
        const uint32_t byte1 = rest / (10u * 126u * 10u);
        rest %= (10u * 126u * 10u);
        const uint32_t byte2 = rest / (10u * 126u);
        rest %= (10u * 126u);
        const uint32_t byte3 = rest / 10u;
        const uint32_t byte4 = rest % 10u;
        push_byte(out, byte1 + 0x81u);
        push_byte(out, byte2 + 0x30u);
        push_byte(out, byte3 + 0x81u);
        push_byte(out, byte4 + 0x30u);
    }
}

bool encode_big5(std::string_view utf8_text, std::string& out) {
    std::size_t pos = 0;
    for (;;) {
        uint32_t code_point = next_code_point(utf8_text, pos);
        if (code_point == kNoPointer) {
            return true;
        }
        if (code_point <= 0x7Fu) {
            push_byte(out, code_point);
            continue;
        }
        const uint32_t pointer = big5_pointer(code_point);
        if (pointer == kNoPointer) {
            return false;
        }
        const uint32_t trailing = pointer % 157u;
        const uint32_t offset = trailing < 0x3Fu ? 0x40u : 0x62u;
        push_byte(out, pointer / 157u + 0x81u);
        push_byte(out, trailing + offset);
    }
}

bool encode_shift_jis(std::string_view utf8_text, std::string& out) {
    std::size_t pos = 0;
    for (;;) {
        uint32_t code_point = next_code_point(utf8_text, pos);
        if (code_point == kNoPointer) {
            return true;
        }
        if (code_point <= 0x7Fu || code_point == 0x80u) {
            push_byte(out, code_point);
            continue;
        }
        if (code_point == 0x00A5u) {
            push_byte(out, 0x5Cu);
            continue;
        }
        if (code_point == 0x203Eu) {
            push_byte(out, 0x7Eu);
            continue;
        }
        if (code_point >= 0xFF61u && code_point <= 0xFF9Fu) {
            push_byte(out, code_point - 0xFF61u + 0xA1u);
            continue;
        }
        if (code_point == 0x2212u) {
            code_point = 0xFF0Du;
        }
        const uint32_t pointer = shift_jis_pointer(code_point);
        if (pointer == kNoPointer) {
            return false;
        }
        const uint32_t leading = pointer / 188u;
        const uint32_t leading_offset = leading < 0x1Fu ? 0x81u : 0xC1u;
        const uint32_t trailing = pointer % 188u;
        const uint32_t offset = trailing < 0x3Fu ? 0x40u : 0x41u;
        push_byte(out, leading + leading_offset);
        push_byte(out, trailing + offset);
    }
}

bool encode_euc_jp(std::string_view utf8_text, std::string& out) {
    std::size_t pos = 0;
    for (;;) {
        uint32_t code_point = next_code_point(utf8_text, pos);
        if (code_point == kNoPointer) {
            return true;
        }
        if (code_point <= 0x7Fu) {
            push_byte(out, code_point);
            continue;
        }
        if (code_point == 0x00A5u) {
            push_byte(out, 0x5Cu);
            continue;
        }
        if (code_point == 0x203Eu) {
            push_byte(out, 0x7Eu);
            continue;
        }
        if (code_point >= 0xFF61u && code_point <= 0xFF9Fu) {
            push_byte(out, 0x8Eu);
            push_byte(out, code_point - 0xFF61u + 0xA1u);
            continue;
        }
        if (code_point == 0x2212u) {
            code_point = 0xFF0Du;
        }
        const uint32_t pointer =
            index_pointer(encoding_tables::kIndexJis0208, 11280u, code_point);
        if (pointer == kNoPointer) {
            return false;
        }
        push_byte(out, pointer / 94u + 0xA1u);
        push_byte(out, pointer % 94u + 0xA1u);
    }
}

bool encode_euc_kr(std::string_view utf8_text, std::string& out) {
    std::size_t pos = 0;
    for (;;) {
        uint32_t code_point = next_code_point(utf8_text, pos);
        if (code_point == kNoPointer) {
            return true;
        }
        if (code_point <= 0x7Fu) {
            push_byte(out, code_point);
            continue;
        }
        const uint32_t pointer =
            index_pointer(encoding_tables::kIndexEucKr, 23940u, code_point);
        if (pointer == kNoPointer) {
            return false;
        }
        push_byte(out, pointer / 190u + 0x81u);
        push_byte(out, pointer % 190u + 0x41u);
    }
}

bool encode_single_byte(EncodingId encoding, std::string_view utf8_text, std::string& out) {
    const uint16_t* table = single_byte_table(encoding);
    std::size_t pos = 0;
    for (;;) {
        uint32_t code_point = next_code_point(utf8_text, pos);
        if (code_point == kNoPointer) {
            return true;
        }
        if (code_point <= 0x7Fu) {
            push_byte(out, code_point);
            continue;
        }
        uint16_t match = 0;
        for (std::size_t i = 0; i < 128u; ++i) {
            if (table[i] == code_point) {
                match = static_cast<uint16_t>(i + 0x80u);
                break;
            }
        }
        if (match == 0) {
            return false;
        }
        push_byte(out, match);
    }
}

} // namespace

bool get_encoding_from_label(std::string_view label, EncodingId& out) {
    while (!label.empty() && is_ascii_whitespace(label.front())) {
        label.remove_prefix(1);
    }
    while (!label.empty() && is_ascii_whitespace(label.back())) {
        label.remove_suffix(1);
    }
    if (label.empty() || label.size() > 64) {
        return false;
    }

    char lowered[65];
    for (std::size_t i = 0; i < label.size(); ++i) {
        lowered[i] = ascii_lower(label[i]);
    }
    lowered[label.size()] = '\0';
    std::string_view key(lowered, label.size());

    const auto* begin = encoding_tables::kLabels;
    const auto* end = begin + 228;
    const auto* it = std::lower_bound(
        begin, end, key,
        [](const encoding_tables::LabelEntry& entry, std::string_view k) {
            return std::string_view(entry.label) < k;
        });
    if (it != end && std::string_view(it->label) == key) {
        out = it->id;
        return true;
    }
    return false;
}

bool encode_code_point(EncodingId encoding, uint32_t code_point, std::string& out) {
    switch (encoding) {
    case EncodingId::Utf8:
        if (code_point > 0x10FFFFu || (code_point >= 0xD800u && code_point <= 0xDFFFu)) {
            return false;
        }
        append_utf8(out, code_point);
        return true;

    case EncodingId::Gbk: {
        // GBK is gb18030 without the four-byte form: the index it shares, and
        // nothing past it.
        if (code_point <= 0x7Fu) {
            push_byte(out, code_point);
            return true;
        }
        if (code_point == 0x20ACu) {
            push_byte(out, 0x80u);
            return true;
        }
        if (code_point == 0xE5E5u) {
            return false;
        }
        for (const auto& entry : kGb18030EncoderSideTable) {
            if (entry.code_point == code_point) {
                push_byte(out, entry.first);
                push_byte(out, entry.second);
                return true;
            }
        }
        const uint32_t pointer =
            index_pointer(encoding_tables::kIndexGb18030, 23940u, code_point);
        if (pointer == kNoPointer) {
            return false;
        }
        const uint32_t trailing = pointer % 190u;
        const uint32_t offset = trailing < 0x3Fu ? 0x40u : 0x41u;
        push_byte(out, pointer / 190u + 0x81u);
        push_byte(out, trailing + offset);
        return true;
    }

    case EncodingId::Big5:
    case EncodingId::EucJp:
    case EncodingId::ShiftJis:
    case EncodingId::EucKr:
    case EncodingId::Gb18030: {
        // These have a canonical name but no per-code-point entry point worth
        // duplicating: the whole-string encoders below carry the run.
        const std::string one = encode_utf8_of(code_point);
        return encode_with_encoding(encoding, one, out);
    }

    case EncodingId::Iso2022Jp: {
        // Stateful, so one code point is one call to the whole-string encoder,
        // which switches back to ASCII at the end. Two calls are not the
        // concatenation of one, and that is the standard's own warning.
        const std::string one = encode_utf8_of(code_point);
        return encode_with_encoding(EncodingId::Iso2022Jp, one, out);
    }

    case EncodingId::Replacement:
        // §14.1: replacement has a decoder and no encoder at all.
        return false;

    case EncodingId::Utf16BE:
    case EncodingId::Utf16LE:
        // §6 "get an encoder" asserts the caller never asks: neither has one.
        return false;

    case EncodingId::XUserDefined:
        if (code_point <= 0x7Fu) {
            push_byte(out, code_point);
            return true;
        }
        if (code_point >= 0xF780u && code_point <= 0xF7FFu) {
            push_byte(out, code_point - 0xF780u + 0x80u);
            return true;
        }
        return false;

    default:
        break;
    }

    if (code_point <= 0x7Fu) {
        push_byte(out, code_point);
        return true;
    }
    const uint16_t* table = single_byte_table(encoding);
    for (std::size_t i = 0; i < 128u; ++i) {
        if (table[i] == code_point) {
            push_byte(out, i + 0x80u);
            return true;
        }
    }
    return false;
}

bool encode_with_encoding(EncodingId encoding, std::string_view utf8_text, std::string& out) {
    switch (encoding) {
    case EncodingId::Utf8: {
        std::size_t pos = 0;
        for (;;) {
            const uint32_t code_point = next_code_point(utf8_text, pos);
            if (code_point == kNoPointer) {
                return true;
            }
            if (code_point > 0x10FFFFu || (code_point >= 0xD800u && code_point <= 0xDFFFu)) {
                return false;
            }
            append_utf8(out, code_point);
        }
    }

    case EncodingId::Gbk:
    case EncodingId::Gb18030: {
        const std::size_t before = out.size();
        if (encoding == EncodingId::Gb18030) {
            return encode_gb18030(utf8_text, out);
        }
        // Run the GBK rules code point by code point off the gb18030 walk, by
        // refusing anything that would need the four-byte form.
        std::size_t pos = 0;
        for (;;) {
            uint32_t code_point = next_code_point(utf8_text, pos);
            if (code_point == kNoPointer) {
                return true;
            }
            if (code_point <= 0x7Fu) {
                push_byte(out, code_point);
                continue;
            }
            if (code_point == 0x20ACu) {
                push_byte(out, 0x80u);
                continue;
            }
            if (code_point == 0xE5E5u) {
                return false;
            }
            bool side_entry = false;
            for (const auto& entry : kGb18030EncoderSideTable) {
                if (entry.code_point == code_point) {
                    push_byte(out, entry.first);
                    push_byte(out, entry.second);
                    side_entry = true;
                    break;
                }
            }
            if (side_entry) {
                continue;
            }
            const uint32_t pointer =
                index_pointer(encoding_tables::kIndexGb18030, 23940u, code_point);
            if (pointer == kNoPointer) {
                return false;
            }
            const uint32_t trailing = pointer % 190u;
            const uint32_t offset = trailing < 0x3Fu ? 0x40u : 0x41u;
            push_byte(out, pointer / 190u + 0x81u);
            push_byte(out, trailing + offset);
        }
        (void)before;
    }

    case EncodingId::Big5:
        return encode_big5(utf8_text, out);

    case EncodingId::EucJp:
        return encode_euc_jp(utf8_text, out);

    case EncodingId::Iso2022Jp:
        return encode_iso2022_jp(utf8_text, out);

    case EncodingId::ShiftJis:
        return encode_shift_jis(utf8_text, out);

    case EncodingId::EucKr:
        return encode_euc_kr(utf8_text, out);

    case EncodingId::Replacement:
        return false;

    case EncodingId::Utf16BE:
    case EncodingId::Utf16LE:
        return false;

    case EncodingId::XUserDefined: {
        std::size_t pos = 0;
        for (;;) {
            const uint32_t code_point = next_code_point(utf8_text, pos);
            if (code_point == kNoPointer) {
                return true;
            }
            if (code_point <= 0x7Fu) {
                push_byte(out, code_point);
            } else if (code_point >= 0xF780u && code_point <= 0xF7FFu) {
                push_byte(out, code_point - 0xF780u + 0x80u);
            } else {
                return false;
            }
        }
    }

    default:
        break;
    }
    return encode_single_byte(encoding, utf8_text, out);
}

std::string_view canonical_encoding_name(EncodingId id) {
    auto idx = static_cast<std::size_t>(id);
    if (idx < 40) {
        return encoding_tables::kCanonicalNames[idx];
    }
    return "UTF-8";
}

bool sniff_bom_encoding(std::string_view bytes, EncodingId& out, std::size_t& bom_length) {
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEFu &&
        static_cast<unsigned char>(bytes[1]) == 0xBBu &&
        static_cast<unsigned char>(bytes[2]) == 0xBFu) {
        out = EncodingId::Utf8;
        bom_length = 3;
        return true;
    }
    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFEu &&
        static_cast<unsigned char>(bytes[1]) == 0xFFu) {
        out = EncodingId::Utf16BE;
        bom_length = 2;
        return true;
    }
    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xFFu &&
        static_cast<unsigned char>(bytes[1]) == 0xFEu) {
        out = EncodingId::Utf16LE;
        bom_length = 2;
        return true;
    }
    bom_length = 0;
    return false;
}

bool decode_stream_chunk(std::string_view bytes, EncodingId encoding, std::string& out_utf8,
                         bool fatal, int mode_in, int* mode_out) {
    if (encoding != EncodingId::Iso2022Jp) return decode_with_encoding(bytes, encoding, out_utf8, fatal);
    return decode_iso2022_jp(bytes, out_utf8, fatal, mode_in, mode_out);
}

bool decode_with_encoding(std::string_view bytes, EncodingId encoding, std::string& out_utf8, bool fatal) {
    out_utf8.clear();
    out_utf8.reserve(bytes.size());

    switch (encoding) {
    case EncodingId::Utf8:
        return decode_utf8(bytes, out_utf8, fatal);

    case EncodingId::Ibm866:
    case EncodingId::Iso8859_2:
    case EncodingId::Iso8859_3:
    case EncodingId::Iso8859_4:
    case EncodingId::Iso8859_5:
    case EncodingId::Iso8859_6:
    case EncodingId::Iso8859_7:
    case EncodingId::Iso8859_8:
    case EncodingId::Iso8859_8_I:
    case EncodingId::Iso8859_10:
    case EncodingId::Iso8859_13:
    case EncodingId::Iso8859_14:
    case EncodingId::Iso8859_15:
    case EncodingId::Iso8859_16:
    case EncodingId::Koi8R:
    case EncodingId::Koi8U:
    case EncodingId::Macintosh:
    case EncodingId::Windows874:
    case EncodingId::Windows1250:
    case EncodingId::Windows1251:
    case EncodingId::Windows1252:
    case EncodingId::Windows1253:
    case EncodingId::Windows1254:
    case EncodingId::Windows1255:
    case EncodingId::Windows1256:
    case EncodingId::Windows1257:
    case EncodingId::Windows1258:
    case EncodingId::XMacCyrillic:
        return decode_single_byte(bytes, single_byte_table(encoding), out_utf8, fatal);

    case EncodingId::Gbk:
    case EncodingId::Gb18030:
        return decode_gb18030(bytes, out_utf8, fatal);

    case EncodingId::Big5:
        return decode_big5(bytes, out_utf8, fatal);

    case EncodingId::EucJp:
        return decode_euc_jp(bytes, out_utf8, fatal);

    case EncodingId::Iso2022Jp:
        return decode_iso2022_jp(bytes, out_utf8, fatal);

    case EncodingId::ShiftJis:
        return decode_shift_jis(bytes, out_utf8, fatal);

    case EncodingId::EucKr:
        return decode_euc_kr(bytes, out_utf8, fatal);

    case EncodingId::Replacement:
        if (bytes.empty()) {
            return true;
        }
        if (fatal) {
            return false;
        }
        append_replacement(out_utf8);
        return true;

    case EncodingId::Utf16BE:
        return decode_utf16(bytes, true, out_utf8, fatal);

    case EncodingId::Utf16LE:
        return decode_utf16(bytes, false, out_utf8, fatal);

    case EncodingId::XUserDefined:
        for (unsigned char b : bytes) {
            if (b <= 0x7Fu) {
                out_utf8.push_back(static_cast<char>(b));
            } else {
                append_utf8(out_utf8, 0xF780u + b - 0x80u);
            }
        }
        return true;
    }
    return false;
}

std::string whatwg_decode(std::string_view bytes, EncodingId fallback_encoding, EncodingId* actual_encoding) {
    EncodingId chosen = fallback_encoding;
    std::size_t bom_len = 0;
    if (sniff_bom_encoding(bytes, chosen, bom_len)) {
        bytes.remove_prefix(bom_len);
    }
    if (actual_encoding) {
        *actual_encoding = chosen;
    }
    std::string out;
    decode_with_encoding(bytes, chosen, out, false);
    return out;
}

} // namespace litebird
