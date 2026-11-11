#include "litebird/js/JsOps.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Generator.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace litebird {

const char* const kIteratorKey = "@@iterator";
const char* const kAsyncIteratorKey = "@@asyncIterator";

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------

namespace {

std::atomic<int> g_uncaught_errors{0};

const char* error_kind_name(JsErrorKind kind) {
    switch (kind) {
        case JsErrorKind::Error: return "Error";
        case JsErrorKind::TypeError: return "TypeError";
        case JsErrorKind::RangeError: return "RangeError";
        case JsErrorKind::SyntaxError: return "SyntaxError";
        case JsErrorKind::ReferenceError: return "ReferenceError";
        case JsErrorKind::EvalError: return "EvalError";
        case JsErrorKind::URIError: return "URIError";
    }
    return "Error";
}

/// Reads a data property along the prototype chain without running getters.
bool raw_lookup(const JsValue& value, const std::string& name, JsValue& out) {
    int guard = 0;
    for (JsObject* obj = value.object_val; obj && guard < 10000; obj = obj->prototype, ++guard) {
        auto it = obj->properties.find(name);
        if (it != obj->properties.end()) {
            if (it->flags & PropertyMap::Accessor) return false;
            out = it->second;
            return true;
        }
    }
    return false;
}

std::string primitive_text(const JsValue& value) {
    switch (value.type) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "null";
        case JsType::Boolean: return value.bool_val ? "true" : "false";
        case JsType::Number: return js_number_to_string(value.number_val);
        case JsType::String: return value.string_val;
        default: return "";
    }
}

} // namespace

std::string js_describe_thrown(const JsValue& value) {
    if (!value.is_object_like()) {
        if (value.type == JsType::Symbol) return "Symbol()";
        return primitive_text(value);
    }
    if (value.type == JsType::Object && value.object_val) {
        JsValue name;
        JsValue message;
        bool has_name = raw_lookup(value, "name", name);
        bool has_message = raw_lookup(value, "message", message);
        if (has_name || has_message) {
            std::string n = has_name && !name.is_object_like() ? primitive_text(name) : "Error";
            std::string m = has_message && !message.is_object_like() ? primitive_text(message) : "";
            if (n.empty()) return m;
            if (m.empty()) return n;
            return n + ": " + m;
        }
        if (value.object_val->is_array) return "[object Array]";
        return "[object Object]";
    }
    if (value.is_function()) return "[object Function]";
    return "[object Object]";
}

JsThrow::JsThrow(JsValue thrown) : value(std::move(thrown)), m_description(js_describe_thrown(value)) {}

JsValue js_make_error(JsErrorKind kind, const std::string& message) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().error_protos[static_cast<int>(kind)];
    cell->klass = ObjectClass::Error;
    JsValue error = JsValue::make_object(cell);
    error.define_property("message", JsValue::make_string(message), PropertyMap::Hidden);
    std::string stack = std::string(error_kind_name(kind)) + (message.empty() ? "" : ": " + message) +
                        "\n    at <anonymous>";
    error.define_property("stack", JsValue::make_string(stack), PropertyMap::Hidden);
    return error;
}

void js_throw(JsValue value) {
    throw JsThrow(std::move(value));
}

void js_throw_error(JsErrorKind kind, const std::string& message) {
    throw JsThrow(js_make_error(kind, message));
}

void js_throw_type_error(const std::string& message) {
    js_throw_error(JsErrorKind::TypeError, message);
}

void js_throw_range_error(const std::string& message) {
    js_throw_error(JsErrorKind::RangeError, message);
}

JsValue js_value_from_exception(const std::exception& ex) {
    if (auto thrown = dynamic_cast<const JsThrow*>(&ex)) return thrown->value;
    std::string text = ex.what();
    // Engine-internal errors arrive as "RangeError: ..." style strings.
    static const std::pair<const char*, JsErrorKind> prefixes[] = {
        {"RangeError: ", JsErrorKind::RangeError},
        {"TypeError: ", JsErrorKind::TypeError},
        {"SyntaxError: ", JsErrorKind::SyntaxError},
        {"ReferenceError: ", JsErrorKind::ReferenceError},
    };
    for (const auto& [prefix, kind] : prefixes) {
        size_t n = std::strlen(prefix);
        if (text.compare(0, n, prefix) == 0) return js_make_error(kind, text.substr(n));
    }
    return js_make_error(JsErrorKind::Error, text);
}

