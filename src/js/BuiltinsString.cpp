// String, String.prototype and the URI encoding globals.
//
// JavaScript strings are sequences of UTF-16 code units; this engine stores
// UTF-8. Every index-based method therefore goes through StringView, which
// answers in UTF-16 units and only converts when the text is not plain ASCII.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

JsValue self() { return JsValue::current_this(); }

std::string this_string(const char* method) {
    JsValue me = self();
    if (me.is_nullish()) {
        js_throw_type_error(std::string("String.prototype.") + method + " called on null or undefined");
    }
    return js_to_string(me);
}

/// A string addressed in UTF-16 code units.
class StringView {
public:
    explicit StringView(const std::string& source) : m_source(source), m_ascii(js_is_ascii(source)) {
        if (!m_ascii) m_wide = js_utf8_to_utf16(source);
    }

    size_t size() const { return m_ascii ? m_source.size() : m_wide.size(); }
    bool ascii() const { return m_ascii; }

    char16_t at(size_t i) const {
        return m_ascii ? static_cast<char16_t>(static_cast<unsigned char>(m_source[i])) : m_wide[i];
    }

    std::string slice(size_t begin, size_t end) const {
        if (begin >= end) return std::string();
        if (m_ascii) return m_source.substr(begin, end - begin);
        return js_utf16_to_utf8(m_wide.substr(begin, end - begin));
    }

    std::u16string wide() const {
        if (!m_ascii) return m_wide;
        return std::u16string(m_source.begin(), m_source.end());
    }

    size_t find(const std::string& needle, size_t from) const {
        if (from > size()) return std::string::npos;
        if (m_ascii) {
            if (!js_is_ascii(needle)) return std::string::npos;
            return m_source.find(needle, from);
        }
        return m_wide.find(js_utf8_to_utf16(needle), from);
    }

    size_t rfind(const std::string& needle, size_t from) const {
        if (m_ascii) {
            if (!js_is_ascii(needle)) return std::string::npos;
            return m_source.rfind(needle, from);
        }
        return m_wide.rfind(js_utf8_to_utf16(needle), from);
    }

private:
    const std::string& m_source;
    bool m_ascii;
    std::u16string m_wide;
};

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

std::string trim(const std::string& s, bool start, bool end) {
    StringView view(s);
    size_t b = 0;
    size_t e = view.size();
    if (start) while (b < e && is_space_unit(view.at(b))) ++b;
    if (end) while (e > b && is_space_unit(view.at(e - 1))) --e;
    return view.slice(b, e);
}

// Simple (one-to-one) case mapping for the scripts that have case at all in
// everyday text: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic.
char16_t to_upper_unit(char16_t c) {
    if (c >= 'a' && c <= 'z') return static_cast<char16_t>(c - 32);
    if (c < 0x80) return c;
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return static_cast<char16_t>(c - 32);
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        bool odd_is_lower = (c < 0x139) || (c >= 0x14A && c <= 0x177);
        if (odd_is_lower) return (c & 1) ? static_cast<char16_t>(c - 1) : c;
        return (c & 1) ? c : static_cast<char16_t>(c - 1);
    }
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return static_cast<char16_t>(c - 32);
    if (c >= 0x430 && c <= 0x44F) return static_cast<char16_t>(c - 32);
    if (c >= 0x450 && c <= 0x45F) return static_cast<char16_t>(c - 80);
    return c;
}

char16_t to_lower_unit(char16_t c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char16_t>(c + 32);
    if (c < 0x80) return c;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return static_cast<char16_t>(c + 32);
    if (c == 0x178) return 0xFF;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x138 && c != 0x149 && c != 0x17F) {
        bool odd_is_lower = (c < 0x139) || (c >= 0x14A && c <= 0x177);
        if (odd_is_lower) return (c & 1) ? c : static_cast<char16_t>(c + 1);
        return (c & 1) ? static_cast<char16_t>(c + 1) : c;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return static_cast<char16_t>(c + 32);
    if (c >= 0x410 && c <= 0x42F) return static_cast<char16_t>(c + 32);
    if (c >= 0x400 && c <= 0x40F) return static_cast<char16_t>(c + 80);
    return c;
}

