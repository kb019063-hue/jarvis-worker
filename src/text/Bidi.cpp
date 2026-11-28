#include "litebird/text/Bidi.hpp"

#include "litebird/text/BidiTables.hpp"

#include <algorithm>
#include <array>

namespace litebird {

namespace {

constexpr BidiClass kNeutral = BidiClass::ON;  // "the override status is neutral"

bool is_isolate_initiator(BidiClass c) {
    return c == BidiClass::LRI || c == BidiClass::RLI || c == BidiClass::FSI;
}

bool is_isolate_formatting(BidiClass c) {
    return is_isolate_initiator(c) || c == BidiClass::PDI;
}

bool is_removed_by_x9(BidiClass c) {
    return c == BidiClass::LRE || c == BidiClass::RLE || c == BidiClass::LRO ||
           c == BidiClass::RLO || c == BidiClass::PDF || c == BidiClass::BN;
}

// N1 and N2 ask about "neutral or isolate formatting" characters.
bool is_ni(BidiClass c) {
    return c == BidiClass::B || c == BidiClass::S || c == BidiClass::WS || c == BidiClass::ON ||
           is_isolate_formatting(c);
}

// Rules W1-W7, N0-N2 and I1-I2 resolve EN and AN as R when they ask for a
// "strong" type in the surrounding text.
BidiClass strong_of(BidiClass c) {
    if (c == BidiClass::EN || c == BidiClass::AN) return BidiClass::R;
    return c;
}

const std::vector<BidiClassRange>& class_ranges() {
    static const std::vector<BidiClassRange> table = [] {
        std::size_t count = 0;
        const BidiClassRange* ranges = bidi_class_ranges(count);
        return std::vector<BidiClassRange>(ranges, ranges + count);
    }();
    return table;
}

const std::vector<BidiBracketPair>& bracket_pairs() {
    static const std::vector<BidiBracketPair> table = [] {
        std::size_t count = 0;
        const BidiBracketPair* pairs = bidi_bracket_pairs(count);
        return std::vector<BidiBracketPair>(pairs, pairs + count);
    }();
    return table;
}

const std::vector<BidiMirrorPair>& mirror_pairs() {
    static const std::vector<BidiMirrorPair> table = [] {
        std::size_t count = 0;
        const BidiMirrorPair* pairs = bidi_mirror_pairs(count);
        return std::vector<BidiMirrorPair>(pairs, pairs + count);
    }();
    return table;
}

// BD16 compares brackets by canonical equivalence, so U+2329 and U+232A are
// the same characters as U+3008 and U+3009.
uint32_t canonical_bracket(uint32_t cp) {
    if (cp == 0x2329) return 0x3008;
    if (cp == 0x232A) return 0x3009;
    return cp;
}

const BidiBracketPair* find_bracket(uint32_t cp) {
    const auto& table = bracket_pairs();
    auto it = std::lower_bound(table.begin(), table.end(), cp,
                               [](const BidiBracketPair& p, uint32_t value) { return p.bracket < value; });
    if (it == table.end() || it->bracket != cp) return nullptr;
    return &*it;
}

// Rule P2 and P3 over [first, last): the level of the first strong character,
// skipping the characters inside an isolate. -1 when there is none.
int first_strong_level(const std::vector<BidiClass>& classes, const std::vector<int>& matching_pdi,
                       std::size_t first, std::size_t last) {
    for (std::size_t i = first; i < last; ++i) {
        const BidiClass c = classes[i];
        if (is_isolate_initiator(c)) {
            const int pdi = i < matching_pdi.size() ? matching_pdi[i] : -1;
            if (pdi < 0) return -1;  // the rest of the paragraph is inside the isolate
            i = static_cast<std::size_t>(pdi);
            continue;
        }
        if (c == BidiClass::L) return 0;
        if (c == BidiClass::R || c == BidiClass::AL) return 1;
    }
    return -1;
}

// The least greater odd level (X2), and the least greater even level (X3).
int least_greater_odd(int level) { return (level + 1) | 1; }
int least_greater_even(int level) { return (level + 2) & ~1; }

constexpr int kMaxDepth = 125;

struct StatusEntry {
    int level;
    BidiClass override_status;
    bool isolate;
};

// Everything in X10 that is a function of the levels: one isolating run
// sequence, as positions in the spliced sequence.
struct IsolatingRunSequence {
    std::vector<std::size_t> positions;  // indices into the spliced sequence
    std::size_t first_spliced{0};
    std::size_t last_spliced{0};  // inclusive
    BidiClass sos{BidiClass::L};
    BidiClass eos{BidiClass::L};
};

int paragraph_level_from(const std::vector<BidiClass>& classes, const std::vector<int>& matching_pdi,
                         std::size_t first, std::size_t last, int fallback) {
    const int level = first_strong_level(classes, matching_pdi, first, last);
    return level < 0 ? fallback : level;
}

} // namespace

BidiClass bidi_class(uint32_t codepoint) {
    const auto& table = class_ranges();
    auto it = std::upper_bound(table.begin(), table.end(), codepoint,
                               [](uint32_t value, const BidiClassRange& r) { return value < r.first; });
    if (it == table.begin()) return BidiClass::L;
    --it;
    if (codepoint < it->first || codepoint > it->last) return BidiClass::L;
    return static_cast<BidiClass>(it->cls);
}

bool bidi_is_mirrored(uint32_t codepoint) {
    const auto& table = mirror_pairs();
    auto it = std::lower_bound(table.begin(), table.end(), codepoint,
                               [](const BidiMirrorPair& p, uint32_t value) { return p.from < value; });
    return it != table.end() && it->from == codepoint;
}

uint32_t bidi_mirror(uint32_t codepoint) {
    const auto& table = mirror_pairs();
    auto it = std::lower_bound(table.begin(), table.end(), codepoint,
                               [](const BidiMirrorPair& p, uint32_t value) { return p.from < value; });
    if (it == table.end() || it->from != codepoint) return codepoint;
    return it->to;
}

namespace {

BidiParagraph resolve(const std::vector<BidiClass>& classes_in, const std::u32string* text,
                      int paragraph_level, int fallback_level) {
    const std::size_t n = classes_in.size();
    BidiParagraph p;
    p.classes = classes_in;
    p.levels.assign(n, 0);
    p.removed.assign(n, false);
    if (n == 0) {
        p.base_level = (paragraph_level >= 0 ? paragraph_level : fallback_level) > 0 ? 1 : 0;
        return p;
    }

    // BD9: each isolate initiator's matching PDI, and the other way round.
    std::vector<int> matching_pdi(n, -1);
    std::vector<int> matching_initiator(n, -1);
    {
        std::vector<std::size_t> stack;
        for (std::size_t i = 0; i < n; ++i) {
            if (is_isolate_initiator(p.classes[i])) {
                stack.push_back(i);
            } else if (p.classes[i] == BidiClass::PDI && !stack.empty()) {
                const std::size_t initiator = stack.back();
                stack.pop_back();
                matching_pdi[initiator] = static_cast<int>(i);
                matching_initiator[i] = static_cast<int>(initiator);
            }
        }
    }

    // P2 and P3. With no strong character the paragraph takes the caller's
    // fallback — its `direction` — rather than an unconditional ltr.
    if (paragraph_level >= 0) {
        p.base_level = paragraph_level > 0 ? 1 : 0;
    } else {
        const int level = first_strong_level(p.classes, matching_pdi, 0, n);
        p.base_level = level < 0 ? fallback_level : level;
    }

    // X1 to X8: the explicit levels and directions.
    std::vector<StatusEntry> stack;
    stack.push_back(StatusEntry{p.base_level, kNeutral, false});
    int overflow_isolate = 0;
    int overflow_embedding = 0;
    int valid_isolate = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const BidiClass c = p.classes[i];
        const StatusEntry top = stack.back();
        if (c == BidiClass::RLE || c == BidiClass::LRE || c == BidiClass::RLO ||
            c == BidiClass::LRO) {
            p.levels[i] = static_cast<uint8_t>(top.level);
            const bool rtl = c == BidiClass::RLE || c == BidiClass::RLO;
            const int new_level =
                rtl ? least_greater_odd(top.level) : least_greater_even(top.level);
            if (new_level <= kMaxDepth && overflow_isolate == 0 && overflow_embedding == 0) {
                // X2 and X3 push a neutral override; X4 and X5 push their own.
                BidiClass override_status = kNeutral;
                if (c == BidiClass::RLO) override_status = BidiClass::R;
                if (c == BidiClass::LRO) override_status = BidiClass::L;
                stack.push_back(StatusEntry{new_level, override_status, false});
            } else if (overflow_isolate == 0) {
                ++overflow_embedding;
            }
        } else if (is_isolate_initiator(c)) {
            p.levels[i] = static_cast<uint8_t>(top.level);
            if (top.override_status != kNeutral) p.classes[i] = top.override_status;
            int rtl = c == BidiClass::RLI ? 1 : 0;
            if (c == BidiClass::FSI) {
                const std::size_t inner_first = i + 1;
                const std::size_t inner_last =
                    (i < matching_pdi.size() && matching_pdi[i] >= 0)
                        ? static_cast<std::size_t>(matching_pdi[i])
                        : n;
                rtl = paragraph_level_from(p.classes, matching_pdi, inner_first, inner_last, 0);
            }
            const int new_level = rtl ? least_greater_odd(top.level) : least_greater_even(top.level);
            if (new_level <= kMaxDepth && overflow_isolate == 0 && overflow_embedding == 0) {
                ++valid_isolate;
                stack.push_back(StatusEntry{new_level, kNeutral, true});
            } else {
                ++overflow_isolate;
            }
        } else if (c == BidiClass::PDI) {
            if (overflow_isolate > 0) {
                --overflow_isolate;
            } else if (valid_isolate > 0) {
                overflow_embedding = 0;
                while (!stack.back().isolate) stack.pop_back();
                stack.pop_back();
                --valid_isolate;
            }
            p.levels[i] = static_cast<uint8_t>(stack.back().level);
            if (stack.back().override_status != kNeutral) p.classes[i] = stack.back().override_status;
        } else if (c == BidiClass::PDF) {
            p.levels[i] = static_cast<uint8_t>(top.level);
            if (overflow_isolate > 0) {
                // A PDF inside an overflow isolate is ignored.
            } else if (overflow_embedding > 0) {
                --overflow_embedding;
            } else if (stack.size() > 1 && !stack.back().isolate) {
                stack.pop_back();
            }
        } else if (c == BidiClass::B) {
            // X8: the paragraph separator takes the paragraph level.
            p.levels[i] = static_cast<uint8_t>(p.base_level);
        } else if (c == BidiClass::BN) {
            p.levels[i] = static_cast<uint8_t>(top.level);
        } else {
            // X6.
            p.levels[i] = static_cast<uint8_t>(top.level);
            if (top.override_status != kNeutral) p.classes[i] = top.override_status;
        }
    }

