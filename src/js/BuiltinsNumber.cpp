// Number, Math and the numeric global functions.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInfinity = std::numeric_limits<double>::infinity();

double this_number(const char* method) {
    const JsValue& me = JsValue::current_this();
    if (me.type == JsType::Number) return me.number_val;
    // A Number object, as in `new Number(5).toFixed(1)`.
    if (const JsValue* inner = js_wrapped_primitive(me); inner && inner->type == JsType::Number) {
        return inner->number_val;
    }
    js_throw_type_error(std::string("Number.prototype.") + method + " requires that 'this' be a Number");
}

bool is_space_byte(unsigned char c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D);
}

size_t skip_leading_space(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (is_space_byte(c)) { ++i; continue; }
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) { i += 2; continue; }
        if (c == 0xEF && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBB &&
            static_cast<unsigned char>(s[i + 2]) == 0xBF) { i += 3; continue; }
        if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80) {
            unsigned char d = static_cast<unsigned char>(s[i + 2]);
            if ((d >= 0x80 && d <= 0x8A) || d == 0xA8 || d == 0xA9 || d == 0xAF) { i += 3; continue; }
        }
        break;
    }
    return i;
}

JsValue parse_int(Args args) {
    std::string s = js_to_string(js_arg(args, 0));
    size_t i = skip_leading_space(s);
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) negative = s[i++] == '-';

    int radix = js_to_int32(js_to_number(js_arg(args, 1)));
    bool strip_prefix = true;
    if (radix != 0) {
        if (radix < 2 || radix > 36) return JsValue::make_number(kNaN);
        if (radix != 16) strip_prefix = false;
    } else {
        radix = 10;
    }
    if (strip_prefix && i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        radix = 16;
    }

    double value = 0.0;
    bool any = false;
    const size_t first_digit = i;
    for (; i < s.size(); ++i) {
        char c = s[i];
        int digit = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'z') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : 99;
        if (digit >= radix) break;
        value = value * radix + digit;
        any = true;
    }
    if (!any) return JsValue::make_number(kNaN);
    // Accumulating digit by digit rounds at every step once the value passes
    // 2^53. For decimal input, convert the run of digits in one correctly
    // rounded step instead: parseInt("123456789012345680000") must give the
    // same number as the literal.
    if (radix == 10 && i - first_digit > 15) {
        value = js_string_to_number(s.substr(first_digit, i - first_digit));
    }
    return JsValue::make_number(negative ? -value : value);
}

JsValue parse_float(Args args) {
    std::string s = js_to_string(js_arg(args, 0));
    size_t begin = skip_leading_space(s);
    size_t i = begin;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    if (s.compare(i, 8, "Infinity") == 0) {
        return JsValue::make_number(s[begin] == '-' ? -kInfinity : kInfinity);
    }
    // The longest prefix that is a decimal literal.
    size_t digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; ++digits; }
    if (i < s.size() && s[i] == '.') {
        size_t save = i++;
        size_t frac = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; ++frac; }
        if (digits + frac == 0) i = save;
        digits += frac;
    }
    if (digits == 0) return JsValue::make_number(kNaN);
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t save = i++;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        size_t exp_digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; ++exp_digits; }
        if (exp_digits == 0) i = save;
    }
    return JsValue::make_number(std::strtod(s.substr(begin, i - begin).c_str(), nullptr));
}

/// Adds one unit in the last place of a decimal digit string such as "12.99".
void increment_decimal(std::string& digits) {
    size_t i = digits.size();
    while (i-- > 0) {
        if (digits[i] == '.') continue;
        if (digits[i] != '9') {
            ++digits[i];
            return;
        }
        digits[i] = '0';
    }
    digits.insert(digits.begin(), '1');
}