void js_report_uncaught(const JsValue& value) {
    g_uncaught_errors.fetch_add(1);
    std::cerr << "Uncaught " << js_describe_thrown(value) << std::endl;
}

void js_report_uncaught(const std::exception& ex) {
    if (auto thrown = dynamic_cast<const JsThrow*>(&ex)) {
        js_report_uncaught(thrown->value);
        return;
    }
    g_uncaught_errors.fetch_add(1);
    std::cerr << "Uncaught " << ex.what() << std::endl;
}

int js_uncaught_error_count() {
    return g_uncaught_errors.load();
}

bool js_is_internal_unwind(const std::exception& ex) {
    return dynamic_cast<const GeneratorAbortException*>(&ex) != nullptr;
}

JsValue js_call_guarded(const JsValue& fn, const JsValue& this_val, const std::vector<JsValue>& args) {
    JsValue result;
    js_run_guarded([&] { result = fn.call_with_this(this_val, args); });
    return result;
}

// ---------------------------------------------------------------------------
// Number <-> string
// ---------------------------------------------------------------------------

std::string js_number_to_string(double value) {
    if (std::isnan(value)) return "NaN";
    if (value == 0.0) return "0";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";

    // Integers are by far the common case.
    if (value == std::floor(value) && std::fabs(value) < 9007199254740992.0) {
        char buffer[32];
        auto res = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(value));
        return std::string(buffer, res.ptr);
    }

    // Shortest digits that round-trip, as "d.ddddde±xx".
    char buffer[64];
    auto res = std::to_chars(buffer, buffer + sizeof(buffer), std::fabs(value), std::chars_format::scientific);
    std::string sci(buffer, res.ptr);
    size_t e_pos = sci.find('e');
    std::string digits;
    for (size_t i = 0; i < e_pos; ++i) {
        if (sci[i] != '.') digits += sci[i];
    }
    int exponent = std::atoi(sci.c_str() + e_pos + 1);
    const int k = static_cast<int>(digits.size());
    const int n = exponent + 1;         // position of the decimal point

    std::string out;
    if (value < 0) out += '-';
    if (k <= n && n <= 21) {
        out += digits;
        out.append(static_cast<size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out.append(digits, 0, static_cast<size_t>(n));
        out += '.';
        out.append(digits, static_cast<size_t>(n), std::string::npos);
    } else if (-6 < n && n <= 0) {
        out += "0.";
        out.append(static_cast<size_t>(-n), '0');
        out += digits;
    } else {
        out += digits[0];
        if (k > 1) {
            out += '.';
            out.append(digits, 1, std::string::npos);
        }
        out += 'e';
        out += (n - 1 < 0) ? '-' : '+';
        out += std::to_string(std::abs(n - 1));
    }
    return out;
}

std::string js_number_to_string_radix(double value, int radix) {
    if (radix == 10) return js_number_to_string(value);
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    if (value == 0.0) return "0";

    static const char* digit_chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    bool negative = value < 0;
    double magnitude = std::fabs(value);
    double integer_part = std::floor(magnitude);
    double fraction = magnitude - integer_part;

    std::string integer_digits;
    if (integer_part == 0.0) {
        integer_digits = "0";
    } else {
        while (integer_part >= 1.0) {
            double digit = std::fmod(integer_part, radix);
            integer_digits += digit_chars[static_cast<int>(digit)];
            integer_part = std::floor(integer_part / radix);
        }
        std::reverse(integer_digits.begin(), integer_digits.end());
    }

    std::string out = negative ? "-" : "";
    out += integer_digits;
    if (fraction > 0.0) {
        out += '.';
        // Emit digits until the remaining fraction is below what a double can
        // distinguish at this magnitude.
        double delta = std::max(0.5 * (std::nextafter(magnitude, std::numeric_limits<double>::infinity()) - magnitude),
                                std::numeric_limits<double>::denorm_min());
        int emitted = 0;
        do {
            fraction *= radix;
            delta *= radix;
            int digit = static_cast<int>(fraction);
            out += digit_chars[digit];
            fraction -= digit;
            if (fraction > 0.5 || (fraction == 0.5 && (digit & 1))) {
                if (fraction + delta > 1.0) {
                    // Round up, propagating the carry.
                    size_t i = out.size();
                    while (true) {
                        --i;
                        if (out[i] == '.') continue;
                        const char* pos = std::strchr(digit_chars, out[i]);
                        int d = static_cast<int>(pos - digit_chars) + 1;
                        if (d < radix) { out[i] = digit_chars[d]; break; }
                        out[i] = '0';
                        if (i == 0 || out[i - 1] == '-') { out.insert(i, 1, '1'); break; }
                    }
                    break;
                }
            }
        } while (fraction >= delta && ++emitted < 1100);
        // Trailing zeros after a carry are not significant.
        while (out.back() == '0') out.pop_back();
        if (out.back() == '.') out.pop_back();
    }
    return out;
}

