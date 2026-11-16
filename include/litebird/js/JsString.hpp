#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace litebird {

/// The text of a JavaScript string value.
///
/// A JsString is a handle onto an immutable range of a refcounted block whose
/// code units never change once written, so any number of values can point at
/// the same block. Copying a string value (reading a variable, passing an
/// argument, binding `this`) copies this handle and bumps a count; the text
/// itself is never copied.
///
/// Before this type existed a string value owned a private std::string. Every
/// read of a variable copied the whole text, `s.length` re-counted it and
/// `s[i]` re-scanned it, so a loop over a string of n characters did n*n
/// work: 76 seconds for 200,000 characters. See AUDIT.md, finding 25.
///
/// Storage and indexing (Phase 53)
///
/// Script counts and indexes text in UTF-16 code units (`char16_t`), and two
/// halves of a surrogate pair (`\uD83D`, `\uDE00`) can be written, sliced,
/// read, and later joined at any boundary. Storing pure-ASCII blocks as one
/// byte per unit and non-ASCII blocks directly as UTF-16 code units (`char16_t`)
/// means `s.length`, `s[i]`, `charCodeAt`, `indexOf`, `slice`, `RegExp`, and
/// `Utf16View` all read contiguous memory in O(1) with no widening and no
/// surrogate settling.
///
/// UTF-8 views (`view()`, `str()`) for ASCII blocks return the bytes directly;
/// for UTF-16 blocks, UTF-8 bytes are materialized lazily onto a thread-safe
/// cache owned by the block (or a slice entry when a handle splits a surrogate
/// pair at either edge) and kept for the lifetime of the block.
///
/// Appending
///
/// A handle shows a range of its block. A block can have room after the text
/// it holds, and `a + b` writes b into that room when `a` ends where the
/// block's written text ends (or when `a` is the sole owner of the block and
/// no cached view covers the tail being rewritten). Blocks that keep growing
/// are given room to grow by half, so building a string of n code units by
/// appending costs O(n), even when alternating half-pair appends with reads
/// or trailing slices (`s += '\uD83D'; s.indexOf('zz'); s += '\uDE00'`).
///
/// Taking pieces
///
/// `s.substring(a, b)` copies a small piece into a block of its own. A piece
/// that is a quarter of its block's capacity or more becomes a range of the
/// same block instead, with nothing copied; the quarter bounds how much memory
/// a piece can keep alive. A tokenizer eating its input with `s = s.slice(n)`
/// therefore costs what it reads, not the length of what is left each time.
///
/// Threads
///
/// Suspended async functions and workers run on other threads. Handles may be
/// copied, read, appended to and dropped from several threads at once (the
/// refcount, the claim on a block's free room, the lazy UTF-8 cache, and
/// per-handle lazy byte-count cache are atomic); one handle object must not
/// be mutated by two threads concurrently, like any value.
class JsString {
public:
    /// Longest string, in bytes. Past it script gets `RangeError: Invalid
    /// string length`, as it does in every engine.
    static constexpr size_t kMaxBytes = (size_t{1} << 29) - 24;
    static constexpr size_t kMaxUnits = kMaxBytes / sizeof(char16_t);

    JsString() noexcept = default;
    /// Copies `utf8`. Malformed input is repaired (U+FFFD; adjacent surrogate
    /// halves become one pair), not rejected.
    explicit JsString(std::string_view utf8);
    static JsString from_utf16(std::u16string_view units);
    /// The one-unit string. ASCII units are shared and cost no allocation.
    static JsString from_unit(char16_t unit);

