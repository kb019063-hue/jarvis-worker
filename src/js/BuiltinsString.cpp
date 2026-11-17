// String, String.prototype and the URI encoding globals.
//
// JavaScript strings are sequences of UTF-16 code units; this engine stores
// UTF-8. The text of a string value is a JsString, which knows its length in
// code units and reads one in constant time, and every index-based method
// here goes through it or through Utf16View. No method copies `this`: a
// method that runs once per character of a long string (charCodeAt in a
// loop) must cost the same whatever the length of the string.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/StringUnicodeTables.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace litebird {

namespace unicode {

namespace {

using namespace tables;

bool is_ascii_u16(std::u16string_view s) {
    for (char16_t c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

std::vector<uint32_t> decode_utf16(std::u16string_view in) {
    std::vector<uint32_t> out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        uint32_t cp = in[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < in.size()) {
            uint32_t lo = in[i + 1];
            if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                ++i;
            }
        }
        out.push_back(cp);
    }
    return out;
}

void append_cp_utf16(std::u16string& out, uint32_t cp) {
    if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char16_t>(cp));
    } else {
        uint32_t u = cp - 0x10000u;
        out.push_back(static_cast<char16_t>(0xD800u + (u >> 10)));
        out.push_back(static_cast<char16_t>(0xDC00u + (u & 0x3FFu)));
    }
}

std::u16string encode_utf16(const std::vector<uint32_t>& cps) {
    std::u16string out;
    out.reserve(cps.size());
    for (uint32_t cp : cps) append_cp_utf16(out, cp);
    return out;
}

bool in_cp_ranges(const CpRange* arr, size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < arr[mid].lo) hi = mid;
        else if (cp > arr[mid].hi) lo = mid + 1;
        else return true;
    }
    return false;
}

const DecompEntry* lookup_decomp(const DecompEntry* arr, size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < arr[mid].cp) hi = mid;
        else if (cp > arr[mid].cp) lo = mid + 1;
        else return &arr[mid];
    }
    return nullptr;
}

uint32_t lookup_compose(uint32_t starter, uint32_t ch) {
    if (starter >= 0x1100u && starter <= 0x1112u && ch >= 0x1161u && ch <= 0x1175u) {
        return 0xAC00u + ((starter - 0x1100u) * 21u + (ch - 0x1161u)) * 28u;
    }
    if (starter >= 0xAC00u && starter <= 0xD7A3u && ((starter - 0xAC00u) % 28u) == 0u &&
        ch >= 0x11A8u && ch <= 0x11C2u) {
        return starter + (ch - 0x11A7u);
    }
    uint64_t key = (static_cast<uint64_t>(starter) << 21) | ch;
    size_t lo = 0, hi = kComposePairsCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (key < kComposePairs[mid].key) hi = mid;
        else if (key > kComposePairs[mid].key) lo = mid + 1;
        else return kComposePairs[mid].composite;
    }
    return 0;
}

const SpecialCaseEntry* lookup_special(const SpecialCaseEntry* arr, size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < arr[mid].cp) hi = mid;
        else if (cp > arr[mid].cp) lo = mid + 1;
        else return &arr[mid];
    }
    return nullptr;
}

uint32_t lookup_case_run(const CaseRun* arr, size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < arr[mid].lo) hi = mid;
        else if (cp > arr[mid].hi) lo = mid + 1;
        else {
            if (((cp - arr[mid].lo) % arr[mid].step) == 0u) {
                return static_cast<uint32_t>(static_cast<int32_t>(cp) + arr[mid].delta);
            }
            return cp;
        }
    }
    return cp;
}

void append_default_upper(std::u16string& out, uint32_t cp) {
    if (const SpecialCaseEntry* sp = lookup_special(kSpecialUpper, kSpecialUpperCount, cp)) {
        for (uint8_t k = 0; k < sp->len; ++k) append_cp_utf16(out, sp->out[k]);
        return;
    }
    append_cp_utf16(out, lookup_case_run(kSimpleUpperRuns, kSimpleUpperRunsCount, cp));
}

void append_default_lower(std::u16string& out, uint32_t cp) {
    if (const SpecialCaseEntry* sp = lookup_special(kSpecialLower, kSpecialLowerCount, cp)) {
        for (uint8_t k = 0; k < sp->len; ++k) append_cp_utf16(out, sp->out[k]);
        return;
    }
    append_cp_utf16(out, lookup_case_run(kSimpleLowerRuns, kSimpleLowerRunsCount, cp));
}

bool is_final_sigma(const std::vector<uint32_t>& cps, size_t idx) {
    bool before = false;
    for (size_t j = idx; j > 0; --j) {
        uint32_t c = cps[j - 1];
        if (is_case_ignorable(c)) continue;
        before = is_cased(c);
        break;
    }
    if (!before) return false;
    for (size_t j = idx + 1; j < cps.size(); ++j) {
        uint32_t c = cps[j];
        if (is_case_ignorable(c)) continue;
        if (is_cased(c)) return false;
        break;
    }
    return true;
}

bool is_before_dot(const std::vector<uint32_t>& cps, size_t idx) {
    for (size_t j = idx + 1; j < cps.size(); ++j) {
        if (cps[j] == 0x0307u) return true;
        uint8_t c = canonical_combining_class(cps[j]);
        if (c == 0 || c == 230) return false;
    }
    return false;
}

bool is_after_i(const std::vector<uint32_t>& cps, size_t idx) {
    for (size_t j = idx; j > 0; --j) {
        uint32_t cp = cps[j - 1];
        if (cp == 0x0049u) return true;
        uint8_t c = canonical_combining_class(cp);
        if (c == 0 || c == 230) return false;
    }
    return false;
}

bool is_more_above(const std::vector<uint32_t>& cps, size_t idx) {
    for (size_t j = idx + 1; j < cps.size(); ++j) {
        uint8_t c = canonical_combining_class(cps[j]);
        if (c == 230) return true;
        if (c == 0) return false;
    }
    return false;
}

bool is_after_soft_dotted(const std::vector<uint32_t>& cps, size_t idx) {
    for (size_t j = idx; j > 0; --j) {
        uint32_t cp = cps[j - 1];
        if (is_soft_dotted(cp)) return true;
        uint8_t c = canonical_combining_class(cp);
        if (c == 0 || c == 230) return false;
    }
    return false;
}

// Greek (el) locale uppercasing (CLDR / ICU 78.2 state machine): strips Greek
// diacritics on Greek letters, preserves dialytika, inserts dialytika on the
// second vowel (Ι/Υ) of a formerly accented diphthong without dialytika,
// maps each ypogegrammeni to spacing Ι, and preserves tonos on standalone
// disjunctive ή -> Ή using Final_Sigma word-boundary rules.
bool is_greek_accent(uint32_t cp) {
    return cp == 0x0300u || cp == 0x0301u || cp == 0x0342u;
}

bool is_greek_diacritic(uint32_t cp) {
    switch (cp) {
        case 0x0300u: case 0x0301u: case 0x0304u: case 0x0306u: case 0x0308u:
        case 0x0311u: case 0x0313u: case 0x0314u: case 0x0342u: case 0x0343u:
        case 0x0344u: case 0x0345u:
            return true;
        default:
            return false;
    }
}

bool is_greek_vowel_base(uint32_t base) {
    switch (base) {
        case 0x0391u: case 0x0395u: case 0x0397u: case 0x0399u:
        case 0x039Fu: case 0x03A5u: case 0x03A9u:
            return true;
        default:
            return false;
    }
}

struct GreekLetterInfo {
    uint32_t base = 0;
    bool has_vowel = false;
    bool has_accent = false;
    bool has_dial = false;
    bool has_ypo = false;
};

bool analyze_greek_letter(uint32_t cp, GreekLetterInfo& info) {
    // Greek & Coptic (excluding Coptic 03E2..03EF) + Greek Extended + Ohm sign
    if (cp >= 0x03E2u && cp <= 0x03EFu) return false;
    if (!((cp >= 0x0370u && cp <= 0x03FFu) || (cp >= 0x1F00u && cp <= 0x1FFFu) || cp == 0x2126u)) {
        return false;
    }
    if (!is_cased(cp) && cp != 0x037Au && cp != 0x03D2u && cp != 0x03D3u && cp != 0x03D4u) {
        return false;
    }
    uint32_t first = cp;
    bool has_accent = false;
    bool has_dial = false;
    bool has_ypo = false;
    if (const DecompEntry* d = lookup_decomp(kCanonDecomp, kCanonDecompCount, cp)) {
        first = kDecompPool[d->offset];
        for (uint8_t k = 1; k < d->len; ++k) {
            uint32_t m = kDecompPool[d->offset + k];
            if (is_greek_accent(m)) has_accent = true;
            else if (m == 0x0308u) has_dial = true;
            else if (m == 0x0345u) has_ypo = true;
        }
    }
    uint32_t base = lookup_case_run(kSimpleUpperRuns, kSimpleUpperRunsCount, first);
    info.base = base;
    info.has_vowel = is_greek_vowel_base(base);
    info.has_accent = has_accent;
    info.has_dial = has_dial;
    info.has_ypo = has_ypo;
    return true;
}

bool is_followed_by_cased(const std::vector<uint32_t>& cps, size_t start) {
    for (size_t k = start; k < cps.size(); ++k) {
        if (is_case_ignorable(cps[k])) continue;
        return is_cased(cps[k]);
    }
    return false;
}