namespace {

bool is_js_space(const std::string& s, size_t i, size_t& width) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || (c >= 0x09 && c <= 0x0D)) { width = 1; return true; }
    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) { width = 2; return true; }
    if (c == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBB &&
        static_cast<unsigned char>(s[i + 2]) == 0xBF) { width = 3; return true; }
    if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80) {
        unsigned char d = static_cast<unsigned char>(s[i + 2]);
        if ((d >= 0x80 && d <= 0x8A) || d == 0xA8 || d == 0xA9 || d == 0xAF) { width = 3; return true; }
    }
    if (c == 0xE3 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
        static_cast<unsigned char>(s[i + 2]) == 0x80) { width = 3; return true; }
    return false;
}

} // namespace

double js_string_to_number(const std::string& text) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    size_t begin = 0;
    size_t end = text.size();
    size_t width = 0;
    while (begin < end && is_js_space(text, begin, width)) begin += width;
    while (end > begin) {
        // Step back over one trailing space (ASCII or a multi-byte one).
        size_t back = end - 1;
        while (back > begin && (static_cast<unsigned char>(text[back]) & 0xC0) == 0x80) --back;
        if (is_js_space(text, back, width) && back + width == end) end = back;
        else break;
    }
    if (begin == end) return 0.0;
    std::string s = text.substr(begin, end - begin);

    if (s.size() > 2 && s[0] == '0') {
        int radix = 0;
        if (s[1] == 'x' || s[1] == 'X') radix = 16;
        else if (s[1] == 'o' || s[1] == 'O') radix = 8;
        else if (s[1] == 'b' || s[1] == 'B') radix = 2;
        if (radix) {
            double value = 0.0;
            for (size_t i = 2; i < s.size(); ++i) {
                char c = s[i];
                int d = (c >= '0' && c <= '9') ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 99;
                if (d >= radix) return nan;
                value = value * radix + d;
            }
            return value;
        }
    }

    size_t i = 0;
    bool negative = false;
    if (s[i] == '+' || s[i] == '-') { negative = s[i] == '-'; ++i; }
    if (s.compare(i, std::string::npos, "Infinity") == 0) {
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    // Validate against StrDecimalLiteral before handing it to strtod, which
    // accepts things JavaScript does not ("nan", "0x1p3", "inf").
    size_t j = i;
    size_t int_digits = 0, frac_digits = 0;
    while (j < s.size() && s[j] >= '0' && s[j] <= '9') { ++j; ++int_digits; }
    if (j < s.size() && s[j] == '.') {
        ++j;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') { ++j; ++frac_digits; }
    }
    if (int_digits + frac_digits == 0) return nan;
    if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
        ++j;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
        size_t exp_digits = 0;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') { ++j; ++exp_digits; }
        if (exp_digits == 0) return nan;
    }
    if (j != s.size()) return nan;
    return std::strtod(s.c_str(), nullptr);
}

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

JsValue js_to_primitive(const JsValue& value, JsPrimitiveHint hint) {
    if (!value.is_object_like()) return value;
    if (value.type == JsType::DomNode) return JsValue::make_string(value.to_string());

    bool string_first = hint == JsPrimitiveHint::String;
    if (hint == JsPrimitiveHint::Default && value.object_val && value.object_val->klass == ObjectClass::Date) {
        string_first = true;
    }
    const char* order[2] = {string_first ? "toString" : "valueOf", string_first ? "valueOf" : "toString"};
    for (const char* name : order) {
        JsValue method = value.get_property(name);
        if (!method.is_function()) continue;
        JsValue result = method.call_with_this(value, {});
        if (!result.is_object_like()) return result;
    }
    js_throw_type_error("Cannot convert object to primitive value");
}

