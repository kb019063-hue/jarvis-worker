#include "litebird/js/JsString.hpp"

#include "litebird/js/JsOps.hpp"

#include <cstring>
#include <mutex>
#include <new>

namespace litebird {

namespace {

/// Work counters. Per thread, so counting costs an increment and no more.
thread_local JsString::Stats t_stats;

/// Serialises the (rare) building of UTF-16 forms. Readers never take it.
std::mutex g_wide_mutex;

bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

/// Length of the well-formed sequence that starts at `p`, or 0 when the bytes
/// there are not one. Well-formed means shortest form, nothing above U+10FFFF;
/// surrogates (U+D800..U+DFFF, three bytes) are let through, see JsString.
size_t sequence_length(const unsigned char* p, const unsigned char* end) {
    const unsigned char c = *p;
    if (c < 0x80) return 1;
    if (c < 0xC2) return 0;                             // a continuation byte, or an overlong lead
    const size_t left = static_cast<size_t>(end - p);
    if (c < 0xE0) return (left >= 2 && is_continuation(p[1])) ? 2 : 0;
    if (c < 0xF0) {
        if (left < 3 || !is_continuation(p[1]) || !is_continuation(p[2])) return 0;
        if (c == 0xE0 && p[1] < 0xA0) return 0;         // overlong
        return 3;
    }
    if (c < 0xF5) {
        if (left < 4 || !is_continuation(p[1]) || !is_continuation(p[2]) || !is_continuation(p[3])) return 0;
        if (c == 0xF0 && p[1] < 0x90) return 0;         // overlong
        if (c == 0xF4 && p[1] > 0x8F) return 0;         // above U+10FFFF
        return 4;
    }
    return 0;
}

// A surrogate takes three bytes: ED A0..AF xx is a high (leading) half,
// ED B0..BF xx a low (trailing) half.
bool is_high_half(const unsigned char* p) { return p[0] == 0xED && p[1] >= 0xA0 && p[1] <= 0xAF; }
bool is_low_half(const unsigned char* p) { return p[0] == 0xED && p[1] >= 0xB0 && p[1] <= 0xBF; }

uint32_t decode3(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0] & 0x0F) << 12) | (static_cast<uint32_t>(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
}

void encode(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Scan {
    size_t units{0};
    bool well_formed{true};
};

/// Counts UTF-16 code units and checks the text in one pass. Stops at the
/// first problem: the caller repairs the text and scans again.
Scan scan(std::string_view text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char* const end = p + text.size();
    Scan result;
    while (p < end) {
        // Most text is ASCII: take it eight bytes at a time.
        while (end - p >= 8) {
            uint64_t word;
            std::memcpy(&word, p, 8);
            if (word & 0x8080808080808080ull) break;
            p += 8;
            result.units += 8;
        }
        if (p >= end) break;
        if (*p < 0x80) {
            ++p;
            ++result.units;
            continue;
        }
        const size_t length = sequence_length(p, end);
        if (length == 0) {
            result.well_formed = false;
            return result;
        }
        if (length == 3 && is_high_half(p) && end - p >= 6 && sequence_length(p + 3, end) == 3 && is_low_half(p + 3)) {
            result.well_formed = false;                 // a pair written as two halves
            return result;
        }
        result.units += length == 4 ? 2 : 1;
        p += length;
    }
    return result;
}

/// The same text, well-formed: every byte that is not part of a sequence
/// becomes U+FFFD, and two surrogate halves in a row become the character
/// they spell.
std::string repair(std::string_view text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char* const end = p + text.size();
    std::string out;
    out.reserve(text.size() + 8);
    while (p < end) {
        const size_t length = sequence_length(p, end);
        if (length == 0) {
            out += "\xEF\xBF\xBD";
            ++p;
            continue;
        }
        if (length == 3 && is_high_half(p) && end - p >= 6 && sequence_length(p + 3, end) == 3 && is_low_half(p + 3)) {
            const uint32_t high = decode3(p);
            const uint32_t low = decode3(p + 3);
            encode(out, 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00));
            p += 6;
            continue;
        }
        out.append(reinterpret_cast<const char*>(p), length);
        p += length;
    }
    return out;
}

/// True when `first` ends in a high half and `second` starts with a low half:
/// joined, they are one character and must be stored as one.
bool halves_meet(std::string_view first, std::string_view second) {
    if (first.size() < 3 || second.size() < 3) return false;
    return is_high_half(reinterpret_cast<const unsigned char*>(first.data() + first.size() - 3)) &&
           is_low_half(reinterpret_cast<const unsigned char*>(second.data()));
}

/// Capacities are rounded up to what the allocator hands out anyway.
size_t round_capacity(size_t bytes) { return (bytes + 15) & ~size_t{15}; }

JsString* ascii_units() {
    // Never destroyed: values created during static destruction may still
    // want one of these.
    static JsString* const table = [] {
        auto* strings = new JsString[128];
        return strings;
    }();
    return table;
}

} // namespace