// Number.prototype.toFixed: rounds the exact binary value, and a true tie goes
// away from zero (printf rounds ties to even).
std::string to_fixed(double x, int fraction_digits) {
    if (std::isnan(x)) return "NaN";
    if (std::fabs(x) >= 1e21 || std::isinf(x)) return js_number_to_string(x);
    // The sign is determined before rounding: -0 has no sign, but a
    // negative nonzero value that rounds to zero keeps its minus sign.
    const bool negative = x < 0;
    double magnitude = std::fabs(x);

    const int extra = 40;
    std::vector<char> buffer(static_cast<size_t>(fraction_digits + extra + 400));
    int written = std::snprintf(buffer.data(), buffer.size(), "%.*f", fraction_digits + extra, magnitude);
    std::string full(buffer.data(), static_cast<size_t>(written));
    size_t dot = full.find('.');
    size_t keep = dot + 1 + static_cast<size_t>(fraction_digits);
    std::string kept = full.substr(0, fraction_digits == 0 ? dot : keep);
    std::string rest = full.substr(keep);

    bool round_up = false;
    if (!rest.empty() && rest[0] >= '5') {
        round_up = true;        // above half, or exactly half: both round away from zero
    }
    if (round_up) increment_decimal(kept);

    return (negative ? "-" : "") + kept;
}

std::string format_exponent(int exponent) {
    return std::string(exponent < 0 ? "e-" : "e+") + std::to_string(std::abs(exponent));
}

std::string to_exponential(double x, int fraction_digits, bool shortest) {
    if (std::isnan(x)) return "NaN";
    if (std::isinf(x)) return x < 0 ? "-Infinity" : "Infinity";
    if (shortest) {
        // As many digits as needed to round-trip.
        std::string plain = js_number_to_string(std::fabs(x));
        // Re-derive mantissa and exponent from the shortest digits.
        char buffer[64];
        int written = std::snprintf(buffer, sizeof(buffer), "%.17e", std::fabs(x));
        std::string sci(buffer, static_cast<size_t>(written));
        int exponent = std::atoi(sci.c_str() + sci.find('e') + 1);
        std::string digits;
        for (char c : plain) {
            if (c >= '0' && c <= '9') digits += c;
            if (c == 'e') break;
        }
        size_t first = digits.find_first_not_of('0');
        digits = first == std::string::npos ? "0" : digits.substr(first);
        while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
        std::string out = x < 0 ? "-" : "";
        out += digits[0];
        if (digits.size() > 1) out += "." + digits.substr(1);
        return out + format_exponent(x == 0 ? 0 : exponent);
    }
    char buffer[160];
    int written = std::snprintf(buffer, sizeof(buffer), "%.*e", fraction_digits, std::fabs(x));
    std::string sci(buffer, static_cast<size_t>(written));
    size_t e_pos = sci.find('e');
    int exponent = std::atoi(sci.c_str() + e_pos + 1);
    return (x < 0 ? "-" : "") + sci.substr(0, e_pos) + format_exponent(exponent);
}

std::string to_precision(double x, int precision) {
    if (std::isnan(x)) return "NaN";
    if (std::isinf(x)) return x < 0 ? "-Infinity" : "Infinity";
    char buffer[160];
    int written = std::snprintf(buffer, sizeof(buffer), "%.*e", precision - 1, std::fabs(x));
    std::string sci(buffer, static_cast<size_t>(written));
    size_t e_pos = sci.find('e');
    int exponent = x == 0 ? 0 : std::atoi(sci.c_str() + e_pos + 1);
    std::string digits;
    for (size_t i = 0; i < e_pos; ++i) {
        if (sci[i] != '.') digits += sci[i];
    }
    std::string out = (x < 0) ? "-" : "";
    if (exponent < -6 || exponent >= precision) {
        out += digits[0];
        if (precision > 1) out += "." + digits.substr(1);
        return out + format_exponent(exponent);
    }
    if (exponent >= 0) {
        out += digits.substr(0, static_cast<size_t>(exponent) + 1);
        if (static_cast<size_t>(exponent) + 1 < digits.size()) out += "." + digits.substr(static_cast<size_t>(exponent) + 1);
        return out;
    }
    out += "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
    return out;
}