double js_to_number(const JsValue& value) {
    switch (value.type) {
        case JsType::Undefined: return std::numeric_limits<double>::quiet_NaN();
        case JsType::Null: return 0.0;
        case JsType::Boolean: return value.bool_val ? 1.0 : 0.0;
        case JsType::Number: return value.number_val;
        case JsType::String: return js_string_to_number(value.string_val);
        case JsType::Symbol: js_throw_type_error("Cannot convert a Symbol value to a number");
        default: return js_to_number(js_to_primitive(value, JsPrimitiveHint::Number));
    }
}

double js_to_integer(double value) {
    if (std::isnan(value)) return 0.0;
    if (std::isinf(value)) return value;
    return std::trunc(value);
}

uint32_t js_to_uint32(double value) {
    if (!std::isfinite(value)) return 0;
    double truncated = std::trunc(value);
    double modulo = std::fmod(truncated, 4294967296.0);
    if (modulo < 0) modulo += 4294967296.0;
    return static_cast<uint32_t>(modulo);
}

int32_t js_to_int32(double value) {
    return static_cast<int32_t>(js_to_uint32(value));
}

std::string js_to_string(const JsValue& value) {
    switch (value.type) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "null";
        case JsType::Boolean: return value.bool_val ? "true" : "false";
        case JsType::Number: return js_number_to_string(value.number_val);
        case JsType::String: return value.string_val;
        case JsType::Symbol: js_throw_type_error("Cannot convert a Symbol value to a string");
        default: return js_to_string(js_to_primitive(value, JsPrimitiveHint::String));
    }
}

std::string js_to_property_key(const JsValue& value) {
    switch (value.type) {
        case JsType::String: return value.string_val;
        case JsType::Symbol: return value.string_val;       // the symbol's unique key
        case JsType::Number: {
            double n = value.number_val;
            if (n >= 0 && n < 4294967295.0 && n == std::floor(n)) {
                char buffer[16];
                auto res = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<uint64_t>(n));
                return std::string(buffer, res.ptr);
            }
            return js_number_to_string(n);
        }
        default: return js_to_string(value);
    }
}

const char* js_typeof(const JsValue& value) {
    switch (value.type) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "object";
        case JsType::Boolean: return "boolean";
        case JsType::Number: return "number";
        case JsType::String: return "string";
        case JsType::Symbol: return "symbol";
        case JsType::Function:
        case JsType::NativeFunction: return "function";
        case JsType::Object:
        case JsType::DomNode: return "object";
    }
    return "undefined";
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

bool js_loose_equals(const JsValue& a, const JsValue& b) {
    if (a.type == b.type || (a.is_function() && b.is_function())) return strict_equals(a, b);
    if (a.is_nullish() && b.is_nullish()) return true;
    if (a.is_nullish() || b.is_nullish()) return false;

    if (a.type == JsType::Number && b.type == JsType::String) return a.number_val == js_string_to_number(b.string_val);
    if (a.type == JsType::String && b.type == JsType::Number) return js_string_to_number(a.string_val) == b.number_val;
    if (a.type == JsType::Boolean) return js_loose_equals(JsValue::make_number(a.bool_val ? 1 : 0), b);
    if (b.type == JsType::Boolean) return js_loose_equals(a, JsValue::make_number(b.bool_val ? 1 : 0));

    const bool a_primitive = !a.is_object_like();
    const bool b_primitive = !b.is_object_like();
    if (a_primitive && !b_primitive) return js_loose_equals(a, js_to_primitive(b));
    if (!a_primitive && b_primitive) return js_loose_equals(js_to_primitive(a), b);
    return false;
}

bool js_same_value_zero(const JsValue& a, const JsValue& b) {
    if (a.type == JsType::Number && b.type == JsType::Number) {
        if (std::isnan(a.number_val) && std::isnan(b.number_val)) return true;
        return a.number_val == b.number_val;
    }
    return strict_equals(a, b);
}