    // X9: the embedding and override characters leave the sequence.
    std::vector<std::size_t> spliced;
    spliced.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        p.removed[i] = is_removed_by_x9(classes_in[i]);
        if (!p.removed[i]) spliced.push_back(i);
    }
    if (spliced.empty()) return p;

    // X10: the isolating run sequences, as maximal chains of level runs whose
    // last character is an isolate initiator matched by the next run's PDI.
    std::vector<std::pair<std::size_t, std::size_t>> runs;  // [begin, end) in `spliced`
    for (std::size_t k = 0; k < spliced.size();) {
        std::size_t j = k;
        while (j + 1 < spliced.size() && p.levels[spliced[j + 1]] == p.levels[spliced[k]]) ++j;
        runs.emplace_back(k, j + 1);
        k = j + 1;
    }
    std::vector<std::size_t> run_of(spliced.size(), 0);
    for (std::size_t r = 0; r < runs.size(); ++r) {
        for (std::size_t k = runs[r].first; k < runs[r].second; ++k) run_of[k] = r;
    }
    std::vector<bool> continued(runs.size(), false);
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const std::size_t last_index = spliced[runs[r].second - 1];
        if (is_isolate_initiator(p.classes[last_index]) && matching_pdi[last_index] >= 0) {
            const std::size_t pdi = static_cast<std::size_t>(matching_pdi[last_index]);
            // The PDI is the first spliced character of its own run.
            continued[run_of[static_cast<std::size_t>(
                std::lower_bound(spliced.begin(), spliced.end(), pdi) - spliced.begin())]] = true;
        }
    }

    std::vector<IsolatingRunSequence> sequences;
    std::vector<bool> used(runs.size(), false);
    for (std::size_t r = 0; r < runs.size(); ++r) {
        if (continued[r] || used[r]) continue;
        IsolatingRunSequence sequence;
        std::size_t current = r;
        while (true) {
            used[current] = true;
            for (std::size_t k = runs[current].first; k < runs[current].second; ++k) {
                sequence.positions.push_back(k);
            }
            const std::size_t last_index = spliced[runs[current].second - 1];
            if (is_isolate_initiator(p.classes[last_index]) && matching_pdi[last_index] >= 0) {
                const std::size_t pdi = static_cast<std::size_t>(matching_pdi[last_index]);
                const std::size_t next = run_of[static_cast<std::size_t>(
                    std::lower_bound(spliced.begin(), spliced.end(), pdi) - spliced.begin())];
                if (!used[next]) {
                    current = next;
                    continue;
                }
            }
            break;
        }
        sequence.first_spliced = sequence.positions.front();
        sequence.last_spliced = sequence.positions.back();
        sequences.push_back(std::move(sequence));
    }

    // X10's sos and eos, and N2's embedding direction, are the levels the X
    // rules assigned. Keep them: I1 and I2 of one sequence must not move the
    // answer for the next one.
    const std::vector<uint8_t> explicit_levels = p.levels;

    // The types the W, N and I rules work on, per spliced position.
    std::vector<BidiClass> types(spliced.size());
    for (std::size_t k = 0; k < spliced.size(); ++k) types[k] = p.classes[spliced[k]];

    for (IsolatingRunSequence& sequence : sequences) {
        // sos and eos (X10): the higher of the sequence's edge level and the
        // level before it, with an isolate initiator standing in for the
        // character its matching PDI follows.
        const std::size_t first_char = spliced[sequence.first_spliced];
        const std::size_t last_char = spliced[sequence.last_spliced];
        int before_level;
        if (p.classes[first_char] == BidiClass::PDI && matching_initiator[first_char] >= 0) {
            before_level = explicit_levels[static_cast<std::size_t>(matching_initiator[first_char])];
        } else if (sequence.first_spliced == 0) {
            before_level = p.base_level;
        } else {
            before_level = explicit_levels[spliced[sequence.first_spliced - 1]];
        }
        int after_level;
        if (is_isolate_initiator(p.classes[last_char]) && matching_pdi[last_char] >= 0) {
            after_level = explicit_levels[static_cast<std::size_t>(matching_pdi[last_char])];
        } else if (is_isolate_initiator(p.classes[last_char])) {
            // The characters after it are inside an isolate this sequence does
            // not reach, so the paragraph level ends the sequence.
            after_level = p.base_level;
        } else if (sequence.last_spliced + 1 >= spliced.size()) {
            after_level = p.base_level;
        } else {
            after_level = explicit_levels[spliced[sequence.last_spliced + 1]];
        }
        sequence.sos = (std::max<int>(explicit_levels[first_char], before_level) % 2) ? BidiClass::R
                                                                                     : BidiClass::L;
        sequence.eos = (std::max<int>(explicit_levels[last_char], after_level) % 2) ? BidiClass::R
                                                                                    : BidiClass::L;

        const std::size_t length = sequence.positions.size();
        std::vector<BidiClass>& t = types;  // indexed by spliced position
        auto type_at = [&](std::size_t k) -> BidiClass& { return t[sequence.positions[k]]; };

        // W1: a nonspacing mark takes the type of what precedes it.
        for (std::size_t k = 0; k < length; ++k) {
            if (type_at(k) != BidiClass::NSM) continue;
            BidiClass previous = k == 0 ? sequence.sos : type_at(k - 1);
            if (is_isolate_formatting(previous)) previous = BidiClass::ON;
            type_at(k) = previous;
        }
        // W2: a European number after an Arabic letter is an Arabic number.
        for (std::size_t k = 0; k < length; ++k) {
            if (type_at(k) != BidiClass::EN) continue;
            BidiClass previous = sequence.sos;
            for (std::size_t j = k; j-- > 0;) {
                const BidiClass candidate = type_at(j);
                if (candidate == BidiClass::L || candidate == BidiClass::R ||
                    candidate == BidiClass::AL) {
                    previous = candidate;
                    break;
                }
            }
            if (previous == BidiClass::AL) type_at(k) = BidiClass::AN;
        }
        // W3: an Arabic letter is a right-to-left character from here on.
        for (std::size_t k = 0; k < length; ++k) {
            if (type_at(k) == BidiClass::AL) type_at(k) = BidiClass::R;
        }
        // W4: a separator between two numbers of one kind is that number.
        for (std::size_t k = 1; k + 1 < length; ++k) {
            const BidiClass c = type_at(k);
            if (c == BidiClass::ES && type_at(k - 1) == BidiClass::EN &&
                type_at(k + 1) == BidiClass::EN) {
                type_at(k) = BidiClass::EN;
            } else if (c == BidiClass::CS) {
                if (type_at(k - 1) == BidiClass::EN && type_at(k + 1) == BidiClass::EN) {
                    type_at(k) = BidiClass::EN;
                } else if (type_at(k - 1) == BidiClass::AN && type_at(k + 1) == BidiClass::AN) {
                    type_at(k) = BidiClass::AN;
                }
            }
        }
        // W5: a terminator next to a European number is that number.
        for (std::size_t k = 0; k < length;) {
            if (type_at(k) != BidiClass::ET) {
                ++k;
                continue;
            }
            std::size_t end = k;
            while (end + 1 < length && type_at(end + 1) == BidiClass::ET) ++end;
            const bool next_to_en = (k > 0 && type_at(k - 1) == BidiClass::EN) ||
                                    (end + 1 < length && type_at(end + 1) == BidiClass::EN);
            if (next_to_en) {
                for (std::size_t j = k; j <= end; ++j) type_at(j) = BidiClass::EN;
            }
            k = end + 1;
        }
        // W6: the separators and terminators left are neutral.
        for (std::size_t k = 0; k < length; ++k) {
            const BidiClass c = type_at(k);
            if (c == BidiClass::ES || c == BidiClass::ET || c == BidiClass::CS) {
                type_at(k) = BidiClass::ON;
            }
        }
        // W7: a European number after a left-to-right character is one.
        for (std::size_t k = 0; k < length; ++k) {
            if (type_at(k) != BidiClass::EN) continue;
            BidiClass previous = sequence.sos;
            for (std::size_t j = k; j-- > 0;) {
                const BidiClass candidate = type_at(j);
                if (candidate == BidiClass::L || candidate == BidiClass::R) {
                    previous = candidate;
                    break;
                }
            }
            if (previous == BidiClass::L) type_at(k) = BidiClass::L;
        }

        // N0: a bracket pair takes a direction from what it encloses.
        if (text != nullptr) {
            struct Pair {
                std::size_t open;
                std::size_t close;
            };
            std::vector<Pair> pairs;
            bool bracket_overflow = false;
            {
                // BD16's stack holds 63 opening brackets. Beyond that UAX #9
                // lets an implementation stop pairing; this one drops every
                // pair in the sequence, which is what the conformance suite
                // (BidiCharacterTest's 64-deep nesting case) expects.
                constexpr std::size_t kMaxBracketDepth = 63;
                std::vector<std::size_t> openers;
                for (std::size_t k = 0; k < length; ++k) {
                    // BD16's pairs are of brackets that are still neutral: an
                    // explicit override (X4/X5) has already made this one L or
                    // R, and N0 must not move it back.
                    if (type_at(k) != BidiClass::ON) continue;
                    const uint32_t cp = canonical_bracket((*text)[spliced[sequence.positions[k]]]);
                    const BidiBracketPair* bracket = find_bracket(cp);
                    if (bracket == nullptr) continue;
                    if (bracket->opening) {
                        if (openers.size() >= kMaxBracketDepth) {
                            bracket_overflow = true;
                            break;
                        }
                        openers.push_back(k);
                    } else {
                        for (std::size_t o = openers.size(); o-- > 0;) {
                            const uint32_t open_cp =
                                canonical_bracket((*text)[spliced[sequence.positions[openers[o]]]]);
                            const BidiBracketPair* candidate = find_bracket(open_cp);
                            if (candidate != nullptr && candidate->paired == cp) {
                                pairs.push_back(Pair{openers[o], k});
                                openers.resize(o);
                                break;
                            }
                        }
                    }
                }
            }
            std::sort(pairs.begin(), pairs.end(),
                      [](const Pair& a, const Pair& b) { return a.open < b.open; });
            if (bracket_overflow) pairs.clear();
            for (const Pair& pair : pairs) {
                const BidiClass embedding =
                    (p.levels[spliced[sequence.positions[pair.open]]] % 2) ? BidiClass::R
                                                                          : BidiClass::L;
                const BidiClass opposite =
                    embedding == BidiClass::L ? BidiClass::R : BidiClass::L;
                bool found_embedding = false;
                bool found_opposite = false;
                for (std::size_t k = pair.open + 1; k < pair.close; ++k) {
                    const BidiClass kind = strong_of(type_at(k));
                    if (kind == embedding) {
                        found_embedding = true;
                        break;
                    }
                    if (kind == opposite) found_opposite = true;
                }
                BidiClass resolved = kNeutral;
                if (found_embedding) {
                    resolved = embedding;
                } else if (found_opposite) {
                    BidiClass preceding = sequence.sos;
                    for (std::size_t k = pair.open; k-- > 0;) {
                        const BidiClass kind = strong_of(type_at(k));
                        if (kind == BidiClass::L || kind == BidiClass::R) {
                            preceding = kind;
                            break;
                        }
                    }
                    resolved = preceding == opposite ? opposite : embedding;
                }
                if (resolved == kNeutral) continue;
                type_at(pair.open) = resolved;
                type_at(pair.close) = resolved;
                // The marks that follow a resolved bracket go with it.
                for (std::size_t k = pair.open + 1; k < length; ++k) {
                    if (classes_in[spliced[sequence.positions[k]]] != BidiClass::NSM) break;
                    type_at(k) = resolved;
                }
                for (std::size_t k = pair.close + 1; k < length; ++k) {
                    if (classes_in[spliced[sequence.positions[k]]] != BidiClass::NSM) break;
                    type_at(k) = resolved;
                }
            }
        }

        // N1 and N2: the neutral runs between two strong types.
        for (std::size_t k = 0; k < length;) {
            if (!is_ni(type_at(k))) {
                ++k;
                continue;
            }
            std::size_t end = k;
            while (end + 1 < length && is_ni(type_at(end + 1))) ++end;
            const BidiClass before = k == 0 ? sequence.sos : strong_of(type_at(k - 1));
            const BidiClass after =
                end + 1 >= length ? sequence.eos : strong_of(type_at(end + 1));
            if (before == after && (before == BidiClass::L || before == BidiClass::R)) {
                for (std::size_t j = k; j <= end; ++j) type_at(j) = before;
            } else {
                for (std::size_t j = k; j <= end; ++j) {
                    const int level = explicit_levels[spliced[sequence.positions[j]]];
                    type_at(j) = (level % 2) ? BidiClass::R : BidiClass::L;
                }
            }
            k = end + 1;
        }

        // I1 and I2: the levels follow the resolved types.
        for (std::size_t k = 0; k < length; ++k) {
            const std::size_t index = spliced[sequence.positions[k]];
            int level = explicit_levels[index];
            if (level % 2 == 0) {
                if (type_at(k) == BidiClass::R) {
                    level += 1;
                } else if (type_at(k) == BidiClass::AN || type_at(k) == BidiClass::EN) {
                    level += 2;
                }
            } else if (type_at(k) == BidiClass::L || type_at(k) == BidiClass::EN ||
                       type_at(k) == BidiClass::AN) {
                level += 1;
            }
            p.levels[index] = static_cast<uint8_t>(level);
        }
    }

    // L1: the whitespace at the end of the paragraph, and what precedes a
    // separator, goes back to the paragraph level.
    bool trailing = true;
    for (std::size_t k = spliced.size(); k-- > 0;) {
        const std::size_t index = spliced[k];
        const BidiClass original = classes_in[index];
        if (original == BidiClass::B || original == BidiClass::S) {
            p.levels[index] = static_cast<uint8_t>(p.base_level);
            trailing = true;
        } else if (original == BidiClass::WS || is_isolate_formatting(original)) {
            if (trailing) p.levels[index] = static_cast<uint8_t>(p.base_level);
        } else {
            trailing = false;
        }
    }
    return p;
}

