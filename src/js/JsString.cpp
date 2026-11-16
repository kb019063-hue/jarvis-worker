#include "litebird/js/JsString.hpp"

#include "litebird/js/JsOps.hpp"

#include <cstring>
#include <mutex>
#include <new>

namespace litebird {

namespace {

/// Work counters. Per thread, so counting costs an increment and no more.
thread_local JsString::Stats t_stats;

/// Serialises lazy UTF-8 / wide cache construction. Steady-state readers never take it.
std::mutex g_cache_mutex;

bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

bool is_high_surrogate(char16_t u) noexcept { return u >= 0xD800 && u <= 0xDBFF; }
bool is_low_surrogate(char16_t u) noexcept { return u >= 0xDC00 && u <= 0xDFFF; }

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

struct Scan {
    size_t units{0};
    size_t repaired_utf8_bytes{0};
    bool well_formed{true};
    bool ascii{true};
};

/// Counts UTF-16 code units, exact repaired UTF-8 byte count, ASCII purity, and
/// UTF-8 well-formedness in a single pass.
Scan scan_utf8(std::string_view text) {
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
            result.repaired_utf8_bytes += 8;
        }
        if (p >= end) break;
        if (*p < 0x80) {
            ++p;
            ++result.units;
            ++result.repaired_utf8_bytes;
            continue;
        }
        result.ascii = false;
        const size_t length = sequence_length(p, end);
        if (length == 0) {
            result.well_formed = false;
            result.units += 1;
            result.repaired_utf8_bytes += 3;            // repaired as U+FFFD
            ++p;
            continue;
        }
        if (length == 3 && is_high_half(p) && end - p >= 6 && sequence_length(p + 3, end) == 3 && is_low_half(p + 3)) {
            result.well_formed = false;                 // a pair written as two halves becomes one 4-byte character
            result.units += 2;
            result.repaired_utf8_bytes += 4;
            p += 6;
            continue;
        }
        result.units += length == 4 ? 2 : 1;
        result.repaired_utf8_bytes += length;
        p += length;
    }
    return result;
}

/// Decodes UTF-8 (repairing malformed bytes to U+FFFD) straight into `out`,
/// which must have room for `scan_utf8(text).units` code units.
void decode_utf8_to_utf16(std::string_view text, char16_t* out) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char* const end = p + text.size();
    while (p < end) {
        const unsigned char c = *p;
        if (c < 0x80) {
            *out++ = c;
            ++p;
            continue;
        }
        const size_t length = sequence_length(p, end);
        if (length == 0) {
            *out++ = 0xFFFD;
            ++p;
        } else if (length == 2) {
            *out++ = static_cast<char16_t>(((c & 0x1F) << 6) | (p[1] & 0x3F));
            p += 2;
        } else if (length == 3) {
            *out++ = static_cast<char16_t>(decode3(p));
            p += 3;
        } else {
            uint32_t cp = (static_cast<uint32_t>(c & 0x07) << 18) | (static_cast<uint32_t>(p[1] & 0x3F) << 12) |
                          (static_cast<uint32_t>(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            cp -= 0x10000;
            *out++ = static_cast<char16_t>(0xD800 + (cp >> 10));
            *out++ = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
            p += 4;
        }
    }
}

void widen_bytes(const char* src, char16_t* dst, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) dst[i] = static_cast<unsigned char>(src[i]);
}

void narrow_units(const char16_t* src, char* dst, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) dst[i] = static_cast<char>(src[i]);
}

size_t count_utf8_bytes_raw(const char16_t* units, size_t count) noexcept {
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        const char16_t c = units[i];
        if (c < 0x80) {
            bytes += 1;
        } else if (c < 0x800) {
            bytes += 2;
        } else if (is_high_surrogate(c) && i + 1 < count && is_low_surrogate(units[i + 1])) {
            bytes += 4;
            ++i;
        } else {
            bytes += 3;
        }
    }
    return bytes;
}

uint64_t current_thread_tag() noexcept {
    static std::atomic<uint64_t> next_tag{1};
    thread_local const uint64_t tag = next_tag.fetch_add(1, std::memory_order_relaxed);
    return tag;
}

/// Capacities are rounded up to what the allocator hands out anyway (16-byte granularity).
size_t round_capacity_units(size_t capacity_units, bool is_utf16) noexcept {
    const size_t max_units = is_utf16 ? JsString::kMaxUnits : JsString::kMaxBytes;
    const size_t rounded = is_utf16 ? ((capacity_units + 7) & ~size_t{7}) : ((capacity_units + 15) & ~size_t{15});
    return rounded > max_units ? max_units : rounded;
}

JsString* ascii_units() {
    // Never destroyed: values created during static destruction may still
    // want one of these.
    static JsString* const table = [] { return new JsString[128]; }();
    return table;
}

uint32_t join_utf8_bytes(const JsString& a, const JsString& b, size_t a_bytes, size_t b_bytes) noexcept {
    if (a.empty()) return static_cast<uint32_t>(b_bytes);
    if (b.empty()) return static_cast<uint32_t>(a_bytes);
    const char16_t a_last = a.unit_at(a.length() - 1);
    const char16_t b_first = b.unit_at(0);
    const size_t merged = a_bytes + b_bytes - (is_high_surrogate(a_last) && is_low_surrogate(b_first) ? 2 : 0);
    return static_cast<uint32_t>(merged);
}

} // namespace

JsString::Buffer* JsString::allocate(size_t capacity_units, bool is_utf16) {
    const size_t max_units = is_utf16 ? kMaxUnits : kMaxBytes;
    if (capacity_units > max_units) js_throw_range_error("Invalid string length");
    capacity_units = round_capacity_units(capacity_units, is_utf16);
    const size_t elem_size = is_utf16 ? sizeof(char16_t) : sizeof(char);
    void* memory = ::operator new(sizeof(Buffer) + capacity_units * elem_size);
    auto* buffer = new (memory) Buffer;
    buffer->sole_thread.store(current_thread_tag(), std::memory_order_relaxed);
    buffer->capacity = static_cast<uint32_t>(capacity_units);
    buffer->is_utf16 = is_utf16;
    ++t_stats.blocks;
    return buffer;
}