bool js_same_value(const JsValue& a, const JsValue& b) {
    if (a.type == JsType::Number && b.type == JsType::Number) {
        if (std::isnan(a.number_val) && std::isnan(b.number_val)) return true;
        if (a.number_val == 0.0 && b.number_val == 0.0) return std::signbit(a.number_val) == std::signbit(b.number_val);
        return a.number_val == b.number_val;
    }
    return strict_equals(a, b);
}

int js_less_than(const JsValue& a, const JsValue& b) {
    if (a.type == JsType::Number && b.type == JsType::Number) {
        if (std::isnan(a.number_val) || std::isnan(b.number_val)) return -1;
        return a.number_val < b.number_val ? 1 : 0;
    }
    JsValue pa = js_to_primitive(a, JsPrimitiveHint::Number);
    JsValue pb = js_to_primitive(b, JsPrimitiveHint::Number);
    if (pa.type == JsType::String && pb.type == JsType::String) {
        return pa.string_val < pb.string_val ? 1 : 0;
    }
    double na = js_to_number(pa);
    double nb = js_to_number(pb);
    if (std::isnan(na) || std::isnan(nb)) return -1;
    return na < nb ? 1 : 0;
}

JsValue js_add(const JsValue& a, const JsValue& b) {
    if (a.type == JsType::Number && b.type == JsType::Number) {
        return JsValue::make_number(a.number_val + b.number_val);
    }
    if (a.type == JsType::String && b.type == JsType::String) {
        return JsValue::make_string(a.string_val + b.string_val);
    }
    JsValue pa = js_to_primitive(a);
    JsValue pb = js_to_primitive(b);
    if (pa.type == JsType::String || pb.type == JsType::String) {
        return JsValue::make_string(js_to_string(pa) + js_to_string(pb));
    }
    return JsValue::make_number(js_to_number(pa) + js_to_number(pb));
}

bool js_instance_of(const JsValue& value, const JsValue& constructor) {
    if (!constructor.is_function()) js_throw_type_error("Right-hand side of 'instanceof' is not callable");
    if (!value.is_object_like()) return false;
    JsValue proto = constructor.get_property("prototype");
    if (!proto.object_val) return false;
    int guard = 0;
    for (JsObject* obj = value.prototype_object(); obj && guard < 10000; obj = obj->prototype, ++guard) {
        if (obj == proto.object_val) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

JsValue js_call(const JsValue& fn, const JsValue& this_val, const std::vector<JsValue>& args) {
    if (!fn.is_function()) js_throw_type_error(js_describe_thrown(fn) + " is not a function");
    return fn.call_with_this(this_val, args);
}

JsValue js_construct(const JsValue& ctor, const std::vector<JsValue>& args) {
    if (!ctor.is_function()) js_throw_type_error(js_describe_thrown(ctor) + " is not a constructor");
    const JsValue::FunctionData* fd = ctor.function_data();
    if (fd && fd->node) {
        if (!fd->is_constructor) {
            js_throw_type_error((fd->name.empty() ? std::string("function") : fd->name) + " is not a constructor");
        }
        JsValue instance = JsValue::make_object();
        JsValue proto = ctor.get_property("prototype");
        if (proto.object_val && proto.is_object_like()) instance.object_val->prototype = proto.object_val;
        JsValue result = ctor.construct(args, &instance);
        return result.is_object_like() ? result : instance;
    }
    return ctor.construct(args, nullptr);
}

JsValue js_bind_this(const JsValue& fn, const JsValue& receiver) {
    return JsValue::make_native_function([fn, receiver](const std::vector<JsValue>& args) {
        return fn.call_with_this(receiver, args);
    });
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

bool js_is_ascii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

void js_append_code_point(std::string& out, uint32_t cp) {
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

std::u16string js_utf8_to_utf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        auto cont = [&](size_t k) {
            return i + k < n && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
        };
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && cont(1)) {
            cp = (static_cast<uint32_t>(c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && cont(1) && cont(2)) {
            cp = (static_cast<uint32_t>(c & 0x0F) << 12) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
            cp = (static_cast<uint32_t>(c & 0x07) << 18) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3F);
            len = 4;
        }
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (cp >> 10));
            out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<char16_t>(cp > 0xFFFF ? 0xFFFD : cp);
        }
        i += len;
    }
    return out;
}

std::string js_utf16_to_utf8(const std::u16string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        js_append_code_point(out, cp);
    }
    return out;
}

