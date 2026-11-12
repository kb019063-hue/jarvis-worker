#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace litebird {

/// The text of a JavaScript string value.
///
/// A JsString is a handle onto a block of bytes that never change once they
/// are written, so any number of values can point at the same block. Copying a
/// string value (reading a variable, passing an argument, binding `this`)
/// copies this handle and bumps a count; the text itself is never copied.
///
/// Before this type existed a string value owned a private std::string. Every
/// read of a variable copied the whole text, `s.length` re-counted it and
/// `s[i]` re-scanned it, so a loop over a string of n characters did n*n
/// work: 76 seconds for 200,000 characters. See AUDIT.md, finding 25.
///
/// Length and indexing
///
/// The text is UTF-8, because that is what the DOM, property keys and every
/// output path of the engine use. Script, however, counts in UTF-16 code
/// units: `"é".length` is 1 although the text is two bytes. The handle
/// therefore carries the length in code units, worked out once when the
/// string is created, and `length()` is a field read. A string whose two
/// lengths agree is pure ASCII, and for those a code unit is a byte: `s[i]`
/// is one array read. Any other string gets a UTF-16 copy the first time it is
/// indexed; the copy is kept with the block, so the second index is an array
/// read as well.
///
/// Appending
///
/// A handle shows a range of its block. A block can have room after the text
/// it holds, and `a + b` writes b into that room when a ends where the
/// block's text ends. The result is a longer range of the same block; `a`
/// still sees exactly the bytes it saw before, because bytes that have been
/// written are never written again. Blocks that are being extended are given
/// room to grow by half, so building a string of n bytes by appending costs
/// n, however many appends it takes. Before, every `s += x` copied all of s:
/// 100,000 appends took 9 seconds.
///
/// Taking pieces
///
/// `s.slice(a, b)` copies a small piece into a block of its own. A piece that
/// is a quarter of its block or more is a new range of the same block
/// instead, with nothing copied; the quarter bounds how much memory a piece
/// can keep alive. A tokenizer that eats its input with `s = s.slice(n)`
/// therefore costs what it reads, not the length of what is left each time.
///
/// Well-formedness
///
/// Bytes that are not UTF-8 become U+FFFD when a string is created, exactly as
/// a browser decodes a page, so the two lengths can never disagree. The one
/// extension is that half a surrogate pair may stand alone (encoded in three
/// bytes), since script can make one: `"\uD83D"`. Two halves that meet are
/// always stored as the one four-byte character they spell, so equal strings
/// have equal bytes.
///
/// Threads
///
/// Suspended async functions and workers run on other threads. Handles may be
/// copied, read, appended to and dropped from several threads at once (the
/// count, the claim on a block's free room and the UTF-16 copy are atomic);
/// one handle object must not be written by two threads, like any value.
class JsString {
public:
    /// Longest string, in bytes. Past it script gets `RangeError: Invalid
    /// string length`, as it does in every engine.
    static constexpr size_t kMaxBytes = (size_t{1} << 29) - 24;

    JsString() noexcept = default;
    /// Copies `utf8`. Malformed input is repaired, not rejected.
    explicit JsString(std::string_view utf8);
    static JsString from_utf16(std::u16string_view units);
    /// The one-unit string. ASCII units are shared and cost no allocation.
    static JsString from_unit(char16_t unit);

    JsString(const JsString& other) noexcept
        : m_buffer(other.m_buffer), m_offset(other.m_offset), m_bytes(other.m_bytes),
          m_unit_offset(other.m_unit_offset), m_units(other.m_units) {
        retain();
    }
    JsString(JsString&& other) noexcept
        : m_buffer(other.m_buffer), m_offset(other.m_offset), m_bytes(other.m_bytes),
          m_unit_offset(other.m_unit_offset), m_units(other.m_units) {
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
    /// Not NUL-terminated.
    std::string_view view() const noexcept {
        return m_buffer ? std::string_view(m_buffer->bytes() + m_offset, m_bytes) : std::string_view();
    }
    /// An owned copy of the bytes. This is the one read here whose cost grows
    /// with the text; code that runs once per character must not call it.
    std::string str() const;
    size_t size() const noexcept { return m_bytes; }
    bool empty() const noexcept { return m_bytes == 0; }

    // --- The text as script sees it: UTF-16 code units -----------------------------

    /// What script calls `.length`.
    size_t length() const noexcept { return m_units; }
    bool is_ascii() const noexcept { return m_units == m_bytes; }
    /// The code unit at `index`, which must be below length().
    char16_t unit_at(size_t index) const {
        return is_ascii() ? static_cast<unsigned char>(m_buffer->bytes()[m_offset + index]) : wide()[index];
    }
    /// length() code units. Only for strings that are not ASCII. Built the
    /// first time it is asked for and kept for as long as the block is; the
    /// pointer stays valid for as long as this handle, or any copy, lives.
    const char16_t* wide() const {
        const Wide* cached = m_buffer->wide.load(std::memory_order_acquire);
        if (cached && cached->units.load(std::memory_order_acquire) >= m_unit_offset + m_units) {
            return cached->data() + m_unit_offset;
        }
        return widen();
    }
    /// Code units [begin, end), clamped to the string. See "Taking pieces".
    JsString substring(size_t begin, size_t end) const;

    /// `a + b`. See "Appending" above.
    static JsString concat(const JsString& a, const JsString& b);
    static JsString concat(const JsString& a, std::string_view b);
    static JsString concat(std::string_view a, const JsString& b);

    /// Orders text the way `<` and `sort()` do: by UTF-16 code unit. Negative,
    /// zero or positive. Comparing the bytes is almost the same thing and is
    /// what this does until the texts differ; it is wrong only for a character
    /// outside the Basic Multilingual Plane, which has the highest bytes in
    /// UTF-8 but is a surrogate pair, below U+E000, in UTF-16. Both arguments
    /// must be text taken from a JsString.
    static int compare(std::string_view a, std::string_view b) noexcept;

    friend bool operator==(const JsString& a, const JsString& b) noexcept {
        if (a.m_bytes != b.m_bytes || a.m_units != b.m_units) return false;
        if (a.m_buffer == b.m_buffer && a.m_offset == b.m_offset) return true;     // the same range of one block
        return a.view() == b.view();
    }
    friend bool operator!=(const JsString& a, const JsString& b) noexcept { return !(a == b); }
    friend bool operator==(const JsString& a, std::string_view b) noexcept { return a.view() == b; }
    friend bool operator!=(const JsString& a, std::string_view b) noexcept { return a.view() != b; }

    /// True when no half of a surrogate pair stands alone in the text, which is
    /// when the bytes are UTF-8 that anything outside the engine will accept.
    bool is_well_formed() const noexcept { return is_ascii() || find_lone_half(view()) == std::string_view::npos; }
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
        uint64_t bytes_copied{0};      // bytes written into blocks or handed out by str()
        uint64_t units_widened{0};     // code units converted to UTF-16
        uint64_t blocks{0};            // blocks allocated
    };
    static Stats stats() noexcept;

private:
    /// The UTF-16 form of the start of a block. `units` code units are ready,
    /// made from the first `bytes` bytes; both only grow, and what is ready
    /// never changes. It has room for anything the block can ever hold, so it
    /// never moves. After the code units comes a table of marks: where in the
    /// block's bytes every kMarkEvery-th code unit is, which is what lets a
    /// piece be found by its index without reading the text before it.
    struct Wide {
        std::atomic<uint32_t> units{0};
        uint32_t bytes{0};
        uint32_t capacity{0};       // code units there is room for
        uint32_t reserved{0};

