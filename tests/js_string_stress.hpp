#pragma once

// Two stress tests for JsString, the text of a JavaScript string value.
//
// Strings share blocks of text: `a + b` can write b into the block a lives in,
// and a slice can be a range of its parent. Script must never be able to tell.
// The first test here checks that against a plain model; the second checks it
// with several threads at work on the same strings.
//
// They are in a header because they are built twice: into the unit tests
// (tests/test_suite.cpp) and into a small binary of their own
// (tests/js_string_stress_main.cpp, `make tsan`) that ThreadSanitizer and
// LeakSanitizer can run, which needs nothing but JsString.

#include "litebird/js/JsString.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace litebird_tests {

/// UTF-16 to UTF-8, written without looking at JsString: a surrogate pair is
/// one four-byte character, a half on its own is three bytes.
inline std::string js_string_test_encode(const std::u16string& units) {
    std::string out;
    for (size_t i = 0; i < units.size(); ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            ++i;
        }
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
    return out;
}

/// Keeps a pool of strings, each next to a std::u16string holding what the
/// string should be, applies random operations to both and compares after
/// every one. Every 50 operations it compares the whole pool, which is what
/// catches an operation on one string disturbing another.
inline void js_string_model_test() {
    using litebird::JsString;
    using litebird::Utf16View;

    auto sign = [](int v) { return v < 0 ? -1 : v > 0 ? 1 : 0; };

    struct Entry {
        JsString text;
        std::u16string model;
    };
    auto same = [](const Entry& entry) {
        const JsString& text = entry.text;
        const std::u16string& model = entry.model;
        if (text.length() != model.size()) return false;
        if (text.view() != js_string_test_encode(model)) return false;      // the one way to write this text
        if (text.is_ascii() != (text.size() == model.size())) return false;
        for (size_t i = 0; i < model.size(); ++i) {
            if (text.unit_at(i) != model[i]) return false;
        }
        Utf16View view(text);
        return view.size() == model.size() && view.copy() == model;
    };

    const std::u16string pieces[] = {
        u"", u"a", u"bc", u"\u00e9", u"\u4e2d\u6587", u"\U0001F600", std::u16string(1, char16_t(0xD83D)),
        std::u16string(1, char16_t(0xDE00)), u"0123456789abcdef", u"<td>", u"\uffff", u"\ue000", u"\ud7ff",
        std::u16string(40, u'x'), std::u16string(300, u'y'), u"z\u00e9z\u00e9z\u00e9z\u00e9z\u00e9z\u00e9z\u00e9z\u00e9",
        std::u16string(u"q") + char16_t(0xD83D), char16_t(0xDE00) + std::u16string(u"r"),
    };
    const size_t piece_count = sizeof(pieces) / sizeof(pieces[0]);

    for (uint64_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
        uint64_t state = seed * 0x9E3779B97F4A7C15ull + 12345;
        auto rnd = [&state](size_t n) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return static_cast<size_t>((state >> 11) % n);
        };
        std::vector<Entry> pool(24);
        for (auto& entry : pool) {
            entry.model = pieces[rnd(piece_count)];
            entry.text = JsString::from_utf16(entry.model);
        }
        for (int step = 0; step < 10000; ++step) {
            const size_t i = rnd(pool.size());
            const size_t j = rnd(pool.size());
            const size_t k = rnd(pool.size());
            const std::u16string& piece = pieces[rnd(piece_count)];
            Entry result;
            switch (rnd(12)) {
                case 0:     // a + b
                case 1:
                    result.text = JsString::concat(pool[j].text, pool[k].text);
                    result.model = pool[j].model + pool[k].model;
                    break;
                case 2:     // a + raw text
                case 3:
                    result.text = JsString::concat(pool[j].text, std::string_view(js_string_test_encode(piece)));
                    result.model = pool[j].model + piece;
                    break;
                case 4:     // raw text + a
                    result.text = JsString::concat(std::string_view(js_string_test_encode(piece)), pool[j].text);
                    result.model = piece + pool[j].model;
                    break;
                case 5:     // a piece from the middle, and a second piece of the same length from elsewhere
                case 6: {
                    const size_t length = pool[j].model.size();
                    const size_t begin = rnd(length + 1);
                    const size_t count = rnd(length - begin + 1);
                    result.text = pool[j].text.substring(begin, begin + count);
                    result.model = pool[j].model.substr(begin, count);
                    const size_t other_begin = rnd(length - count + 1);
                    const JsString other = pool[j].text.substring(other_begin, other_begin + count);
                    const std::u16string other_model = pool[j].model.substr(other_begin, count);
                    assert((result.text == other) == (result.model == other_model));
                    assert(sign(JsString::compare(result.text.view(), other.view())) == sign(result.model.compare(other_model)));
                    break;
                }
                case 7: {   // what a tokenizer does: drop a little from the front
                    const size_t length = pool[j].model.size();
                    const size_t begin = std::min(length, rnd(6));
                    result.text = pool[j].text.substring(begin, length);
                    result.model = pool[j].model.substr(begin);
                    break;
                }
                case 8: {   // drop a little from the end
                    const size_t length = pool[j].model.size();
                    const size_t end = length - std::min(length, rnd(6));
                    result.text = pool[j].text.substring(0, end);
                    result.model = pool[j].model.substr(0, end);
                    break;
                }
                case 9:     // a run of appends to one string, reading in between
                    result = pool[j];
                    for (int r = 0; r < 8; ++r) {
                        result.text = JsString::concat(result.text, JsString::from_utf16(piece));
                        result.model += piece;
                        if (!result.model.empty()) {
                            const size_t at = rnd(result.model.size());
                            assert(result.text.unit_at(at) == result.model[at]);
                        }
                    }
                    break;
                case 10:    // through UTF-8 and back
                    result.text = JsString(pool[j].text.str());
                    result.model = pool[j].model;
                    break;
                default: {  // order and equality agree with the model
                    const int by_text = JsString::compare(pool[j].text.view(), pool[k].text.view());
                    assert(sign(by_text) == sign(pool[j].model.compare(pool[k].model)));
                    assert((pool[j].text == pool[k].text) == (pool[j].model == pool[k].model));
                    result = pool[j];
                    break;
                }
            }
            if (result.model.size() > 3000) {
                result.text = result.text.substring(0, 200);
                result.model.resize(200);
            }
            assert(same(result));
            pool[i] = std::move(result);
            if (step % 50 == 0) {
                for (const auto& entry : pool) assert(same(entry));
            }
        }
        for (const auto& entry : pool) assert(same(entry));
    }

    // Order. `<` compares UTF-16 code units, and the text is stored as UTF-8;
    // the two orders part ways exactly around surrogates, so that is where
    // these short strings are drawn from. Every pair of strings is compared.
    {
        const char16_t alphabet[] = {0x61, 0xD7FF, 0xD83D, 0xDBFF, 0xDC00, 0xDE00, 0xE000, 0xFFFF};
        std::vector<Entry> all;
        for (size_t length = 0; length <= 3; ++length) {
            size_t combinations = 1;
            for (size_t n = 0; n < length; ++n) combinations *= 8;
            for (size_t code = 0; code < combinations; ++code) {
                Entry entry;
                size_t rest = code;
                for (size_t n = 0; n < length; ++n) {
                    entry.model += alphabet[rest % 8];
                    rest /= 8;
                }
                entry.text = JsString::from_utf16(entry.model);
                assert(same(entry));
                all.push_back(std::move(entry));
            }
        }
        assert(all.size() == 585);
        for (const auto& a : all) {
            for (const auto& b : all) {
                assert(sign(JsString::compare(a.text.view(), b.text.view())) == sign(a.model.compare(b.model)));
                assert((a.text == b.text) == (a.model == b.model));
            }
        }
    }
}