JsString::Buffer* JsString::allocate(size_t capacity) {
    if (capacity > kMaxBytes) js_throw_range_error("Invalid string length");
    capacity = round_capacity(capacity);
    void* memory = ::operator new(sizeof(Buffer) + capacity);
    auto* buffer = new (memory) Buffer;
    buffer->capacity = static_cast<uint32_t>(capacity);
    ++t_stats.blocks;
    return buffer;
}

void JsString::destroy(Buffer* buffer) noexcept {
    if (Wide* wide = buffer->wide.load(std::memory_order_acquire)) {
        wide->~Wide();
        ::operator delete(wide);
    }
    buffer->~Buffer();
    ::operator delete(buffer);
}

JsString JsString::join(std::string_view first, std::string_view second, size_t units, size_t capacity) {
    const size_t bytes = first.size() + second.size();
    JsString result;
    if (bytes == 0) return result;
    Buffer* buffer = allocate(capacity < bytes ? bytes : capacity);
    if (!first.empty()) std::memcpy(buffer->bytes(), first.data(), first.size());
    if (!second.empty()) std::memcpy(buffer->bytes() + first.size(), second.data(), second.size());
    buffer->used.store(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
    t_stats.bytes_copied += bytes;
    result.m_buffer = buffer;
    result.m_bytes = static_cast<uint32_t>(bytes);
    result.m_units = static_cast<uint32_t>(units);
    return result;
}

JsString JsString::from_checked(std::string_view utf8, size_t units) {
    if (utf8.size() == 1) return from_unit(static_cast<unsigned char>(utf8[0]));
    return join(utf8, std::string_view(), units, utf8.size());
}

JsString::JsString(std::string_view utf8) {
    if (utf8.empty()) return;
    const Scan scanned = scan(utf8);
    if (scanned.well_formed) {
        *this = from_checked(utf8, scanned.units);
        return;
    }
    const std::string repaired = repair(utf8);
    *this = from_checked(repaired, scan(repaired).units);
}

JsString JsString::from_unit(char16_t unit) {
    if (unit < 0x80) {
        static std::once_flag once;
        JsString* table = ascii_units();
        std::call_once(once, [table] {
            for (int c = 0; c < 128; ++c) {
                const char byte = static_cast<char>(c);
                table[c] = join(std::string_view(&byte, 1), std::string_view(), 1, 1);
                // Shared by every script on every thread: leave no room, so
                // nothing is ever appended into one of these blocks.
                table[c].m_buffer->capacity = 1;
            }
        });
        return table[unit];
    }
    return from_utf16(std::u16string_view(&unit, 1));
}

JsString JsString::from_utf16(std::u16string_view units) {
    const size_t count = units.size();
    if (count == 0) return JsString();
    if (count == 1 && units[0] < 0x80) return from_unit(units[0]);

    auto pair_at = [&](size_t i) {
        return units[i] >= 0xD800 && units[i] <= 0xDBFF && i + 1 < count && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF;
    };
    // Measure, then encode straight into the block. A surrogate pair becomes
    // one four-byte character; a half on its own is kept, in three bytes.
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        const char16_t unit = units[i];
        if (unit < 0x80) bytes += 1;
        else if (unit < 0x800) bytes += 2;
        else if (pair_at(i)) { bytes += 4; ++i; }
        else bytes += 3;
    }
    Buffer* buffer = allocate(bytes);
    char* out = buffer->bytes();
    for (size_t i = 0; i < count; ++i) {
        uint32_t cp = units[i];
        if (cp < 0x80) {
            *out++ = static_cast<char>(cp);
        } else if (cp < 0x800) {
            *out++ = static_cast<char>(0xC0 | (cp >> 6));
            *out++ = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (pair_at(i)) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            ++i;
            *out++ = static_cast<char>(0xF0 | (cp >> 18));
            *out++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out++ = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            *out++ = static_cast<char>(0xE0 | (cp >> 12));
            *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out++ = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    buffer->used.store(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
    t_stats.bytes_copied += bytes;
    JsString result;
    result.m_buffer = buffer;
    result.m_bytes = static_cast<uint32_t>(bytes);
    result.m_units = static_cast<uint32_t>(count);
    return result;
}

std::string JsString::str() const {
    t_stats.bytes_copied += m_bytes;
    return std::string(view());
}

const char16_t* JsString::widen() const {
    std::lock_guard<std::mutex> lock(g_wide_mutex);
    Buffer* const buffer = m_buffer;
    const uint32_t end_byte = m_offset + m_bytes;
    const uint32_t end_unit = m_unit_offset + m_units;
    Wide* wide = buffer->wide.load(std::memory_order_relaxed);
    if (!wide) {
        // Room for the block's text up to the end of this string, and for
        // whatever else the block holds or can still be given: a byte of text
        // is never more than one code unit. The array is therefore never
        // moved, and pointers into it stay good.
        const size_t capacity = size_t{end_unit} + (buffer->capacity - end_byte);
        const size_t marks = capacity / kMarkEvery + 1;
        const size_t data_bytes = sizeof(char16_t) * ((capacity + 1) & ~size_t{1});
        void* memory = ::operator new(sizeof(Wide) + data_bytes + sizeof(uint32_t) * marks);
        wide = new (memory) Wide;
        wide->capacity = static_cast<uint32_t>(capacity);
    }
    uint32_t unit = wide->units.load(std::memory_order_relaxed);
    if (unit < end_unit) {
        // Convert only what was added since the last time. Every handle ends
        // between two characters, so this starts and stops between characters.
        const uint32_t first = unit;
        char16_t* const out = wide->data();
        uint32_t* const marks = wide->marks();
        const auto* const base = reinterpret_cast<const unsigned char*>(buffer->bytes());
        uint32_t at = wide->bytes;
        while (at < end_byte) {
            const unsigned char c = base[at];
            if (unit % kMarkEvery == 0) marks[unit / kMarkEvery] = at * 2;
            if (c < 0x80) {
                out[unit++] = c;
                at += 1;
            } else if (c < 0xE0) {
                out[unit++] = static_cast<char16_t>(((c & 0x1F) << 6) | (base[at + 1] & 0x3F));
                at += 2;
            } else if (c < 0xF0) {
                out[unit++] = static_cast<char16_t>(decode3(base + at));
                at += 3;
            } else {
                uint32_t cp = (static_cast<uint32_t>(c & 0x07) << 18) | (static_cast<uint32_t>(base[at + 1] & 0x3F) << 12) |
                              (static_cast<uint32_t>(base[at + 2] & 0x3F) << 6) | (base[at + 3] & 0x3F);
                cp -= 0x10000;
                if ((unit + 1) % kMarkEvery == 0) marks[(unit + 1) / kMarkEvery] = at * 2 + 1;
                out[unit++] = static_cast<char16_t>(0xD800 + (cp >> 10));
                out[unit++] = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
                at += 4;
            }
        }
        t_stats.units_widened += unit - first;
        wide->bytes = end_byte;
        wide->units.store(unit, std::memory_order_release);
    }
    buffer->wide.store(wide, std::memory_order_release);
    return wide->data() + m_unit_offset;
}

JsString JsString::piece(size_t byte_begin, size_t bytes, size_t unit_begin, size_t units) const noexcept {
    JsString result(*this);
    result.m_offset = static_cast<uint32_t>(m_offset + byte_begin);
    result.m_bytes = static_cast<uint32_t>(bytes);
    result.m_unit_offset = static_cast<uint32_t>(m_unit_offset + unit_begin);
    result.m_units = static_cast<uint32_t>(units);
    return result;
}

size_t JsString::byte_of_unit(size_t unit) const {
    if (is_ascii()) return unit;
    wide();     // makes sure the marks up to the end of this string are written
    const Wide* const cached = m_buffer->wide.load(std::memory_order_acquire);
    const size_t target = m_unit_offset + unit;             // counted from the start of the block
    const uint32_t mark = cached->marks()[target / kMarkEvery];
    size_t at = mark >> 1;
    size_t reached = target - target % kMarkEvery - (mark & 1);
    const auto* const base = reinterpret_cast<const unsigned char*>(m_buffer->bytes());
    while (reached < target) {
        const unsigned char c = base[at];
        if (c < 0x80) { at += 1; reached += 1; }
        else if (c < 0xE0) { at += 2; reached += 1; }
        else if (c < 0xF0) { at += 3; reached += 1; }
        else { at += 4; reached += 2; }
    }
    return reached == target ? at - m_offset : static_cast<size_t>(-1);
}

JsString JsString::substring(size_t begin, size_t end) const {
    if (end > m_units) end = m_units;
    if (begin >= end) return JsString();
    if (begin == 0 && end == m_units) return *this;
    const size_t count = end - begin;
    if (count == 1) {
        const char16_t unit = unit_at(begin);
        if (unit < 0x80) return from_unit(unit);
    }
    // A piece that is a good part of its block (a quarter of its bytes or
    // more) becomes another range of the same block. Smaller pieces are
    // copied, so that a few characters taken from a long text do not keep the
    // whole text alive.
    const size_t capacity = m_buffer->capacity;
    if (is_ascii()) {
        if (count * kShareDivisor >= capacity) return piece(begin, count, begin, count);
        // A piece of an ASCII string is ASCII: nothing to check or count.
        return join(view().substr(begin, count), std::string_view(), count, count);
    }
    // Text that is not ASCII: a code unit takes one to three bytes, so how
    // many bytes the piece has is only known by looking. A piece too small to
    // be shared even at three bytes a unit is settled without looking.
    if (count * 3 * kShareDivisor >= capacity) {
        // The piece has to start and end between characters. One that starts or
        // ends between the halves of a surrogate pair holds a half that is not
        // in the block as such, and is built from the code units instead.
        const size_t byte_begin = begin == 0 ? 0 : byte_of_unit(begin);
        const size_t byte_end = end == m_units ? m_bytes : byte_of_unit(end);
        if (byte_begin != static_cast<size_t>(-1) && byte_end != static_cast<size_t>(-1) &&
            (byte_end - byte_begin) * kShareDivisor >= capacity) {
            return piece(byte_begin, byte_end - byte_begin, begin, count);
        }
    }
    return from_utf16(std::u16string_view(wide() + begin, count));
}

JsString JsString::append(const JsString& a, std::string_view more, size_t more_units) {
    Buffer* const buffer = a.m_buffer;
    const size_t total = size_t{a.m_bytes} + more.size();
    if (total > kMaxBytes) js_throw_range_error("Invalid string length");

    // Is there room after `a`, and does `a` end where the block's text ends?
    // Then claim the room and write there. The claim is a compare-and-swap, so
    // two appends to the same text cannot both win; the loser copies, below.
    const uint32_t a_end = a.m_offset + a.m_bytes;
    if (more.size() <= buffer->capacity - a_end) {
        uint32_t expected = a_end;
        if (buffer->used.compare_exchange_strong(expected, static_cast<uint32_t>(a_end + more.size()),
                                                 std::memory_order_acq_rel, std::memory_order_relaxed)) {
            std::memcpy(buffer->bytes() + a_end, more.data(), more.size());
            t_stats.bytes_copied += more.size();
            JsString result(a);
            result.m_bytes = static_cast<uint32_t>(total);
            result.m_units = static_cast<uint32_t>(a.m_units + more_units);
            return result;
        }
    }

    // A new block. Text that keeps being extended (`s += x` in a loop) is given
    // room to grow by half, which is what makes n appends cost n and not n*n.
    // The first moves get no spare room: most strings are joined once or
    // twice and then only read, and those should not carry slack around.
    const bool newest = buffer->used.load(std::memory_order_relaxed) == a_end;
    const uint32_t moves = newest ? (buffer->moves < 1000 ? buffer->moves + 1 : buffer->moves) : 1;
    size_t capacity = total;
    if (moves >= 3) {
        capacity = total + total / 2;
        if (capacity > kMaxBytes) capacity = kMaxBytes;
    }
    JsString result = join(a.view(), more, a.m_units + more_units, capacity);
    result.m_buffer->moves = moves;
    return result;
}

namespace {

/// `first` ends in a high half and `second` starts with the low half: the two
/// become one character, so the text is rebuilt (rare, and never in place).
JsString merge_halves(std::string_view first, std::string_view second) {
    std::string joined;
    joined.reserve(first.size() + second.size());
    joined += first;
    joined += second;
    return JsString(joined);
}

} // namespace

JsString JsString::concat(const JsString& a, const JsString& b) {
    if (b.empty()) return a;
    if (a.empty()) return b;
    if (halves_meet(a.view(), b.view())) return merge_halves(a.view(), b.view());
    return append(a, b.view(), b.m_units);
}

JsString JsString::concat(const JsString& a, std::string_view b) {
    if (b.empty()) return a;
    const Scan scanned = scan(b);
    if (!scanned.well_formed) return concat(a, JsString(b));
    if (a.empty()) return from_checked(b, scanned.units);
    if (halves_meet(a.view(), b)) return merge_halves(a.view(), b);
    return append(a, b, scanned.units);
}

JsString JsString::concat(std::string_view a, const JsString& b) {
    if (a.empty()) return b;
    const Scan scanned = scan(a);
    if (!scanned.well_formed) return concat(JsString(a), b);
    if (b.empty()) return from_checked(a, scanned.units);
    if (halves_meet(a, b.view())) return merge_halves(a, b.view());
    const size_t total = a.size() + b.size();
    if (total > kMaxBytes) js_throw_range_error("Invalid string length");
    return join(a, b.view(), scanned.units + b.m_units, total);
}

int JsString::compare(std::string_view a, std::string_view b) noexcept {
    const size_t common = a.size() < b.size() ? a.size() : b.size();
    size_t i = 0;
    while (i < common && a[i] == b[i]) ++i;
    if (i == common) return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);

    const auto x = static_cast<unsigned char>(a[i]);
    const auto y = static_cast<unsigned char>(b[i]);
    // Lead bytes F0..F4 start a character outside the BMP. Unless exactly one
    // side has one, and the other side is at U+D000 or above (lead bytes ED,
    // EE, EF), byte order and code unit order agree.
    if ((x >= 0xF0) == (y >= 0xF0) || x < 0xED || y < 0xED) return x < y ? -1 : 1;

    const bool a_is_pair = x >= 0xF0;
    const std::string_view pair_side = a_is_pair ? a : b;
    const std::string_view other_side = a_is_pair ? b : a;
    const auto* pair = reinterpret_cast<const unsigned char*>(pair_side.data()) + i;
    const auto* other = reinterpret_cast<const unsigned char*>(other_side.data()) + i;
    int pair_order = -1;        // U+E000..U+FFFF is above every surrogate
    if (*other == 0xED) {
        // U+D000..U+DFFF, which includes a surrogate half standing alone.
        const uint32_t cp = ((static_cast<uint32_t>(pair[0] & 0x07) << 18) | (static_cast<uint32_t>(pair[1] & 0x3F) << 12) |
                             (static_cast<uint32_t>(pair[2] & 0x3F) << 6) | (pair[3] & 0x3F)) - 0x10000;
        const uint32_t high = 0xD800 + (cp >> 10);
        const uint32_t low = 0xDC00 + (cp & 0x3FF);
        const uint32_t unit = decode3(other);
        if (high != unit) {
            pair_order = high < unit ? -1 : 1;
        } else if (other_side.size() - i <= 3) {
            pair_order = 1;     // the other text ends with that same half: it is the shorter one
        } else {
            // The same high half, alone. What follows it cannot be a low half
            // (the two would have been stored as one character), so it differs
            // from this pair's low half.
            const unsigned char* next = other + 3;
            uint32_t next_unit = *next;
            if (*next >= 0xF0) next_unit = 0xD800;                      // another pair: its high half
            else if (*next >= 0xE0) next_unit = decode3(next);
            else if (*next >= 0xC0) next_unit = ((*next & 0x1Fu) << 6) | (next[1] & 0x3Fu);
            pair_order = low < next_unit ? -1 : 1;
        }
    }
    return a_is_pair ? pair_order : -pair_order;
}

size_t JsString::find_lone_half(std::string_view utf8, size_t from) noexcept {
    // Two halves that meet are stored as one four-byte character, so a
    // three-byte sequence in the surrogate range (0xED, then 0xA0 or more) is
    // always a half on its own. 0xED is never a continuation byte.
    for (size_t at = utf8.find('\xED', from); at != std::string_view::npos; at = utf8.find('\xED', at + 1)) {
        if (at + 1 < utf8.size() && static_cast<unsigned char>(utf8[at + 1]) >= 0xA0) return at;
    }
    return std::string_view::npos;
}

JsString JsString::to_well_formed() const {
    if (is_ascii()) return *this;
    const std::string_view text = view();
    size_t at = find_lone_half(text);
    if (at == std::string_view::npos) return *this;
    std::string fixed(text);
    for (; at != std::string_view::npos; at = find_lone_half(text, at + 3)) {
        fixed.replace(at, 3, "\xEF\xBF\xBD");        // U+FFFD: three bytes and one code unit, like the half
    }
    return from_checked(fixed, m_units);
}

JsString::Stats JsString::stats() noexcept { return t_stats; }

// ---------------------------------------------------------------------------
// Utf16View
// ---------------------------------------------------------------------------

Utf16View::Utf16View(JsString string) : m_string(std::move(string)), m_size(m_string.length()) {
    if (m_size == 0) return;
    if (m_string.is_ascii()) m_narrow = m_string.view().data();
    else m_wide = m_string.wide();
}

std::u16string Utf16View::copy(size_t begin, size_t count) const {
    std::u16string out;
    append_to(out, begin, count);
    return out;
}

void Utf16View::append_to(std::u16string& out, size_t begin, size_t count) const {
    if (begin >= m_size) return;
    if (count > m_size - begin) count = m_size - begin;
    if (m_wide) {
        out.append(m_wide + begin, count);
        return;
    }
    const size_t old_size = out.size();
    out.resize(old_size + count);
    for (size_t i = 0; i < count; ++i) out[old_size + i] = static_cast<unsigned char>(m_narrow[begin + i]);
}

size_t Utf16View::find(char16_t unit, size_t from) const noexcept {
    if (from >= m_size) return npos;
    if (m_wide) {
        const char16_t* hit = std::char_traits<char16_t>::find(m_wide + from, m_size - from, unit);
        return hit ? static_cast<size_t>(hit - m_wide) : npos;
    }
    if (unit >= 0x80) return npos;
    const void* hit = std::memchr(m_narrow + from, static_cast<int>(unit), m_size - from);
    return hit ? static_cast<size_t>(static_cast<const char*>(hit) - m_narrow) : npos;
}

size_t Utf16View::find(const Utf16View& needle, size_t from) const {
    if (from > m_size) return npos;
    if (needle.m_size == 0) return from;
    if (needle.m_size > m_size - from) return npos;
    if (!m_wide) {
        if (needle.m_wide) return npos;         // ASCII text holds no unit above 0x7F
        return std::string_view(m_narrow, m_size).find(std::string_view(needle.m_narrow, needle.m_size), from);
    }
    if (needle.m_size == 1) return find(needle[0], from);
    if (needle.m_wide) {
        return std::u16string_view(m_wide, m_size).find(std::u16string_view(needle.m_wide, needle.m_size), from);
    }
    const std::u16string widened = needle.copy();
    return std::u16string_view(m_wide, m_size).find(widened, from);
}

size_t Utf16View::rfind(const Utf16View& needle, size_t from) const {
    if (needle.m_size > m_size) return npos;
    if (needle.m_size == 0) return from < m_size ? from : m_size;
    if (!m_wide) {
        if (needle.m_wide) return npos;
        return std::string_view(m_narrow, m_size).rfind(std::string_view(needle.m_narrow, needle.m_size), from);
    }
    if (needle.m_wide) {
        return std::u16string_view(m_wide, m_size).rfind(std::u16string_view(needle.m_wide, needle.m_size), from);
    }
    const std::u16string widened = needle.copy();
    return std::u16string_view(m_wide, m_size).rfind(widened, from);
}

bool Utf16View::matches_at(size_t at, const Utf16View& other) const noexcept {
    if (at > m_size || other.m_size > m_size - at) return false;
    if (!m_wide && !other.m_wide) {
        return other.m_size == 0 || std::memcmp(m_narrow + at, other.m_narrow, other.m_size) == 0;
    }
    for (size_t i = 0; i < other.m_size; ++i) {
        if ((*this)[at + i] != other[i]) return false;
    }
    return true;
}

} // namespace litebird