std::string change_case(const std::string& s, bool upper) {
    if (js_is_ascii(s)) {
        std::string out = s;
        for (char& c : out) {
            if (upper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
            else if (!upper && c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        }
        return out;
    }
    std::u16string wide = js_utf8_to_utf16(s);
    std::u16string out;
    out.reserve(wide.size());
    for (char16_t c : wide) {
        if (upper && c == 0xDF) { out += u"SS"; continue; }     // sharp s has no single upper form
        out += upper ? to_upper_unit(c) : to_lower_unit(c);
    }
    return js_utf16_to_utf8(out);
}

std::string pad(const std::string& s, Args args, bool at_start) {
    StringView view(s);
    double target = js_to_integer(js_to_number(js_arg(args, 0)));
    std::string filler = js_arg(args, 1).is_undefined() ? " " : js_to_string(js_arg(args, 1));
    if (target <= static_cast<double>(view.size()) || filler.empty()) return s;
    if (target > (1u << 28)) js_throw_range_error("Invalid string length");
    size_t needed = static_cast<size_t>(target) - view.size();
    StringView fill_view(filler);
    std::string padding;
    size_t produced = 0;
    while (produced + fill_view.size() <= needed) {
        padding += filler;
        produced += fill_view.size();
    }
    padding += fill_view.slice(0, needed - produced);
    return at_start ? padding + s : s + padding;
}

JsValue split_string(const std::string& s, Args args) {
    const JsValue& separator = js_arg(args, 0);
    double limit = js_arg(args, 1).is_undefined() ? 4294967295.0 : js_to_uint32(js_to_number(js_arg(args, 1)));
    if (js_is_regexp(separator)) return js_regexp_split(separator, s, limit);

    std::vector<JsValue> parts;
    if (limit == 0) return JsValue::make_array(parts);
    if (separator.is_undefined()) {
        parts.push_back(JsValue::make_string(s));
        return JsValue::make_array(parts);
    }
    std::string sep = js_to_string(separator);
    if (sep.empty()) {
        // Split into UTF-16 code units, as the specification says.
        StringView view(s);
        for (size_t i = 0; i < view.size() && parts.size() < limit; ++i) {
            parts.push_back(JsValue::make_string(view.slice(i, i + 1)));
        }
        return JsValue::make_array(parts);
    }
    size_t start = 0;
    while (parts.size() < limit) {
        size_t found = s.find(sep, start);      // byte search is exact for UTF-8
        if (found == std::string::npos) {
            parts.push_back(JsValue::make_string(s.substr(start)));
            break;
        }
        parts.push_back(JsValue::make_string(s.substr(start, found - start)));
        start = found + sep.size();
    }
    return JsValue::make_array(parts);
}

JsValue replace_string(const std::string& s, const JsValue& pattern, const JsValue& replacement, bool all) {
    if (js_is_regexp(pattern)) {
        if (all && !pattern.get_property("global").is_truthy()) {
            js_throw_type_error("replaceAll must be called with a global RegExp");
        }
        return js_regexp_replace(pattern, s, replacement);
    }
    std::string needle = js_to_string(pattern);
    const bool functional = replacement.is_function();
    std::u16string templ = functional ? std::u16string() : js_utf8_to_utf16(js_to_string(replacement));
    std::u16string subject = js_utf8_to_utf16(s);
    std::u16string wide_needle = js_utf8_to_utf16(needle);

    std::u16string out;
    size_t cursor = 0;
    size_t search_from = 0;
    while (true) {
        size_t found = subject.find(wide_needle, search_from);
        if (found == std::u16string::npos) break;
        out.append(subject, cursor, found - cursor);
        if (functional) {
            JsValue result = replacement.call({JsValue::make_string(needle), JsValue::make_number(static_cast<double>(found)),
                                               JsValue::make_string(s)});
            out += js_utf8_to_utf16(js_to_string(result));
        } else {
            JsReplaceCapture whole;
            whole.matched = true;
            whole.text = wide_needle;
            out += js_expand_replacement(templ, subject, found, {whole}, {});
        }
        cursor = found + wide_needle.size();
        search_from = wide_needle.empty() ? found + 1 : cursor;
        if (!all || search_from > subject.size()) break;
    }
    out.append(subject, cursor, std::u16string::npos);
    return JsValue::make_string(js_utf16_to_utf8(out));
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
            if (keep_reserved && std::strchr(kUriReserved, first)) out.append(input, i, 3);
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
        out += bytes;
        i = at;
    }
    return out;
}

} // namespace