std::u16string to_upper_greek_utf16(const std::vector<uint32_t>& cps) {
    constexpr uint32_t kAfterCased = 1u;
    constexpr uint32_t kAfterVowelPreAccent = 2u;
    constexpr uint32_t kAfterVowelCombAccent = 4u;

    std::u16string out;
    out.reserve(cps.size());
    uint32_t state = 0;
    const size_t n = cps.size();
    for (size_t i = 0; i < n;) {
        uint32_t cp = cps[i];
        uint32_t next_state = 0;
        if (is_case_ignorable(cp)) {
            next_state |= (state & kAfterCased);
        } else if (is_cased(cp)) {
            next_state |= kAfterCased;
        }

        GreekLetterInfo info;
        if (!analyze_greek_letter(cp, info)) {
            append_default_upper(out, cp);
            ++i;
            state = next_state;
            continue;
        }

        uint32_t upper = info.base;
        bool has_dial = info.has_dial;
        bool has_comb_dial = false;
        const bool has_precomp_accent = info.has_accent;
        bool has_any_accent = has_precomp_accent;
        size_t num_ypo = info.has_ypo ? 1u : 0u;

        if (info.has_vowel &&
            (state & (kAfterVowelPreAccent | kAfterVowelCombAccent)) != 0u &&
            (upper == 0x0399u || upper == 0x03A5u)) {
            if (state & kAfterVowelPreAccent) has_dial = true;
            else has_comb_dial = true;
        }

        size_t j = i + 1;
        while (j < n && is_greek_diacritic(cps[j])) {
            uint32_t m = cps[j];
            if (m == 0x0308u) has_comb_dial = true;
            else if (is_greek_accent(m)) has_any_accent = true;
            else if (m == 0x0344u) { has_comb_dial = true; has_any_accent = true; }
            else if (m == 0x0345u) ++num_ypo;
            ++j;
        }

        if (info.has_vowel && has_any_accent && !has_dial && !has_comb_dial) {
            next_state |= has_precomp_accent ? kAfterVowelPreAccent : kAfterVowelCombAccent;
        }

        bool add_tonos = false;
        if (upper == 0x0397u && has_any_accent && num_ypo == 0u &&
            (state & kAfterCased) == 0u && !is_followed_by_cased(cps, j)) {
            if (has_precomp_accent) {
                upper = 0x0389u;
            } else {
                add_tonos = true;
            }
        } else if (has_dial) {
            if (upper == 0x0399u) {
                upper = 0x03AAu;
                has_dial = false;
                has_comb_dial = false;
            } else if (upper == 0x03A5u) {
                upper = 0x03ABu;
                has_dial = false;
                has_comb_dial = false;
            }
        }

        append_cp_utf16(out, upper);
        if (has_dial || has_comb_dial) append_cp_utf16(out, 0x0308u);
        if (add_tonos) append_cp_utf16(out, 0x0301u);
        for (size_t k = 0; k < num_ypo; ++k) append_cp_utf16(out, 0x0399u);

        i = j;
        state = next_state;
    }
    return out;
}

// ---- UCA 17.0 / CLDR 48 Root Collation ------------------------------------

bool is_contract_starter(uint32_t cp) {
    size_t lo = 0, hi = kCollationContractStartersCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < kCollationContractStarters[mid]) hi = mid;
        else if (cp > kCollationContractStarters[mid]) lo = mid + 1;
        else return true;
    }
    return false;
}

const CollationContraction* lookup_contraction(uint32_t cp0, uint32_t cp1, uint32_t cp2) {
    size_t lo = 0, hi = kCollationContractionsCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const auto& e = kCollationContractions[mid];
        if (cp0 != e.cp0) {
            if (cp0 < e.cp0) hi = mid; else lo = mid + 1;
        } else if (cp1 != e.cp1) {
            if (cp1 < e.cp1) hi = mid; else lo = mid + 1;
        } else if (cp2 != e.cp2) {
            if (cp2 < e.cp2) hi = mid; else lo = mid + 1;
        } else {
            return &e;
        }
    }
    return nullptr;
}

bool is_cjk_compat_unified(uint32_t cp) {
    switch (cp) {
        case 0xFA0Eu: case 0xFA0Fu: case 0xFA11u: case 0xFA13u:
        case 0xFA14u: case 0xFA1Fu: case 0xFA21u: case 0xFA23u:
        case 0xFA24u: case 0xFA27u: case 0xFA28u: case 0xFA29u:
            return true;
        default:
            return false;
    }
}

bool is_other_han(uint32_t cp) {
    return (cp >= 0x3400u && cp <= 0x4DBFu) ||
           (cp >= 0x20000u && cp <= 0x2A6DFu) ||
           (cp >= 0x2A700u && cp <= 0x2B73Fu) ||
           (cp >= 0x2B740u && cp <= 0x2B81Du) ||
           (cp >= 0x2B820u && cp <= 0x2CEADu) ||
           (cp >= 0x2CEB0u && cp <= 0x2EBE0u) ||
           (cp >= 0x2EBF0u && cp <= 0x2EE5Du) ||
           (cp >= 0x30000u && cp <= 0x3134Au) ||
           (cp >= 0x31350u && cp <= 0x323AFu) ||
           (cp >= 0x323B0u && cp <= 0x33479u);
}

void append_single_cp_ces(std::vector<CollationCE>& out, uint32_t cp) {
    // 1. Single-CE run table
    size_t lo = 0, hi = kCollationRunsCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const auto& r = kCollationRuns[mid];
        if (cp < r.cp_lo) {
            hi = mid;
        } else if (cp > r.cp_lo + r.count_minus_1) {
            lo = mid + 1;
        } else {
            int32_t off = static_cast<int32_t>(cp - r.cp_lo);
            uint16_t p = static_cast<uint16_t>(static_cast<int32_t>(r.p_start) + off * static_cast<int32_t>(r.p_step));
            out.push_back({p, r.s, r.t_var});
            return;
        }
    }
    // 2. Multi-CE single-CP table
    lo = 0;
    hi = kCollationMultiCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < kCollationMulti[mid].cp) hi = mid;
        else if (cp > kCollationMulti[mid].cp) lo = mid + 1;
        else {
            const auto& m = kCollationMulti[mid];
            for (uint8_t k = 0; k < m.len; ++k) out.push_back(kCollationCEPool[m.offset + k]);
            return;
        }
    }
    // 3. UCA 17.0 Implicit Weights (7 groups matching FractionalUCA / CLDR 48)
    uint16_t aaaa = 0;
    uint16_t bbbb = 0;
    if ((cp >= 0x17000u && cp <= 0x187FFu) || (cp >= 0x18D00u && cp <= 0x18D7Fu)) {
        aaaa = 0xFB00u;
        bbbb = static_cast<uint16_t>((cp - 0x17000u) | 0x8000u);
    } else if ((cp >= 0x18800u && cp <= 0x18AFFu) || (cp >= 0x18D80u && cp <= 0x18DFFu)) {
        aaaa = 0xFB01u;
        bbbb = static_cast<uint16_t>((cp - 0x18800u) | 0x8000u);
    } else if (cp >= 0x1B170u && cp <= 0x1B2FFu) {
        aaaa = 0xFB02u;
        bbbb = static_cast<uint16_t>((cp - 0x1B170u) | 0x8000u);
    } else if (cp >= 0x18B00u && cp <= 0x18CFFu) {
        aaaa = 0xFB03u;
        bbbb = static_cast<uint16_t>((cp - 0x18B00u) | 0x8000u);
    } else if ((cp >= 0x4E00u && cp <= 0x9FFFu) || is_cjk_compat_unified(cp)) {
        aaaa = static_cast<uint16_t>(0xFB40u + (cp >> 15));
        bbbb = static_cast<uint16_t>((cp & 0x7FFFu) | 0x8000u);
    } else if (is_other_han(cp)) {
        aaaa = static_cast<uint16_t>(0xFB80u + (cp >> 15));
        bbbb = static_cast<uint16_t>((cp & 0x7FFFu) | 0x8000u);
    } else {
        aaaa = static_cast<uint16_t>(0xFBC0u + (cp >> 15));
        bbbb = static_cast<uint16_t>((cp & 0x7FFFu) | 0x8000u);
    }
    out.push_back({aaaa, 0x0020u, 0x0002u});
    out.push_back({bbbb, 0x0000u, 0x0000u});
}

struct SortKey {
    std::vector<uint32_t> l1;
    std::vector<uint16_t> l2;
    std::vector<uint16_t> l3;
};

constexpr uint32_t kDigitZeroPrimary = 0x21E6u;

bool is_upper_tertiary(uint8_t t) {
    return t >= 0x08u && t <= 0x12u;
}

void push_ce_to_key(SortKey& key, CollationCE ce, const CollationOptions& opts, bool& after_variable) {
    bool is_var = (ce.t_var & 0x80u) != 0u;
    uint8_t t = static_cast<uint8_t>(ce.t_var & 0x7Fu);
    if (opts.ignore_punctuation) {
        if (is_var) {
            after_variable = true;
            return;
        }
        if (after_variable && ce.p == 0) {
            return;
        }
        if (ce.p != 0) after_variable = false;
    }
    if (ce.p != 0) key.l1.push_back(ce.p);
    if (opts.sensitivity == CollationSensitivity::Accent || opts.sensitivity == CollationSensitivity::Variant) {
        if (ce.s != 0) key.l2.push_back(ce.s);
    }
    if (opts.sensitivity == CollationSensitivity::Case) {
        if (ce.p != 0 && t != 0) {
            bool up = is_upper_tertiary(t);
            uint16_t w = (opts.case_first == 1) ? (up ? 1u : 2u) : (up ? 2u : 1u);
            key.l3.push_back(w);
        }
    } else if (opts.sensitivity == CollationSensitivity::Variant) {
        if (t != 0) {
            if (opts.case_first == 1) {
                uint16_t bucket = is_upper_tertiary(t) ? 0u : 1u;
                key.l3.push_back(static_cast<uint16_t>((bucket << 8) | t));
            } else {
                key.l3.push_back(t);
            }
        }
    }
}

