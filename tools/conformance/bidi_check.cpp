// Runs the Unicode Bidirectional Algorithm against the two conformance suites
// the Unicode Consortium publishes, and prints the score.
//
//   build/bidi_check tests/bidi/BidiTest.txt tests/bidi/BidiCharacterTest.txt
//
// BidiTest.txt states its cases as sequences of Bidi_Class values (so rule N0
// has nothing to pair: the file assumes no paired brackets are present);
// BidiCharacterTest.txt states them as code points, which is where the
// bracket pairs and mirroring are tested. Both give the expected levels and
// the expected visual order, so neither needs a reference implementation to
// compare against: the expectations are Unicode's own.
//
// Options:
//   --show N    print the first N failures with their inputs and expectations
//   --quiet     print only the score line
//   --filter X  run only the cases whose text contains X (a substring of the
//               input line, for debugging one case)
//   --jobs N    ignored; accepted so the runner interface matches the others

#include "litebird/text/Bidi.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using litebird::BidiClass;

const struct {
    const char* name;
    BidiClass value;
} kClassNames[] = {
    {"L", BidiClass::L},     {"R", BidiClass::R},     {"AL", BidiClass::AL},
    {"EN", BidiClass::EN},   {"ES", BidiClass::ES},   {"ET", BidiClass::ET},
    {"AN", BidiClass::AN},   {"CS", BidiClass::CS},   {"NSM", BidiClass::NSM},
    {"BN", BidiClass::BN},   {"B", BidiClass::B},     {"S", BidiClass::S},
    {"WS", BidiClass::WS},   {"ON", BidiClass::ON},   {"LRE", BidiClass::LRE},
    {"RLE", BidiClass::RLE}, {"LRO", BidiClass::LRO}, {"RLO", BidiClass::RLO},
    {"PDF", BidiClass::PDF}, {"LRI", BidiClass::LRI}, {"RLI", BidiClass::RLI},
    {"FSI", BidiClass::FSI}, {"PDI", BidiClass::PDI},
};

bool class_from_name(const std::string& name, BidiClass& out) {
    for (const auto& entry : kClassNames) {
        if (name == entry.name) {
            out = entry.value;
            return true;
        }
    }
    return false;
}

struct Levels {
    std::vector<int> levels;  // -1 for 'x'
    std::vector<std::size_t> order;
};

void print_expected(const Levels& expected) {
    std::cout << "      levels:";
    for (int level : expected.levels) {
        if (level < 0) {
            std::cout << " x";
        } else {
            std::cout << ' ' << level;
        }
    }
    std::cout << "\n      order:";
    for (std::size_t index : expected.order) std::cout << ' ' << index;
    std::cout << "\n";
}

std::string join_levels(const std::vector<uint8_t>& levels, const std::vector<bool>& removed) {
    std::string out;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (removed[i]) {
            out += " x";
        } else {
            out += ' ';
            out += std::to_string(levels[i]);
        }
    }
    return out;
}

std::string join_order(const std::vector<std::size_t>& order) {
    std::string out;
    for (std::size_t index : order) {
        out += ' ';
        out += std::to_string(index);
    }
    return out;
}

int failures_shown = 0;
int show_limit = 0;

bool check_case(const std::string& label, const litebird::BidiParagraph& paragraph,
                const Levels& expected) {
    bool ok = true;
    if (paragraph.levels.size() != expected.levels.size()) {
        ok = false;
    } else {
        for (std::size_t i = 0; i < expected.levels.size(); ++i) {
            const bool removed = expected.levels[i] < 0;
            if (paragraph.removed[i] != removed) {
                ok = false;
                break;
            }
            if (!removed && paragraph.levels[i] != expected.levels[i]) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        const std::vector<std::size_t> order = litebird::bidi_reorder(paragraph);
        if (order != expected.order) ok = false;
    }
    if (ok) return true;
    ++failures_shown;
    if (show_limit > 0 && failures_shown <= show_limit) {
        std::cout << "  FAIL " << label << "\n";
        print_expected(expected);
        std::cout << "      got levels:" << join_levels(paragraph.levels, paragraph.removed) << "\n";
        std::cout << "      got order:" << join_order(litebird::bidi_reorder(paragraph)) << "\n";
    }
    return false;
}

bool matches_filter(const std::string& line, const std::string& filter) {
    return filter.empty() || line.find(filter) != std::string::npos;
}

struct Score {
    long passed{0};
    long total{0};
};

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

std::string trim(const std::string& text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

std::vector<std::string> tokens(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) out.push_back(token);
    return out;
}

Score run_bidi_test(const std::string& path, const std::string& filter) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "bidi_check: cannot read " << path << "\n";
        std::exit(2);
    }
    Score score;
    Levels expected;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("@Levels:", 0) == 0) {
            expected.levels.clear();
            expected.order.clear();
            for (const std::string& token : tokens(line.substr(8))) {
                expected.levels.push_back(token == "x" ? -1 : std::atoi(token.c_str()));
            }
            continue;
        }
        if (line.rfind("@Reorder:", 0) == 0) {
            for (const std::string& token : tokens(line.substr(9))) {
                expected.order.push_back(static_cast<std::size_t>(std::atol(token.c_str())));
            }
            continue;
        }
        if (line[0] == '@') continue;
        if (!matches_filter(line, filter)) continue;

        const std::size_t semicolon = line.find(';');
        if (semicolon == std::string::npos) continue;
        const std::vector<std::string> names = tokens(line.substr(0, semicolon));
        const int bitset = static_cast<int>(std::strtol(line.c_str() + semicolon + 1, nullptr, 16));

        std::vector<BidiClass> classes;
        bool known = true;
        for (const std::string& name : names) {
            BidiClass value;
            if (!class_from_name(name, value)) {
                known = false;
                break;
            }
            classes.push_back(value);
        }
        if (!known) continue;
        if (expected.levels.size() != classes.size()) {
            std::cerr << "bidi_check: @Levels does not fit the case: " << line << "\n";
            std::exit(2);
        }

        for (int bit : {1, 2, 4}) {
            if ((bitset & bit) == 0) continue;
            // 1 = auto-LTR (P2/P3, LTR when nothing strong), 2 = explicit LTR, 4 = explicit RTL.
            const int paragraph_level = bit == 1 ? -1 : (bit == 2 ? 0 : 1);
            const litebird::BidiParagraph paragraph =
                litebird::bidi_paragraph_classes(classes, paragraph_level);
            ++score.total;
            if (check_case(trim(line), paragraph, expected)) ++score.passed;
        }
    }
    return score;
}