std::u16string js_expand_replacement(const std::u16string& replacement, const std::u16string& subject,
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
            out.append(subject, 0, position);
            ++i;
        } else if (next == '\'') {
            size_t after = std::min(subject.size(), position + matched.size());
            out.append(subject, after, std::u16string::npos);
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
        if (args.empty()) return JsValue::make_string("");
        // String(symbol) is allowed and gives the description form.
        if (args[0].type == JsType::Symbol) return JsValue::make_string(args[0].to_string());
        return JsValue::make_string(js_to_string(args[0]));
    });
    string_ctor.define_property("prototype", js_object_value(sp), 0);
    js_define_hidden(sp, "constructor", string_ctor);

    js_define_method(string_ctor, "fromCharCode", 1, [](Args args) {
        std::u16string units;
        for (const auto& arg : args) units += static_cast<char16_t>(js_to_uint32(js_to_number(arg)) & 0xFFFF);
        return JsValue::make_string(js_utf16_to_utf8(units));
    });
    js_define_method(string_ctor, "fromCodePoint", 1, [](Args args) {
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
    js_define_method(string_ctor, "raw", 1, [](Args args) {
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
    js_define_method(sp, "toString", 0, [](Args) { return JsValue::make_string(this_string("toString")); });
    js_define_method(sp, "valueOf", 0, [](Args) { return JsValue::make_string(this_string("valueOf")); });
    js_define_method(sp, "charAt", 1, [](Args args) {
        std::string s = this_string("charAt");
        StringView view(s);
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(view.size())) return JsValue::make_string("");
        size_t i = static_cast<size_t>(index);
        return JsValue::make_string(view.slice(i, i + 1));
    });
    js_define_method(sp, "charCodeAt", 1, [](Args args) {
        std::string s = this_string("charCodeAt");
        StringView view(s);
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(view.size())) {
            return JsValue::make_number(std::numeric_limits<double>::quiet_NaN());
        }
        return JsValue::make_number(view.at(static_cast<size_t>(index)));
    });
    js_define_method(sp, "codePointAt", 1, [](Args args) {
        std::string s = this_string("codePointAt");
        StringView view(s);
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(view.size())) return JsValue::make_undefined();
        size_t i = static_cast<size_t>(index);
        uint32_t cp = view.at(i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < view.size()) {
            uint32_t low = view.at(i + 1);
            if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        return JsValue::make_number(cp);
    });
    js_define_method(sp, "at", 1, [](Args args) {
        std::string s = this_string("at");
        StringView view(s);
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(view.size());
        if (index < 0 || index >= static_cast<double>(view.size())) return JsValue::make_undefined();
        size_t i = static_cast<size_t>(index);
        return JsValue::make_string(view.slice(i, i + 1));
    });

    // ---- Searching --------------------------------------------------------
    js_define_method(sp, "indexOf", 1, [](Args args) {
        std::string s = this_string("indexOf");
        StringView view(s);
        size_t found = view.find(js_to_string(js_arg(args, 0)), clamp_index(js_arg(args, 1), view.size(), 0));
        return JsValue::make_number(found == std::string::npos ? -1.0 : static_cast<double>(found));
    });
    js_define_method(sp, "lastIndexOf", 1, [](Args args) {
        std::string s = this_string("lastIndexOf");
        StringView view(s);
        double position = js_to_number(js_arg(args, 1));
        size_t from = std::isnan(position) ? std::string::npos : clamp_index(js_arg(args, 1), view.size(), view.size());
        size_t found = view.rfind(js_to_string(js_arg(args, 0)), from);
        return JsValue::make_number(found == std::string::npos ? -1.0 : static_cast<double>(found));
    });
    js_define_method(sp, "includes", 1, [](Args args) {
        std::string s = this_string("includes");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.includes must not be a regular expression");
        StringView view(s);
        return JsValue::make_boolean(view.find(js_to_string(js_arg(args, 0)), clamp_index(js_arg(args, 1), view.size(), 0)) != std::string::npos);
    });
    js_define_method(sp, "startsWith", 1, [](Args args) {
        std::string s = this_string("startsWith");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.startsWith must not be a regular expression");
        StringView view(s);
        std::string needle = js_to_string(js_arg(args, 0));
        size_t start = clamp_index(js_arg(args, 1), view.size(), 0);
        StringView needle_view(needle);
        if (start + needle_view.size() > view.size()) return JsValue::make_boolean(false);
        return JsValue::make_boolean(view.slice(start, start + needle_view.size()) == needle);
    });
    js_define_method(sp, "endsWith", 1, [](Args args) {
        std::string s = this_string("endsWith");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.endsWith must not be a regular expression");
        StringView view(s);
        std::string needle = js_to_string(js_arg(args, 0));
        size_t end = clamp_index(js_arg(args, 1), view.size(), view.size());
        StringView needle_view(needle);
        if (needle_view.size() > end) return JsValue::make_boolean(false);
        return JsValue::make_boolean(view.slice(end - needle_view.size(), end) == needle);
    });

    // ---- Extracting -------------------------------------------------------
    js_define_method(sp, "slice", 2, [](Args args) {
        std::string s = this_string("slice");
        StringView view(s);
        size_t begin = relative_index(js_arg(args, 0), view.size(), 0);
        size_t end = relative_index(js_arg(args, 1), view.size(), view.size());
        return JsValue::make_string(view.slice(begin, end));
    });
    js_define_method(sp, "substring", 2, [](Args args) {
        std::string s = this_string("substring");
        StringView view(s);
        size_t a = clamp_index(js_arg(args, 0), view.size(), 0);
        size_t b = clamp_index(js_arg(args, 1), view.size(), view.size());
        return JsValue::make_string(view.slice(std::min(a, b), std::max(a, b)));
    });
    js_define_method(sp, "substr", 2, [](Args args) {
        std::string s = this_string("substr");
        StringView view(s);
        size_t begin = relative_index(js_arg(args, 0), view.size(), 0);
        double length = js_arg(args, 1).is_undefined() ? static_cast<double>(view.size())
                                                      : js_to_integer(js_to_number(js_arg(args, 1)));
        if (length <= 0) return JsValue::make_string("");
        size_t end = static_cast<size_t>(std::min(static_cast<double>(view.size()), static_cast<double>(begin) + length));
        return JsValue::make_string(view.slice(begin, end));
    });

    // ---- Transforming -----------------------------------------------------
    js_define_method(sp, "toUpperCase", 0, [](Args) { return JsValue::make_string(change_case(this_string("toUpperCase"), true)); });
    js_define_method(sp, "toLowerCase", 0, [](Args) { return JsValue::make_string(change_case(this_string("toLowerCase"), false)); });
    js_define_method(sp, "toLocaleUpperCase", 0, [](Args) { return JsValue::make_string(change_case(this_string("toLocaleUpperCase"), true)); });
    js_define_method(sp, "toLocaleLowerCase", 0, [](Args) { return JsValue::make_string(change_case(this_string("toLocaleLowerCase"), false)); });
    js_define_method(sp, "trim", 0, [](Args) { return JsValue::make_string(trim(this_string("trim"), true, true)); });
    js_define_method(sp, "trimStart", 0, [](Args) { return JsValue::make_string(trim(this_string("trimStart"), true, false)); });
    js_define_method(sp, "trimEnd", 0, [](Args) { return JsValue::make_string(trim(this_string("trimEnd"), false, true)); });
    js_define_method(sp, "trimLeft", 0, [](Args) { return JsValue::make_string(trim(this_string("trimLeft"), true, false)); });
    js_define_method(sp, "trimRight", 0, [](Args) { return JsValue::make_string(trim(this_string("trimRight"), false, true)); });
    js_define_method(sp, "padStart", 2, [](Args args) { return JsValue::make_string(pad(this_string("padStart"), args, true)); });
    js_define_method(sp, "padEnd", 2, [](Args args) { return JsValue::make_string(pad(this_string("padEnd"), args, false)); });
    js_define_method(sp, "repeat", 1, [](Args args) {
        std::string s = this_string("repeat");
        double count = js_to_integer(js_to_number(js_arg(args, 0)));
        if (count < 0 || std::isinf(count)) js_throw_range_error("Invalid count value: " + js_to_string(js_arg(args, 0)));
        if (static_cast<double>(s.size()) * count > (1u << 28)) js_throw_range_error("Invalid string length");
        std::string out;
        out.reserve(s.size() * static_cast<size_t>(count));
        for (size_t i = 0; i < static_cast<size_t>(count); ++i) out += s;
        return JsValue::make_string(out);
    });
    js_define_method(sp, "concat", 1, [](Args args) {
        std::string s = this_string("concat");
        for (const auto& arg : args) s += js_to_string(arg);
        return JsValue::make_string(s);
    });
    js_define_method(sp, "normalize", 0, [](Args) { return JsValue::make_string(this_string("normalize")); });
    js_define_method(sp, "localeCompare", 1, [](Args args) {
        std::string a = this_string("localeCompare");
        std::string b = js_to_string(js_arg(args, 0));
        // No collation data: compare case-insensitively first so that "a" sorts
        // before "B", then by code unit.
        std::string la = change_case(a, false);
        std::string lb = change_case(b, false);
        if (la != lb) return JsValue::make_number(la < lb ? -1 : 1);
        if (a == b) return JsValue::make_number(0);
        return JsValue::make_number(a > b ? -1 : 1);
    });
    js_define_method(sp, "split", 2, [](Args args) { return split_string(this_string("split"), args); });
    js_define_method(sp, "replace", 2, [](Args args) {
        return replace_string(this_string("replace"), js_arg(args, 0), js_arg(args, 1), false);
    });
    js_define_method(sp, "replaceAll", 2, [](Args args) {
        return replace_string(this_string("replaceAll"), js_arg(args, 0), js_arg(args, 1), true);
    });
    js_define_method(sp, "match", 1, [](Args args) {
        std::string s = this_string("match");
        JsValue regex = js_is_regexp(js_arg(args, 0)) ? js_arg(args, 0)
                      : js_regexp_create(js_arg(args, 0).is_undefined() ? "(?:)" : js_to_string(js_arg(args, 0)), "");
        return js_regexp_match(regex, s);
    });
    js_define_method(sp, "matchAll", 1, [](Args args) {
        std::string s = this_string("matchAll");
        JsValue regex = js_arg(args, 0);
        if (js_is_regexp(regex)) {
            if (!regex.get_property("global").is_truthy()) {
                js_throw_type_error("String.prototype.matchAll called with a non-global RegExp argument");
            }
        } else {
            regex = js_regexp_create(js_to_string(regex), "g");
        }
        return js_regexp_match_all(regex, s);
    });
    js_define_method(sp, "search", 1, [](Args args) {
        std::string s = this_string("search");
        JsValue regex = js_is_regexp(js_arg(args, 0)) ? js_arg(args, 0)
                      : js_regexp_create(js_arg(args, 0).is_undefined() ? "(?:)" : js_to_string(js_arg(args, 0)), "");
        return JsValue::make_number(js_regexp_search(regex, s));
    });

    // Strings iterate by code point.
    auto string_iterator = [](Args) {
        auto text = std::make_shared<std::string>(this_string("[Symbol.iterator]"));
        auto offset = std::make_shared<size_t>(0);
        return js_make_iterator([text, offset](JsValue& value) {
            const std::string& s = *text;
            size_t i = *offset;
            if (i >= s.size()) return false;
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            if (i + len > s.size()) len = s.size() - i;
            value = JsValue::make_string(s.substr(i, len));
            *offset = i + len;
            return true;
        });
    };
    js_define_method(sp, kIteratorKey, 0, string_iterator);
    js_define_method(sp, "Symbol.iterator", 0, string_iterator);

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
}

} // namespace litebird