void JsString::destroy(Buffer* buffer) noexcept {
    if (Utf8Cache* utf8 = buffer->utf8.load(std::memory_order_acquire)) {
        utf8->~Utf8Cache();
        ::operator delete(utf8);
    }
    if (Wide* wide = buffer->wide_cache.load(std::memory_order_acquire)) {
        wide->~Wide();
        ::operator delete(wide);
    }
    for (Buffer::Utf8Slice* node = buffer->utf8_slices.load(std::memory_order_acquire); node;) {
        Buffer::Utf8Slice* const next = node->next.load(std::memory_order_relaxed);
        node->~Utf8Slice();
        ::operator delete(node);
        node = next;
    }
    buffer->~Buffer();
    ::operator delete(buffer);
}

JsString JsString::hold(Buffer* buffer, uint32_t offset, uint32_t units, uint32_t utf8_bytes) noexcept {
    JsString result;
    result.m_buffer = buffer;
    result.m_offset = offset;
    result.m_units = units;
    result.m_utf8_bytes.store(utf8_bytes, std::memory_order_relaxed);
    result.retain();
    return result;
}

JsString JsString::join_ascii(std::string_view first, std::string_view second, size_t capacity) {
    const size_t bytes = first.size() + second.size();
    JsString result;
    if (bytes == 0) return result;
    Buffer* buffer = allocate(capacity < bytes ? bytes : capacity, false);
    if (!first.empty()) std::memcpy(buffer->bytes(), first.data(), first.size());
    if (!second.empty()) std::memcpy(buffer->bytes() + first.size(), second.data(), second.size());
    buffer->used.store(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
    t_stats.bytes_copied += bytes;
    result.m_buffer = buffer;
    result.m_units = static_cast<uint32_t>(bytes);
    result.m_utf8_bytes.store(static_cast<uint32_t>(bytes), std::memory_order_relaxed);
    return result;
}

JsString JsString::from_checked(std::string_view utf8, size_t units, bool ascii) {
    if (utf8.empty()) return JsString();
    if (ascii) {
        if (units == 1) return from_unit(static_cast<unsigned char>(utf8[0]));
        return join_ascii(utf8, std::string_view(), units);
    }
    Buffer* buffer = allocate(units, true);
    decode_utf8_to_utf16(utf8, buffer->utf16());
    buffer->used.store(static_cast<uint32_t>(units), std::memory_order_relaxed);
    t_stats.bytes_copied += units * sizeof(char16_t);
    t_stats.units_widened += units;
    JsString result;
    result.m_buffer = buffer;
    result.m_units = static_cast<uint32_t>(units);
    result.m_utf8_bytes.store(static_cast<uint32_t>(utf8.size()), std::memory_order_relaxed);
    return result;
}

JsString::JsString(std::string_view utf8) {
    if (utf8.empty()) return;
    const Scan scanned = scan_utf8(utf8);
    if (scanned.ascii) {
        *this = from_checked(utf8, scanned.units, true);
        return;
    }
    Buffer* buffer = allocate(scanned.units, true);
    decode_utf8_to_utf16(utf8, buffer->utf16());
    buffer->used.store(static_cast<uint32_t>(scanned.units), std::memory_order_relaxed);
    t_stats.bytes_copied += scanned.units * sizeof(char16_t);
    t_stats.units_widened += scanned.units;
    m_buffer = buffer;
    m_units = static_cast<uint32_t>(scanned.units);
    m_utf8_bytes.store(static_cast<uint32_t>(scanned.repaired_utf8_bytes), std::memory_order_relaxed);
}

JsString JsString::from_unit(char16_t unit) {
    if (unit < 0x80) {
        static std::once_flag once;
        JsString* table = ascii_units();
        std::call_once(once, [table] {
            for (int c = 0; c < 128; ++c) {
                const char byte = static_cast<char>(c);
                table[c] = join_ascii(std::string_view(&byte, 1), std::string_view(), 1);
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

    bool ascii = true;
    size_t utf8_bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        const char16_t unit = units[i];
        if (unit < 0x80) {
            utf8_bytes += 1;
        } else {
            ascii = false;
            if (unit < 0x800) {
                utf8_bytes += 2;
            } else if (is_high_surrogate(unit) && i + 1 < count && is_low_surrogate(units[i + 1])) {
                utf8_bytes += 4;
                ++i;
            } else {
                utf8_bytes += 3;
            }
        }
    }

    if (ascii) {
        Buffer* buffer = allocate(count, false);
        narrow_units(units.data(), buffer->bytes(), count);
        buffer->used.store(static_cast<uint32_t>(count), std::memory_order_relaxed);
        t_stats.bytes_copied += count;
        JsString result;
        result.m_buffer = buffer;
        result.m_units = static_cast<uint32_t>(count);
        result.m_utf8_bytes.store(static_cast<uint32_t>(count), std::memory_order_relaxed);
        return result;
    }

    Buffer* buffer = allocate(count, true);
    std::memcpy(buffer->utf16(), units.data(), count * sizeof(char16_t));
    buffer->used.store(static_cast<uint32_t>(count), std::memory_order_relaxed);
    t_stats.bytes_copied += count * sizeof(char16_t);
    JsString result;
    result.m_buffer = buffer;
    result.m_units = static_cast<uint32_t>(count);
    result.m_utf8_bytes.store(static_cast<uint32_t>(utf8_bytes), std::memory_order_relaxed);
    return result;
}

size_t JsString::size() const noexcept {
    if (!m_buffer || m_units == 0) return 0;
    if (!m_buffer->is_utf16) return m_units;
    const uint32_t known = m_utf8_bytes.load(std::memory_order_relaxed);
    if (known != kUnknownUtf8Bytes) return known;
    const uint32_t counted = static_cast<uint32_t>(count_utf8_bytes_raw(m_buffer->utf16() + m_offset, m_units));
    m_utf8_bytes.store(counted, std::memory_order_relaxed);
    return counted;
}

std::string JsString::str() const {
    const std::string_view text = view();
    t_stats.bytes_copied += text.size();
    return std::string(text);
}

namespace {

uint32_t byte_of_unit_in_utf8(const auto* cached, const char16_t* u, uint32_t target, size_t kMarkEvery) noexcept {
    if (target == 0) return 0;
    const uint32_t mark = cached->marks()[target / kMarkEvery];
    uint32_t at_unit = target - (target % static_cast<uint32_t>(kMarkEvery)) - (mark & 1u);
    uint32_t at_byte = mark >> 1;
    while (at_unit < target) {
        const char16_t c = u[at_unit];
        if (c < 0x80) {
            ++at_unit;
            at_byte += 1;
        } else if (c < 0x800) {
            ++at_unit;
            at_byte += 2;
        } else if (is_high_surrogate(c) && at_unit + 1 < target && is_low_surrogate(u[at_unit + 1])) {
            at_unit += 2;
            at_byte += 4;
        } else {
            ++at_unit;
            at_byte += 3;
        }
    }
    return at_byte;
}

} // namespace

std::string_view JsString::utf8_view() const noexcept {
    Buffer* const buffer = m_buffer;
    const char16_t* const u = buffer->utf16();
    const uint32_t end = m_offset + m_units;
    const bool split_left = m_offset > 0 && is_low_surrogate(u[m_offset]) && is_high_surrogate(u[m_offset - 1]);
    const bool ends_high = is_high_surrogate(u[end - 1]);

    if (!split_left && !ends_high) {
        if (const Utf8Cache* cached = buffer->utf8.load(std::memory_order_acquire)) {
            if (cached->units.load(std::memory_order_acquire) >= end) {
                const uint32_t byte_begin = byte_of_unit_in_utf8(cached, u, m_offset, kMarkEvery);
                const uint32_t byte_end = byte_of_unit_in_utf8(cached, u, end, kMarkEvery);
                return std::string_view(cached->data() + byte_begin, byte_end - byte_begin);
            }
        }
        return build_utf8_cache(end);
    }

    for (const Buffer::Utf8Slice* node = buffer->utf8_slices.load(std::memory_order_acquire); node;
         node = node->next.load(std::memory_order_acquire)) {
        if (node->offset == m_offset && node->units == m_units) {
            return std::string_view(node->data(), node->bytes);
        }
    }
    return make_utf8_slice();
}

std::string_view JsString::build_utf8_cache(uint32_t end) const noexcept {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    Buffer* const buffer = m_buffer;
    const char16_t* const u = buffer->utf16();
    Utf8Cache* cache = buffer->utf8.load(std::memory_order_relaxed);
    if (!cache) {
        // Only inspect u[0 .. end), which is guaranteed already written before
        // this handle was constructed; remaining capacity needs <= 3 bytes/unit.
        const size_t cap_bytes = count_utf8_bytes_raw(u, end) + 2 + size_t{buffer->capacity - end} * 3;
        const size_t marks = size_t{buffer->capacity} / kMarkEvery + 1;
        const size_t data_bytes = (cap_bytes + 3) & ~size_t{3};
        void* memory = ::operator new(sizeof(Utf8Cache) + data_bytes + sizeof(uint32_t) * marks);
        cache = new (memory) Utf8Cache;
        cache->capacity_units = buffer->capacity;
        cache->data_bytes = static_cast<uint32_t>(data_bytes);
    }
    uint32_t unit = cache->units.load(std::memory_order_relaxed);
    if (unit < end) {
        char* const out = cache->data();
        uint32_t* const marks = cache->marks();
        uint32_t byte = cache->bytes;
        const uint32_t prev_byte = byte;
        const uint32_t start_unit = unit;
        while (unit < end) {
            if (unit % kMarkEvery == 0 && (unit == 0 || unit > start_unit)) {
                marks[unit / kMarkEvery] = byte * 2;
            }
            const char16_t c = u[unit];
            if (c < 0x80) {
                out[byte++] = static_cast<char>(c);
                ++unit;
            } else if (c < 0x800) {
                out[byte++] = static_cast<char>(0xC0 | (c >> 6));
                out[byte++] = static_cast<char>(0x80 | (c & 0x3F));
                ++unit;
            } else if (is_high_surrogate(c) && unit + 1 < end && is_low_surrogate(u[unit + 1])) {
                if ((unit + 1) % kMarkEvery == 0) {
                    marks[(unit + 1) / kMarkEvery] = byte * 2 + 1;
                }
                const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(c) - 0xD800) << 10) +
                                    (static_cast<uint32_t>(u[unit + 1]) - 0xDC00);
                out[byte++] = static_cast<char>(0xF0 | (cp >> 18));
                out[byte++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out[byte++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out[byte++] = static_cast<char>(0x80 | (cp & 0x3F));
                unit += 2;
            } else {
                out[byte++] = static_cast<char>(0xE0 | (c >> 12));
                out[byte++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out[byte++] = static_cast<char>(0x80 | (c & 0x3F));
                ++unit;
            }
        }
        if (end % kMarkEvery == 0) marks[end / kMarkEvery] = byte * 2;
        t_stats.bytes_copied += byte - prev_byte;
        cache->bytes = byte;
        cache->units.store(unit, std::memory_order_release);
    }
    buffer->utf8.store(cache, std::memory_order_release);
    const uint32_t byte_begin = byte_of_unit_in_utf8(cache, u, m_offset, kMarkEvery);
    const uint32_t byte_end = byte_of_unit_in_utf8(cache, u, end, kMarkEvery);
    return std::string_view(cache->data() + byte_begin, byte_end - byte_begin);
}

std::string_view JsString::make_utf8_slice() const noexcept {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    Buffer* const buffer = m_buffer;
    for (const Buffer::Utf8Slice* node = buffer->utf8_slices.load(std::memory_order_relaxed); node;
         node = node->next.load(std::memory_order_relaxed)) {
        if (node->offset == m_offset && node->units == m_units) {
            return std::string_view(node->data(), node->bytes);
        }
    }
    const char16_t* const u = buffer->utf16() + m_offset;
    const size_t exact_bytes = count_utf8_bytes_raw(u, m_units);
    void* memory = ::operator new(sizeof(Buffer::Utf8Slice) + exact_bytes);
    auto* node = new (memory) Buffer::Utf8Slice;
    node->offset = m_offset;
    node->units = m_units;
    node->bytes = static_cast<uint32_t>(exact_bytes);
    char* out = node->data();
    for (size_t i = 0; i < m_units; ++i) {
        const char16_t c = u[i];
        if (c < 0x80) {
            *out++ = static_cast<char>(c);
        } else if (c < 0x800) {
            *out++ = static_cast<char>(0xC0 | (c >> 6));
            *out++ = static_cast<char>(0x80 | (c & 0x3F));
        } else if (is_high_surrogate(c) && i + 1 < m_units && is_low_surrogate(u[i + 1])) {
            const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(c) - 0xD800) << 10) +
                                (static_cast<uint32_t>(u[i + 1]) - 0xDC00);
            *out++ = static_cast<char>(0xF0 | (cp >> 18));
            *out++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out++ = static_cast<char>(0x80 | (cp & 0x3F));
            ++i;
        } else {
            *out++ = static_cast<char>(0xE0 | (c >> 12));
            *out++ = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            *out++ = static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    t_stats.bytes_copied += exact_bytes;
    m_utf8_bytes.store(static_cast<uint32_t>(exact_bytes), std::memory_order_relaxed);
    node->next.store(buffer->utf8_slices.load(std::memory_order_relaxed), std::memory_order_relaxed);
    buffer->utf8_slices.store(node, std::memory_order_release);
    return std::string_view(node->data(), node->bytes);
}

const char16_t* JsString::widen_ascii() const {
    const uint32_t end = m_offset + m_units;
    const Wide* cached = m_buffer->wide_cache.load(std::memory_order_acquire);
    if (cached && cached->units.load(std::memory_order_acquire) >= end) {
        return cached->data() + m_offset;
    }
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    Buffer* const buffer = m_buffer;
    Wide* wide = buffer->wide_cache.load(std::memory_order_relaxed);
    if (!wide) {
        void* memory = ::operator new(sizeof(Wide) + sizeof(char16_t) * buffer->capacity);
        wide = new (memory) Wide;
        wide->capacity = buffer->capacity;
    }
    uint32_t unit = wide->units.load(std::memory_order_relaxed);
    if (unit < end) {
        widen_bytes(buffer->bytes() + unit, wide->data() + unit, end - unit);
        t_stats.units_widened += end - unit;
        wide->units.store(end, std::memory_order_release);
    }
    buffer->wide_cache.store(wide, std::memory_order_release);
    return wide->data() + m_offset;
}

bool JsString::equals_units(const JsString& other) const noexcept {
    if (!m_buffer->is_utf16 && !other.m_buffer->is_utf16) {
        return std::memcmp(m_buffer->bytes() + m_offset, other.m_buffer->bytes() + other.m_offset, m_units) == 0;
    }
    if (m_buffer->is_utf16 && other.m_buffer->is_utf16) {
        return std::memcmp(m_buffer->utf16() + m_offset, other.m_buffer->utf16() + other.m_offset,
                           m_units * sizeof(char16_t)) == 0;
    }
    const uint32_t a_bytes = m_utf8_bytes.load(std::memory_order_relaxed);
    const uint32_t b_bytes = other.m_utf8_bytes.load(std::memory_order_relaxed);
    if (a_bytes != kUnknownUtf8Bytes && b_bytes != kUnknownUtf8Bytes && a_bytes != b_bytes) return false;
    const char* const narrow = m_buffer->is_utf16 ? (other.m_buffer->bytes() + other.m_offset)
                                                  : (m_buffer->bytes() + m_offset);
    const char16_t* const wide = m_buffer->is_utf16 ? (m_buffer->utf16() + m_offset)
                                                    : (other.m_buffer->utf16() + other.m_offset);
    for (uint32_t i = 0; i < m_units; ++i) {
        if (static_cast<unsigned char>(narrow[i]) != wide[i]) return false;
    }
    return true;
}

bool JsString::equals_utf8(std::string_view utf8) const noexcept {
    const uint32_t known = m_utf8_bytes.load(std::memory_order_relaxed);
    if (known != kUnknownUtf8Bytes && known != utf8.size()) return false;
    if (utf8.size() < m_units) return false;
    const char16_t* const u = m_buffer->utf16() + m_offset;
    const auto* const target = reinterpret_cast<const unsigned char*>(utf8.data());
    size_t pos = 0;
    const size_t n = utf8.size();
    for (uint32_t i = 0; i < m_units; ++i) {
        const char16_t c = u[i];
        if (c < 0x80) {
            if (pos >= n || target[pos] != c) return false;
            ++pos;
        } else if (c < 0x800) {
            if (pos + 2 > n || target[pos] != (0xC0 | (c >> 6)) || target[pos + 1] != (0x80 | (c & 0x3F))) return false;
            pos += 2;
        } else if (is_high_surrogate(c) && i + 1 < m_units && is_low_surrogate(u[i + 1])) {
            const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(c) - 0xD800) << 10) +
                                (static_cast<uint32_t>(u[i + 1]) - 0xDC00);
            if (pos + 4 > n || target[pos] != (0xF0 | (cp >> 18)) || target[pos + 1] != (0x80 | ((cp >> 12) & 0x3F)) ||
                target[pos + 2] != (0x80 | ((cp >> 6) & 0x3F)) || target[pos + 3] != (0x80 | (cp & 0x3F))) {
                return false;
            }
            pos += 4;
            ++i;
        } else {
            if (pos + 3 > n || target[pos] != (0xE0 | (c >> 12)) || target[pos + 1] != (0x80 | ((c >> 6) & 0x3F)) ||
                target[pos + 2] != (0x80 | (c & 0x3F))) {
                return false;
            }
            pos += 3;
        }
    }
    return pos == n;
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
    const size_t capacity = m_buffer->capacity;
    if (!m_buffer->is_utf16) {
        if (count * kShareDivisor >= capacity) {
            return hold(m_buffer, static_cast<uint32_t>(m_offset + begin), static_cast<uint32_t>(count),
                        static_cast<uint32_t>(count));
        }
        return join_ascii(std::string_view(m_buffer->bytes() + m_offset + begin, count), std::string_view(), count);
    }
    const char16_t* const u = m_buffer->utf16() + m_offset;
    if (count * kShareDivisor >= capacity) {
        uint32_t new_utf8 = kUnknownUtf8Bytes;
        const uint32_t parent_utf8 = m_utf8_bytes.load(std::memory_order_relaxed);
        const size_t dropped = begin + (m_units - end);
        if (parent_utf8 != kUnknownUtf8Bytes && dropped <= 256) {
            size_t dropped_bytes = count_utf8_bytes_raw(u, begin) + count_utf8_bytes_raw(u + end, m_units - end);
            const size_t split_adjust =
                (begin > 0 && is_high_surrogate(u[begin - 1]) && is_low_surrogate(u[begin]) ? 2 : 0) +
                (end < m_units && is_high_surrogate(u[end - 1]) && is_low_surrogate(u[end]) ? 2 : 0);
            new_utf8 = static_cast<uint32_t>(size_t{parent_utf8} - dropped_bytes + split_adjust);
        } else if (count <= 256) {
            new_utf8 = static_cast<uint32_t>(count_utf8_bytes_raw(u + begin, count));
        }
        return hold(m_buffer, static_cast<uint32_t>(m_offset + begin), static_cast<uint32_t>(count), new_utf8);
    }
    return from_utf16(std::u16string_view(u + begin, count));
}

namespace {

bool cache_covers_past(const auto* buffer, uint32_t a_end) noexcept {
    if (const auto* u8 = buffer->utf8.load(std::memory_order_acquire)) {
        if (u8->units.load(std::memory_order_acquire) > a_end) return true;
    }
    if (const auto* w = buffer->wide_cache.load(std::memory_order_acquire)) {
        if (w->units.load(std::memory_order_acquire) > a_end) return true;
    }
    for (const auto* node = buffer->utf8_slices.load(std::memory_order_acquire); node;
         node = node->next.load(std::memory_order_acquire)) {
        if (node->offset + node->units > a_end) return true;
    }
    return false;
}

} // namespace

JsString JsString::append_ascii(const JsString& a, std::string_view more) {
    if (more.empty()) return a;
    Buffer* const buffer = a.m_buffer;
    const size_t total = size_t{a.m_units} + more.size();
    if (total > kMaxBytes) js_throw_range_error("Invalid string length");

    const uint64_t tag = current_thread_tag();
    if (buffer->sole_thread.load(std::memory_order_relaxed) != tag) {
        buffer->sole_thread.store(0, std::memory_order_release);
    }

    const uint32_t a_end = a.m_offset + a.m_units;
    if (more.size() <= buffer->capacity - a_end) {
        uint32_t expected = a_end;
        if (buffer->used.compare_exchange_strong(expected, static_cast<uint32_t>(a_end + more.size()),
                                                 std::memory_order_acq_rel, std::memory_order_relaxed)) {
            std::memcpy(buffer->bytes() + a_end, more.data(), more.size());
            t_stats.bytes_copied += more.size();
            JsString result(a);
            result.m_units = static_cast<uint32_t>(total);
            result.m_utf8_bytes.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
            return result;
        }
        const bool non_overlapping =
            more.data() < buffer->bytes() || more.data() >= buffer->bytes() + buffer->capacity;
        if (non_overlapping && buffer->sole_thread.load(std::memory_order_acquire) == tag &&
            buffer->refs.load(std::memory_order_acquire) == 1 && !cache_covers_past(buffer, a_end)) {
            uint32_t cur_used = buffer->used.load(std::memory_order_acquire);
            if (buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                buffer->used.compare_exchange_strong(cur_used, static_cast<uint32_t>(a_end + more.size()),
                                                     std::memory_order_acq_rel, std::memory_order_relaxed)) {
                std::memcpy(buffer->bytes() + a_end, more.data(), more.size());
                t_stats.bytes_copied += more.size();
                JsString result(a);
                result.m_units = static_cast<uint32_t>(total);
                result.m_utf8_bytes.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
                return result;
            }
        }
    }

    const bool newest = (buffer->used.load(std::memory_order_relaxed) == a_end) ||
                        (buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                         buffer->refs.load(std::memory_order_acquire) == 1);
    const uint32_t moves = newest ? (buffer->moves < 1000 ? buffer->moves + 1 : buffer->moves) : 1;
    size_t capacity = total;
    if (moves >= 3) {
        capacity = total + total / 2;
        if (capacity > kMaxBytes) capacity = kMaxBytes;
    }
    JsString result = join_ascii(a.view(), more, capacity);
    result.m_buffer->moves = moves;
    return result;
}

JsString JsString::append_utf16_handle(const JsString& a, const JsString& b, uint32_t utf8_bytes) {
    Buffer* const buffer = a.m_buffer;
    const size_t total = size_t{a.m_units} + b.m_units;
    if (total > kMaxUnits) js_throw_range_error("Invalid string length");

    const uint64_t tag = current_thread_tag();
    if (buffer->sole_thread.load(std::memory_order_relaxed) != tag) {
        buffer->sole_thread.store(0, std::memory_order_release);
    }

    auto write_b = [&](char16_t* dst) noexcept {
        if (!b.m_buffer->is_utf16) {
            widen_bytes(b.m_buffer->bytes() + b.m_offset, dst, b.m_units);
            t_stats.units_widened += b.m_units;
        } else {
            std::memcpy(dst, b.m_buffer->utf16() + b.m_offset, size_t{b.m_units} * sizeof(char16_t));
        }
        t_stats.bytes_copied += size_t{b.m_units} * sizeof(char16_t);
    };

    const uint32_t a_end = a.m_offset + a.m_units;
    if (b.m_units <= buffer->capacity - a_end) {
        uint32_t expected = a_end;
        if (buffer->used.compare_exchange_strong(expected, static_cast<uint32_t>(a_end + b.m_units),
                                                 std::memory_order_acq_rel, std::memory_order_relaxed)) {
            write_b(buffer->utf16() + a_end);
            JsString result(a);
            result.m_units = static_cast<uint32_t>(total);
            result.m_utf8_bytes.store(utf8_bytes, std::memory_order_relaxed);
            return result;
        }
        if (buffer != b.m_buffer && buffer->sole_thread.load(std::memory_order_acquire) == tag &&
            buffer->refs.load(std::memory_order_acquire) == 1 && !cache_covers_past(buffer, a_end)) {
            uint32_t cur_used = buffer->used.load(std::memory_order_acquire);
            if (buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                buffer->used.compare_exchange_strong(cur_used, static_cast<uint32_t>(a_end + b.m_units),
                                                     std::memory_order_acq_rel, std::memory_order_relaxed)) {
                write_b(buffer->utf16() + a_end);
                JsString result(a);
                result.m_units = static_cast<uint32_t>(total);
                result.m_utf8_bytes.store(utf8_bytes, std::memory_order_relaxed);
                return result;
            }
        }
    }

    const bool newest = (buffer->used.load(std::memory_order_relaxed) == a_end) ||
                        (buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                         buffer->refs.load(std::memory_order_acquire) == 1);
    const uint32_t moves = newest ? (buffer->moves < 1000 ? buffer->moves + 1 : buffer->moves) : 1;
    size_t capacity = total;
    if (moves >= 3) {
        capacity = total + total / 2;
        if (capacity > kMaxUnits) capacity = kMaxUnits;
    }
    Buffer* next = allocate(capacity, true);
    next->moves = moves;
    std::memcpy(next->utf16(), buffer->utf16() + a.m_offset, size_t{a.m_units} * sizeof(char16_t));
    t_stats.bytes_copied += size_t{a.m_units} * sizeof(char16_t);
    write_b(next->utf16() + a.m_units);
    next->used.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
    JsString result;
    result.m_buffer = next;
    result.m_units = static_cast<uint32_t>(total);
    result.m_utf8_bytes.store(utf8_bytes, std::memory_order_relaxed);
    return result;
}

JsString JsString::append_utf16_utf8(const JsString& a, std::string_view more, size_t more_units,
                                     uint32_t more_utf8_bytes) {
    Buffer* const buffer = a.m_buffer;
    const size_t total = size_t{a.m_units} + more_units;
    if (total > kMaxUnits) js_throw_range_error("Invalid string length");

    const uint64_t tag = current_thread_tag();
    if (buffer->sole_thread.load(std::memory_order_relaxed) != tag) {
        buffer->sole_thread.store(0, std::memory_order_release);
    }

    const uint32_t a_bytes = a.m_utf8_bytes.load(std::memory_order_relaxed);
    uint32_t merged_utf8 = kUnknownUtf8Bytes;
    if (a_bytes != kUnknownUtf8Bytes) {
        const char16_t a_last = a.unit_at(a.length() - 1);
        const auto* const mp = reinterpret_cast<const unsigned char*>(more.data());
        const bool b_low = more.size() >= 3 && is_low_half(mp);
        merged_utf8 = static_cast<uint32_t>(size_t{a_bytes} + more_utf8_bytes -
                                            (is_high_surrogate(a_last) && b_low ? 2 : 0));
    }

    const uint32_t a_end = a.m_offset + a.m_units;
    if (more_units <= buffer->capacity - a_end) {
        uint32_t expected = a_end;
        if (buffer->used.compare_exchange_strong(expected, static_cast<uint32_t>(a_end + more_units),
                                                 std::memory_order_acq_rel, std::memory_order_relaxed)) {
            decode_utf8_to_utf16(more, buffer->utf16() + a_end);
            t_stats.bytes_copied += more_units * sizeof(char16_t);
            t_stats.units_widened += more_units;
            JsString result(a);
            result.m_units = static_cast<uint32_t>(total);
            result.m_utf8_bytes.store(merged_utf8, std::memory_order_relaxed);
            return result;
        }
        if (buffer->sole_thread.load(std::memory_order_acquire) == tag &&
            buffer->refs.load(std::memory_order_acquire) == 1 && !cache_covers_past(buffer, a_end)) {
            uint32_t cur_used = buffer->used.load(std::memory_order_acquire);
            if (buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                buffer->used.compare_exchange_strong(cur_used, static_cast<uint32_t>(a_end + more_units),
                                                     std::memory_order_acq_rel, std::memory_order_relaxed)) {
                decode_utf8_to_utf16(more, buffer->utf16() + a_end);
                t_stats.bytes_copied += more_units * sizeof(char16_t);
                t_stats.units_widened += more_units;
                JsString result(a);
                result.m_units = static_cast<uint32_t>(total);
                result.m_utf8_bytes.store(merged_utf8, std::memory_order_relaxed);
                return result;
            }
        }
    }

    const bool newest = (buffer->used.load(std::memory_order_relaxed) == a_end) ||
                        (buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                         buffer->refs.load(std::memory_order_acquire) == 1);
    const uint32_t moves = newest ? (buffer->moves < 1000 ? buffer->moves + 1 : buffer->moves) : 1;
    size_t capacity = total;
    if (moves >= 3) {
        capacity = total + total / 2;
        if (capacity > kMaxUnits) capacity = kMaxUnits;
    }
    Buffer* next = allocate(capacity, true);
    next->moves = moves;
    std::memcpy(next->utf16(), buffer->utf16() + a.m_offset, size_t{a.m_units} * sizeof(char16_t));
    t_stats.bytes_copied += size_t{a.m_units} * sizeof(char16_t);
    decode_utf8_to_utf16(more, next->utf16() + a.m_units);
    t_stats.bytes_copied += more_units * sizeof(char16_t);
    t_stats.units_widened += more_units;
    next->used.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
    JsString result;
    result.m_buffer = next;
    result.m_units = static_cast<uint32_t>(total);
    result.m_utf8_bytes.store(merged_utf8, std::memory_order_relaxed);
    return result;
}

JsString JsString::concat(const JsString& a, const JsString& b) {
    if (b.empty()) return a;
    if (a.empty()) return b;
    if (!a.m_buffer->is_utf16 && !b.m_buffer->is_utf16) {
        return append_ascii(a, b.view());
    }
    const uint32_t a_bytes = a.m_utf8_bytes.load(std::memory_order_relaxed);
    const uint32_t b_bytes = b.m_utf8_bytes.load(std::memory_order_relaxed);
    const uint32_t utf8_bytes = (a_bytes != kUnknownUtf8Bytes && b_bytes != kUnknownUtf8Bytes)
                                    ? join_utf8_bytes(a, b, a_bytes, b_bytes)
                                    : kUnknownUtf8Bytes;
    if (a.m_buffer->is_utf16) {
        return append_utf16_handle(a, b, utf8_bytes);
    }
    // `a` is ASCII storage while `b` lives in a UTF-16 block. If `b` is known
    // to be pure ASCII (all units < 0x80), stay in 1-byte ASCII storage;
    // otherwise promote `a + b` to a UTF-16 block (growing if `a` was extended).
    if (b_bytes == b.m_units) {
        const size_t total = size_t{a.m_units} + b.m_units;
        if (total > kMaxBytes) js_throw_range_error("Invalid string length");
        const uint32_t a_end = a.m_offset + a.m_units;
        const uint64_t tag = current_thread_tag();
        const bool newest = (a.m_buffer->used.load(std::memory_order_relaxed) == a_end) ||
                            (a.m_buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                             a.m_buffer->refs.load(std::memory_order_acquire) == 1);
        const uint32_t moves = newest ? (a.m_buffer->moves < 1000 ? a.m_buffer->moves + 1 : a.m_buffer->moves) : 1;
        size_t capacity = total;
        if (moves >= 3) {
            capacity = total + total / 2;
            if (capacity > kMaxBytes) capacity = kMaxBytes;
        }
        Buffer* next = allocate(capacity, false);
        next->moves = moves;
        std::memcpy(next->bytes(), a.m_buffer->bytes() + a.m_offset, a.m_units);
        narrow_units(b.m_buffer->utf16() + b.m_offset, next->bytes() + a.m_units, b.m_units);
        next->used.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
        t_stats.bytes_copied += total;
        JsString result;
        result.m_buffer = next;
        result.m_units = static_cast<uint32_t>(total);
        result.m_utf8_bytes.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
        return result;
    }

    const size_t total = size_t{a.m_units} + b.m_units;
    if (total > kMaxUnits) js_throw_range_error("Invalid string length");
    const uint32_t a_end = a.m_offset + a.m_units;
    const uint64_t tag = current_thread_tag();
    const bool newest = (a.m_buffer->used.load(std::memory_order_relaxed) == a_end) ||
                        (a.m_buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                         a.m_buffer->refs.load(std::memory_order_acquire) == 1);
    const uint32_t moves = newest ? (a.m_buffer->moves < 1000 ? a.m_buffer->moves + 1 : a.m_buffer->moves) : 1;
    size_t capacity = total;
    if (moves >= 3) {
        capacity = total + total / 2;
        if (capacity > kMaxUnits) capacity = kMaxUnits;
    }
    Buffer* next = allocate(capacity, true);
    next->moves = moves;
    widen_bytes(a.m_buffer->bytes() + a.m_offset, next->utf16(), a.m_units);
    t_stats.units_widened += a.m_units;
    std::memcpy(next->utf16() + a.m_units, b.m_buffer->utf16() + b.m_offset, size_t{b.m_units} * sizeof(char16_t));
    next->used.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
    t_stats.bytes_copied += total * sizeof(char16_t);
    JsString result;
    result.m_buffer = next;
    result.m_units = static_cast<uint32_t>(total);
    result.m_utf8_bytes.store(utf8_bytes, std::memory_order_relaxed);
    return result;
}

JsString JsString::concat(const JsString& a, std::string_view b) {
    if (b.empty()) return a;
    const Scan scanned = scan_utf8(b);
    if (!scanned.well_formed) return concat(a, JsString(b));
    if (a.empty()) return from_checked(b, scanned.units, scanned.ascii);
    if (!a.m_buffer->is_utf16 && scanned.ascii) {
        return append_ascii(a, b);
    }
    if (a.m_buffer->is_utf16) {
        return append_utf16_utf8(a, b, scanned.units, static_cast<uint32_t>(scanned.repaired_utf8_bytes));
    }

    // `a` is ASCII, `b` is non-ASCII UTF-8: promote `a + b` to UTF-16.
    const size_t total = size_t{a.m_units} + scanned.units;
    if (total > kMaxUnits) js_throw_range_error("Invalid string length");
    const uint32_t a_end = a.m_offset + a.m_units;
    const uint64_t tag = current_thread_tag();
    const bool newest = (a.m_buffer->used.load(std::memory_order_relaxed) == a_end) ||
                        (a.m_buffer->sole_thread.load(std::memory_order_acquire) == tag &&
                         a.m_buffer->refs.load(std::memory_order_acquire) == 1);
    const uint32_t moves = newest ? (a.m_buffer->moves < 1000 ? a.m_buffer->moves + 1 : a.m_buffer->moves) : 1;
    size_t capacity = total;
    if (moves >= 3) {
        capacity = total + total / 2;
        if (capacity > kMaxUnits) capacity = kMaxUnits;
    }
    Buffer* next = allocate(capacity, true);
    next->moves = moves;
    widen_bytes(a.m_buffer->bytes() + a.m_offset, next->utf16(), a.m_units);
    decode_utf8_to_utf16(b, next->utf16() + a.m_units);
    next->used.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
    t_stats.bytes_copied += total * sizeof(char16_t);
    t_stats.units_widened += total;
    JsString result;
    result.m_buffer = next;
    result.m_units = static_cast<uint32_t>(total);
    result.m_utf8_bytes.store(static_cast<uint32_t>(size_t{a.m_units} + b.size()), std::memory_order_relaxed);
    return result;
}

JsString JsString::concat(std::string_view a, const JsString& b) {
    if (a.empty()) return b;
    const Scan scanned = scan_utf8(a);
    if (!scanned.well_formed) return concat(JsString(a), b);
    if (b.empty()) return from_checked(a, scanned.units, scanned.ascii);
    if (scanned.ascii && !b.m_buffer->is_utf16) {
        const size_t total = scanned.units + b.m_units;
        if (total > kMaxBytes) js_throw_range_error("Invalid string length");
        return join_ascii(a, b.view(), total);
    }

    const size_t total = scanned.units + b.m_units;
    if (total > kMaxUnits) js_throw_range_error("Invalid string length");
    Buffer* next = allocate(total, true);
    decode_utf8_to_utf16(a, next->utf16());
    t_stats.units_widened += scanned.units;
    if (!b.m_buffer->is_utf16) {
        widen_bytes(b.m_buffer->bytes() + b.m_offset, next->utf16() + scanned.units, b.m_units);
        t_stats.units_widened += b.m_units;
    } else {
        std::memcpy(next->utf16() + scanned.units, b.m_buffer->utf16() + b.m_offset,
                    size_t{b.m_units} * sizeof(char16_t));
    }
    next->used.store(static_cast<uint32_t>(total), std::memory_order_relaxed);
    t_stats.bytes_copied += total * sizeof(char16_t);

    uint32_t utf8_bytes = kUnknownUtf8Bytes;
    const uint32_t b_bytes = b.m_utf8_bytes.load(std::memory_order_relaxed);
    if (b_bytes != kUnknownUtf8Bytes) {
        const char16_t a_last = next->utf16()[scanned.units - 1];
        const char16_t b_first = next->utf16()[scanned.units];
        utf8_bytes = static_cast<uint32_t>(a.size() + b_bytes -
                                           (is_high_surrogate(a_last) && is_low_surrogate(b_first) ? 2 : 0));
    }
    JsString result;
    result.m_buffer = next;
    result.m_units = static_cast<uint32_t>(total);
    result.m_utf8_bytes.store(utf8_bytes, std::memory_order_relaxed);
    return result;
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

bool JsString::is_well_formed() const noexcept {
    if (!m_buffer || !m_buffer->is_utf16 || m_units == 0) return true;
    const char16_t* const u = m_buffer->utf16() + m_offset;
    for (uint32_t i = 0; i < m_units; ++i) {
        const char16_t c = u[i];
        if (is_high_surrogate(c)) {
            if (i + 1 >= m_units || !is_low_surrogate(u[i + 1])) return false;
            ++i;
        } else if (is_low_surrogate(c)) {
            return false;
        }
    }
    return true;
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
    if (is_well_formed()) return *this;
    Buffer* next = allocate(m_units, true);
    const char16_t* const src = m_buffer->utf16() + m_offset;
    char16_t* const dst = next->utf16();
    for (uint32_t i = 0; i < m_units; ++i) {
        const char16_t c = src[i];
        if (is_high_surrogate(c)) {
            if (i + 1 < m_units && is_low_surrogate(src[i + 1])) {
                dst[i] = c;
                dst[i + 1] = src[i + 1];
                ++i;
            } else {
                dst[i] = 0xFFFD;
            }
        } else if (is_low_surrogate(c)) {
            dst[i] = 0xFFFD;
        } else {
            dst[i] = c;
        }
    }
    next->used.store(m_units, std::memory_order_relaxed);
    t_stats.bytes_copied += size_t{m_units} * sizeof(char16_t);
    JsString result;
    result.m_buffer = next;
    result.m_units = m_units;
    const uint32_t known = m_utf8_bytes.load(std::memory_order_relaxed);
    result.m_utf8_bytes.store(known, std::memory_order_relaxed);      // U+FFFD is 3 UTF-8 bytes, identical to a lone half
    return result;
}

JsString::Stats JsString::stats() noexcept { return t_stats; }

// ---------------------------------------------------------------------------
// Utf16View
// ---------------------------------------------------------------------------

Utf16View::Utf16View(JsString string) : m_string(std::move(string)), m_size(m_string.length()) {
    if (m_size == 0) return;
    if (m_string.has_ascii_storage()) {
        m_narrow = m_string.m_buffer->bytes() + m_string.m_offset;
    } else {
        m_wide = m_string.m_buffer->utf16() + m_string.m_offset;
    }
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
        if (needle.m_wide && !needle.m_string.is_ascii()) return npos;         // ASCII text holds no unit above 0x7F
        if (!needle.m_wide) {
            return std::string_view(m_narrow, m_size).find(std::string_view(needle.m_narrow, needle.m_size), from);
        }
        // `needle` is an all-ASCII slice inside a UTF-16 block.
        for (size_t at = from; at + needle.m_size <= m_size; ++at) {
            bool match = true;
            for (size_t j = 0; j < needle.m_size; ++j) {
                if (static_cast<unsigned char>(m_narrow[at + j]) != needle.m_wide[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return at;
        }
        return npos;
    }
    if (needle.m_size == 1) return find(needle[0], from);
    if (needle.m_wide) {
        return std::u16string_view(m_wide, m_size).find(std::u16string_view(needle.m_wide, needle.m_size), from);
    }
    if (needle.m_size <= 64) {
        char16_t buf[64];
        widen_bytes(needle.m_narrow, buf, needle.m_size);
        return std::u16string_view(m_wide, m_size).find(std::u16string_view(buf, needle.m_size), from);
    }
    const std::u16string widened = needle.copy();
    return std::u16string_view(m_wide, m_size).find(widened, from);
}

size_t Utf16View::rfind(const Utf16View& needle, size_t from) const {
    if (needle.m_size > m_size) return npos;
    if (needle.m_size == 0) return from < m_size ? from : m_size;
    if (!m_wide) {
        if (needle.m_wide && !needle.m_string.is_ascii()) return npos;
        if (!needle.m_wide) {
            return std::string_view(m_narrow, m_size).rfind(std::string_view(needle.m_narrow, needle.m_size), from);
        }
        const size_t limit = from < m_size - needle.m_size ? from : m_size - needle.m_size;
        for (size_t at = limit + 1; at-- > 0;) {
            bool match = true;
            for (size_t j = 0; j < needle.m_size; ++j) {
                if (static_cast<unsigned char>(m_narrow[at + j]) != needle.m_wide[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return at;
        }
        return npos;
    }
    if (needle.m_wide) {
        return std::u16string_view(m_wide, m_size).rfind(std::u16string_view(needle.m_wide, needle.m_size), from);
    }
    if (needle.m_size <= 64) {
        char16_t buf[64];
        widen_bytes(needle.m_narrow, buf, needle.m_size);
        return std::u16string_view(m_wide, m_size).rfind(std::u16string_view(buf, needle.m_size), from);
    }
    const std::u16string widened = needle.copy();
    return std::u16string_view(m_wide, m_size).rfind(widened, from);
}

bool Utf16View::matches_at(size_t at, const Utf16View& other) const noexcept {
    if (at > m_size || other.m_size > m_size - at) return false;
    if (!m_wide && !other.m_wide) {
        return other.m_size == 0 || std::memcmp(m_narrow + at, other.m_narrow, other.m_size) == 0;
    }
    if (m_wide && other.m_wide) {
        return other.m_size == 0 ||
               std::memcmp(m_wide + at, other.m_wide, other.m_size * sizeof(char16_t)) == 0;
    }
    for (size_t i = 0; i < other.m_size; ++i) {
        if ((*this)[at + i] != other[i]) return false;
    }
    return true;
}

} // namespace litebird
