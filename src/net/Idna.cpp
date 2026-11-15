// UTS #46 domain-to-ASCII for WHATWG URL parsing.
//
// Pipeline: percent-decoded UTF-8 scalar values -> UTS #46 mapping (the
// table in IdnaTables.cpp, built from the idna package's uts46data, Unicode
// 15.1.0) -> NFC -> per label: validity checks (leading mark, joiners,
// bidi, optional strict hyphen/STD3/length) -> Punycode. The all-ASCII fast
// path required by WHATWG (the domain is returned ASCII-lowercased regardless
// of what Unicode ToASCII would say) lives in domain_to_ascii.

#include "litebird/net/Idna.hpp"

#include "litebird/net/IdnaTables.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace litebird {
namespace {

// --- table lookups --------------------------------------------------------------

const Uts46Range* uts46_find(uint32_t cp) {
    std::size_t lo = 0, hi = uts46_range_count();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        if (cp < uts46_ranges()[mid].lo) {
            hi = mid;
        } else if (cp > uts46_ranges()[mid].hi) {
            lo = mid + 1;
        } else {
            return &uts46_ranges()[mid];
        }
    }
    return nullptr; // unreachable: the table covers 0..0x10FFFF
}

uint8_t combining_class(uint32_t cp) {
    std::size_t lo = 0, hi = combining_class_range_count();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        const Uts46Range2U8& r = combining_class_ranges()[mid];
        if (cp < r.lo) {
            hi = mid;
        } else if (cp > r.hi) {
            lo = mid + 1;
        } else {
            return r.payload;
        }
    }
    return 0;
}

bool is_mark(uint32_t cp) {
    std::size_t lo = 0, hi = mark_range_count();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        const Uts46Range2& r = mark_ranges()[mid];
        if (cp < r.lo) {
            hi = mid;
        } else if (cp > r.hi) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

const Uts46Decomp* decomp_find(uint32_t cp) {
    std::size_t lo = 0, hi = decomp_entry_count();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        const Uts46Decomp& e = decomp_entries()[mid];
        if (cp < e.cp) {
            hi = mid;
        } else if (cp > e.cp) {
            lo = mid + 1;
        } else {
            return &e;
        }
    }
    return nullptr;
}

bool composition_pair(uint32_t starter, uint32_t combining, uint32_t& out) {
    const uint64_t key = (static_cast<uint64_t>(starter) << 21) | combining;
    std::size_t lo = 0, hi = composition_pair_count();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        if (key < composition_pairs()[mid].key) {
            hi = mid;
        } else if (key > composition_pairs()[mid].key) {
            lo = mid + 1;
        } else {
            out = composition_pairs()[mid].composite;
            return true;
        }
    }
    return false;
}

uint8_t bidi_class(uint32_t cp) {
    std::size_t lo = 0, hi = bidi_range_count();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        const Uts46Range2U8& r = bidi_ranges()[mid];
        if (cp < r.lo) {
            hi = mid;
        } else if (cp > r.hi) {
            lo = mid + 1;
        } else {
            return r.payload;
        }
    }
    return 0; // no class: matches no UTS #46 bidi set
}

uint8_t joining_type(uint32_t cp) {
    std::size_t lo = 0, hi = joining_type_range_count();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        const Uts46Range2U8& r = joining_type_ranges()[mid];
        if (cp < r.lo) {
            hi = mid;
        } else if (cp > r.hi) {
            lo = mid + 1;
        } else {
            return r.payload;
        }
    }
    return 0;
}

// --- UTF-8 ----------------------------------------------------------------------

bool decode_utf8(std::string_view bytes, std::vector<uint32_t>& out) {
    out.clear();
    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t i = 0, n = bytes.size();
    while (i < n) {
        const unsigned char lead = p[i];
        if (lead < 0x80) {
            out.push_back(lead);
            i += 1;
            continue;
        }
        uint32_t cp = 0;
        std::size_t len = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            cp = lead & 0x1F;
            len = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            cp = lead & 0x0F;
            len = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            cp = lead & 0x07;
            len = 4;
        } else {
            return false; // stray continuation byte or 0xC0/0xC1/0xF5..
        }
        if (i + len > n) return false;
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char c = p[i + k];
            if (c < 0x80 || c > 0xBF) return false;
            cp = (cp << 6) | (c & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
        out.push_back(cp);
        i += len;
    }
    return true;
}