SortKey build_sort_key(std::u16string_view in, const CollationOptions& opts) {
    std::u16string nfd_buf;
    std::u16string_view src = in;
    if (!is_ascii_u16(in) && normalize_utf16(in.data(), in.size(), NormForm::NFD, nfd_buf)) {
        src = nfd_buf;
    }
    std::vector<uint32_t> cps = decode_utf16(src);
    std::vector<uint8_t> consumed(cps.size(), 0);

    SortKey key;
    key.l1.reserve(cps.size());
    bool after_variable = false;
    std::vector<CollationCE> ce_buf;
    ce_buf.reserve(8);

    const size_t n = cps.size();
    for (size_t i = 0; i < n; ++i) {
        if (consumed[i]) continue;
        uint32_t cp0 = cps[i];

        // Numeric collation: contiguous ASCII decimal digits compare by integer value
        if (opts.numeric && cp0 >= '0' && cp0 <= '9') {
            size_t j = i;
            while (j < n && !consumed[j] && cps[j] >= '0' && cps[j] <= '9') ++j;
            size_t sig = i;
            while (sig + 1 < j && cps[sig] == '0') ++sig;
            uint32_t num_len = static_cast<uint32_t>(j - sig);
            after_variable = false;
            key.l1.push_back(kDigitZeroPrimary);
            key.l1.push_back(num_len);
            for (size_t k = sig; k < j; ++k) {
                key.l1.push_back(cps[k] - '0');
                if (opts.sensitivity == CollationSensitivity::Accent || opts.sensitivity == CollationSensitivity::Variant) {
                    key.l2.push_back(0x0020u);
                }
                if (opts.sensitivity == CollationSensitivity::Variant) {
                    key.l3.push_back(opts.case_first == 1 ? 0x0102u : 0x0002u);
                } else if (opts.sensitivity == CollationSensitivity::Case) {
                    key.l3.push_back(opts.case_first == 1 ? 2u : 1u);
                }
            }
            i = j - 1;
            continue;
        }

        ce_buf.clear();
        const CollationContraction* matched = nullptr;
        if (is_contract_starter(cp0)) {
            // Find next two unconsumed indices for contiguous 3-CP / 2-CP checks
            size_t i1 = i + 1;
            while (i1 < n && consumed[i1]) ++i1;
            size_t i2 = (i1 < n) ? i1 + 1 : n;
            while (i2 < n && consumed[i2]) ++i2;

            size_t cur_len = 1;
            uint32_t m1 = 0xFFFFFFFFu;
            size_t scan_start = i1;
            if (i2 < n) {
                if (const CollationContraction* c3 = lookup_contraction(cp0, cps[i1], cps[i2])) {
                    matched = c3;
                    consumed[i1] = 1;
                    consumed[i2] = 1;
                    cur_len = 3;
                }
            }
            if (!matched && i1 < n) {
                if (const CollationContraction* c2 = lookup_contraction(cp0, cps[i1], 0xFFFFFFFFu)) {
                    matched = c2;
                    consumed[i1] = 1;
                    cur_len = 2;
                    m1 = cps[i1];
                    scan_start = i2;
                }
            }

            // UCA S2.1.1-S2.1.3: Discontiguous contractions across non-blocked combining marks
            if (cur_len < 3) {
                int last_ccc = -1;
                for (size_t j = scan_start; j < n; ++j) {
                    if (consumed[j]) continue;
                    uint8_t c_ccc = canonical_combining_class(cps[j]);
                    if (c_ccc == 0) break;
                    if (last_ccc < static_cast<int>(c_ccc)) {
                        const CollationContraction* ext =
                            (cur_len == 1) ? lookup_contraction(cp0, cps[j], 0xFFFFFFFFu)
                                           : lookup_contraction(cp0, m1, cps[j]);
                        if (ext) {
                            matched = ext;
                            consumed[j] = 1;
                            if (cur_len == 1) {
                                m1 = cps[j];
                                cur_len = 2;
                            } else {
                                cur_len = 3;
                                break;
                            }
                            continue;
                        }
                    }
                    last_ccc = c_ccc;
                }
            }
        }

        if (matched) {
            for (uint8_t k = 0; k < matched->len; ++k) {
                ce_buf.push_back(kCollationCEPool[matched->offset + k]);
            }
        } else {
            append_single_cp_ces(ce_buf, cp0);
        }
        for (const auto& ce : ce_buf) {
            push_ce_to_key(key, ce, opts, after_variable);
        }
    }
    return key;
}

template <typename T>
int compare_vec(const std::vector<T>& a, const std::vector<T>& b) {
    size_t len = std::min(a.size(), b.size());
    for (size_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

} // namespace

uint8_t canonical_combining_class(uint32_t cp) {
    if (cp < 0x0300u) return 0;
    size_t lo = 0, hi = tables::kCccRangesCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < tables::kCccRanges[mid].lo) hi = mid;
        else if (cp > tables::kCccRanges[mid].hi) lo = mid + 1;
        else return tables::kCccRanges[mid].ccc;
    }
    return 0;
}

bool is_cased(uint32_t cp) {
    return in_cp_ranges(tables::kCasedRanges, tables::kCasedRangesCount, cp);
}

bool is_case_ignorable(uint32_t cp) {
    return in_cp_ranges(tables::kCaseIgnorableRanges, tables::kCaseIgnorableRangesCount, cp);
}

bool is_soft_dotted(uint32_t cp) {
    return in_cp_ranges(tables::kSoftDottedRanges, tables::kSoftDottedRangesCount, cp);
}

bool normalize_utf16(const char16_t* data, size_t len, NormForm form, std::u16string& out) {
    std::u16string_view in(data, len);
    if (in.empty() || is_ascii_u16(in)) return false;
    const bool compat = (form == NormForm::NFKC || form == NormForm::NFKD);
    const bool compose = (form == NormForm::NFC || form == NormForm::NFKC);

    std::vector<uint32_t> buf;
    buf.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size(); ++i) {
        uint32_t cp = in[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < in.size()) {
            uint32_t lo = in[i + 1];
            if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                ++i;
            }
        }
        if (cp >= 0xAC00u && cp <= 0xD7A3u) {
            uint32_t si = cp - 0xAC00u;
            buf.push_back(0x1100u + si / 588u);
            buf.push_back(0x1161u + (si % 588u) / 28u);
            if (si % 28u) buf.push_back(0x11A7u + (si % 28u));
        } else {
            const tables::DecompEntry* d =
                compat ? lookup_decomp(tables::kCompatDecomp, tables::kCompatDecompCount, cp) : nullptr;
            if (!d) d = lookup_decomp(tables::kCanonDecomp, tables::kCanonDecompCount, cp);
            if (d) {
                for (uint8_t k = 0; k < d->len; ++k) buf.push_back(tables::kDecompPool[d->offset + k]);
            } else {
                buf.push_back(cp);
            }
        }
    }

    // Canonical Ordering Algorithm (UAX #15)
    for (size_t i = 0; i < buf.size();) {
        if (canonical_combining_class(buf[i]) == 0) {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < buf.size() && canonical_combining_class(buf[j]) != 0) ++j;
        if (j - i > 1) {
            std::stable_sort(buf.begin() + static_cast<std::ptrdiff_t>(i),
                             buf.begin() + static_cast<std::ptrdiff_t>(j),
                             [](uint32_t a, uint32_t b) {
                                 return canonical_combining_class(a) < canonical_combining_class(b);
                             });
        }
        i = j;
    }

    // Canonical Composition Algorithm (UAX #15)
    if (compose) {
        size_t w = 0;
        size_t starter_idx = std::string::npos;
        int last_ccc = -1;
        for (size_t i = 0; i < buf.size(); ++i) {
            uint32_t ch = buf[i];
            uint8_t c_ccc = canonical_combining_class(ch);
            if (starter_idx != std::string::npos && (last_ccc == -1 || last_ccc < static_cast<int>(c_ccc))) {
                uint32_t comp = lookup_compose(buf[starter_idx], ch);
                if (comp != 0) {
                    buf[starter_idx] = comp;
                    continue;
                }
            }
            buf[w] = ch;
            if (c_ccc == 0) {
                starter_idx = w;
                last_ccc = -1;
            } else {
                last_ccc = c_ccc;
            }
            ++w;
        }
        buf.resize(w);
    }

    out = encode_utf16(buf);
    return out != in;
}

