#include "litebird/js/DomBindings.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Generator.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsLexer.hpp"
#include "litebird/js/Promise.hpp"
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
    for (JsObject* obj = value.object_val; obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
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
        case JsType::String: return value.string_val.str();
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
    if (dynamic_cast<const JsSyntaxError*>(&ex)) return js_make_error(JsErrorKind::SyntaxError, ex.what());
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

bool (*js_uncaught_handler)(const JsValue& value) = nullptr;

void js_report_uncaught(const JsValue& value) {
    if (js_uncaught_handler && js_uncaught_handler(value)) return;
    g_uncaught_errors.fetch_add(1);
    std::cerr << "Uncaught " << js_output_text(js_describe_thrown(value)) << std::endl;
}

void js_report_uncaught(const std::exception& ex) {
    if (auto thrown = dynamic_cast<const JsThrow*>(&ex)) {
        js_report_uncaught(thrown->value);
        return;
    }
    if (dynamic_cast<const JsSyntaxError*>(&ex)) {
        if (js_uncaught_handler && js_uncaught_handler(js_make_error(JsErrorKind::SyntaxError, ex.what()))) return;
        g_uncaught_errors.fetch_add(1);
        std::cerr << "Uncaught SyntaxError: " << ex.what() << std::endl;
        return;
    }
    g_uncaught_errors.fetch_add(1);
    std::cerr << "Uncaught " << ex.what() << std::endl;
}

void js_report_unhandled_rejection(const JsValue& reason) {
    g_uncaught_errors.fetch_add(1);
    std::cerr << "Uncaught (in promise) " << js_output_text(js_describe_thrown(reason)) << std::endl;
}

JsScriptScope::JsScriptScope() : m_loop(EventLoop::default_loop()) {
    // Counted per agent, not per thread: a suspended function's thread
    // continues the JavaScript stack of whichever thread resumed it.
    m_loop.enter_script();
}

JsScriptScope::~JsScriptScope() {
    if (m_loop.leave_script() != 0) return;
    // The JavaScript stack is empty: microtask checkpoint. Not while an
    // exception is unwinding through here, which only happens when the engine
    // is discarding a frame.
    if (std::uncaught_exceptions() > 0) return;
    m_loop.run_microtasks();
}

int js_uncaught_error_count() {
    return g_uncaught_errors.load();
}

bool js_is_internal_unwind(const std::exception& ex) {
    return dynamic_cast<const GeneratorAbortException*>(&ex) != nullptr ||
           dynamic_cast<const GeneratorReturnException*>(&ex) != nullptr;
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

bool is_js_space(std::string_view s, size_t i, size_t& width) {
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

double js_string_to_number(std::string_view text) {
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
    const std::string s(text.substr(begin, end - begin));

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

const JsValue* js_wrapped_primitive(const JsValue& value) {
    if (value.type != JsType::Object || !value.object_val) return nullptr;
    switch (value.object_val->klass) {
        case ObjectClass::String:
        case ObjectClass::Number:
        case ObjectClass::Boolean:
        case ObjectClass::Symbol:
            return static_cast<const JsValue*>(value.object_val->internal.get());
        default:
            return nullptr;
    }
}

JsValue js_to_object(const JsValue& value) {
    if (value.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
    if (value.is_object_like()) return value;

    auto& realm = JsIntrinsics::get();
    JsObject* cell = Heap::default_heap().allocate_object();
    switch (value.type) {
        case JsType::String:
            cell->prototype = realm.string_proto;
            cell->klass = ObjectClass::String;
            // `length` is an own property of a String object, fixed at creation.
            {
                auto& length = cell->properties.entry("length");
                length.second = JsValue::make_number(static_cast<double>(value.string_val.length()));
                length.flags = 0;
            }
            break;
        case JsType::Number:
            cell->prototype = realm.number_proto;
            cell->klass = ObjectClass::Number;
            break;
        case JsType::Boolean:
            cell->prototype = realm.boolean_proto;
            cell->klass = ObjectClass::Boolean;
            break;
        default:
            cell->prototype = realm.symbol_proto;
            cell->klass = ObjectClass::Symbol;
            break;
    }
    cell->internal = std::make_shared<JsValue>(value);
    return JsValue::make_object(cell);
}

JsValue js_to_primitive(const JsValue& value, JsPrimitiveHint hint) {
    if (!value.is_object_like()) return value;

    // An object can take over its own conversion with Symbol.toPrimitive.
    JsValue exotic = value.get_property("@@toPrimitive");
    if (exotic.is_function()) {
        const char* hint_name = hint == JsPrimitiveHint::String ? "string"
                              : hint == JsPrimitiveHint::Number ? "number" : "default";
        JsValue result = exotic.call_with_this(value, {JsValue::make_string(hint_name)});
        if (result.is_object_like()) js_throw_type_error("Cannot convert object to primitive value");
        return result;
    }

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
        case JsType::String: return js_string_to_number(value.string_val.view());
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
        case JsType::String: return value.string_val.str();
        case JsType::Symbol: js_throw_type_error("Cannot convert a Symbol value to a string");
        default: return js_to_string(js_to_primitive(value, JsPrimitiveHint::String));
    }
}

std::string js_output_text(std::string text) {
    if (JsString::find_lone_half(text) == std::string::npos) return text;
    // Rare. `text` may be several values written side by side, so a high half
    // from one can stand next to a low half from the next; a string value
    // joins those into the character they spell before the rest is replaced.
    return JsString(text).to_well_formed().str();
}

JsString js_to_js_string(const JsValue& value) {
    if (value.type == JsType::String) return value.string_val;
    if (value.is_object_like()) {
        JsValue primitive = js_to_primitive(value, JsPrimitiveHint::String);
        if (primitive.type == JsType::String) return primitive.string_val;
        return JsString(js_to_string(primitive));
    }
    return JsString(js_to_string(value));
}

std::string js_to_property_key(const JsValue& value) {
    switch (value.type) {
        case JsType::String: return value.string_val.str();
        case JsType::Symbol: return value.string_val.str();     // the symbol's unique key
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
        case JsType::Object: return "object";
    }
    return "undefined";
}

JsValue js_typeof_value(const JsValue& value) {
    // `typeof x === "string"` runs in every hot loop of real script. The
    // answers are made once; handing one out copies a handle, not text.
    struct Answers {
        JsValue undefined_{JsValue::make_string("undefined")};
        JsValue object_{JsValue::make_string("object")};
        JsValue boolean_{JsValue::make_string("boolean")};
        JsValue number_{JsValue::make_string("number")};
        JsValue string_{JsValue::make_string("string")};
        JsValue symbol_{JsValue::make_string("symbol")};
        JsValue function_{JsValue::make_string("function")};
    };
    // Never destroyed: a value can ask for its type during static destruction.
    static const Answers* const answers = new Answers;
    switch (value.type) {
        case JsType::Undefined: return answers->undefined_;
        case JsType::Boolean: return answers->boolean_;
        case JsType::Number: return answers->number_;
        case JsType::String: return answers->string_;
        case JsType::Symbol: return answers->symbol_;
        case JsType::Function:
        case JsType::NativeFunction: return answers->function_;
        case JsType::Null:
        case JsType::Object: return answers->object_;
    }
    return answers->undefined_;
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

bool js_loose_equals(const JsValue& a, const JsValue& b) {
    if (a.type == b.type || (a.is_function() && b.is_function())) return strict_equals(a, b);
    if (a.is_nullish() && b.is_nullish()) return true;
    if (a.is_nullish() || b.is_nullish()) return false;

    if (a.type == JsType::Number && b.type == JsType::String) return a.number_val == js_string_to_number(b.string_val.view());
    if (a.type == JsType::String && b.type == JsType::Number) return js_string_to_number(a.string_val.view()) == b.number_val;
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
        // By UTF-16 code unit, which is not the order of the UTF-8 bytes.
        return JsString::compare(pa.string_val.view(), pb.string_val.view()) < 0 ? 1 : 0;
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
    // JsString::concat is what keeps `s += x` in a loop linear: when `a` is
    // the newest text in its block, `b` is written after it and nothing of `a`
    // is copied.
    if (a.type == JsType::String && b.type == JsType::String) {
        return JsValue::make_string(JsString::concat(a.string_val, b.string_val));
    }
    // A string and a number, as in `'item' + i`: nothing here can run script,
    // so the general path's conversions (and the copies they make) are skipped.
    if (a.type == JsType::String && b.type == JsType::Number) {
        return JsValue::make_string(JsString::concat(a.string_val, std::string_view(js_number_to_string(b.number_val))));
    }
    if (a.type == JsType::Number && b.type == JsType::String) {
        return JsValue::make_string(JsString::concat(std::string_view(js_number_to_string(a.number_val)), b.string_val));
    }
    JsValue pa = js_to_primitive(a);
    JsValue pb = js_to_primitive(b);
    if (pa.type == JsType::String && pb.type == JsType::String) {
        return JsValue::make_string(JsString::concat(pa.string_val, pb.string_val));
    }
    if (pa.type == JsType::String) {
        return JsValue::make_string(JsString::concat(pa.string_val, std::string_view(js_to_string(pb))));
    }
    if (pb.type == JsType::String) {
        return JsValue::make_string(JsString::concat(std::string_view(js_to_string(pa)), pb.string_val));
    }
    return JsValue::make_number(js_to_number(pa) + js_to_number(pb));
}

bool js_instance_of(const JsValue& value, const JsValue& constructor) {
    if (!constructor.is_function()) js_throw_type_error("Right-hand side of 'instanceof' is not callable");
    if (!value.is_object_like()) return false;
    {
        // A cross-origin WindowProxy or Location has no prototype (HTML
        // §7.2.3.1), so it can never be `instanceof` anything.
        std::vector<std::string> exposed;
        if (litebird::dom::cross_origin_own_keys(value, true, exposed)) return false;
    }
    JsValue proto = constructor.get_property("prototype");
    // A `prototype` that is there and is not an object is a TypeError, as the
    // language has it. One that is not there at all still answers false. The
    // language throws for that too, but two kinds of function here have none
    // and must not start to throw: a bound function, which should be asked
    // for its target's and cannot be told from another built-in, and some of
    // the host's constructors (`r instanceof Response`).
    if (!proto.is_object_like()) {
        if (proto.type == JsType::Undefined) return false;
        js_throw_type_error("Function has non-object prototype '" + js_describe_thrown(proto) +
                            "' in instanceof check");
    }
    // The cell of an object, or of a function (`C.prototype = someFunction`).
    // A function with no cell is in nobody's chain.
    const JsObject* target = proto.property_cell();
    if (!target) return false;
    int guard = 0;
    for (JsObject* obj = value.prototype_object(); obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
        if (obj == target) return true;
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

bool js_is_constructor(const JsValue& value) {
    if (!value.is_function()) return false;
    const JsValue::FunctionData* fd = value.function_data();
    if (fd && fd->node) return fd->is_constructor;
    // A built-in is taken to be a constructor unless its cell says otherwise.
    // Only one is marked (see JsIntrinsics::JsIntrinsics). The mark is on the
    // cell, not a comparison with this realm's Function.prototype (the
    // function may be another realm's) and not a property (a script could
    // read a property, and every function would inherit it).
    if (const JsObject* cell = value.property_cell()) return !cell->not_constructor;
    return true;
}

JsValue js_construct(const JsValue& ctor, const std::vector<JsValue>& args, const JsValue* new_target) {
    if (!ctor.is_function()) js_throw_type_error(js_describe_thrown(ctor) + " is not a constructor");
    const JsValue::FunctionData* fd = ctor.function_data();
    if (!js_is_constructor(ctor)) {
        js_throw_type_error((!fd || fd->name.empty() ? std::string("function") : fd->name) + " is not a constructor");
    }
    if (new_target) {
        // It has to be something `new` could be used on as well.
        if (!js_is_constructor(*new_target)) {
            js_throw_type_error(js_describe_thrown(*new_target) + " is not a constructor");
        }
        if (new_target->identity() == ctor.identity()) new_target = nullptr;     // the ordinary case
    }
    if (fd && fd->node) {
        // A script function is given the object: it has the `prototype` of
        // new.target, and inside the function new.target is that function.
        JsValue instance = JsValue::make_object();
        JsValue proto = (new_target ? *new_target : ctor).get_property("prototype");
        if (proto.is_object_like()) {
            if (JsObject* target = proto.as_prototype()) instance.object_val->prototype = target;
        }
        JsValue result = ctor.construct(args, &instance, new_target);
        return result.is_object_like() ? result : instance;
    }
    // A built-in constructor makes its own object.
    if (!new_target) return ctor.construct(args, nullptr);
    // A bound function constructs its target with this new.target, and what
    // that makes or returns is the answer as it stands.
    if (ctor.forwards_construct()) return ctor.construct(args, nullptr, new_target);
    // Any other built-in, with another function as new.target. A host
    // constructor asks for the object it is to construct on, the way it does
    // under `super(...)`, and is given one that has new.target's prototype:
    // this is how `Reflect.construct(HTMLElement, [], Derived)` makes an
    // element. The others build their own (an array, an error), and that
    // gets the prototype afterwards: after the built-in has looked at its
    // arguments, which is the order the language has
    // (`Reflect.construct(Promise, [], NewTarget)` complains about the
    // executor before `NewTarget.prototype` is read).
    // Either way `NewTarget.prototype` is read once, and where it is not an
    // object the built-in's own prototype stays.
    JsValue asked_for;
    JsValue result = ctor.construct_for(args, *new_target, asked_for);
    if (!result.is_object_like()) return result;
    JsObject* target = nullptr;
    if (asked_for.is_object()) {
        if (result.identity() == asked_for.identity()) return result;
        target = asked_for.object_val->prototype;       // asked for and then not used: what was read then
    } else if (asked_for.is_undefined()) {
        JsValue proto = new_target->get_property("prototype");
        if (proto.is_object_like()) target = proto.as_prototype();
    }
    if (target) {
        JsObject* cell = result.is_function() ? result.function_cell() : result.object_val.get();
        if (cell) js_set_prototype(cell, target);
    }
    return result;
}

JsValue js_bind_this(const JsValue& fn, const JsValue& receiver) {
    return JsValue::make_native_function([fn, receiver](const std::vector<JsValue>& args) {
        return fn.call_with_this(receiver, args);
    });
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

bool js_is_ascii(std::string_view s) {
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

std::u16string js_utf8_to_utf16(std::string_view s) {
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

std::string js_utf16_to_utf8(std::u16string_view s) {
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

size_t js_string_length(std::string_view s) {
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
    // Workers can still be unwinding at process exit. Keep the registry alive
    // like Heap::default_heap() rather than relying on static-destruction order
    // between an atexit worker join and an unordered_map destructor.
    static auto* registry = new std::unordered_map<std::string, std::string>();
    return *registry;
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
// Structured clone packets
// ---------------------------------------------------------------------------

namespace {

// A message should fail rather than turn an accidental graph into unbounded
// native memory. This counts every primitive and object node in the neutral
// packet, not just the objects a script allocated.
constexpr std::size_t kMaxStructuredCloneNodes = 1'000'000;

bool array_element_key(std::string_view key, std::size_t length) {
    if (key.empty() || key.size() > 20) return false;
    if (key.size() > 1 && key.front() == '0') return false;
    std::size_t index = 0;
    for (char c : key) {
        if (c < '0' || c > '9') return false;
        const std::size_t digit = static_cast<std::size_t>(c - '0');
        if (index > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
        index = index * 10 + digit;
    }
    return index < length;
}

const char* unsupported_clone_kind(const JsValue& value) {
    if (value.is_function()) return "function";
    if (value.is_symbol()) return "symbol";
    if (value.dom_node_value()) return "DOM node";
    if (value.type != JsType::Object || !value.object_val) return "value";
    const JsObject* cell = value.object_val;
    if (cell->exotic) return "host object";
    switch (cell->klass) {
        case ObjectClass::Date: return "Date";
        case ObjectClass::RegExp: return "RegExp";
        case ObjectClass::Map: return "Map";
        case ObjectClass::Set: return "Set";
        case ObjectClass::WeakMap: return "WeakMap";
        case ObjectClass::WeakSet: return "WeakSet";
        case ObjectClass::Error: return "Error";
        case ObjectClass::Promise: return "Promise";
        case ObjectClass::Generator: return "Generator";
        case ObjectClass::Iterator: return "iterator";
        default: return "object";
    }
}

class StructuredCloneWriter {
public:
    StructuredCloneWriter(JsStructuredClone& out, std::string& error)
        : m_out(out), m_error(error) {}

    bool write(const JsValue& value, std::size_t& index) {
        js_check_stack();
        switch (value.type) {
            case JsType::Undefined: return append_primitive(JsStructuredClone::Kind::Undefined, value, index);
            case JsType::Null: return append_primitive(JsStructuredClone::Kind::Null, value, index);
            case JsType::Boolean: return append_primitive(JsStructuredClone::Kind::Boolean, value, index);
            case JsType::Number: return append_primitive(JsStructuredClone::Kind::Number, value, index);
            case JsType::String: return append_primitive(JsStructuredClone::Kind::String, value, index);
            case JsType::Symbol:
            case JsType::Function:
            case JsType::NativeFunction:
                return unsupported(value);
            case JsType::Object:
                return write_object(value, index);
        }
        return unsupported(value);
    }

private:
    bool reserve_node(std::size_t& index) {
        if (m_out.nodes.size() >= kMaxStructuredCloneNodes) {
            m_error = "The message graph exceeds the 1000000-node structured-clone limit.";
            return false;
        }
        index = m_out.nodes.size();
        m_out.nodes.emplace_back();
        return true;
    }

    bool append_primitive(JsStructuredClone::Kind kind, const JsValue& value, std::size_t& index) {
        if (!reserve_node(index)) return false;
        auto& node = m_out.nodes[index];
        node.kind = kind;
        if (kind == JsStructuredClone::Kind::Boolean) node.boolean = value.bool_val;
        if (kind == JsStructuredClone::Kind::Number) node.number = value.number_val;
        if (kind == JsStructuredClone::Kind::String) node.string = value.string_val.str();
        return true;
    }

    bool unsupported(const JsValue& value) {
        m_error = std::string("A ") + unsupported_clone_kind(value) + " could not be cloned.";
        return false;
    }

    bool write_object(const JsValue& value, std::size_t& index) {
        JsObject* cell = value.object_val;
        if (!cell) return unsupported(value);
        if (const auto found = m_seen.find(value.identity()); found != m_seen.end()) {
            index = found->second;
            return true;
        }

        // Only ordinary data objects and arrays are safe to materialize in a
        // different agent. A DOM wrapper, an exotic, or a native internal
        // payload would otherwise retain source-agent state behind the packet.
        const bool array = cell->is_array;
        if ((!array && cell->klass != ObjectClass::Plain) || cell->exotic || cell->dom_node ||
            cell->internal || cell->trace_extra || cell->lookup_fallback || cell->assign_hook) {
            return unsupported(value);
        }

        if (!reserve_node(index)) return false;
        m_seen.emplace(value.identity(), index);
        m_out.nodes[index].kind = array ? JsStructuredClone::Kind::Array : JsStructuredClone::Kind::Object;

        if (array) {
            m_out.nodes[index].elements.reserve(cell->array_elements.size());
            for (std::size_t i = 0; i < cell->array_elements.size(); ++i) {
                // A hole is written as the undefined it reads as; the receiver
                // of a message keeps it a hole because `undefined` and a hole
                // are the same thing to a clone that has no descriptor.
                JsValue element = value.array_element(i);
                std::size_t child = 0;
                if (!write(element, child)) return false;
                m_out.nodes[index].elements.push_back(child);
            }
        }

        // Structured cloning observes enumerable own string properties. It
        // does not carry descriptors or a custom prototype; getters run while
        // the sender is serializing, as they do for the web algorithm.
        for (const std::string& key : value.own_keys(/*include_non_enumerable=*/false)) {
            if (array && array_element_key(key, cell->array_elements.size())) continue;
            const JsValue property = value.get_property(key);
            std::size_t child = 0;
            if (!write(property, child)) return false;
            m_out.nodes[index].properties.emplace_back(key, child);
        }
        return true;
    }

    JsStructuredClone& m_out;
    std::string& m_error;
    std::unordered_map<const void*, std::size_t> m_seen;
};

bool clone_index_valid(const JsStructuredClone& clone, std::size_t index) {
    return index < clone.nodes.size();
}

} // namespace

bool js_write_structured_clone(const JsValue& value, JsStructuredClone& out, std::string& error) {
    out.nodes.clear();
    out.root = 0;
    error.clear();
    StructuredCloneWriter writer(out, error);
    if (!writer.write(value, out.root)) {
        out.nodes.clear();
        out.root = 0;
        return false;
    }
    return true;
}

JsValue js_read_structured_clone(const JsStructuredClone& clone) {
    if (clone.nodes.empty() || !clone_index_valid(clone, clone.root)) return JsValue::make_undefined();

    std::vector<JsValue> values(clone.nodes.size());
    for (std::size_t i = 0; i < clone.nodes.size(); ++i) {
        const auto& node = clone.nodes[i];
        switch (node.kind) {
            case JsStructuredClone::Kind::Undefined: values[i] = JsValue::make_undefined(); break;
            case JsStructuredClone::Kind::Null: values[i] = JsValue::make_null(); break;
            case JsStructuredClone::Kind::Boolean: values[i] = JsValue::make_boolean(node.boolean); break;
            case JsStructuredClone::Kind::Number: values[i] = JsValue::make_number(node.number); break;
            case JsStructuredClone::Kind::String: values[i] = JsValue::make_string(node.string); break;
            case JsStructuredClone::Kind::Object: values[i] = JsValue::make_object(); break;
            case JsStructuredClone::Kind::Array: values[i] = JsValue::make_array(); break;
        }
    }

    for (std::size_t i = 0; i < clone.nodes.size(); ++i) {
        const auto& node = clone.nodes[i];
        if (node.kind != JsStructuredClone::Kind::Object && node.kind != JsStructuredClone::Kind::Array) continue;
        JsObject* destination = values[i].object_val;
        if (!destination) return JsValue::make_undefined();
        if (node.kind == JsStructuredClone::Kind::Array) {
            for (std::size_t child : node.elements) {
                if (!clone_index_valid(clone, child)) return JsValue::make_undefined();
                destination->array_elements.push_back(values[child]);
            }
        }
        for (const auto& [key, child] : node.properties) {
            if (!clone_index_valid(clone, child)) return JsValue::make_undefined();
            // Defining, rather than assigning, prevents a cloned "__proto__"
            // key or an inherited setter from changing the new object's shape.
            values[i].define_property(key, values[child], PropertyMap::Default);
        }
    }
    return values[clone.root];
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

namespace {

/// The property key of `Symbol.iterator`, as the std::string that property
/// lookups take. Made on first use, not when the program is loaded, so that
/// it is there whatever asks first.
const std::string& iterator_name() {
    static const std::string name = kIteratorKey;
    return name;
}

/// An array's elements, in order. Read by index each time round, because
/// `visit` runs script and the array may grow or shrink under it; the array
/// iterator reads it the same way.
void visit_elements(const JsValue& array, const std::function<bool(const JsValue&)>& visit) {
    for (size_t i = 0; i < array.object_val->array_elements.size(); ++i) {
        // The array iterator reads every place, so a hole is yielded too:
        // `[...a]` and `for (x of a)` see a value there. It is what `a[i]`
        // reads, which is `undefined` unless the index has an accessor of
        // its own or the prototype has the index.
        JsValue item = js_array_get(array, i);
        if (!visit(item)) return;
    }
}

/// A string's code points, in order, which is what the string iterator
/// yields. The text is well-formed UTF-8, so a code point is one sequence of
/// bytes. The handle is held here because `visit` runs script, which may drop
/// the last other reference to the text.
void visit_code_points(const JsValue& string, const std::function<bool(const JsValue&)>& visit) {
    const JsString text = string.string_val;
    const std::string_view s = text.view();
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (i + len > s.size()) len = s.size() - i;
        JsValue item = len == 1 ? JsValue::make_string(JsString::from_unit(c)) : JsValue::make_string(s.substr(i, len));
        if (!visit(item)) return;
        i += len;
    }
}

/// IteratorClose (ECMA-262 7.4.11) for a loop that stops early and has no
/// error of its own: what `return` is and what it gives back are checked, and
/// what it throws is thrown.
void close_iterator(const JsValue& iterator) {
    JsValue method = iterator.get_property("return");
    if (method.is_nullish()) return;
    if (!method.is_function()) js_throw_type_error(js_describe_thrown(method) + " is not a function");
    JsValue result = method.call_with_this(iterator, {});
    if (!result.is_object_like()) {
        js_throw_type_error("Iterator result " + js_describe_thrown(result) + " is not an object");
    }
}

/// The same for a loop that an error is ending. That error is the one the
/// script sees: whatever `return` is, does or throws is dropped.
void close_iterator_quietly(const JsValue& iterator) {
    try {
        JsValue method = iterator.get_property("return");
        if (method.is_function()) method.call_with_this(iterator, {});
    } catch (...) {
    }
}

/// GetIterator and the loop over IteratorStep (ECMA-262 7.4.3, 7.4.8), with
/// the method already looked up. `next` is read once. An iterator is closed
/// when the loop leaves it unfinished, and not when it failed itself: an
/// error out of next(), `done` or `value` leaves `return` alone.
void iterate_with(const JsValue& iterable, const JsValue& method, const std::function<bool(const JsValue&)>& visit) {
    JsValue iterator = method.call_with_this(iterable, {});
    if (!iterator.is_object_like()) js_throw_type_error("Result of the Symbol.iterator method is not an object");
    JsValue next = iterator.get_property("next");

    while (true) {
        JsValue step = js_call(next, iterator, {});
        if (!step.is_object_like()) {
            js_throw_type_error("Iterator result " + js_describe_thrown(step) + " is not an object");
        }
        if (step.get_property("done").is_truthy()) return;
        JsValue value = step.get_property("value");
        bool keep_going;
        try {
            keep_going = visit(value);
        } catch (...) {
            close_iterator_quietly(iterator);
            throw;
        }
        if (!keep_going) {
            close_iterator(iterator);
            return;
        }
    }
}

/// Whether `proto` still holds, as a plain data property, the method the
/// realm installed under @@iterator.
bool holds_installed_method(const JsObject* proto, const JsValue& installed) {
    if (proto->exotic || proto->has_legacy_getters || !installed.is_function()) return false;
    auto it = proto->properties.find(iterator_name());
    return it != proto->properties.end() && !(it->flags & PropertyMap::Accessor) &&
           it->second.is_function() && it->second.identity() == installed.identity();
}

/// Whether `proto` still holds the `next` install() put there. An iterator's
/// `next` is a property of its prototype now, so a script can replace it; a
/// replaced `next` is a function that has to be called once for each step, and
/// never is by a loop that reads the elements directly.
bool holds_installed_next(const JsObject* proto, const JsValue& installed) {
    if (proto->exotic || proto->has_legacy_getters || !installed.is_function()) return false;
    auto it = proto->properties.find("next");
    return it != proto->properties.end() && !(it->flags & PropertyMap::Accessor) &&
           it->second.is_function() && it->second.identity() == installed.identity();
}

/// The second question, for a value the first one could not answer without
/// running script: `method` is what the lookup found for it.
bool method_is_installed(const JsValue& value, const JsValue& method) {
    if (!method.is_function()) return false;
    const JsIntrinsics& realm = JsIntrinsics::get();
    if (value.is_array()) {
        return method.identity() == realm.array_iterator_method.identity() &&
               holds_installed_next(realm.array_iterator_proto, realm.array_iterator_next);
    }
    if (value.type == JsType::String) {
        return method.identity() == realm.string_iterator_method.identity() &&
               holds_installed_next(realm.string_iterator_proto, realm.string_iterator_next);
    }
    return false;
}

[[noreturn]] void throw_not_iterable(const JsValue& value) {
    js_throw_type_error(js_describe_thrown(value) + " is not iterable");
}

} // namespace

bool js_iterator_method(const JsValue& value, JsValue& method) {
    method = value.is_nullish() ? JsValue::make_undefined() : value.get_property(iterator_name());
    if (method.is_nullish()) return false;
    if (!method.is_function()) throw_not_iterable(value);
    return true;
}

bool js_iterates_as_builtin(const JsValue& value) {
    // An iterator made by either installed method reads the value the way
    // visit_elements() and visit_code_points() do. Its `next` is a property of
    // the prototype its kind shares, where a script can replace it, so the
    // direct read is only taken while that property is still the function
    // install() wrote (holds_installed_next): a replaced `next` may do anything,
    // including yield something other than the elements, and the loop has to
    // call it.
    const JsIntrinsics& realm = JsIntrinsics::get();
    if (value.type == JsType::String) {
        // A primitive has no properties of its own: the lookup starts at the prototype.
        return holds_installed_method(realm.string_proto, realm.string_iterator_method) &&
               holds_installed_next(realm.string_iterator_proto, realm.string_iterator_next);
    }
    if (!value.is_array()) return false;
    // The lookup an array's `Symbol.iterator` would take, step by step, given
    // up at the first thing that is not the plain case: a host object, a
    // property of the array's own by that name (found without running it), or
    // a prototype that is not this realm's Array.prototype (a subclass, an
    // array of another realm, one whose prototype was set).
    const JsObject* cell = value.object_val.get();
    if (cell->exotic || cell->has_legacy_getters || cell->prototype != realm.array_proto) return false;
    if (!cell->properties.empty() && cell->properties.find(iterator_name()) != cell->properties.end()) return false;
    return holds_installed_method(realm.array_proto, realm.array_iterator_method) &&
           holds_installed_next(realm.array_iterator_proto, realm.array_iterator_next);
}

void js_iterate(const JsValue& iterable, const std::function<bool(const JsValue&)>& visit) {
    const bool is_string = iterable.type == JsType::String;
    if (js_iterates_as_builtin(iterable)) {
        if (is_string) visit_code_points(iterable, visit);
        else visit_elements(iterable, visit);
        return;
    }
    JsValue method;
    if (!js_iterator_method(iterable, method)) throw_not_iterable(iterable);
    if (method_is_installed(iterable, method)) {
        // The long way round to the same answer: a subclass, a getter that
        // hands out the installed method, the method assigned back.
        if (is_string) visit_code_points(iterable, visit);
        else visit_elements(iterable, visit);
        return;
    }
    iterate_with(iterable, method, visit);
}

std::vector<JsValue> js_iterable_to_vector(const JsValue& iterable) {
    std::vector<JsValue> out;
    auto collect = [&](const JsValue& v) {
        out.push_back(v);
        return true;
    };
    const bool is_string = iterable.type == JsType::String;
    bool direct = js_iterates_as_builtin(iterable);
    JsValue method;
    if (!direct) {
        if (!js_iterator_method(iterable, method)) throw_not_iterable(iterable);
        direct = method_is_installed(iterable, method);
    }
    if (!direct) {
        iterate_with(iterable, method, collect);
    } else if (is_string) {
        visit_code_points(iterable, collect);
    } else {
        // Nothing runs between the first element and the last: one copy.
        // The iterator reads every place, so a hole is handed on as
        // `undefined`: spread and `Array.from` turn holes into elements.
        out = iterable.array_snapshot();
    }
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

namespace {
thread_local JsIntrinsics* t_current_realm = nullptr;

JsIntrinsics& default_realm() {
    // Leaked on purpose: values created in it may be destroyed by static
    // destructors that run in an unknown order.
    static JsIntrinsics* realm = new JsIntrinsics();
    return *realm;
}
}

JsIntrinsics::JsIntrinsics() {
    // Only allocation happens here. Built-in methods are added by install(),
    // because installing them creates objects, and creating an object needs
    // these prototypes to exist already.
    auto& heap = Heap::default_heap();
    auto make = [&](JsObject* parent) {
        JsObject* obj = heap.allocate_object();
        obj->prototype = parent;
        return obj;
    };
    object_proto = make(nullptr);
    function_proto = make(object_proto);
    // Function.prototype is a function itself: it takes anything and returns
    // undefined. So the prototype of every function is a function, `typeof
    // Object.getPrototypeOf(f)` says so, and the cell knows its function the
    // way the cell of a parent class does (JsValue::as_prototype).
    // It is not a constructor: `new Function.prototype()` throws, and so does
    // `super()` in a class that extends null, whose parent is this function.
    {
        JsValue function = js_make_native("", 0, [](const std::vector<JsValue>&) { return JsValue::make_undefined(); });
        function.function_cell(function_proto);
        function_proto->set_function_value(function);
        function_proto->not_constructor = true;
    }
    array_proto = make(object_proto);
    array_proto->is_array = true;
    array_proto->klass = ObjectClass::Array;
    string_proto = make(object_proto);
    number_proto = make(object_proto);
    boolean_proto = make(object_proto);
    symbol_proto = make(object_proto);
    iterator_proto = make(object_proto);
    array_iterator_proto = make(iterator_proto);
    map_iterator_proto = make(iterator_proto);
    set_iterator_proto = make(iterator_proto);
    string_iterator_proto = make(iterator_proto);
    regexp_string_iterator_proto = make(iterator_proto);
    dom_iterator_proto = make(iterator_proto);
    regexp_proto = make(object_proto);
    date_proto = make(object_proto);
    map_proto = make(object_proto);
    set_proto = make(object_proto);
    weakmap_proto = make(object_proto);
    weakset_proto = make(object_proto);
    promise_proto = make(object_proto);
    error_protos[static_cast<int>(JsErrorKind::Error)] = make(object_proto);
    for (int i = 1; i < 7; ++i) error_protos[i] = make(error_protos[0]);

    for (JsObject* cell : {object_proto, function_proto, array_proto, string_proto, number_proto,
                           boolean_proto, symbol_proto, iterator_proto, array_iterator_proto,
                           map_iterator_proto, set_iterator_proto, string_iterator_proto,
                           regexp_string_iterator_proto, dom_iterator_proto, regexp_proto, date_proto,
                           map_proto, set_proto, weakmap_proto, weakset_proto, promise_proto}) {
        m_roots.push_back(JsValue::make_object(cell));
    }
    for (JsObject* cell : error_protos) m_roots.push_back(JsValue::make_object(cell));

    // The collector frees anything it cannot reach, and nothing else points at
    // these: they are roots for as long as the realm exists.
    heap.add_root_provider(this, [this](std::vector<JsValue*>& roots) {
        for (auto& value : m_roots) roots.push_back(&value);
        for (auto& entry : globals) roots.push_back(&entry.second);
        roots.push_back(&array_iterator_method);
        roots.push_back(&string_iterator_method);
    });
}

JsIntrinsics::~JsIntrinsics() {
    Heap::default_heap().remove_root_provider(this);
    if (t_current_realm == this) t_current_realm = nullptr;
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

void JsIntrinsics::install() {
    std::lock_guard<std::recursive_mutex> lock(m_install_mutex);
    if (m_ready.load() || m_installing) return;
    m_installing = true;

    // Objects the installers create (Math, JSON, every function) must get this
    // realm's prototypes, so it is the current realm while they run.
    JsIntrinsics* previous = t_current_realm;
    t_current_realm = this;

    install_object_builtins(*this);
    install_error_builtins(*this);
    install_array_builtins(*this);
    // The two kinds with no built-in interface of their own to install them:
    // RegExp String Iterator, and the one Web IDL iterators share.
    js_install_iterator_prototype(regexp_string_iterator_proto, "RegExp String Iterator");
    js_install_iterator_prototype(dom_iterator_proto, nullptr);
    install_string_builtins(*this);
    // What js_iterate() compares with: see js_iterates_as_builtin().
    array_iterator_method = js_object_value(array_proto).get_property(kIteratorKey);
    string_iterator_method = js_object_value(string_proto).get_property(kIteratorKey);
    array_iterator_next = js_object_value(array_iterator_proto).get_property("next");
    string_iterator_next = js_object_value(string_iterator_proto).get_property("next");
    install_number_builtins(*this);
    install_collection_builtins(*this);
    install_regexp_builtins(*this);
    install_date_builtins(*this);
    install_json_builtins(*this);
    install_promise_builtins(*this);

    t_current_realm = previous;
    m_ready.store(true);
}

void JsIntrinsics::set_current(JsIntrinsics* realm) {
    t_current_realm = realm;
}

JsIntrinsics& JsIntrinsics::get() {
    if (t_current_realm) return *t_current_realm;
    JsIntrinsics& realm = default_realm();
    if (!realm.m_ready.load(std::memory_order_acquire)) realm.install();
    return realm;
}

} // namespace litebird