Score run_bidi_character_test(const std::string& path, const std::string& filter) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "bidi_check: cannot read " << path << "\n";
        std::exit(2);
    }
    Score score;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!matches_filter(trim(line), filter)) continue;
        const std::vector<std::string> fields = split(line, ';');
        if (fields.size() < 5) continue;

        std::u32string text;
        for (const std::string& token : tokens(fields[0])) {
            text.push_back(static_cast<char32_t>(std::strtoul(token.c_str(), nullptr, 16)));
        }
        const int direction = std::atoi(trim(fields[1]).c_str());
        const int expected_base = std::atoi(trim(fields[2]).c_str());

        Levels expected;
        for (const std::string& token : tokens(fields[3])) {
            expected.levels.push_back(token == "x" ? -1 : std::atoi(token.c_str()));
        }
        for (const std::string& token : tokens(fields[4])) {
            expected.order.push_back(static_cast<std::size_t>(std::atol(token.c_str())));
        }

        const int paragraph_level = direction == 0 ? 0 : (direction == 1 ? 1 : -1);
        const litebird::BidiParagraph paragraph = litebird::bidi_paragraph(text, paragraph_level);
        ++score.total;
        bool ok = check_case(trim(line), paragraph, expected);
        if (ok && paragraph.base_level != expected_base) {
            ok = false;
            ++failures_shown;
            if (show_limit > 0 && failures_shown <= show_limit) {
                std::cout << "  FAIL (paragraph level) " << trim(line) << "\n      expected base "
                          << expected_base << ", got " << paragraph.base_level << "\n";
            }
        }
        if (ok) ++score.passed;
    }
    return score;
}

} // namespace

int main(int argc, char** argv) {
    std::string test_path;
    std::string character_path;
    std::string filter;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--show" && i + 1 < argc) {
            show_limit = std::atoi(argv[++i]);
        } else if (arg == "--filter" && i + 1 < argc) {
            filter = argv[++i];
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "bidi_check: unknown option " << arg << "\n";
            return 2;
        } else if (test_path.empty()) {
            test_path = arg;
        } else {
            character_path = arg;
        }
    }
    if (test_path.empty()) {
        std::cerr << "usage: bidi_check <BidiTest.txt> [BidiCharacterTest.txt] "
                     "[--show N] [--filter X] [--quiet]\n";
        return 2;
    }

    const Score test = run_bidi_test(test_path, filter);
    if (!quiet) {
        std::printf("BidiTest (Bidi_Class sequences):            %ld/%ld (%.1f%%)\n", test.passed,
                    test.total, test.total ? 100.0 * static_cast<double>(test.passed) / test.total : 0.0);
    }
    Score character;
    if (!character_path.empty()) {
        character = run_bidi_character_test(character_path, filter);
        if (!quiet) {
            std::printf("BidiCharacterTest (code points, brackets):   %ld/%ld (%.1f%%)\n",
                        character.passed, character.total,
                        character.total ? 100.0 * static_cast<double>(character.passed) / character.total
                                        : 0.0);
        }
    }
    const long passed = test.passed + character.passed;
    const long total = test.total + character.total;
    if (quiet) {
        std::printf("bidi: %ld/%ld (%.1f%%)\n", passed, total,
                    total ? 100.0 * static_cast<double>(passed) / total : 0.0);
    } else {
        std::printf("Bidi conformance, total: %ld/%ld (%.1f%%)\n", passed, total,
                    total ? 100.0 * static_cast<double>(passed) / total : 0.0);
    }
    return passed == total ? 0 : 1;
}