size_t js_string_length(const std::string& s) {
    size_t units = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) == 0x80) continue;      // continuation byte
        units += (c >= 0xF0) ? 2 : 1;          // astral code points are a surrogate pair
    }
    return units;
}

// ---------------------------------------------------------------------------
// Symbols
// ---------------------------------------------------------------------------

namespace {
std::mutex g_symbol_mutex;
std::unordered_map<std::string, std::string>& symbol_registry() {
    static std::unordered_map<std::string, std::string> registry;
    return registry;
}
std::atomic<uint64_t> g_symbol_counter{0};
}

JsValue js_symbol_for_key(const std::string& key, const std::string& description) {
    {
        std::lock_guard<std::mutex> lock(g_symbol_mutex);
        symbol_registry().emplace(key, description);
    }
    return JsValue::make_symbol(key, description);
}

JsValue js_new_symbol(const std::string& description) {
    std::string key = "@@sym:" + std::to_string(g_symbol_counter.fetch_add(1) + 1);
    return js_symbol_for_key(key, description);
}

bool js_symbol_from_key(const std::string& key, JsValue& out) {
    if (key.size() < 2 || key[0] != '@' || key[1] != '@') return false;
    std::lock_guard<std::mutex> lock(g_symbol_mutex);
    auto it = symbol_registry().find(key);
    if (it == symbol_registry().end()) return false;
    out = JsValue::make_symbol(key, it->second);
    return true;
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

void js_iterate(const JsValue& iterable, const std::function<bool(const JsValue&)>& visit) {
    if (iterable.is_array()) {
        // Index-based so that an array growing during iteration is still safe.
        for (size_t i = 0; i < iterable.object_val->array_elements.size(); ++i) {
            JsValue item = iterable.object_val->array_elements[i];
            if (!visit(item)) return;
        }
        return;
    }
    if (iterable.type == JsType::String) {
        // Strings iterate by code point, not by UTF-16 code unit.
        const std::string& s = iterable.string_val;
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            if (i + len > s.size()) len = s.size() - i;
            if (!visit(JsValue::make_string(s.substr(i, len)))) return;
            i += len;
        }
        return;
    }

    JsValue iterator;
    if (iterable.is_object_like()) {
        JsValue method = iterable.get_property(kIteratorKey);
        if (!method.is_function()) method = iterable.get_property("Symbol.iterator");
        if (method.is_function()) {
            iterator = method.call_with_this(iterable, {});
        } else if (iterable.get_property("next").is_function()) {
            iterator = iterable;        // a bare iterator object
        }
    }
    if (!iterator.is_object_like()) {
        js_throw_type_error(js_describe_thrown(iterable) + " is not iterable");
    }

    JsValue next = iterator.get_property("next");
    if (!next.is_function()) js_throw_type_error("iterator.next is not a function");

    auto close = [&]() {
        JsValue ret = iterator.get_property("return");
        if (ret.is_function()) ret.call_with_this(iterator, {});
    };

    while (true) {
        JsValue step = next.call_with_this(iterator, {});
        if (!step.is_object_like()) js_throw_type_error("Iterator result is not an object");
        if (step.get_property("done").is_truthy()) return;
        JsValue value = step.get_property("value");
        bool keep_going;
        try {
            keep_going = visit(value);
        } catch (...) {
            try { close(); } catch (...) {}
            throw;
        }
        if (!keep_going) {
            close();
            return;
        }
    }
}

std::vector<JsValue> js_iterable_to_vector(const JsValue& iterable) {
    if (iterable.is_array()) return iterable.object_val->array_elements;
    std::vector<JsValue> out;
    js_iterate(iterable, [&](const JsValue& v) {
        out.push_back(v);
        return true;
    });
    return out;
}

// ---------------------------------------------------------------------------
// Built-in definition helpers
// ---------------------------------------------------------------------------

JsValue js_make_native(const char* name, int length, JsValue::NativeFn fn) {
    JsValue v = JsValue::make_native_function(std::move(fn));
    v.define_property("name", JsValue::make_string(name), PropertyMap::Configurable);
    v.define_property("length", JsValue::make_number(length), PropertyMap::Configurable);
    return v;
}

void js_define_method(JsObject* object, const char* name, int length, JsValue::NativeFn fn) {
    auto& entry = object->properties.entry(name);
    entry.second = js_make_native(name, length, std::move(fn));
    entry.flags = PropertyMap::Hidden;
}