double js_round(double x) {
    if (std::isnan(x) || std::isinf(x)) return x;
    if (std::fabs(x) >= 4503599627370496.0) return x;        // already an integer
    double floored = std::floor(x);
    double result = (x - floored >= 0.5) ? floored + 1.0 : floored;
    if (result == 0.0 && (x < 0 || std::signbit(x))) return -0.0;
    return result;
}

double js_pow(double base, double exponent) {
    if (std::isnan(exponent)) return kNaN;
    if (std::fabs(base) == 1.0 && std::isinf(exponent)) return kNaN;
    return std::pow(base, exponent);
}

std::mt19937_64& random_engine() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

// Exact integer multiples of 2^-1074. Thirty-four limbs hold every finite
// binary64 summed at most 2^53-1 times, including intermediate overflow.
struct PreciseMagnitude {
    std::array<uint64_t, 34> words{};

    void add_word(size_t index, uint64_t value) {
        while (value && index < words.size()) {
            const uint64_t before = words[index];
            words[index] += value;
            value = words[index] < before ? 1 : 0;
            ++index;
        }
    }

    void add(double value) {
        const uint64_t bits = std::bit_cast<uint64_t>(value);
        const unsigned exponent = static_cast<unsigned>((bits >> 52) & 0x7ff);
        uint64_t significand = bits & ((uint64_t{1} << 52) - 1);
        const unsigned shift = exponent ? exponent - 1 : 0;
        if (exponent) significand |= uint64_t{1} << 52;
        const size_t word = shift / 64;
        const unsigned offset = shift % 64;
        add_word(word, significand << offset);
        if (offset) add_word(word + 1, significand >> (64 - offset));
    }

    int compare(const PreciseMagnitude& other) const {
        for (size_t i = words.size(); i-- > 0;) {
            if (words[i] != other.words[i]) return words[i] < other.words[i] ? -1 : 1;
        }
        return 0;
    }

    void subtract(const PreciseMagnitude& other) { // *this >= other
        uint64_t borrow = 0;
        for (size_t i = 0; i < words.size(); ++i) {
            const uint64_t a = words[i], b = other.words[i];
            words[i] = a - b - borrow;
            borrow = (a < b || (borrow && a == b)) ? 1 : 0;
        }
    }

    bool bit(size_t n) const { return (words[n / 64] >> (n % 64)) & 1; }

    double rounded() const {
        size_t top = words.size();
        while (top && !words[top - 1]) --top;
        if (!top) return 0.0;
        const size_t highest = (top - 1) * 64 + 63 - std::countl_zero(words[top - 1]);
        const size_t shift = highest > 52 ? highest - 52 : 0;
        // Read at most 53 leading bits. This avoids host rounding during the
        // accumulation and also keeps the integer-to-double conversion exact.
        uint64_t significand = 0;
        for (size_t i = highest + 1; i-- > shift;) significand = (significand << 1) | bit(i);
        if (shift) {
            const bool guard = bit(shift - 1);
            bool sticky = false;
            for (size_t i = 0; i + 1 < shift && !sticky; ++i) sticky = bit(i);
            if (guard && (sticky || (significand & 1))) ++significand;
        }
        return std::ldexp(static_cast<double>(significand), static_cast<int>(shift) - 1074);
    }
};