std::vector<uint32_t> decode_utf8(std::string_view text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        int length = 0;
        uint32_t cp = 0;
        if (lead < 0x80) {
            out.push_back(lead);
            ++i;
            continue;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
            cp = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            cp = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            cp = lead & 0x07u;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + static_cast<std::size_t>(length) > text.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool valid = true;
        for (int k = 1; k < length; ++k) {
            const unsigned char continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (continuation & 0x3Fu);
        }
        if (!valid) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += static_cast<std::size_t>(length);
    }
    return out;
}

} // namespace

BidiParagraph bidi_paragraph(const std::u32string& text, int paragraph_level,
                             int fallback_level) {
    std::vector<BidiClass> classes;
    classes.reserve(text.size());
    for (uint32_t cp : text) classes.push_back(bidi_class(cp));
    return resolve(classes, &text, paragraph_level, fallback_level);
}

BidiParagraph bidi_paragraph_utf8(std::string_view text, int paragraph_level,
                                  int fallback_level) {
    const std::u32string cps = [&] {
        const std::vector<uint32_t> decoded = decode_utf8(text);
        return std::u32string(decoded.begin(), decoded.end());
    }();
    return bidi_paragraph(cps, paragraph_level, fallback_level);
}

BidiParagraph bidi_paragraph_classes(const std::vector<BidiClass>& classes, int paragraph_level,
                                     int fallback_level) {
    return resolve(classes, nullptr, paragraph_level, fallback_level);
}