    JsString(const JsString& other) noexcept
        : m_buffer(other.m_buffer), m_offset(other.m_offset), m_units(other.m_units),
          m_utf8_bytes(other.m_utf8_bytes.load(std::memory_order_relaxed)) {
        retain();
    }
    JsString(JsString&& other) noexcept
        : m_buffer(other.m_buffer), m_offset(other.m_offset), m_units(other.m_units),
          m_utf8_bytes(other.m_utf8_bytes.load(std::memory_order_relaxed)) {
        other.forget();
    }
    JsString& operator=(const JsString& other) noexcept {
        if (this != &other) {
            other.retain();
            release();
            copy_fields(other);
        }
        return *this;
    }
    JsString& operator=(JsString&& other) noexcept {
        if (this != &other) {
            release();
            copy_fields(other);
            other.forget();
        }
        return *this;
    }
    ~JsString() { release(); }

    // --- The text as UTF-8 ------------------------------------------------------

    /// The bytes. Valid for as long as this handle, or any copy of it, lives.
    /// Not NUL-terminated. For ASCII blocks this is zero-copy; for UTF-16
    /// blocks the UTF-8 form is materialized lazily and cached on the block.
    std::string_view view() const noexcept {
        if (!m_buffer || m_units == 0) return std::string_view();
        if (!m_buffer->is_utf16) return std::string_view(m_buffer->bytes() + m_offset, m_units);
        return utf8_view();
    }
    /// An owned copy of the bytes. This is the one read here that always costs
    /// the length of the text; code that runs once per character must not call
    /// it.
    std::string str() const;
    /// How many UTF-8 bytes `view()` holds.
    size_t size() const noexcept;
    bool empty() const noexcept { return m_units == 0; }

    // --- The text as script sees it: UTF-16 code units -----------------------------

    /// What script calls `.length`.
    size_t length() const noexcept { return m_units; }
    bool is_ascii() const noexcept {
        if (!m_buffer || !m_buffer->is_utf16) return true;
        return size() == m_units;
    }
    /// The code unit at `index`, which must be below length().
    char16_t unit_at(size_t index) const noexcept {
        return m_buffer->is_utf16 ? m_buffer->utf16()[m_offset + index]
                                  : static_cast<unsigned char>(m_buffer->bytes()[m_offset + index]);
    }
    /// length() code units. Only for strings that are not ASCII. For UTF-16
    /// blocks this returns directly into the block with no allocation or copy;
    /// the pointer stays valid for as long as this handle, or any copy, lives.
    const char16_t* wide() const {
        if (!m_buffer) return nullptr;
        if (m_buffer->is_utf16) return m_buffer->utf16() + m_offset;
        return widen_ascii();
    }
    /// Code units [begin, end), clamped to the string. See "Taking pieces".
    JsString substring(size_t begin, size_t end) const;

    /// `a + b`. See "Appending" above.
    static JsString concat(const JsString& a, const JsString& b);
    static JsString concat(const JsString& a, std::string_view b);
    static JsString concat(std::string_view a, const JsString& b);

    /// Orders text the way `<` and `sort()` do: by UTF-16 code unit. Negative,
    /// zero or positive. Both arguments must be UTF-8 text taken from a JsString.
    static int compare(std::string_view a, std::string_view b) noexcept;

    friend bool operator==(const JsString& a, const JsString& b) noexcept {
        if (a.m_units != b.m_units) return false;
        if (a.m_units == 0) return true;
        if (a.m_buffer == b.m_buffer && a.m_offset == b.m_offset) return true;
        return a.equals_units(b);
    }
    friend bool operator!=(const JsString& a, const JsString& b) noexcept { return !(a == b); }
    friend bool operator==(const JsString& a, std::string_view b) noexcept {
        if (a.empty()) return b.empty();
        if (!a.m_buffer->is_utf16) return a.view() == b;
        return a.equals_utf8(b);
    }
    friend bool operator!=(const JsString& a, std::string_view b) noexcept { return !(a == b); }

    /// True when no half of a surrogate pair stands alone in the text, which is
    /// when the bytes are UTF-8 that anything outside the engine will accept.
    bool is_well_formed() const noexcept;
    /// The text with every lone half replaced by U+FFFD.
    JsString to_well_formed() const;
    /// Where the next lone half is encoded in `utf8`, at or after `from`, or
    /// npos. `utf8` must be text taken from a JsString.
    static size_t find_lone_half(std::string_view utf8, size_t from = 0) noexcept;