        char16_t* data() noexcept { return reinterpret_cast<char16_t*>(this + 1); }
        const char16_t* data() const noexcept { return reinterpret_cast<const char16_t*>(this + 1); }
        /// marks()[k] describes code unit k * kMarkEvery: the byte where its
        /// character starts, times two, plus one when the unit is the second
        /// half of that character's surrogate pair.
        uint32_t* marks() noexcept { return reinterpret_cast<uint32_t*>(data() + ((capacity + 1) & ~size_t{1})); }
        const uint32_t* marks() const noexcept { return const_cast<Wide*>(this)->marks(); }
    };
    static constexpr size_t kMarkEvery = 256;
    /// A piece shares its block when it is at least this fraction of the
    /// block's bytes.
    static constexpr size_t kShareDivisor = 4;

    /// The shared block: this header, then `capacity` bytes of text.
    struct Buffer {
        std::atomic<uint32_t> refs{1};
        /// Bytes written so far. Room is claimed by moving this forward
        /// (atomically) before the bytes are written.
        std::atomic<uint32_t> used{0};
        uint32_t capacity{0};
        /// How many times in a row this text has moved to a bigger block
        /// because something was appended to it.
        uint32_t moves{0};
        std::atomic<Wide*> wide{nullptr};

        char* bytes() noexcept { return reinterpret_cast<char*>(this + 1); }
        const char* bytes() const noexcept { return reinterpret_cast<const char*>(this + 1); }
    };

    static Buffer* allocate(size_t capacity);
    static void destroy(Buffer* buffer) noexcept;
    /// A new block holding `first` then `second`, with room for `capacity` bytes.
    static JsString join(std::string_view first, std::string_view second, size_t units, size_t capacity);
    /// A new block holding exactly `utf8`, which is known to be well-formed.
    static JsString from_checked(std::string_view utf8, size_t units);
    /// `a` followed by `more` (well-formed, `more_units` code units, and not a
    /// low surrogate half after a high one).
    static JsString append(const JsString& a, std::string_view more, size_t more_units);
    const char16_t* widen() const;
    /// A handle on bytes [byte_begin, byte_begin + bytes) of this string,
    /// which are its code units [unit_begin, unit_begin + units).
    JsString piece(size_t byte_begin, size_t bytes, size_t unit_begin, size_t units) const noexcept;
    /// Where code unit `unit` (0 < unit < length()) starts, in bytes from the
    /// start of this string; npos when it is the second half of a pair.
    size_t byte_of_unit(size_t unit) const;
    void copy_fields(const JsString& other) noexcept {
        m_buffer = other.m_buffer;
        m_offset = other.m_offset;
        m_bytes = other.m_bytes;
        m_unit_offset = other.m_unit_offset;
        m_units = other.m_units;
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
        m_bytes = 0;
        m_unit_offset = 0;
        m_units = 0;
    }

    Buffer* m_buffer{nullptr};      // null for the empty string
    uint32_t m_offset{0};           // where this string starts in the block, in bytes
    uint32_t m_bytes{0};
    uint32_t m_unit_offset{0};      // how many code units the block holds before it
    uint32_t m_units{0};
};

/// A string addressed by UTF-16 code unit, which is how String.prototype and
/// the regular-expression engine index text. Building one is cheap: an ASCII
/// string is read in place, any other through its cached UTF-16 form. The
/// view keeps the string alive.
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