std::u16string to_lower_utf16(const char16_t* data, size_t len, CaseLocale locale) {
    std::u16string_view in(data, len);
    if (in.empty()) return {};
    if (is_ascii_u16(in)) {
        std::u16string out;
        out.reserve(in.size());
        for (char16_t c : in) {
            if (c == 'I' && locale == CaseLocale::Turkish) out.push_back(0x0131);
            else if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char16_t>(c + 32));
            else out.push_back(c);
        }
        return out;
    }
    std::vector<uint32_t> cps = decode_utf16(in);
    std::u16string out;
    out.reserve(in.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        uint32_t cp = cps[i];
        if (locale == CaseLocale::Turkish) {
            if (cp == 0x0130u) {
                out.push_back('i');
                continue;
            }
            if (cp == 0x0307u && is_after_i(cps, i)) {
                continue;
            }
            if (cp == 0x0049u && !is_before_dot(cps, i)) {
                out.push_back(0x0131);
                continue;
            }
        } else if (locale == CaseLocale::Lithuanian) {
            if (cp == 0x00CCu) {
                out.push_back('i'); out.push_back(0x0307); out.push_back(0x0300);
                continue;
            }
            if (cp == 0x00CDu) {
                out.push_back('i'); out.push_back(0x0307); out.push_back(0x0301);
                continue;
            }
            if (cp == 0x0128u) {
                out.push_back('i'); out.push_back(0x0307); out.push_back(0x0303);
                continue;
            }
            if ((cp == 0x0049u || cp == 0x004Au || cp == 0x012Eu) && is_more_above(cps, i)) {
                out.push_back(cp == 0x0049u ? u'i' : cp == 0x004Au ? u'j' : static_cast<char16_t>(0x012F));
                out.push_back(0x0307);
                continue;
            }
        }
        if (cp == 0x03A3u && is_final_sigma(cps, i)) {
            out.push_back(0x03C2);
            continue;
        }
        append_default_lower(out, cp);
    }
    return out;
}

std::u16string to_upper_utf16(const char16_t* data, size_t len, CaseLocale locale) {
    std::u16string_view in(data, len);
    if (in.empty()) return {};
    if (is_ascii_u16(in)) {
        std::u16string out;
        out.reserve(in.size());
        for (char16_t c : in) {
            if (c == 'i' && locale == CaseLocale::Turkish) out.push_back(0x0130);
            else if (c >= 'a' && c <= 'z') out.push_back(static_cast<char16_t>(c - 32));
            else out.push_back(c);
        }
        return out;
    }
    std::vector<uint32_t> cps = decode_utf16(in);
    if (locale == CaseLocale::Greek) {
        return to_upper_greek_utf16(cps);
    }
    std::u16string out;
    out.reserve(in.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        uint32_t cp = cps[i];
        if (locale == CaseLocale::Turkish && cp == 0x0069u) {
            out.push_back(0x0130);
            continue;
        }
        if (locale == CaseLocale::Lithuanian && cp == 0x0307u && is_after_soft_dotted(cps, i)) {
            continue;
        }
        append_default_upper(out, cp);
    }
    return out;
}

int locale_compare_utf16(const char16_t* a_data, size_t a_len,
                         const char16_t* b_data, size_t b_len,
                         const CollationOptions& opts) {
    std::u16string_view a(a_data, a_len);
    std::u16string_view b(b_data, b_len);
    if (a == b) return 0;
    SortKey ka = build_sort_key(a, opts);
    SortKey kb = build_sort_key(b, opts);
    if (int c = compare_vec(ka.l1, kb.l1); c != 0) return c;
    if (opts.sensitivity == CollationSensitivity::Accent || opts.sensitivity == CollationSensitivity::Variant) {
        if (int c = compare_vec(ka.l2, kb.l2); c != 0) return c;
    }
    if (opts.sensitivity == CollationSensitivity::Case || opts.sensitivity == CollationSensitivity::Variant) {
        if (int c = compare_vec(ka.l3, kb.l3); c != 0) return c;
    }
    return 0;
}

} // namespace unicode

namespace {

using Args = const std::vector<JsValue>&;

/// `this` as a string. Shares the text; nothing is copied.
JsString this_string(const char* method) {
    const JsValue me = JsValue::current_this();
    if (me.type == JsType::String) return me.string_val;
    if (me.is_nullish()) {
        js_throw_type_error(std::string("String.prototype.") + method + " called on null or undefined");
    }
    // A String object, as in `new String("a").toUpperCase()`.
    if (const JsValue* inner = js_wrapped_primitive(me); inner && inner->type == JsType::String) {
        return inner->string_val;
    }
    return js_to_js_string(me);
}

// ---- ECMA-402 BCP-47 Locale Validation & Options --------------------------

bool is_ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
bool is_ascii_alnum(char c) { return is_ascii_alpha(c) || is_ascii_digit(c); }

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return out;
}

bool is_all_alpha(std::string_view s) {
    for (char c : s) if (!is_ascii_alpha(c)) return false;
    return !s.empty();
}

bool is_all_digit(std::string_view s) {
    for (char c : s) if (!is_ascii_digit(c)) return false;
    return !s.empty();
}

bool is_all_alnum(std::string_view s) {
    for (char c : s) if (!is_ascii_alnum(c)) return false;
    return !s.empty();
}

bool is_valid_variant_subtag(std::string_view s) {
    if (s.size() >= 5 && s.size() <= 8 && is_all_alnum(s)) return true;
    if (s.size() == 4 && is_ascii_digit(s[0]) && is_all_alnum(s)) return true;
    return false;
}

bool is_structurally_valid_bcp47_tag(std::string_view tag) {
    if (tag.empty() || tag.front() == '-' || tag.back() == '-') return false;
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= tag.size(); ++i) {
        if (i == tag.size() || tag[i] == '-') {
            if (i == start) return false;
            parts.push_back(ascii_lower(tag.substr(start, i - start)));
            start = i + 1;
        } else if (!is_ascii_alnum(tag[i])) {
            return false;
        }
    }
    std::string lower_tag = ascii_lower(tag);
    static const char* const kGrandfathered[] = {
        "art-lojban", "cel-gaulish", "en-gb-oed", "i-ami", "i-bnn", "i-default",
        "i-enochian", "i-hak", "i-klingon", "i-lux", "i-mingo", "i-navajo",
        "i-pwn", "i-tao", "i-tay", "i-tsu", "no-bok", "no-nyn", "sgn-be-fr",
        "sgn-be-nl", "sgn-ch-de", "zh-guoyu", "zh-hakka", "zh-min", "zh-min-nan", "zh-xiang",
    };
    for (const char* gf : kGrandfathered) {
        if (lower_tag == gf) return true;
    }

    size_t idx = 0;
    const size_t n = parts.size();
    // Primary language: 2..3 or 5..8 alpha
    if (idx >= n || !is_all_alpha(parts[idx]) ||
        !((parts[idx].size() >= 2 && parts[idx].size() <= 3) || (parts[idx].size() >= 5 && parts[idx].size() <= 8))) {
        return false;
    }
    const bool allow_extlang = (parts[idx].size() <= 3);
    ++idx;

    // Optional extlang (up to 3 subtags of 3 alpha, only after 2-3 alpha language)
    if (allow_extlang) {
        size_t ext_count = 0;
        while (idx < n && ext_count < 3 && parts[idx].size() == 3 && is_all_alpha(parts[idx])) {
            ++idx;
            ++ext_count;
        }
    }
    // Optional script (4 alpha)
    if (idx < n && parts[idx].size() == 4 && is_all_alpha(parts[idx])) ++idx;
    // Optional region (2 alpha or 3 digit)
    if (idx < n && ((parts[idx].size() == 2 && is_all_alpha(parts[idx])) ||
                    (parts[idx].size() == 3 && is_all_digit(parts[idx])))) {
        ++idx;
    }
    // Zero or more unique variants
    std::unordered_set<std::string> seen_variants;
    while (idx < n && is_valid_variant_subtag(parts[idx])) {
        if (!seen_variants.insert(parts[idx]).second) return false;
        ++idx;
    }
    // Extensions (unique singletons != 'x')
    std::unordered_set<char> seen_singletons;
    while (idx < n && parts[idx].size() == 1 && parts[idx][0] != 'x') {
        char singleton = parts[idx][0];
        if (!seen_singletons.insert(singleton).second) return false;
        ++idx;
        size_t ext_subtags = 0;
        while (idx < n && parts[idx].size() >= 2 && parts[idx].size() <= 8 && is_all_alnum(parts[idx])) {
            ++idx;
            ++ext_subtags;
        }
        if (ext_subtags == 0) return false;
    }
    // Optional privateuse (-x-...)
    if (idx < n && parts[idx] == "x") {
        ++idx;
        size_t pu_subtags = 0;
        while (idx < n && parts[idx].size() >= 1 && parts[idx].size() <= 8 && is_all_alnum(parts[idx])) {
            ++idx;
            ++pu_subtags;
        }
        if (pu_subtags == 0) return false;
    }
    return idx == n;
}

std::vector<std::string> canonicalize_locale_list(const JsValue& locales) {
    std::vector<std::string> seen;
    if (locales.is_undefined()) return seen;
    if (locales.type == JsType::String) {
        std::string tag = locales.string_val.str();
        if (!is_structurally_valid_bcp47_tag(tag)) {
            js_throw_range_error("Invalid language tag: " + tag);
        }
        seen.push_back(ascii_lower(tag));
        return seen;
    }
    JsValue obj = js_to_object(locales);
    double raw_len = js_to_integer(js_to_number(obj.get_property("length")));
    if (raw_len <= 0) return seen;
    size_t len = static_cast<size_t>(std::min(raw_len, 4294967295.0));
    std::unordered_set<std::string> dedup;
    for (size_t k = 0; k < len; ++k) {
        std::string pk = std::to_string(k);
        if (!obj.has_property(pk)) continue;
        JsValue k_val = obj.get_property(pk);
        if (k_val.type != JsType::String && !k_val.is_object_like()) {
            js_throw_type_error("Language ID should be string or object");
        }
        std::string tag = js_to_string(k_val);
        if (!is_structurally_valid_bcp47_tag(tag)) {
            js_throw_range_error("Invalid language tag: " + tag);
        }
        std::string canon = ascii_lower(tag);
        if (dedup.insert(canon).second) seen.push_back(std::move(canon));
    }
    return seen;
}

