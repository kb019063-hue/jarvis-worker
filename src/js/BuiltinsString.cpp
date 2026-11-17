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
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

/// `this` as a string. Shares the text; nothing is copied.
JsString this_string(const char* method) {
    const JsValue& me = JsValue::current_this();
    if (me.type == JsType::String) return me.string_val;
    if (me.is_nullish()) {
        js_throw_type_error(std::string("String.prototype.") + method + " called on null or undefined");
    }
    // A String object, as in `new String("a").toUpperCase()`.
    if (const JsValue* inner = js_wrapped_primitive(me); inner && inner->type == JsType::String) {
        return inner->string_val;
    }
    return JsString(js_to_string(me));
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

JsString change_case(const JsString& s, bool upper) {
    if (s.is_ascii()) {
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
    Utf16View view(s);
    std::u16string out;
    out.reserve(view.size());
    for (size_t i = 0; i < view.size(); ++i) {
        const char16_t c = view[i];
        if (upper && c == 0xDF) { out += u"SS"; continue; }     // sharp s has no single upper form
        out += upper ? to_upper_unit(c) : to_lower_unit(c);
    }
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

    js_define_method(string_ctor, "fromCharCode", 1, [](Args args) {
        std::u16string units;
        for (const auto& arg : args) units += static_cast<char16_t>(js_to_uint32(js_to_number(arg)) & 0xFFFF);
        if (units.size() == 1) return JsValue::make_string(JsString::from_unit(units[0]));
        return JsValue::make_string(JsString::from_utf16(units));
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
        const JsString s = this_string("charAt");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(s.length())) return JsValue::make_string(JsString());
        return JsValue::make_string(JsString::from_unit(s.unit_at(static_cast<size_t>(index))));
    });
    js_define_method(sp, "charCodeAt", 1, [](Args args) {
        const JsString s = this_string("charCodeAt");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0 || index >= static_cast<double>(s.length())) {
            return JsValue::make_number(std::numeric_limits<double>::quiet_NaN());
        }
        return JsValue::make_number(s.unit_at(static_cast<size_t>(index)));
    });
    js_define_method(sp, "codePointAt", 1, [](Args args) {
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
    js_define_method(sp, "at", 1, [](Args args) {
        const JsString s = this_string("at");
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(s.length());
        if (index < 0 || index >= static_cast<double>(s.length())) return JsValue::make_undefined();
        return JsValue::make_string(JsString::from_unit(s.unit_at(static_cast<size_t>(index))));
    });

    // ---- Searching --------------------------------------------------------
    js_define_method(sp, "indexOf", 1, [](Args args) {
        const Utf16View view = this_units("indexOf");
        const Utf16View needle = arg_units(args, 0);
        size_t found = view.find(needle, clamp_index(js_arg(args, 1), view.size(), 0));
        return JsValue::make_number(found == Utf16View::npos ? -1.0 : static_cast<double>(found));
    });
    js_define_method(sp, "lastIndexOf", 1, [](Args args) {
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
    js_define_method(sp, "includes", 1, [](Args args) {
        const Utf16View view = this_units("includes");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.includes must not be a regular expression");
        const Utf16View needle = arg_units(args, 0);
        return JsValue::make_boolean(view.find(needle, clamp_index(js_arg(args, 1), view.size(), 0)) != Utf16View::npos);
    });
    js_define_method(sp, "startsWith", 1, [](Args args) {
        const Utf16View view = this_units("startsWith");
        if (js_is_regexp(js_arg(args, 0))) js_throw_type_error("First argument to String.prototype.startsWith must not be a regular expression");
        const Utf16View needle = arg_units(args, 0);
        size_t start = clamp_index(js_arg(args, 1), view.size(), 0);
        return JsValue::make_boolean(view.matches_at(start, needle));
    });
    js_define_method(sp, "endsWith", 1, [](Args args) {
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
    js_define_method(sp, "slice", 2, [](Args args) {
        const JsString s = this_string("slice");
        size_t begin = relative_index(js_arg(args, 0), s.length(), 0);
        size_t end = relative_index(js_arg(args, 1), s.length(), s.length());
        return JsValue::make_string(s.substring(begin, end));
    });
    js_define_method(sp, "substring", 2, [](Args args) {
        const JsString s = this_string("substring");
        size_t a = clamp_index(js_arg(args, 0), s.length(), 0);
        size_t b = clamp_index(js_arg(args, 1), s.length(), s.length());
        return JsValue::make_string(s.substring(std::min(a, b), std::max(a, b)));
    });
    js_define_method(sp, "substr", 2, [](Args args) {
        const JsString s = this_string("substr");
        size_t begin = relative_index(js_arg(args, 0), s.length(), 0);
        double length = js_arg(args, 1).is_undefined() ? static_cast<double>(s.length())
                                                      : js_to_integer(js_to_number(js_arg(args, 1)));
        if (length <= 0) return JsValue::make_string(JsString());
        size_t end = static_cast<size_t>(std::min(static_cast<double>(s.length()), static_cast<double>(begin) + length));
        return JsValue::make_string(s.substring(begin, end));
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
    js_define_method(sp, "concat", 1, [](Args args) {
        JsString s = this_string("concat");
        for (const auto& arg : args) s = JsString::concat(s, js_to_js_string(arg));
        return JsValue::make_string(s);
    });
    js_define_method(sp, "normalize", 0, [](Args) { return JsValue::make_string(this_string("normalize")); });
    js_define_method(sp, "isWellFormed", 0, [](Args) {
        return JsValue::make_boolean(this_string("isWellFormed").is_well_formed());
    });
    js_define_method(sp, "toWellFormed", 0, [](Args) {
        return JsValue::make_string(this_string("toWellFormed").to_well_formed());
    });
    js_define_method(sp, "localeCompare", 1, [](Args args) {
        const JsString a = this_string("localeCompare");
        const JsString b = js_to_js_string(js_arg(args, 0));
        // No collation data: compare case-insensitively first so that "a" sorts
        // before "B", then by code unit.
        const JsString la = change_case(a, false);
        const JsString lb = change_case(b, false);
        if (la != lb) return JsValue::make_number(JsString::compare(la.view(), lb.view()) < 0 ? -1 : 1);
        if (a == b) return JsValue::make_number(0);
        return JsValue::make_number(JsString::compare(a.view(), b.view()) > 0 ? -1 : 1);
    });
    auto define_str_method = [&](const char* name, int length, JsValue::NativeFn fn) {
        JsValue v = js_make_native(name, length, std::move(fn));
        if (JsObject* cell = v.function_cell()) cell->not_constructor = true;
        auto& entry = sp->properties.entry(name);
        entry.second = std::move(v);
        entry.flags = PropertyMap::Hidden;
    };
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