void append_utf8(std::string& out, uint32_t cp) {
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

// --- UTS #46 mapping --------------------------------------------------------------

bool uts46_map(const std::vector<uint32_t>& input, std::vector<uint32_t>& out) {
    out.clear();
    for (uint32_t cp : input) {
        const Uts46Range* range = uts46_find(cp);
        if (!range) return false;
        const bool std3_mapped = range->status == kUts46DisallowedStd3 && range->map != 0xFFFFFFFF;
        switch (range->status) {
            case kUts46Valid:
            case kUts46Deviation: // nontransitional processing: kept as-is
                out.push_back(cp);
                break;
            case kUts46DisallowedStd3: // no STD3 rules here: valid as-is, unless it maps
                if (!std3_mapped) out.push_back(cp);
                break;
            case kUts46Ignored:
                break;
            case kUts46Mapped:
            case kUts46Disallowed:
                break;
            default:
                return false;
        }
        if (range->status == kUts46Mapped || std3_mapped) {
            std::string bytes(reinterpret_cast<const char*>(mapping_pool() + range->map), range->map_len);
            std::vector<uint32_t> cps;
            if (!decode_utf8(bytes, cps)) return false;
            out.insert(out.end(), cps.begin(), cps.end());
        } else if (range->status == kUts46Disallowed) {
            return false;
        }
    }
    return true;
}

// --- NFC --------------------------------------------------------------------------

std::vector<uint32_t> nfc_normalize(const std::vector<uint32_t>& input) {
    // 1) Recursive canonical decomposition.
    std::vector<uint32_t> seq;
    seq.reserve(input.size() * 2);
    for (uint32_t cp : input) {
        if (const Uts46Decomp* entry = decomp_find(cp)) {
            const uint32_t* pool = decomp_pool();
            seq.insert(seq.end(), pool + entry->offset, pool + entry->offset + entry->length);
        } else {
            seq.push_back(cp);
        }
    }
    // 2) Canonical ordering of combining marks (stable swap by ccc).
    for (std::size_t i = 1; i < seq.size(); ++i) {
        uint32_t cp = seq[i];
        const uint8_t ccc = combining_class(cp);
        if (ccc == 0) continue;
        std::size_t j = i;
        while (j > 0) {
            const uint8_t prev = combining_class(seq[j - 1]);
            if (prev == 0 || prev <= ccc) break;
            seq[j] = seq[j - 1];
            seq[j - 1] = cp;
            --j;
        }
    }
    // 3) Canonical composition.
    std::vector<uint32_t> out;
    out.reserve(seq.size());
    std::size_t starter = static_cast<std::size_t>(-1);
    uint8_t last_ccc = 0;
    for (uint32_t cp : seq) {
        const uint8_t ccc = combining_class(cp);
        bool composed = false;
        if (starter != static_cast<std::size_t>(-1)) {
            const bool composable = (last_ccc == 0 && ccc == 0) || (last_ccc < ccc);
            uint32_t composite = 0;
            if (composable && composition_pair(out[starter], cp, composite)) {
                // The composite is a starter (ccc 0): marks already absorbed into it
                // no longer sit between it and any later combining mark, so they
                // must not block further composition. Reset the blocking class.
                out[starter] = composite;
                last_ccc = 0;
                composed = true;
            }
        }
        if (composed) continue;
        if (ccc == 0) {
            starter = out.size();
            last_ccc = 0;
        } else {
            last_ccc = ccc;
        }
        out.push_back(cp);
    }
    return out;
}

// --- checks -------------------------------------------------------------------------

bool check_joiners(const std::vector<uint32_t>& label) {
    for (std::size_t i = 0; i < label.size(); ++i) {
        const uint32_t cp = label[i];
        if (cp != 0x200C && cp != 0x200D) continue;
        if (i > 0 && combining_class(label[i - 1]) == 9) continue; // virama before the joiner
        if (cp == 0x200D) return false; // ZWJ needs the virama
        // ZWNJ: preceded by a joining letter, followed by a joining letter.
        bool left = false;
        for (std::size_t j = i; j > 0;) {
            --j;
            const uint8_t jt = joining_type(label[j]);
            if (jt == kJoiningT) continue;
            left = (jt == kJoiningL || jt == kJoiningD);
            break;
        }
        if (!left) return false;
        bool right = false;
        for (std::size_t j = i + 1; j < label.size(); ++j) {
            const uint8_t jt = joining_type(label[j]);
            if (jt == kJoiningT) continue;
            right = (jt == kJoiningR || jt == kJoiningD);
            break;
        }
        if (!right) return false;
    }
    return true;
}

bool check_bidi(const std::vector<uint32_t>& label) {
    bool bidi_label = false;
    for (uint32_t cp : label) {
        const uint8_t d = bidi_class(cp);
        if (d == kBidiR || d == kBidiAL || d == kBidiAN) {
            bidi_label = true;
            break;
        }
    }
    if (!bidi_label) return true;

    const uint8_t first = bidi_class(label.front());
    bool rtl;
    if (first == kBidiR || first == kBidiAL) {
        rtl = true;
    } else if (first == kBidiL) {
        rtl = false;
    } else {
        return false;
    }

    bool valid_ending = false;
    uint8_t number_type = 0; // 0 = none, kBidiEN or kBidiAN
    for (uint32_t cp : label) {
        const uint8_t d = bidi_class(cp);
        if (rtl) {
            if (!(d == kBidiR || d == kBidiAL || d == kBidiAN || d == kBidiEN || d == kBidiES ||
                  d == kBidiCS || d == kBidiET || d == kBidiON || d == kBidiBN || d == kBidiNSM)) {
                return false;
            }
            if (d == kBidiR || d == kBidiAL || d == kBidiEN || d == kBidiAN) {
                valid_ending = true;
            } else if (d != kBidiNSM) {
                valid_ending = false;
            }
            if (d == kBidiEN || d == kBidiAN) {
                if (number_type == 0) {
                    number_type = d;
                } else if (number_type != d) {
                    return false;
                }
            }
        } else {
            if (!(d == kBidiL || d == kBidiEN || d == kBidiES || d == kBidiCS || d == kBidiET ||
                  d == kBidiON || d == kBidiBN || d == kBidiNSM)) {
                return false;
            }
            if (d == kBidiL || d == kBidiEN) {
                valid_ending = true;
            } else if (d != kBidiNSM) {
                valid_ending = false;
            }
        }
    }
    return valid_ending;
}

bool check_label(const std::vector<uint32_t>& label, bool beStrict) {
    // UTS #46 validity criteria apply "for a non-empty label". When
    // VerifyDnsLength (beStrict) is true, empty labels are disallowed.
    if (label.empty()) return !beStrict;
    if (is_mark(label.front())) return false;
    if (label.size() >= 4 && label[2] == '-' && label[3] == '-') {
        // UTS #46 CheckHyphens: only when strict. WHATWG never sets it.
        if (beStrict) return false;
    }
    if (beStrict && (label.front() == '-' || label.back() == '-')) return false;
    // Strict STD3 rules are not needed for WHATWG; beStrict implies them but
    // the only consumer (the URL parser) always passes false.
    if (!check_joiners(label)) return false;
    if (!check_bidi(label)) return false;
    return true;
}

// --- Punycode (RFC 3492) ----------------------------------------------------------

uint32_t punycode_adapt(uint32_t delta, uint32_t numpoints, bool first) {
    delta = first ? delta / 700 : delta / 2;
    delta += delta / numpoints;
    uint32_t k = 0;
    while (delta > 455) {
        delta /= 35;
        k += 36;
    }
    return k + ((36 - 1 + 1) * delta) / (delta + 38);
}

bool punycode_encode(const std::vector<uint32_t>& input, std::string& out) {
    out.clear();
    std::size_t basics = 0;
    for (uint32_t cp : input) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
            ++basics;
        }
    }
    const std::size_t handled_basics = basics;
    if (handled_basics == input.size()) return true; // pure ASCII label
    if (handled_basics > 0) out += '-';

    std::size_t handled = handled_basics;
    uint32_t delta = 0;
    uint32_t it = 0x80; // smallest unhandled code point (RFC 3492 initial_n)
    uint32_t bias = 72;
    while (handled < input.size()) {
        uint32_t m = 0x10FFFF + 1;
        for (uint32_t cp : input) {
            if (cp >= it && cp < m) m = cp;
        }
        if (m > 0x10FFFF) return false; // overflow
        const uint32_t delta_next = delta + (m - it) * (static_cast<uint32_t>(handled) + 1);
        if (delta_next < delta) return false; // overflow
        delta = delta_next;
        it = m;
        for (uint32_t cp : input) {
            if (cp < it) {
                ++delta;
                continue;
            }
            if (cp != it) continue;
            uint32_t q = delta;
            for (uint32_t k = 36;; k += 36) {
                const uint32_t t = k <= bias ? 1 : (k >= bias + 26 ? 26 : k - bias);
                if (q < t) break;
                const uint32_t digit = t + (q - t) % (36 - t);
                out += static_cast<char>(digit < 26 ? 'a' + digit : '0' + digit - 26);
                q = (q - t) / (36 - t);
            }
            out += static_cast<char>(q < 26 ? 'a' + q : '0' + q - 26);
            bias = punycode_adapt(delta, static_cast<uint32_t>(handled + 1), handled == handled_basics);
            delta = 0;
            ++handled;
        }
        ++delta;
        ++it;
    }
    return true;
}