JsValue math_sum_precise(Args args) {
    PreciseMagnitude positive, negative;
    bool any_non_negative_zero = false;
    bool nan = false, positive_inf = false, negative_inf = false;
    uint64_t count = 0;
    js_iterate(js_arg(args, 0), [&](const JsValue& item) {
        if (++count >= (uint64_t{1} << 53)) js_throw_range_error("Math.sumPrecise: too many values");
        if (item.type != JsType::Number) js_throw_type_error("Math.sumPrecise requires numbers");
        const double value = item.number_val;
        if (std::isnan(value)) nan = true;
        else if (std::isinf(value)) (std::signbit(value) ? negative_inf : positive_inf) = true;
        else if (value == 0.0) {
            if (!std::signbit(value)) any_non_negative_zero = true;
        } else {
            any_non_negative_zero = true;
            (std::signbit(value) ? negative : positive).add(value);
        }
        return true; // Even NaN does not skip later values or iterator effects.
    });
    if (nan || (positive_inf && negative_inf)) return JsValue::make_number(kNaN);
    if (positive_inf) return JsValue::make_number(kInfinity);
    if (negative_inf) return JsValue::make_number(-kInfinity);
    const int ordering = positive.compare(negative);
    if (!ordering) return JsValue::make_number(any_non_negative_zero ? 0.0 : -0.0);
    if (ordering > 0) {
        positive.subtract(negative);
        return JsValue::make_number(positive.rounded());
    }
    negative.subtract(positive);
    return JsValue::make_number(-negative.rounded());
}

template <typename Fn>
JsValue math_unary(const char* name, Fn fn) {
    return js_make_native(name, 1, [fn](Args args) {
        return JsValue::make_number(fn(js_to_number(js_arg(args, 0))));
    });
}

} // namespace