unicode::CaseLocale resolve_case_locale(const JsValue& locales) {
    std::vector<std::string> list = canonicalize_locale_list(locales);
    if (list.empty()) return unicode::CaseLocale::Default;
    std::string_view primary = list[0];
    if (size_t dash = primary.find('-'); dash != std::string_view::npos) {
        primary = primary.substr(0, dash);
    }
    if (primary == "tr" || primary == "az") return unicode::CaseLocale::Turkish;
    if (primary == "lt") return unicode::CaseLocale::Lithuanian;
    if (primary == "el") return unicode::CaseLocale::Greek;
    return unicode::CaseLocale::Default;
}

void apply_unicode_ext_collation(std::string_view locale_tag, unicode::CollationOptions& opts) {
    size_t pos = locale_tag.find("-u-");
    if (pos == std::string_view::npos) return;
    std::string_view ext = locale_tag.substr(pos + 3);
    if (size_t x = ext.find("-x-"); x != std::string_view::npos) ext = ext.substr(0, x);
    std::vector<std::string_view> sub;
    size_t start = 0;
    for (size_t i = 0; i <= ext.size(); ++i) {
        if (i == ext.size() || ext[i] == '-') {
            sub.push_back(ext.substr(start, i - start));
            start = i + 1;
        }
    }
    for (size_t i = 0; i < sub.size(); ++i) {
        if (sub[i].size() == 1) break; // next extension singleton
        if (sub[i] == "kn") {
            if (i + 1 < sub.size() && sub[i + 1].size() >= 3) {
                opts.numeric = (sub[i + 1] != "false");
                ++i;
            } else {
                opts.numeric = true;
            }
        } else if (sub[i] == "kf" && i + 1 < sub.size() && sub[i + 1].size() >= 3) {
            if (sub[i + 1] == "upper") opts.case_first = 1;
            else if (sub[i + 1] == "lower") opts.case_first = 2;
            else if (sub[i + 1] == "false") opts.case_first = 0;
            ++i;
        }
    }
}

unicode::CollationOptions parse_collation_args(const JsValue& locales, const JsValue& options) {
    std::vector<std::string> list = canonicalize_locale_list(locales);
    unicode::CollationOptions opts;
    if (!list.empty()) apply_unicode_ext_collation(list[0], opts);
    if (options.is_undefined()) return opts;
    if (!options.is_object_like()) {
        js_throw_type_error("String.prototype.localeCompare options must be an object");
    }
    bool search_usage = false;
    JsValue usage = options.get_property("usage");
    if (!usage.is_undefined()) {
        std::string u = js_to_string(usage);
        if (u == "search") search_usage = true;
        else if (u != "sort") js_throw_range_error("Invalid usage option: " + u);
    }
    JsValue matcher = options.get_property("localeMatcher");
    if (!matcher.is_undefined()) {
        std::string m = js_to_string(matcher);
        if (m != "lookup" && m != "best fit") js_throw_range_error("Invalid localeMatcher option: " + m);
    }
    JsValue numeric = options.get_property("numeric");
    if (!numeric.is_undefined()) opts.numeric = numeric.is_truthy();

    JsValue cf = options.get_property("caseFirst");
    if (!cf.is_undefined()) {
        std::string s = js_to_string(cf);
        if (s == "upper") opts.case_first = 1;
        else if (s == "lower") opts.case_first = 2;
        else if (s == "false") opts.case_first = 0;
        else js_throw_range_error("Invalid caseFirst option: " + s);
    }
    JsValue sens = options.get_property("sensitivity");
    if (!sens.is_undefined()) {
        std::string s = js_to_string(sens);
        if (s == "base") opts.sensitivity = unicode::CollationSensitivity::Base;
        else if (s == "accent") opts.sensitivity = unicode::CollationSensitivity::Accent;
        else if (s == "case") opts.sensitivity = unicode::CollationSensitivity::Case;
        else if (s == "variant") opts.sensitivity = unicode::CollationSensitivity::Variant;
        else js_throw_range_error("Invalid sensitivity option: " + s);
    } else if (search_usage) {
        opts.sensitivity = unicode::CollationSensitivity::Base;
    }
    JsValue ip = options.get_property("ignorePunctuation");
    if (!ip.is_undefined()) opts.ignore_punctuation = ip.is_truthy();
    return opts;
}

/// `this`, addressed by UTF-16 code unit.
Utf16View this_units(const char* method) { return Utf16View(this_string(method)); }

/// ToString(args[index]), addressed by UTF-16 code unit.
Utf16View arg_units(Args args, size_t index) { return Utf16View(js_to_js_string(js_arg(args, index))); }

/// ToIntegerOrInfinity clamped into [0, length].
size_t clamp_index(const JsValue& arg, size_t length, size_t fallback) {
    if (arg.is_undefined()) return fallback;
    double k = js_to_integer(js_to_number(arg));
    if (k < 0) return 0;
    if (k > static_cast<double>(length)) return length;
    return static_cast<size_t>(k);
}

/// Relative index: negative values count from the end.
size_t relative_index(const JsValue& arg, size_t length, size_t fallback) {
    if (arg.is_undefined()) return fallback;
    double k = js_to_integer(js_to_number(arg));
    if (k < 0) {
        k += static_cast<double>(length);
        return k < 0 ? 0 : static_cast<size_t>(k);
    }
    return k > static_cast<double>(length) ? length : static_cast<size_t>(k);
}

bool is_space_unit(char16_t c) {
    switch (c) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0:
        case 0x1680: case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;
    }
}

JsString trim(const JsString& s, bool start, bool end) {
    Utf16View view(s);
    size_t b = 0;
    size_t e = view.size();
    if (start) while (b < e && is_space_unit(view[b])) ++b;
    if (end) while (e > b && is_space_unit(view[e - 1])) --e;
    return view.slice(b, e);
}