// --- the domain ---------------------------------------------------------------------

bool is_ascii_domain(std::string_view domain) {
    for (unsigned char c : domain) {
        if (c >= 0x80) return false;
    }
    return true;
}

std::string ascii_lowercase(std::string_view domain) {
    std::string out;
    out.reserve(domain.size());
    for (unsigned char c : domain) out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return out;
}

} // namespace

bool domain_to_ascii(std::string_view utf8_domain, std::string& out, bool beStrict) {
    out.clear();
    if (utf8_domain.empty()) return false;

    // WHATWG domain parser, beStrict = false: an all-ASCII domain is returned
    // ASCII-lowercased no matter what ToASCII would say.
    if (!beStrict && is_ascii_domain(utf8_domain)) {
        out = ascii_lowercase(utf8_domain);
        return true;
    }

    std::vector<uint32_t> input, mapped, normalized;
    if (!decode_utf8(utf8_domain, input)) return false;
    if (!uts46_map(input, mapped)) return false;
    normalized = nfc_normalize(mapped);

    // Split on U+002E (the UTS #46 splitting dots were mapped to it by then).
    std::vector<std::vector<uint32_t>> labels;
    std::vector<uint32_t> current;
    for (uint32_t cp : normalized) {
        if (cp == '.') {
            labels.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(cp);
        }
    }
    labels.push_back(std::move(current));

    std::string result;
    for (std::size_t li = 0; li < labels.size(); ++li) {
        const auto& label = labels[li];
        if (li > 0) result += '.';
        if (label.empty()) {
            // UTS #46 ToASCII: with VerifyDnsLength set, empty labels are
            // disallowed; otherwise the empty root label passes through.
            if (beStrict) return false;
            continue;
        }
        if (!check_label(label, beStrict)) return false;
        bool has_non_ascii = false;
        for (uint32_t cp : label) {
            if (cp >= 0x80) {
                has_non_ascii = true;
                break;
            }
        }
        if (beStrict) {
            std::string plain;
            for (uint32_t cp : label) append_utf8(plain, cp);
            if (plain.size() > 63) return false;
        }
        if (has_non_ascii) {
            std::string encoded;
            if (!punycode_encode(label, encoded)) return false;
            result += "xn--";
            result += encoded;
        } else {
            for (uint32_t cp : label) result += static_cast<char>(cp);
        }
    }
    // The domain parser returns failure for an empty result (e.g. input that
    // mapped to nothing at all).
    if (result.empty()) return false;
    if (beStrict && result.size() > 253) return false;
    out = std::move(result);
    return true;
}

} // namespace litebird