    /// True when both handles read from the same block, which means no text
    /// was copied to get from one to the other.
    bool shares_text_with(const JsString& other) const noexcept {
        return m_buffer != nullptr && m_buffer == other.m_buffer;
    }

    /// How many handles read from this string's block right now (0 for the
    /// empty string). For tests: it is out of date as soon as it is read if
    /// other threads are at work.
    size_t use_count() const noexcept { return m_buffer ? m_buffer->refs.load(std::memory_order_acquire) : 0; }

    /// Work done by the calling thread, for tests that must show an operation
    /// does not copy or re-scan text. The counters only grow; compare two
    /// readings.
    struct Stats {
        uint64_t bytes_copied{0};      // bytes written into blocks, UTF-8 caches, or handed out by str()
        uint64_t units_widened{0};     // code units converted from narrow/UTF-8 to UTF-16
        uint64_t blocks{0};            // blocks allocated
    };
    static Stats stats() noexcept;

private:
    static constexpr uint32_t kUnknownUtf8Bytes = 0xFFFFFFFFu;
    static constexpr size_t kMarkEvery = 256;
    /// A piece shares its block when it is at least this fraction of the
    /// block's capacity.
    static constexpr size_t kShareDivisor = 4;

    /// Cached UTF-8 bytes for `[0 .. units)` of a UTF-16 block, plus marks
    /// mapping every kMarkEvery-th UTF-16 code unit to its UTF-8 byte offset.
    /// Sized once at creation to hold any future appends in the block so it
    /// never relocates.
    struct Utf8Cache {
        std::atomic<uint32_t> units{0};
        uint32_t bytes{0};
        uint32_t capacity_units{0};
        uint32_t data_bytes{0};

        char* data() noexcept { return reinterpret_cast<char*>(this + 1); }
        const char* data() const noexcept { return reinterpret_cast<const char*>(this + 1); }
        uint32_t* marks() noexcept { return reinterpret_cast<uint32_t*>(data() + data_bytes); }
        const uint32_t* marks() const noexcept { return reinterpret_cast<const uint32_t*>(data() + data_bytes); }
    };

    /// Lazy UTF-16 copy if `wide()` is called explicitly on an ASCII block.
    struct Wide {
        std::atomic<uint32_t> units{0};
        uint32_t capacity{0};

        char16_t* data() noexcept { return reinterpret_cast<char16_t*>(this + 1); }
        const char16_t* data() const noexcept { return reinterpret_cast<const char16_t*>(this + 1); }
    };

    /// Shared immutable block: header followed by `capacity` `char` bytes (if
    /// `!is_utf16`) or `capacity` `char16_t` units (if `is_utf16`).
    struct Buffer {
        struct Utf8Slice {
            std::atomic<Utf8Slice*> next{nullptr};
            uint32_t offset{0};
            uint32_t units{0};
            uint32_t bytes{0};
            uint32_t reserved{0};

            char* data() noexcept { return reinterpret_cast<char*>(this + 1); }
            const char* data() const noexcept { return reinterpret_cast<const char*>(this + 1); }
        };

        std::atomic<uint32_t> refs{1};
        /// Code units written so far. Room is claimed atomically before writing.
        std::atomic<uint32_t> used{0};
        /// Creator thread tag while unshared; cleared to 0 if another thread touches `used`.
        std::atomic<uint64_t> sole_thread{0};
        uint32_t capacity{0};
        /// How many times in a row this text has moved to a bigger block on append.
        uint32_t moves{0};
        bool is_utf16{false};
        std::atomic<Utf8Cache*> utf8{nullptr};
        std::atomic<Utf8Slice*> utf8_slices{nullptr};
        std::atomic<Wide*> wide_cache{nullptr};

