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

bool decode_iso2022_jp(std::string_view bytes, std::string& out, bool fatal) {
    enum class State : uint8_t {
        Ascii,
        Roman,
        Katakana,
        LeadingByte,
        TrailingByte,
        EscapeStart,
        Escape,
    };

    ByteQueue q(bytes);
    State state = State::Ascii;
    State output_state = State::Ascii;
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