JsString change_case(const JsString& s, bool upper, unicode::CaseLocale locale = unicode::CaseLocale::Default) {
    if (s.is_ascii() && locale != unicode::CaseLocale::Turkish) {
        const std::string_view text = s.view();
        // Text that is already in the case asked for is the same string.
        size_t first = 0;
        while (first < text.size() && !(upper ? (text[first] >= 'a' && text[first] <= 'z')
                                              : (text[first] >= 'A' && text[first] <= 'Z'))) {
            ++first;
        }
        if (first == text.size()) return s;
        std::string out(text);
        for (size_t i = first; i < out.size(); ++i) {
            char& c = out[i];
            if (upper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
            else if (!upper && c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        }
        return JsString(out);
    }
    std::u16string units = Utf16View(s).copy();
    std::u16string out = upper ? unicode::to_upper_utf16(units.data(), units.size(), locale)
                               : unicode::to_lower_utf16(units.data(), units.size(), locale);
    return JsString::from_utf16(out);
}

JsString pad(const JsString& s, Args args, bool at_start) {
    double target = js_to_integer(js_to_number(js_arg(args, 0)));
    const JsString filler = js_arg(args, 1).is_undefined() ? JsString::from_unit(' ') : js_to_js_string(js_arg(args, 1));
    if (target <= static_cast<double>(s.length()) || filler.empty()) return s;
    if (target > (1u << 28)) js_throw_range_error("Invalid string length");
    size_t needed = static_cast<size_t>(target) - s.length();
    std::string padding;
    size_t produced = 0;
    while (produced + filler.length() <= needed) {
        padding += filler.view();
        produced += filler.length();
    }
    padding += filler.substring(0, needed - produced).view();
    return at_start ? JsString::concat(std::string_view(padding), s) : JsString::concat(s, std::string_view(padding));
}

JsValue get_symbol_method(const JsValue& obj, const char* key) {
    if (!obj.is_object_like()) return JsValue::make_undefined();
    JsValue fn = obj.get_property(key);
    if (fn.is_nullish()) return JsValue::make_undefined();
    if (!fn.is_function()) {
        js_throw_type_error(std::string("Property ") + key + " is not a function");
    }
    return fn;
}

JsValue require_object_coercible_this(const char* method) {
    JsValue me = JsValue::current_this();
    if (me.is_nullish()) {
        js_throw_type_error(std::string("String.prototype.") + method + " called on null or undefined");
    }
    return me;
}

JsValue split_string(const JsValue& receiver, Args args) {
    const JsValue& separator = js_arg(args, 0);
    const JsValue& limit_arg = js_arg(args, 1);
    if (separator.is_object_like()) {
        JsValue splitter = get_symbol_method(separator, kSymbolSplitKey);
        if (!splitter.is_undefined()) {
            return splitter.call_with_this(separator, {receiver, limit_arg});
        }
    }

    const JsString s = js_to_js_string(receiver);
    double limit = limit_arg.is_undefined() ? 4294967295.0 : js_to_uint32(js_to_number(limit_arg));
    const JsString sep_str = separator.is_undefined() ? JsString() : js_to_js_string(separator);
    std::vector<JsValue> parts;
    if (limit == 0) return JsValue::make_array(parts);
    if (separator.is_undefined()) {
        parts.push_back(JsValue::make_string(s));
        return JsValue::make_array(parts);
    }
    const Utf16View sep(sep_str);
    const Utf16View view(s);
    if (sep.empty()) {
        // Split into UTF-16 code units, as the specification says.
        for (size_t i = 0; i < view.size() && parts.size() < limit; ++i) {
            parts.push_back(JsValue::make_string(JsString::from_unit(view[i])));
        }
        return JsValue::make_array(parts);
    }
    size_t start = 0;
    while (parts.size() < limit) {
        size_t found = view.find(sep, start);
        if (found == Utf16View::npos) {
            parts.push_back(JsValue::make_string(view.slice(start, view.size())));
            break;
        }
        parts.push_back(JsValue::make_string(view.slice(start, found)));
        start = found + sep.size();
    }
    return JsValue::make_array(parts);
}

JsValue replace_string(const JsValue& receiver, const JsValue& pattern, const JsValue& replacement, bool all) {
    if (!pattern.is_nullish()) {
        if (all && js_is_regexp(pattern)) {
            JsValue flags_val = pattern.get_property("flags");
            if (flags_val.is_nullish()) {
                js_throw_type_error("String.prototype.replaceAll called with a RegExp with null/undefined flags");
            }
            if (js_to_string(flags_val).find('g') == std::string::npos) {
                js_throw_type_error("replaceAll must be called with a global RegExp");
            }
        }
        JsValue replacer = get_symbol_method(pattern, kSymbolReplaceKey);
        if (!replacer.is_undefined()) {
            return replacer.call_with_this(pattern, {receiver, replacement});
        }
    }
    const JsString s = js_to_js_string(receiver);
    const JsString needle_string = js_to_js_string(pattern);
    const bool functional = replacement.is_function();
    const std::u16string templ = functional ? std::u16string() : Utf16View(js_to_js_string(replacement)).copy();
    const Utf16View subject(s);
    const Utf16View needle(needle_string);

    std::u16string out;
    size_t cursor = 0;
    size_t search_from = 0;
    bool replaced = false;
    while (true) {
        size_t found = subject.find(needle, search_from);
        if (found == Utf16View::npos) break;
        replaced = true;
        subject.append_to(out, cursor, found - cursor);
        if (functional) {
            JsValue result = replacement.call({JsValue::make_string(needle_string), JsValue::make_number(static_cast<double>(found)),
                                               JsValue::make_string(s)});
            Utf16View(js_to_js_string(result)).append_to(out);
        } else {
            JsReplaceCapture whole;
            whole.matched = true;
            whole.text = needle.copy();
            out += js_expand_replacement(templ, subject, found, {whole}, {});
        }
        cursor = found + needle.size();
        search_from = needle.empty() ? found + 1 : cursor;
        if (!all || search_from > subject.size()) break;
    }
    if (!replaced) return JsValue::make_string(s);      // nothing found: the same string, not a copy
    subject.append_to(out, cursor);
    return JsValue::make_string(JsString::from_utf16(out));
}

// ---- URI encoding ---------------------------------------------------------

const char* kUriUnreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'()";
const char* kUriReserved = ";/?:@&=+$,#";

std::string uri_encode(const std::string& input, bool keep_reserved) {
    static const char* hex = "0123456789ABCDEF";
    // A lone surrogate cannot be encoded as UTF-8: that is a URIError.
    std::u16string wide = js_utf8_to_utf16(input);
    for (size_t i = 0; i < wide.size(); ++i) {
        char16_t c = wide[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= wide.size() || wide[i + 1] < 0xDC00 || wide[i + 1] > 0xDFFF) {
                js_throw_error(JsErrorKind::URIError, "URI malformed");
            }
            ++i;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            js_throw_error(JsErrorKind::URIError, "URI malformed");
        }
    }
    std::string out;
    for (unsigned char c : input) {
        if (c < 0x80 && c != 0 && (std::strchr(kUriUnreserved, c) || (keep_reserved && std::strchr(kUriReserved, c)))) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string uri_decode(const std::string& input, bool keep_reserved) {
    std::string out;
    size_t i = 0;
    auto read_byte = [&](size_t at, int& value) {
        if (at + 2 >= input.size()) return false;
        if (input[at] != '%') return false;
        int hi = hex_digit(input[at + 1]);
        int lo = hex_digit(input[at + 2]);
        if (hi < 0 || lo < 0) return false;
        value = hi * 16 + lo;
        return true;
    };
    while (i < input.size()) {
        if (input[i] != '%') {
            out += input[i++];
            continue;
        }
        int first = 0;
        if (i + 2 >= input.size() || !read_byte(i, first)) js_throw_error(JsErrorKind::URIError, "URI malformed");
        if (first < 0x80) {
            // `first != 0`: strchr finds the terminator, and %00 is not reserved.
            if (keep_reserved && first != 0 && std::strchr(kUriReserved, first)) out.append(input, i, 3);
            else out += static_cast<char>(first);
            i += 3;
            continue;
        }
        // A multi-byte UTF-8 sequence: every byte must be percent-encoded and
        // the whole thing must be well-formed.
        int extra = (first & 0xE0) == 0xC0 ? 1 : (first & 0xF0) == 0xE0 ? 2 : (first & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0) js_throw_error(JsErrorKind::URIError, "URI malformed");
        std::string bytes(1, static_cast<char>(first));
        size_t at = i + 3;
        for (int k = 0; k < extra; ++k) {
            int next = 0;
            if (at + 2 >= input.size() || !read_byte(at, next) || (next & 0xC0) != 0x80) {
                js_throw_error(JsErrorKind::URIError, "URI malformed");
            }
            bytes += static_cast<char>(next);
            at += 3;
        }
        // Shortest form, not a surrogate, not above U+10FFFF: anything else is
        // not UTF-8, however well the bytes are shaped.
        uint32_t cp = static_cast<uint32_t>(first) & (extra == 1 ? 0x1Fu : extra == 2 ? 0x0Fu : 0x07u);
        for (size_t k = 1; k < bytes.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(bytes[k]) & 0x3Fu);
        static const uint32_t smallest[] = {0, 0x80, 0x800, 0x10000};
        if (cp < smallest[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            js_throw_error(JsErrorKind::URIError, "URI malformed");
        }
        out += bytes;
        i = at;
    }
    return out;
}

} // namespace

std::u16string js_expand_replacement(const std::u16string& replacement, const Utf16View& subject,
                                     size_t position, const std::vector<JsReplaceCapture>& captures,
                                     const std::vector<std::pair<std::string, size_t>>& named_groups) {
    std::u16string out;
    const std::u16string& matched = captures[0].text;
    for (size_t i = 0; i < replacement.size(); ++i) {
        char16_t c = replacement[i];
        if (c != '$' || i + 1 >= replacement.size()) {
            out += c;
            continue;
        }
        char16_t next = replacement[i + 1];
        if (next == '$') {
            out += u'$';
            ++i;
        } else if (next == '&') {
            out += matched;
            ++i;
        } else if (next == '`') {
            subject.append_to(out, 0, position);
            ++i;
        } else if (next == '\'') {
            size_t after = std::min(subject.size(), position + matched.size());
            subject.append_to(out, after);
            ++i;
        } else if (next >= '0' && next <= '9') {
            // One or two digits; prefer the two-digit group when it exists.
            size_t group = static_cast<size_t>(next - '0');
            size_t consumed = 1;
            if (i + 2 < replacement.size() && replacement[i + 2] >= '0' && replacement[i + 2] <= '9') {
                size_t two = group * 10 + static_cast<size_t>(replacement[i + 2] - '0');
                if (two >= 1 && two < captures.size()) {
                    group = two;
                    consumed = 2;
                }
            }
            if (group >= 1 && group < captures.size()) {
                if (captures[group].matched) out += captures[group].text;
                i += consumed;
            } else {
                out += c;
            }
        } else if (next == '<' && !named_groups.empty()) {
            size_t close = replacement.find(u'>', i + 2);
            if (close == std::u16string::npos) {
                out += c;
                continue;
            }
            std::string name = js_utf16_to_utf8(replacement.substr(i + 2, close - i - 2));
            for (const auto& [group_name, index] : named_groups) {
                if (group_name == name && index < captures.size() && captures[index].matched) {
                    out += captures[index].text;
                    break;
                }
            }
            i = close;
        } else {
            out += c;
        }
    }
    return out;
}

void install_string_builtins(JsIntrinsics& realm) {
    JsObject* sp = realm.string_proto;

    // ---- Constructor ------------------------------------------------------
    JsValue string_ctor = js_make_native("String", 1, [](Args args) {
        const bool construct = JsValue::current_is_construct();
        JsValue text;
        if (args.empty()) {
            text = JsValue::make_string("");
        } else if (args[0].type == JsType::Symbol && !construct) {
            // String(symbol) is allowed and gives the description form.
            text = JsValue::make_string(args[0].to_string());
        } else {
            text = JsValue::make_string(js_to_js_string(args[0]));
        }
        // `new String("a")` is an object that wraps the string.
        return construct ? js_to_object(text) : text;
    });
    string_ctor.define_property("prototype", js_object_value(sp), 0);
    js_define_hidden(sp, "constructor", string_ctor);

    auto define_ctor_method = [&](const char* name, int length, JsValue::NativeFn fn) {
        JsValue v = js_make_native(name, length, std::move(fn));
        if (JsObject* cell = v.function_cell()) cell->not_constructor = true;
        string_ctor.define_property(name, std::move(v), PropertyMap::Hidden);
    };
    auto define_str_method = [&](const char* name, int length, JsValue::NativeFn fn) {
        JsValue v = js_make_native(name, length, std::move(fn));
        if (JsObject* cell = v.function_cell()) cell->not_constructor = true;
        auto& entry = sp->properties.entry(name);
        entry.second = std::move(v);
        entry.flags = PropertyMap::Hidden;
    };
    auto this_string_value = [](const char* method) -> JsString {
        const JsValue me = JsValue::current_this();
        if (me.type == JsType::String) return me.string_val;
        if (const JsValue* inner = js_wrapped_primitive(me); inner && inner->type == JsType::String) {
            return inner->string_val;
        }
        js_throw_type_error(std::string("String.prototype.") + method + " requires that 'this' be a String");
    };

    define_ctor_method("fromCharCode", 1, [](Args args) {
        std::u16string units;
        for (const auto& arg : args) units += static_cast<char16_t>(js_to_uint32(js_to_number(arg)) & 0xFFFF);
        if (units.size() == 1) return JsValue::make_string(JsString::from_unit(units[0]));
        return JsValue::make_string(JsString::from_utf16(units));
    });
    define_ctor_method("fromCodePoint", 1, [](Args args) {
        std::string out;
        for (const auto& arg : args) {
            double cp = js_to_number(arg);
            if (!(cp >= 0 && cp <= 0x10FFFF) || cp != std::floor(cp)) {
                js_throw_range_error("Invalid code point " + js_to_string(arg));
            }
            js_append_code_point(out, static_cast<uint32_t>(cp));
        }
        return JsValue::make_string(out);
    });
    define_ctor_method("raw", 1, [](Args args) {
        const JsValue& strings = js_arg(args, 0);
        if (strings.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        JsValue raw = strings.get_property("raw");
        if (raw.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        double length = js_to_integer(js_to_number(raw.get_property("length")));
        std::string out;
        for (double i = 0; i < length; ++i) {
            out += js_to_string(raw.get_property(js_number_to_string(i)));
            if (i + 1 < length && static_cast<size_t>(i) + 1 < args.size()) out += js_to_string(args[static_cast<size_t>(i) + 1]);
        }
        return JsValue::make_string(out);
    });
    realm.add_global("String", string_ctor);

    // ---- Characters -------------------------------------------------------
    define_str_method("toString", 0, [this_string_value](Args) { return JsValue::make_string(this_string_value("toString")); });
    define_str_method("valueOf", 0, [this_string_value](Args) { return JsValue::make_string(this_string_value("valueOf")); });
    define_str_method("charAt", 1, [](Args args) {
        const JsString s = this_string("charAt");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(s.length())) return JsValue::make_string(JsString());
        return JsValue::make_string(JsString::from_unit(s.unit_at(static_cast<size_t>(index))));
    });
    define_str_method("charCodeAt", 1, [](Args args) {
        const JsString s = this_string("charCodeAt");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(s.length())) {
            return JsValue::make_number(std::numeric_limits<double>::quiet_NaN());
        }
        return JsValue::make_number(s.unit_at(static_cast<size_t>(index)));
    });
    define_str_method("codePointAt", 1, [](Args args) {
        const JsString s = this_string("codePointAt");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(s.length())) return JsValue::make_undefined();
        size_t i = static_cast<size_t>(index);
        uint32_t cp = s.unit_at(i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.length()) {
            uint32_t low = s.unit_at(i + 1);
            if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        return JsValue::make_number(cp);
    });
    define_str_method("at", 1, [](Args args) {
        const JsString s = this_string("at");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(s.length());
        if (index < 0 || index >= static_cast<double>(s.length())) return JsValue::make_undefined();
        return JsValue::make_string(JsString::from_unit(s.unit_at(static_cast<size_t>(index))));
    });

    // ---- Searching --------------------------------------------------------
    define_str_method("indexOf", 1, [](Args args) {
        const Utf16View view = this_units("indexOf");
        const Utf16View needle = arg_units(args, 0);
        size_t found = view.find(needle, clamp_index(js_arg(args, 1), view.size(), 0));
        return JsValue::make_number(found == Utf16View::npos ? -1.0 : static_cast<double>(found));
    });
    define_str_method("lastIndexOf", 1, [](Args args) {
        const Utf16View view = this_units("lastIndexOf");
        const Utf16View needle = arg_units(args, 0);
        // NaN (which is what a missing position converts to) means "from the end".
        const double position = js_to_number(js_arg(args, 1));
        size_t from = Utf16View::npos;
        if (!std::isnan(position)) {
            const double k = js_to_integer(position);
            from = k < 0 ? 0 : k > static_cast<double>(view.size()) ? view.size() : static_cast<size_t>(k);
        }
        size_t found = view.rfind(needle, from);
        return JsValue::make_number(found == Utf16View::npos ? -1.0 : static_cast<double>(found));
    });
    define_str_method("includes", 1, [](Args args) {
        const Utf16View view = this_units("includes");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.includes must not be a regular expression");
        const Utf16View needle = arg_units(args, 0);
        return JsValue::make_boolean(view.find(needle, clamp_index(js_arg(args, 1), view.size(), 0)) != Utf16View::npos);
    });
    define_str_method("startsWith", 1, [](Args args) {
        const Utf16View view = this_units("startsWith");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.startsWith must not be a regular expression");
        const Utf16View needle = arg_units(args, 0);
        size_t start = clamp_index(js_arg(args, 1), view.size(), 0);
        return JsValue::make_boolean(view.matches_at(start, needle));
    });
    define_str_method("endsWith", 1, [](Args args) {
        const Utf16View view = this_units("endsWith");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.endsWith must not be a regular expression");
        const Utf16View needle = arg_units(args, 0);
        size_t end = clamp_index(js_arg(args, 1), view.size(), view.size());
        if (needle.size() > end) return JsValue::make_boolean(false);
        return JsValue::make_boolean(view.matches_at(end - needle.size(), needle));
    });

    // ---- Extracting -------------------------------------------------------
    // JsString::substring decides whether the piece is copied or shares the
    // text it came from; see "Taking pieces" in JsString.hpp.
    define_str_method("slice", 2, [](Args args) {
        const JsString s = this_string("slice");
        size_t begin = relative_index(js_arg(args, 0), s.length(), 0);
        size_t end = relative_index(js_arg(args, 1), s.length(), s.length());
        return JsValue::make_string(s.substring(begin, end));
    });
    define_str_method("substring", 2, [](Args args) {
        const JsString s = this_string("substring");
        size_t a = clamp_index(js_arg(args, 0), s.length(), 0);
        size_t b = clamp_index(js_arg(args, 1), s.length(), s.length());
        return JsValue::make_string(s.substring(std::min(a, b), std::max(a, b)));
    });
    define_str_method("substr", 2, [](Args args) {
        const JsString s = this_string("substr");
        size_t begin = relative_index(js_arg(args, 0), s.length(), 0);
        double length = js_arg(args, 1).is_undefined() ? static_cast<double>(s.length())
                                                      : js_to_integer(js_to_number(js_arg(args, 1)));
        if (length <= 0) return JsValue::make_string(JsString());
        size_t end = static_cast<size_t>(std::min(static_cast<double>(s.length()), static_cast<double>(begin) + length));
        return JsValue::make_string(s.substring(begin, end));
    });

    // ---- Transforming -----------------------------------------------------
    define_str_method("toUpperCase", 0, [](Args) {
        return JsValue::make_string(change_case(this_string("toUpperCase"), true));
    });
    define_str_method("toLowerCase", 0, [](Args) {
        return JsValue::make_string(change_case(this_string("toLowerCase"), false));
    });
    define_str_method("toLocaleUpperCase", 0, [](Args args) {
        const JsString s = this_string("toLocaleUpperCase");
        const unicode::CaseLocale locale = resolve_case_locale(js_arg(args, 0));
        return JsValue::make_string(change_case(s, true, locale));
    });
    define_str_method("toLocaleLowerCase", 0, [](Args args) {
        const JsString s = this_string("toLocaleLowerCase");
        const unicode::CaseLocale locale = resolve_case_locale(js_arg(args, 0));
        return JsValue::make_string(change_case(s, false, locale));
    });
    define_str_method("trim", 0, [](Args) { return JsValue::make_string(trim(this_string("trim"), true, true)); });
    define_str_method("trimStart", 0, [](Args) { return JsValue::make_string(trim(this_string("trimStart"), true, false)); });
    define_str_method("trimEnd", 0, [](Args) { return JsValue::make_string(trim(this_string("trimEnd"), false, true)); });
    define_str_method("trimLeft", 0, [](Args) { return JsValue::make_string(trim(this_string("trimLeft"), true, false)); });
    define_str_method("trimRight", 0, [](Args) { return JsValue::make_string(trim(this_string("trimRight"), false, true)); });
    define_str_method("padStart", 1, [](Args args) { return JsValue::make_string(pad(this_string("padStart"), args, true)); });
    define_str_method("padEnd", 1, [](Args args) { return JsValue::make_string(pad(this_string("padEnd"), args, false)); });
    define_str_method("repeat", 1, [](Args args) {
        const JsString s = this_string("repeat");
        double count = js_to_integer(js_to_number(js_arg(args, 0)));
        if (count < 0 || std::isinf(count)) js_throw_range_error("Invalid count value: " + js_to_string(js_arg(args, 0)));
        if (static_cast<double>(s.size()) * count > (1u << 28)) js_throw_range_error("Invalid string length");
        if (count == 1) return JsValue::make_string(s);
        std::string out;
        out.reserve(s.size() * static_cast<size_t>(count));
        for (size_t i = 0; i < static_cast<size_t>(count); ++i) out += s.view();
        return JsValue::make_string(out);
    });
    define_str_method("concat", 1, [](Args args) {
        JsString s = this_string("concat");
        for (const auto& arg : args) s = JsString::concat(s, js_to_js_string(arg));
        return JsValue::make_string(s);
    });
    define_str_method("normalize", 0, [](Args args) {
        const JsString s = this_string("normalize");
        unicode::NormForm form = unicode::NormForm::NFC;
        const JsValue& form_arg = js_arg(args, 0);
        if (!form_arg.is_undefined()) {
            std::string f = js_to_string(form_arg);
            if (f == "NFC") form = unicode::NormForm::NFC;
            else if (f == "NFD") form = unicode::NormForm::NFD;
            else if (f == "NFKC") form = unicode::NormForm::NFKC;
            else if (f == "NFKD") form = unicode::NormForm::NFKD;
            else js_throw_range_error("The normalization form should be one of NFC, NFD, NFKC, NFKD.");
        }
        if (s.is_ascii()) return JsValue::make_string(s);
        std::u16string in = Utf16View(s).copy();
        std::u16string out;
        if (!unicode::normalize_utf16(in.data(), in.size(), form, out)) return JsValue::make_string(s);
        return JsValue::make_string(JsString::from_utf16(out));
    });
    define_str_method("isWellFormed", 0, [](Args) {
        return JsValue::make_boolean(this_string("isWellFormed").is_well_formed());
    });
    define_str_method("toWellFormed", 0, [](Args) {
        return JsValue::make_string(this_string("toWellFormed").to_well_formed());
    });
    define_str_method("localeCompare", 1, [](Args args) {
        const JsString a = this_string("localeCompare");
        const JsString b = js_to_js_string(js_arg(args, 0));
        const unicode::CollationOptions opts = parse_collation_args(js_arg(args, 1), js_arg(args, 2));
        if (a == b) return JsValue::make_number(0);
        std::u16string ua = Utf16View(a).copy();
        std::u16string ub = Utf16View(b).copy();
        int cmp = unicode::locale_compare_utf16(ua.data(), ua.size(), ub.data(), ub.size(), opts);
        return JsValue::make_number(cmp);
    });
    auto invoke_symbol = [](const JsValue& rx, const char* key, const JsString& s) {
        JsValue fn = rx.get_property(key);
        if (!fn.is_function()) {
            js_throw_type_error(std::string("Property ") + key + " is not a function");
        }
        return fn.call_with_this(rx, {JsValue::make_string(s)});
    };
    define_str_method("split", 2, [](Args args) {
        return split_string(require_object_coercible_this("split"), args);
    });
    define_str_method("replace", 2, [](Args args) {
        return replace_string(require_object_coercible_this("replace"), js_arg(args, 0), js_arg(args, 1), false);
    });
    define_str_method("replaceAll", 2, [](Args args) {
        return replace_string(require_object_coercible_this("replaceAll"), js_arg(args, 0), js_arg(args, 1), true);
    });
    define_str_method("match", 1, [invoke_symbol](Args args) {
        JsValue receiver = require_object_coercible_this("match");
        const JsValue& regexp = js_arg(args, 0);
        if (regexp.is_object_like()) {
            JsValue matcher = get_symbol_method(regexp, kSymbolMatchKey);
            if (!matcher.is_undefined()) {
                return matcher.call_with_this(regexp, {receiver});
            }
        }
        JsString s = js_to_js_string(receiver);
        JsValue rx = js_regexp_create(regexp.is_undefined() ? "" : js_to_string(regexp), "");
        return invoke_symbol(rx, kSymbolMatchKey, s);
    });
    define_str_method("matchAll", 1, [invoke_symbol](Args args) {
        JsValue receiver = require_object_coercible_this("matchAll");
        const JsValue& regexp = js_arg(args, 0);
        if (regexp.is_object_like()) {
            if (js_is_regexp(regexp)) {
                JsValue flags_val = regexp.get_property("flags");
                if (flags_val.is_nullish()) {
                    js_throw_type_error("String.prototype.matchAll called with a RegExp with null/undefined flags");
                }
                if (js_to_string(flags_val).find('g') == std::string::npos) {
                    js_throw_type_error("String.prototype.matchAll called with a non-global RegExp argument");
                }
            }
            JsValue matcher = get_symbol_method(regexp, kSymbolMatchAllKey);
            if (!matcher.is_undefined()) {
                return matcher.call_with_this(regexp, {receiver});
            }
        }
        JsString s = js_to_js_string(receiver);
        JsValue rx = js_regexp_create(regexp.is_undefined() ? "" : js_to_string(regexp), "g");
        return invoke_symbol(rx, kSymbolMatchAllKey, s);
    });
    define_str_method("search", 1, [invoke_symbol](Args args) {
        JsValue receiver = require_object_coercible_this("search");
        const JsValue& regexp = js_arg(args, 0);
        if (regexp.is_object_like()) {
            JsValue searcher = get_symbol_method(regexp, kSymbolSearchKey);
            if (!searcher.is_undefined()) {
                return searcher.call_with_this(regexp, {receiver});
            }
        }
        JsString s = js_to_js_string(receiver);
        JsValue rx = js_regexp_create(regexp.is_undefined() ? "" : js_to_string(regexp), "");
        return invoke_symbol(rx, kSymbolSearchKey, s);
    });

    // Strings iterate by code point.
    auto string_iterator = [](Args) {
        const JsString text = this_string("[Symbol.iterator]");
        auto offset = std::make_shared<size_t>(0);
        return js_make_iterator([text, offset](JsValue& value) {
            const std::string_view s = text.view();
            size_t i = *offset;
            if (i >= s.size()) return false;
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            if (i + len > s.size()) len = s.size() - i;
            value = len == 1 ? JsValue::make_string(JsString::from_unit(c)) : JsValue::make_string(s.substr(i, len));
            *offset = i + len;
            return true;
        });
    };
    {
        JsValue iter_fn = js_make_native("[Symbol.iterator]", 0, string_iterator);
        if (JsObject* cell = iter_fn.function_cell()) cell->not_constructor = true;
        auto& e1 = sp->properties.entry(kIteratorKey);
        e1.second = iter_fn;
        e1.flags = PropertyMap::Hidden;
        auto& e2 = sp->properties.entry("Symbol.iterator");
        e2.second = std::move(iter_fn);
        e2.flags = PropertyMap::Hidden;
    }

    // ---- URI globals ------------------------------------------------------
    realm.add_global("encodeURIComponent", js_make_native("encodeURIComponent", 1, [](Args args) {
        return JsValue::make_string(uri_encode(js_to_string(js_arg(args, 0)), false));
    }));
    realm.add_global("encodeURI", js_make_native("encodeURI", 1, [](Args args) {
        return JsValue::make_string(uri_encode(js_to_string(js_arg(args, 0)), true));
    }));
    realm.add_global("decodeURIComponent", js_make_native("decodeURIComponent", 1, [](Args args) {
        return JsValue::make_string(uri_decode(js_to_string(js_arg(args, 0)), false));
    }));
    realm.add_global("decodeURI", js_make_native("decodeURI", 1, [](Args args) {
        return JsValue::make_string(uri_decode(js_to_string(js_arg(args, 0)), true));
    }));

    // escape() and unescape(): legacy, but still called by old pages (cookie
    // helpers mostly). They work on UTF-16 code units, not on UTF-8 bytes.
    realm.add_global("escape", js_make_native("escape", 1, [](Args args) {
        static const char* const kHex = "0123456789ABCDEF";
        std::string out;
        for (char16_t unit : js_utf8_to_utf16(js_to_string(js_arg(args, 0)))) {
            const bool plain = unit < 128 && (std::isalnum(static_cast<unsigned char>(unit)) ||
                                              std::strchr("@*_+-./", static_cast<char>(unit)));
            if (plain && unit != 0) {
                out += static_cast<char>(unit);
            } else if (unit < 256) {
                out += '%';
                out += kHex[unit >> 4];
                out += kHex[unit & 15];
            } else {
                out += "%u";
                for (int shift = 12; shift >= 0; shift -= 4) out += kHex[(unit >> shift) & 15];
            }
        }
        return JsValue::make_string(std::move(out));
    }));
    realm.add_global("unescape", js_make_native("unescape", 1, [](Args args) {
        const std::u16string in = js_utf8_to_utf16(js_to_string(js_arg(args, 0)));
        auto hex = [&in](size_t at, size_t count, unsigned& value) {
            if (at + count > in.size()) return false;
            value = 0;
            for (size_t i = 0; i < count; ++i) {
                char16_t c = in[at + i];
                unsigned digit = (c >= '0' && c <= '9') ? c - '0'
                               : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                               : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 99;
                if (digit == 99) return false;
                value = value * 16 + digit;
            }
            return true;
        };
        std::u16string out;
        for (size_t i = 0; i < in.size(); ++i) {
            unsigned value = 0;
            if (in[i] == '%' && i + 1 < in.size() && in[i + 1] == 'u' && hex(i + 2, 4, value)) {
                out += static_cast<char16_t>(value);
                i += 5;
            } else if (in[i] == '%' && hex(i + 1, 2, value)) {
                out += static_cast<char16_t>(value);
                i += 2;
            } else {
                out += in[i];
            }
        }
        return JsValue::make_string(js_utf16_to_utf8(out));
    }));
}

} // namespace litebird