        char* bytes() noexcept { return reinterpret_cast<char*>(this + 1); }
        const char* bytes() const noexcept { return reinterpret_cast<const char*>(this + 1); }
        char16_t* utf16() noexcept { return reinterpret_cast<char16_t*>(this + 1); }
        const char16_t* utf16() const noexcept { return reinterpret_cast<const char16_t*>(this + 1); }
    };

    static Buffer* allocate(size_t capacity_units, bool is_utf16);
    static void destroy(Buffer* buffer) noexcept;
    static JsString hold(Buffer* buffer, uint32_t offset, uint32_t units, uint32_t utf8_bytes) noexcept;
    static JsString join_ascii(std::string_view first, std::string_view second, size_t capacity);
    static JsString from_checked(std::string_view utf8, size_t units, bool ascii);
    static JsString append_ascii(const JsString& a, std::string_view more);
    static JsString append_utf16_handle(const JsString& a, const JsString& b, uint32_t utf8_bytes);
    static JsString append_utf16_utf8(const JsString& a, std::string_view more, size_t more_units, uint32_t more_utf8_bytes);

    std::string_view utf8_view() const noexcept;
    std::string_view build_utf8_cache(uint32_t end) const noexcept;
    std::string_view make_utf8_slice() const noexcept;
    const char16_t* widen_ascii() const;
    bool equals_units(const JsString& other) const noexcept;
    bool equals_utf8(std::string_view utf8) const noexcept;
    bool has_ascii_storage() const noexcept { return !m_buffer || !m_buffer->is_utf16; }

    friend class Utf16View;

    void copy_fields(const JsString& other) noexcept {
        m_buffer = other.m_buffer;
        m_offset = other.m_offset;
        m_units = other.m_units;
        m_utf8_bytes.store(other.m_utf8_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void retain() const noexcept {
        if (m_buffer) m_buffer->refs.fetch_add(1, std::memory_order_relaxed);
    }
    void release() noexcept {
        if (m_buffer && m_buffer->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) destroy(m_buffer);
    }
    void forget() noexcept {
        m_buffer = nullptr;
        m_offset = 0;
        m_units = 0;
        m_utf8_bytes.store(0, std::memory_order_relaxed);
    }

    Buffer* m_buffer{nullptr};                   // null for the empty string
    uint32_t m_offset{0};                        // start in code units (bytes or char16_t)
    uint32_t m_units{0};                         // length in UTF-16 code units
    mutable std::atomic<uint32_t> m_utf8_bytes{0}; // UTF-8 byte count, or kUnknownUtf8Bytes
};

/// A string addressed by UTF-16 code unit, which is how String.prototype and
/// the regular-expression engine index text. Building one is free: an ASCII
/// block is read in place, and a UTF-16 block already holds code units in place.
/// The view keeps the string alive.
class Utf16View {
public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    Utf16View() = default;
    explicit Utf16View(JsString string);

    size_t size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }
    bool ascii() const noexcept { return m_wide == nullptr; }
    char16_t operator[](size_t index) const noexcept {
        return m_wide ? m_wide[index] : static_cast<char16_t>(static_cast<unsigned char>(m_narrow[index]));
    }
    const JsString& string() const noexcept { return m_string; }

    /// Code units [begin, end) as a string.
    JsString slice(size_t begin, size_t end) const { return m_string.substring(begin, end); }
    /// Code units [begin, begin + count) as UTF-16.
    std::u16string copy(size_t begin = 0, size_t count = npos) const;
    void append_to(std::u16string& out, size_t begin = 0, size_t count = npos) const;

    /// These follow std::string: the result is an index in code units or npos.
    size_t find(char16_t unit, size_t from = 0) const noexcept;
    size_t find(const Utf16View& needle, size_t from = 0) const;
    size_t rfind(const Utf16View& needle, size_t from = npos) const;
    /// True when `other` occurs at index `at`.
    bool matches_at(size_t at, const Utf16View& other) const noexcept;

private:
    JsString m_string;
    const char* m_narrow{nullptr};
    const char16_t* m_wide{nullptr};
    size_t m_size{0};
};

} // namespace litebird
