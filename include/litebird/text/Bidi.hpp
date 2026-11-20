#pragma once

// The Unicode Bidirectional Algorithm (UAX #9), rules P2 through L2, for one
// paragraph at a time. Hebrew and Arabic text had no bidi pass at all before
// Phase 109: every run was painted left to right, so a Hebrew word came out
// mirrored and an Arabic sentence with a number in it came out in the wrong
// order.
//
// What this file does not do, and why (see docs/phases/109-bidi-uax9.md):
//   * L1's "end of line" is the whole paragraph. Line breaking is the caller's
//     job: it reorders each line with `bidi_reorder`, which is L2 alone, after
//     its own L1 pass over that line's trailing whitespace.
//   * L3 (combining marks) and L4 (mirroring) are the painter's: `bidi_mirror`
//     answers L4's pair for a character.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace litebird {

/// The Bidi_Class property. The numeric values are the order of the generated
/// table in src/text/BidiTables.cpp; do not renumber without regenerating it.
enum class BidiClass : uint8_t {
    L = 0,
    R,
    AL,
    EN,
    ES,
    ET,
    AN,
    CS,
    NSM,
    BN,
    B,
    S,
    WS,
    ON,
    LRE,
    RLE,
    LRO,
    RLO,
    PDF,
    LRI,
    RLI,
    FSI,
    PDI,
};

/// The Bidi_Class of a code point, unassigned ones included (UAX #9's default
/// values are written into the table as explicit ranges).
BidiClass bidi_class(uint32_t codepoint);

/// True when the code point has a Bidi_Mirrored glyph pair.
bool bidi_is_mirrored(uint32_t codepoint);

/// The mirroring code point for rule L4, or the argument when it has none.
uint32_t bidi_mirror(uint32_t codepoint);

/// The levels of one paragraph, as rules P2-L1 leave them.
struct BidiParagraph {
    /// The paragraph embedding level: 0 for left-to-right, 1 for right-to-left.
    int base_level{0};
    /// One level per code point. `removed[i]` is true for the characters rule
    /// X9 takes out of the sequence (LRE, RLE, LRO, RLO, PDF, BN); their level
    /// is the one they had, and callers skip them.
    std::vector<uint8_t> levels;
    std::vector<bool> removed;
    /// The original Bidi_Class of each code point, before W3 turned AL into R.
    std::vector<BidiClass> classes;

    std::size_t size() const { return levels.size(); }
};

/// Resolves one paragraph. `text` is the paragraph's code points.
/// `paragraph_level` is the paragraph's direction: 0 (ltr), 1 (rtl), or -1 to
/// take it from the first strong character (rules P2 and P3).
BidiParagraph bidi_paragraph(const std::u32string& text, int paragraph_level = -1);

/// Same, on UTF-8. Invalid sequences decode as U+FFFD, one per byte, so the
/// result's length is not the string's byte length.
BidiParagraph bidi_paragraph_utf8(std::string_view text, int paragraph_level = -1);

/// The same pass over Bidi_Class values alone, which is how the Unicode
/// conformance suite (tests/bidi/BidiTest.txt) states its cases. Paired
/// brackets cannot be found without characters, so rule N0 does nothing here.
BidiParagraph bidi_paragraph_classes(const std::vector<BidiClass>& classes,
                                     int paragraph_level = -1);

/// Rule L2 on a range of a paragraph's levels: the indices in visual order,
/// left to right. Characters rule X9 removed are left out.
std::vector<std::size_t> bidi_reorder(const BidiParagraph& paragraph);

/// Rule L2 over a sub-range, for one line of a paragraph: `first` and `last`
/// are indices into the paragraph's levels, `last` exclusive. The returned
/// indices are absolute.
std::vector<std::size_t> bidi_reorder(const BidiParagraph& paragraph, std::size_t first,
                                      std::size_t last);

/// L1 for one line whose text is still in one paragraph: a trailing run of
/// whitespace and isolate formatting characters at the end of the line is
/// reset to the paragraph level (the paragraph separator's own reset happened
/// in `bidi_paragraph`). Callers apply it before `bidi_reorder` per line.
void bidi_apply_l1_line(const BidiParagraph& paragraph, std::size_t first, std::size_t last,
                        std::vector<uint8_t>& levels_out);

} // namespace litebird