void install_number_builtins(JsIntrinsics& realm) {
    JsObject* np = realm.number_proto;

    // ---- Number -----------------------------------------------------------
    JsValue number_ctor = js_make_native("Number", 1, [](Args args) {
        JsValue number = JsValue::make_number(args.empty() ? 0.0 : js_to_number(args[0]));
        // `new Number(5)` is an object that wraps the number; `Number(5)` converts.
        return JsValue::current_is_construct() ? js_to_object(number) : number;
    });
    number_ctor.define_property("prototype", js_object_value(np), 0);
    js_define_hidden(np, "constructor", number_ctor);

    JsValue parse_int_fn = js_make_native("parseInt", 2, parse_int);
    JsValue parse_float_fn = js_make_native("parseFloat", 1, parse_float);

    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>>{
             {"EPSILON", std::numeric_limits<double>::epsilon()},
             {"MAX_SAFE_INTEGER", 9007199254740991.0},
             {"MIN_SAFE_INTEGER", -9007199254740991.0},
             {"MAX_VALUE", std::numeric_limits<double>::max()},
             {"MIN_VALUE", std::numeric_limits<double>::denorm_min()},
             {"POSITIVE_INFINITY", kInfinity},
             {"NEGATIVE_INFINITY", -kInfinity},
             {"NaN", kNaN}}) {
        number_ctor.define_property(name, JsValue::make_number(value), 0);
    }
    js_define_method(number_ctor, "isInteger", 1, [](Args args) {
        const JsValue& v = js_arg(args, 0);
        return JsValue::make_boolean(v.type == JsType::Number && std::isfinite(v.number_val) &&
                                     v.number_val == std::floor(v.number_val));
    });
    js_define_method(number_ctor, "isSafeInteger", 1, [](Args args) {
        const JsValue& v = js_arg(args, 0);
        return JsValue::make_boolean(v.type == JsType::Number && std::isfinite(v.number_val) &&
                                     v.number_val == std::floor(v.number_val) &&
                                     std::fabs(v.number_val) <= 9007199254740991.0);
    });
    js_define_method(number_ctor, "isFinite", 1, [](Args args) {
        const JsValue& v = js_arg(args, 0);
        return JsValue::make_boolean(v.type == JsType::Number && std::isfinite(v.number_val));
    });
    js_define_method(number_ctor, "isNaN", 1, [](Args args) {
        const JsValue& v = js_arg(args, 0);
        return JsValue::make_boolean(v.type == JsType::Number && std::isnan(v.number_val));
    });
    number_ctor.define_property("parseInt", parse_int_fn, PropertyMap::Hidden);
    number_ctor.define_property("parseFloat", parse_float_fn, PropertyMap::Hidden);
    realm.add_global("Number", number_ctor);

    js_define_method(np, "toString", 1, [](Args args) {
        double x = this_number("toString");
        if (js_arg(args, 0).is_undefined()) return JsValue::make_string(js_number_to_string(x));
        double radix = js_to_integer(js_to_number(js_arg(args, 0)));
        if (radix < 2 || radix > 36) js_throw_range_error("toString() radix must be between 2 and 36");
        return JsValue::make_string(js_number_to_string_radix(x, static_cast<int>(radix)));
    });
    js_define_method(np, "toLocaleString", 0, [](Args) {
        return JsValue::make_string(js_number_to_string(this_number("toLocaleString")));
    });
    js_define_method(np, "valueOf", 0, [](Args) { return JsValue::make_number(this_number("valueOf")); });
    js_define_method(np, "toFixed", 1, [](Args args) {
        double x = this_number("toFixed");
        double digits = js_to_integer(js_to_number(js_arg(args, 0)));
        if (digits < 0 || digits > 100) js_throw_range_error("toFixed() digits argument must be between 0 and 100");
        return JsValue::make_string(to_fixed(x, static_cast<int>(digits)));
    });
    js_define_method(np, "toExponential", 1, [](Args args) {
        double x = this_number("toExponential");
        if (js_arg(args, 0).is_undefined()) return JsValue::make_string(to_exponential(x, 0, true));
        double digits = js_to_integer(js_to_number(js_arg(args, 0)));
        // The precision is converted even for NaN and infinities, but the
        // range check applies only to finite receivers.
        if (!std::isfinite(x)) return JsValue::make_string(js_number_to_string(x));
        if (digits < 0 || digits > 100) js_throw_range_error("toExponential() argument must be between 0 and 100");
        return JsValue::make_string(to_exponential(x, static_cast<int>(digits), false));
    });
    js_define_method(np, "toPrecision", 1, [](Args args) {
        double x = this_number("toPrecision");
        if (js_arg(args, 0).is_undefined()) return JsValue::make_string(js_number_to_string(x));
        double precision = js_to_integer(js_to_number(js_arg(args, 0)));
        if (!std::isfinite(x)) return JsValue::make_string(js_number_to_string(x));
        if (precision < 1 || precision > 100) js_throw_range_error("toPrecision() argument must be between 1 and 100");
        return JsValue::make_string(to_precision(x, static_cast<int>(precision)));
    });

    // ---- Global numeric functions -----------------------------------------
    realm.add_global("parseInt", parse_int_fn);
    realm.add_global("parseFloat", parse_float_fn);
    realm.add_global("isNaN", js_make_native("isNaN", 1, [](Args args) {
        return JsValue::make_boolean(std::isnan(js_to_number(js_arg(args, 0))));
    }));
    realm.add_global("isFinite", js_make_native("isFinite", 1, [](Args args) {
        return JsValue::make_boolean(std::isfinite(js_to_number(js_arg(args, 0))));
    }));
    realm.add_global("NaN", JsValue::make_number(kNaN));
    realm.add_global("Infinity", JsValue::make_number(kInfinity));

    // ---- Math -------------------------------------------------------------
    JsValue math = JsValue::make_object();
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>>{
             {"PI", 3.141592653589793}, {"E", 2.718281828459045}, {"LN2", 0.6931471805599453},
             {"LN10", 2.302585092994046}, {"LOG2E", 1.4426950408889634}, {"LOG10E", 0.4342944819032518},
             {"SQRT2", 1.4142135623730951}, {"SQRT1_2", 0.7071067811865476}}) {
        math.define_property(name, JsValue::make_number(value), 0);
    }
    // Math's twenty-six methods are MethodDefinitions like every other built-in
    // method -- an ordinary function object with no [[Construct]] -- so each is
    // marked here. They are made by math_unary or by a lambda, which keep the
    // `name` and `length` the standard gives them, so the mark is set on the
    // value rather than the value being rebuilt around a wrapper.
    auto def = [&](const char* name, JsValue fn) {
        js_mark_as_method(fn);
        math.define_property(name, fn, PropertyMap::Hidden);
    };

    def("abs", math_unary("abs", [](double x) { return std::fabs(x); }));
    def("ceil", math_unary("ceil", [](double x) { return std::ceil(x); }));
    def("floor", math_unary("floor", [](double x) { return std::floor(x); }));
    def("round", math_unary("round", js_round));
    def("trunc", math_unary("trunc", [](double x) { return std::trunc(x); }));
    def("sign", math_unary("sign", [](double x) { return std::isnan(x) ? kNaN : x > 0 ? 1.0 : x < 0 ? -1.0 : x; }));
    def("sqrt", math_unary("sqrt", [](double x) { return std::sqrt(x); }));
    def("cbrt", math_unary("cbrt", [](double x) {
        double root = std::cbrt(x);
        if (!std::isfinite(root) || root == 0.0) return root;
        // The C library's cbrt may be an ulp off (for example, cbrt(27)).
        // Refine with an algebraically equivalent Newton step that never
        // cubes the root: root*root*root can overflow for finite x near
        // DBL_MAX, even though the cube root is finite. root squared is
        // representable across the entire finite binary64 input range.
        return root + (x / (root * root) - root) / 3.0;
    }));
    def("exp", math_unary("exp", [](double x) { return std::exp(x); }));
    def("expm1", math_unary("expm1", [](double x) { return std::expm1(x); }));
    def("log", math_unary("log", [](double x) { return std::log(x); }));
    def("log2", math_unary("log2", [](double x) { return std::log2(x); }));
    def("log10", math_unary("log10", [](double x) { return std::log10(x); }));
    def("log1p", math_unary("log1p", [](double x) { return std::log1p(x); }));
    def("sin", math_unary("sin", [](double x) { return std::sin(x); }));
    def("cos", math_unary("cos", [](double x) { return std::cos(x); }));
    def("tan", math_unary("tan", [](double x) { return std::tan(x); }));
    def("asin", math_unary("asin", [](double x) { return std::asin(x); }));
    def("acos", math_unary("acos", [](double x) { return std::acos(x); }));
    def("atan", math_unary("atan", [](double x) { return std::atan(x); }));
    def("sinh", math_unary("sinh", [](double x) { return std::sinh(x); }));
    def("cosh", math_unary("cosh", [](double x) { return std::cosh(x); }));
    def("tanh", math_unary("tanh", [](double x) { return std::tanh(x); }));
    def("asinh", math_unary("asinh", [](double x) { return std::asinh(x); }));
    def("acosh", math_unary("acosh", [](double x) { return std::acosh(x); }));
    def("atanh", math_unary("atanh", [](double x) { return std::atanh(x); }));
    def("fround", math_unary("fround", [](double x) { return static_cast<double>(static_cast<float>(x)); }));
    // Round directly from binary64 to binary16, avoiding a binary32
    // intermediate (which can double-round a value just off a midpoint).
    // Scaling by a power of two is exact; floor and the tie comparison are
    // independent of the host floating-point rounding mode.
    def("f16round", math_unary("f16round", [](double x) {
        if (!std::isfinite(x) || x == 0.0) return x;
        const double magnitude = std::fabs(x);
        if (magnitude >= 65520.0) return std::copysign(kInfinity, x);
        int exponent = 0;
        std::frexp(magnitude, &exponent);
        const int step_exponent = std::max(-24, exponent - 11);
        const double units = std::ldexp(magnitude, -step_exponent);
        double rounded = std::floor(units);
        const double fraction = units - rounded;
        if (fraction > 0.5 || (fraction == 0.5 && std::fmod(rounded, 2.0) != 0.0)) {
            rounded += 1.0;
        }
        return std::copysign(std::ldexp(rounded, step_exponent), x);
    }));
    def("clz32", math_unary("clz32", [](double x) {
        uint32_t v = js_to_uint32(x);
        int count = 0;
        for (uint32_t bit = 0x80000000u; bit && !(v & bit); bit >>= 1) ++count;
        return static_cast<double>(count);
    }));
    def("atan2", js_make_native("atan2", 2, [](Args args) {
        // ToNumber can invoke user code: C++ function arguments have no
        // guaranteed left-to-right evaluation order, but ECMAScript does.
        const double y = js_to_number(js_arg(args, 0));
        const double x = js_to_number(js_arg(args, 1));
        return JsValue::make_number(std::atan2(y, x));
    }));
    def("pow", js_make_native("pow", 2, [](Args args) {
        const double base = js_to_number(js_arg(args, 0));
        const double exponent = js_to_number(js_arg(args, 1));
        return JsValue::make_number(js_pow(base, exponent));
    }));
    def("imul", js_make_native("imul", 2, [](Args args) {
        uint32_t a = js_to_uint32(js_to_number(js_arg(args, 0)));
        uint32_t b = js_to_uint32(js_to_number(js_arg(args, 1)));
        return JsValue::make_number(static_cast<int32_t>(a * b));
    }));
    def("sumPrecise", js_make_native("sumPrecise", 1, math_sum_precise));
    def("hypot", js_make_native("hypot", 2, [](Args args) {
        // Every argument is converted first (that can run script), then the
        // values are scaled by the largest so that squaring neither overflows
        // (1e300) nor vanishes (1e-200).
        std::vector<double> values;
        values.reserve(args.size());
        for (const auto& arg : args) values.push_back(std::fabs(js_to_number(arg)));
        bool has_nan = false;
        double largest = 0.0;
        for (double v : values) {
            if (std::isinf(v)) return JsValue::make_number(kInfinity);
            if (std::isnan(v)) has_nan = true;
            else if (v > largest) largest = v;
        }
        if (has_nan) return JsValue::make_number(kNaN);
        if (largest == 0.0) return JsValue::make_number(0.0);
        if (values.size() == 2) return JsValue::make_number(std::hypot(values[0], values[1]));
        double sum = 0.0;
        for (double v : values) {
            double scaled = v / largest;
            sum += scaled * scaled;
        }
        return JsValue::make_number(largest * std::sqrt(sum));
    }));
    def("max", js_make_native("max", 2, [](Args args) {
        double best = -kInfinity;
        bool has_nan = false;
        for (const auto& arg : args) {
            double v = js_to_number(arg);
            if (std::isnan(v)) has_nan = true;
            else if (v > best || (v == 0 && best == 0 && !std::signbit(v))) best = v;
        }
        return JsValue::make_number(has_nan ? kNaN : best);
    }));
    def("min", js_make_native("min", 2, [](Args args) {
        double best = kInfinity;
        bool has_nan = false;
        for (const auto& arg : args) {
            double v = js_to_number(arg);
            if (std::isnan(v)) has_nan = true;
            else if (v < best || (v == 0 && best == 0 && std::signbit(v))) best = v;
        }
        return JsValue::make_number(has_nan ? kNaN : best);
    }));
    def("random", js_make_native("random", 0, [](Args) {
        // 53 random bits scaled into [0, 1).
        uint64_t bits = random_engine()() >> 11;
        return JsValue::make_number(static_cast<double>(bits) * (1.0 / 9007199254740992.0));
    }));
    math.define_property("@@toStringTag", JsValue::make_string("Math"), PropertyMap::Configurable);
    realm.add_global("Math", math);
}

} // namespace litebird