/// Suspended async functions and workers run on their own threads, and a
/// string made on one can be read, extended and dropped on another.
inline void js_string_thread_test() {
    using litebird::JsString;

    // Part one: eight threads share the same few strings. Each copies them,
    // appends to them, indexes them (racing to build the UTF-16 form) and takes
    // pieces. Every result must be what it would be on one thread.
    {
        std::string accents;
        for (int i = 0; i < 400; ++i) accents += "na\xC3\xAFve \xF0\x9F\x98\x80 ";        // "naïve 😀 ", 9 code units
        const JsString shared_ascii(std::string(1000, 'a'));
        const JsString shared_wide(accents);
        const JsString shared_short("0123456789");       // a 16-byte block: room for one short append
        std::atomic<int> failures{0};
        std::atomic<bool> go{false};

        auto worker = [&](int id) {
            while (!go.load()) std::this_thread::yield();
            const std::string mine = "<" + std::to_string(id) + ">";
            for (int round = 0; round < 3000; ++round) {
                JsString grown = JsString::concat(shared_short, JsString(mine));
                if (grown.view() != "0123456789" + mine) ++failures;
                JsString grown_wide = JsString::concat(shared_wide, JsString(mine));
                if (grown_wide.length() != 3600 + mine.size() || grown_wide.unit_at(3600) != '<') ++failures;
                if (grown_wide.unit_at(2) != 0xEF || grown_wide.unit_at(6) != 0xD83D || grown_wide.unit_at(7) != 0xDE00) ++failures;

                // The first reader builds the UTF-16 form, while others read.
                const size_t at = static_cast<size_t>((round * 7 + id) % 400) * 9;
                if (shared_wide.unit_at(at) != 'n' || shared_wide.unit_at(at + 2) != 0xEF) ++failures;
                if (shared_ascii.unit_at(static_cast<size_t>(round % 1000)) != 'a') ++failures;

                // Pieces: the long one shares the block, the short one is a copy.
                JsString rest = shared_wide.substring(at, 3600);
                if (rest.length() != 3600 - at || rest.unit_at(0) != 'n') ++failures;
                JsString small = shared_wide.substring(at, at + 5);
                if (small.view() != "na\xC3\xAFve") ++failures;
                JsString longer = JsString::concat(rest, JsString(mine));
                if (longer.length() != rest.length() + mine.size() || longer.unit_at(longer.length() - 1) != '>') ++failures;

                // A private string, built by appending, as script does.
                JsString own;
                for (int n = 0; n < 20; ++n) own = JsString::concat(own, JsString(mine));
                if (own.length() != 20 * mine.size() || own.unit_at(own.length() - 1) != '>') ++failures;

                // Handles to the shared strings come and go.
                for (int n = 0; n < 40; ++n) {
                    JsString copy = (n & 1) ? shared_wide : shared_ascii;
                    JsString moved = std::move(copy);
                    if (moved.length() != ((n & 1) ? 3600u : 1000u)) ++failures;
                }
            }
        };
        std::vector<std::thread> threads;
        for (int id = 0; id < 8; ++id) threads.emplace_back(worker, id);
        go.store(true);
        for (auto& thread : threads) thread.join();

        assert(failures.load() == 0);
        // Nothing the threads did changed the strings they shared.
        assert(shared_short.view() == "0123456789");
        assert(shared_wide.view() == accents && shared_wide.length() == 3600);
        assert(shared_ascii.view() == std::string(1000, 'a'));
        // Every handle the threads took has been given back: a count that lost
        // an update would free the text too early, or never.
        assert(shared_wide.use_count() == 1 && shared_ascii.use_count() == 1);
    }

    // Part two: the race for a block's free room. Two threads append to the
    // same fresh string at the same moment, over and over. Both appends fit in
    // the room after the text, and exactly one of them may be written there;
    // if both were, one thread's result would end in the other's suffix.
    //
    // Each round has 64 strings. One thread takes them first to last and the
    // other last to first, so they meet in the middle of every round.
    {
        constexpr int kStrings = 64;
        constexpr int kRounds = 4000;
        std::vector<JsString> bases(kStrings);
        std::vector<JsString> results[2] = {std::vector<JsString>(kStrings), std::vector<JsString>(kStrings)};
        const char* const suffix_text[2] = {"<one>", "<two>"};
        std::atomic<int> round{0};                  // the racers run round r once this reaches r
        std::atomic<int> finished[2] = {{0}, {0}};
        std::atomic<bool> stop{false};

        auto racer = [&](int id) {
            const JsString suffix(suffix_text[id]);
            for (int r = 1;; ++r) {
                while (round.load(std::memory_order_acquire) < r) {
                    if (stop.load(std::memory_order_acquire)) return;
                    std::this_thread::yield();
                }
                for (int n = 0; n < kStrings; ++n) {
                    const int at = id == 0 ? n : kStrings - 1 - n;
                    results[id][at] = JsString::concat(bases[at], suffix);
                }
                finished[id].store(r, std::memory_order_release);
            }
        };
        std::thread one(racer, 0);
        std::thread two(racer, 1);
        int in_place = 0;
        for (int r = 1; r <= kRounds; ++r) {
            for (int n = 0; n < kStrings; ++n) bases[n] = JsString("text " + std::to_string(n % 10));     // 6 bytes of 16
            round.store(r, std::memory_order_release);
            while (finished[0].load(std::memory_order_acquire) < r || finished[1].load(std::memory_order_acquire) < r) {
                std::this_thread::yield();
            }
            for (int n = 0; n < kStrings; ++n) {
                const std::string text = "text " + std::to_string(n % 10);
                assert(bases[n].view() == text);
                assert(results[0][n].view() == text + "<one>");
                assert(results[1][n].view() == text + "<two>");
                // At most one of the two was written into the string's own block.
                const bool first_shares = results[0][n].shares_text_with(bases[n]);
                const bool second_shares = results[1][n].shares_text_with(bases[n]);
                assert(!(first_shares && second_shares));
                in_place += first_shares || second_shares;
            }
        }
        stop.store(true, std::memory_order_release);
        one.join();
        two.join();
        // And exactly one: the room was there, so somebody got it.
        assert(in_place == kStrings * kRounds);
    }

    // Part three: the race to build the UTF-16 form. A string that is not
    // ASCII gets a UTF-16 copy the first time it is indexed, kept with its
    // block; a string that was appended to in place has a copy that stops
    // short and is brought up to date at the next index. Here two threads
    // index the same strings at the same moment: fresh ones, where both want
    // to build the copy, and extended ones, where both want to lengthen it.
    {
        constexpr int kStrings = 32;
        constexpr int kRounds = 1500;
        std::vector<JsString> strings(kStrings);
        std::atomic<int> round{0};
        std::atomic<int> finished[2] = {{0}, {0}};
        std::atomic<bool> stop{false};
        std::atomic<int> failures{0};

        // Every string is "é😀" (3 code units, 6 bytes), then digits, then "!".
        auto reader = [&](int id) {
            for (int r = 1;; ++r) {
                while (round.load(std::memory_order_acquire) < r) {
                    if (stop.load(std::memory_order_acquire)) return;
                    std::this_thread::yield();
                }
                for (int n = 0; n < kStrings; ++n) {
                    const JsString& text = strings[id == 0 ? n : kStrings - 1 - n];
                    const size_t length = text.length();
                    if (text.unit_at(length - 1) != '!' || text.unit_at(0) != 0xE9) ++failures;
                    if (text.unit_at(1) != 0xD83D || text.unit_at(2) != 0xDE00) ++failures;
                    for (size_t at = 3; at + 1 < length; at += 7) {
                        if (text.unit_at(at) != static_cast<char16_t>('0' + (at - 3) % 10)) ++failures;
                    }
                    // A piece that shares the block is found through the marks
                    // that are written along with the UTF-16 copy.
                    const JsString tail = text.substring(2, length);
                    if (tail.length() != length - 2 || tail.unit_at(0) != 0xDE00 || tail.unit_at(tail.length() - 1) != '!') ++failures;
                    const JsString most = text.substring(3, length);
                    if (!most.shares_text_with(text) || most.unit_at(0) != '0' || most.size() != length - 3) ++failures;
                }
                finished[id].store(r, std::memory_order_release);
            }
        };
        std::thread one(reader, 0);
        std::thread two(reader, 1);
        const std::string digits = "0123456789";
        for (int r = 1; r <= kRounds; ++r) {
            for (int n = 0; n < kStrings; ++n) {
                std::string body = "\xC3\xA9\xF0\x9F\x98\x80";
                for (int d = 0; d < 30 + (n % 5) * 10; ++d) body += digits[static_cast<size_t>(d % 10)];
                if (n % 2 == 0) {
                    strings[n] = JsString(body + "!");                      // never indexed
                } else {
                    // Grown by appending, indexed while it was shorter, then
                    // appended to again: its UTF-16 copy stops short.
                    JsString text;
                    for (size_t at = 0; at < body.size(); at += 6) text = JsString::concat(text, std::string_view(body).substr(at, 6));
                    assert(text.unit_at(1) == 0xD83D);
                    strings[n] = JsString::concat(text, JsString("!"));
                    assert(strings[n].shares_text_with(text));
                }
            }
            round.store(r, std::memory_order_release);
            while (finished[0].load(std::memory_order_acquire) < r || finished[1].load(std::memory_order_acquire) < r) {
                std::this_thread::yield();
            }
        }
        stop.store(true, std::memory_order_release);
        one.join();
        two.join();
        assert(failures.load() == 0);
        for (const auto& text : strings) assert(text.use_count() == 1);
    }
}

} // namespace litebird_tests