void js_define_method(JsValue& object, const char* name, int length, JsValue::NativeFn fn) {
    object.define_property(name, js_make_native(name, length, std::move(fn)), PropertyMap::Hidden);
}

void js_define_hidden(JsObject* object, const std::string& name, JsValue value) {
    auto& entry = object->properties.entry(name);
    entry.second = std::move(value);
    entry.flags = PropertyMap::Hidden;
}

void js_define_getter(JsObject* object, const char* name, JsValue::NativeFn getter) {
    auto& entry = object->properties.entry(name);
    entry.second = js_make_native(name, 0, std::move(getter));
    entry.setter.reset();
    entry.flags = PropertyMap::Configurable | PropertyMap::Accessor;
    object->has_accessors = true;
}

JsValue js_object_value(JsObject* object) {
    return JsValue::make_object(object);
}

// ---------------------------------------------------------------------------
// Intrinsics
// ---------------------------------------------------------------------------

JsIntrinsics::JsIntrinsics() {
    // Only allocation happens here. Built-in methods are installed lazily by
    // ensure_installed(), because installing them creates objects, and creating
    // an object needs these prototypes to exist already.
    auto& heap = Heap::default_heap();
    auto make = [&](JsObject* parent) {
        JsObject* obj = heap.allocate_object();
        obj->prototype = parent;
        return obj;
    };
    object_proto = make(nullptr);
    function_proto = make(object_proto);
    array_proto = make(object_proto);
    array_proto->is_array = true;
    array_proto->klass = ObjectClass::Array;
    string_proto = make(object_proto);
    number_proto = make(object_proto);
    boolean_proto = make(object_proto);
    symbol_proto = make(object_proto);
    iterator_proto = make(object_proto);
    regexp_proto = make(object_proto);
    date_proto = make(object_proto);
    map_proto = make(object_proto);
    set_proto = make(object_proto);
    weakmap_proto = make(object_proto);
    weakset_proto = make(object_proto);
    error_protos[static_cast<int>(JsErrorKind::Error)] = make(object_proto);
    for (int i = 1; i < 7; ++i) error_protos[i] = make(error_protos[0]);

    for (JsObject* cell : {object_proto, function_proto, array_proto, string_proto, number_proto,
                           boolean_proto, symbol_proto, iterator_proto, regexp_proto, date_proto,
                           map_proto, set_proto, weakmap_proto, weakset_proto}) {
        m_roots.push_back(JsValue::make_object(cell));
    }
    for (JsObject* cell : error_protos) m_roots.push_back(JsValue::make_object(cell));
}

void JsIntrinsics::add_global(const std::string& name, JsValue value) {
    for (auto& entry : globals) {
        if (entry.first == name) {
            entry.second = std::move(value);
            return;
        }
    }
    globals.emplace_back(name, std::move(value));
}

const JsValue* JsIntrinsics::find_global(const std::string& name) const {
    for (const auto& entry : globals) {
        if (entry.first == name) return &entry.second;
    }
    return nullptr;
}

void JsIntrinsics::ensure_installed() {
    std::lock_guard<std::recursive_mutex> lock(m_install_mutex);
    // A re-entrant call from an installer sees m_installing and returns: the
    // prototypes it needs were allocated by the constructor.
    if (m_ready.load() || m_installing) return;
    m_installing = true;

    install_object_builtins(*this);
    install_error_builtins(*this);
    install_array_builtins(*this);
    install_string_builtins(*this);
    install_number_builtins(*this);
    install_collection_builtins(*this);
    install_regexp_builtins(*this);
    install_date_builtins(*this);
    install_json_builtins(*this);

    // The collector frees anything it cannot reach, and nothing else points at
    // these: register them as permanent roots.
    Heap::default_heap().add_root_provider(this, [this](std::vector<JsValue*>& roots) {
        for (auto& value : m_roots) roots.push_back(&value);
        for (auto& entry : globals) roots.push_back(&entry.second);
    });

    m_ready.store(true);
}

JsIntrinsics& JsIntrinsics::get() {
    static JsIntrinsics instance;
    if (!instance.m_ready.load(std::memory_order_acquire)) instance.ensure_installed();
    return instance;
}

} // namespace litebird