std::vector<std::size_t> bidi_reorder(const BidiParagraph& paragraph) {
    return bidi_reorder(paragraph, 0, paragraph.size());
}

std::vector<std::size_t> bidi_reorder(const BidiParagraph& paragraph, std::size_t first,
                                      std::size_t last) {
    std::vector<std::size_t> order;
    std::vector<std::size_t> spliced;
    for (std::size_t i = first; i < last && i < paragraph.size(); ++i) {
        if (paragraph.removed[i]) continue;
        spliced.push_back(i);
        order.push_back(i);
    }
    if (spliced.size() < 2) return order;

    int highest = 0;
    int lowest_odd = 126;
    for (std::size_t index : spliced) {
        const int level = paragraph.levels[index];
        highest = std::max(highest, level);
        if (level % 2 == 1) lowest_odd = std::min(lowest_odd, level);
    }
    for (int level = highest; level >= lowest_odd; --level) {
        for (std::size_t k = 0; k < order.size();) {
            if (paragraph.levels[order[k]] < level) {
                ++k;
                continue;
            }
            std::size_t end = k;
            while (end + 1 < order.size() && paragraph.levels[order[end + 1]] >= level) ++end;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(k),
                         order.begin() + static_cast<std::ptrdiff_t>(end + 1));
            k = end + 1;
        }
    }
    return order;
}

void bidi_apply_l1_line(const BidiParagraph& paragraph, std::size_t first, std::size_t last,
                        std::vector<uint8_t>& levels_out) {
    levels_out = paragraph.levels;
    for (std::size_t i = last; i-- > first && i < paragraph.size();) {
        if (paragraph.removed[i]) continue;
        const BidiClass original = paragraph.classes[i];
        if (original == BidiClass::WS || is_isolate_formatting(original)) {
            levels_out[i] = static_cast<uint8_t>(paragraph.base_level);
            continue;
        }
        break;
    }
}

} // namespace litebird
