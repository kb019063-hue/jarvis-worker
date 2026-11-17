#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace litebird::unicode {

enum class NormForm : uint8_t { NFC, NFD, NFKC, NFKD };

enum class CaseLocale : uint8_t { Default, Turkish, Lithuanian, Greek };

enum class CollationSensitivity : uint8_t { Base, Accent, Case, Variant };

struct CollationOptions {
    CollationSensitivity sensitivity = CollationSensitivity::Variant;
    bool ignore_punctuation = false;
    bool numeric = false;
    // 0 = default/false (CLDR root: lower before upper), 1 = upper, 2 = lower
    uint8_t case_first = 0;
};

// Unicode 17.0 character properties.
uint8_t canonical_combining_class(uint32_t cp);
bool is_cased(uint32_t cp);
bool is_case_ignorable(uint32_t cp);
bool is_soft_dotted(uint32_t cp);

// Unicode 17.0 UAX #15 normalization on UTF-16 code units (preserves lone surrogates).
// Returns false when the input is already in the requested form (caller may reuse it zero-copy).
bool normalize_utf16(const char16_t* data, size_t len, NormForm form, std::u16string& out);

// Unicode 17.0 full and locale-sensitive case mapping on UTF-16 code units (preserves lone surrogates).
std::u16string to_lower_utf16(const char16_t* data, size_t len, CaseLocale locale = CaseLocale::Default);
std::u16string to_upper_utf16(const char16_t* data, size_t len, CaseLocale locale = CaseLocale::Default);

// CLDR 48 / Unicode 17.0 root UCA collation comparison on UTF-16 code units.
// Returns -1, 0, or 1.
int locale_compare_utf16(const char16_t* a, size_t a_len,
                         const char16_t* b, size_t b_len,
                         const CollationOptions& opts = {});

namespace tables {

struct CpRange {
    uint32_t lo;
    uint32_t hi;
};

struct CccRange {
    uint32_t lo;
    uint32_t hi;
    uint8_t ccc;
};

struct DecompEntry {
    uint32_t cp;
    uint16_t offset;
    uint8_t len;
};

struct ComposePair {
    uint64_t key; // (uint64_t(first) << 21) | second
    uint32_t composite;
};

struct CaseRun {
    uint32_t lo;
    uint32_t hi;
    int32_t delta;
    uint8_t step;
};

struct SpecialCaseEntry {
    uint32_t cp;
    uint32_t out[3];
    uint8_t len;
};

struct CollationCE {
    uint16_t p;
    uint16_t s;
    uint8_t t_var; // bits 0..5 = tertiary (0..31), bit 7 = variable (*)
};

struct CollationRun {
    uint32_t cp_lo;
    uint16_t count_minus_1;
    uint16_t p_start;
    uint16_t s;
    int8_t p_step;
    uint8_t t_var;
};

struct CollationMulti {
    uint32_t cp;
    uint16_t offset;
    uint8_t len;
};

struct CollationContraction {
    uint32_t cp0;
    uint32_t cp1;
    uint32_t cp2; // 0xFFFFFFFF for 2-code-point contractions
    uint16_t offset;
    uint8_t len;
};

extern const CccRange kCccRanges[];
extern const size_t kCccRangesCount;

extern const uint32_t kDecompPool[];
extern const size_t kDecompPoolCount;

extern const DecompEntry kCanonDecomp[];
extern const size_t kCanonDecompCount;

extern const DecompEntry kCompatDecomp[];
extern const size_t kCompatDecompCount;

extern const ComposePair kComposePairs[];
extern const size_t kComposePairsCount;

extern const CaseRun kSimpleLowerRuns[];
extern const size_t kSimpleLowerRunsCount;

extern const CaseRun kSimpleUpperRuns[];
extern const size_t kSimpleUpperRunsCount;

extern const SpecialCaseEntry kSpecialLower[];
extern const size_t kSpecialLowerCount;

extern const SpecialCaseEntry kSpecialUpper[];
extern const size_t kSpecialUpperCount;

extern const CpRange kCasedRanges[];
extern const size_t kCasedRangesCount;

extern const CpRange kCaseIgnorableRanges[];
extern const size_t kCaseIgnorableRangesCount;

extern const CpRange kSoftDottedRanges[];
extern const size_t kSoftDottedRangesCount;

extern const CollationCE kCollationCEPool[];
extern const size_t kCollationCEPoolCount;

extern const CollationRun kCollationRuns[];
extern const size_t kCollationRunsCount;

extern const CollationMulti kCollationMulti[];
extern const size_t kCollationMultiCount;

extern const CollationContraction kCollationContractions[];
extern const size_t kCollationContractionsCount;

extern const uint32_t kCollationContractStarters[];
extern const size_t kCollationContractStartersCount;

} // namespace tables

} // namespace litebird::unicode
