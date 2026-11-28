// Date (ECMA-262 21.4).
//
// A date is one number: milliseconds since 1970-01-01T00:00:00Z, or NaN for an
// invalid date. Calendar arithmetic is done here with integer day counts; the
// C library is only asked for the local time-zone offset.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kMsPerDay = 86400000.0;
const double kMaxTime = 8.64e15;

JsValue self() { return JsValue::current_this(); }

// ---- Calendar arithmetic ---------------------------------------------------

/// Days since the epoch for a proleptic Gregorian date (month is 1-12).
double days_from_civil(double year, int month, int day) {
    year -= month <= 2;
    const double era = std::floor(year / 400.0);
    const double yoe = year - era * 400.0;
    const double doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const double doe = yoe * 365 + std::floor(yoe / 4) - std::floor(yoe / 100) + doy;
    return era * 146097 + doe - 719468;
}

struct Civil {
    double year;
    int month;      // 0-11
    int day;        // 1-31
    int weekday;    // 0 = Sunday
    int hour, minute, second, ms;
};

Civil civil_from_time(double t) {
    Civil c{};
    double days = std::floor(t / kMsPerDay);
    double in_day = t - days * kMsPerDay;
    c.hour = static_cast<int>(in_day / 3600000.0);
    c.minute = static_cast<int>(std::fmod(in_day / 60000.0, 60.0));
    c.second = static_cast<int>(std::fmod(in_day / 1000.0, 60.0));
    c.ms = static_cast<int>(std::fmod(in_day, 1000.0));
    c.weekday = static_cast<int>(std::fmod(std::fmod(days + 4, 7.0) + 7.0, 7.0));

    double z = days + 719468;
    const double era = std::floor(z / 146097.0);
    const double doe = z - era * 146097;
    const double yoe = std::floor((doe - std::floor(doe / 1460) + std::floor(doe / 36524) - std::floor(doe / 146096)) / 365);
    const double doy = doe - (365 * yoe + std::floor(yoe / 4) - std::floor(yoe / 100));
    const double mp = std::floor((5 * doy + 2) / 153);
    c.day = static_cast<int>(doy - std::floor((153 * mp + 2) / 5) + 1);
    int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    c.year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    c.month = month - 1;
    return c;
}

/// MakeDay + MakeTime + MakeDate: components may overflow their ranges and
/// are normalised (month 12 is January of the next year, and so on).
double make_time(double year, double month, double day, double hour, double minute, double second, double ms) {
    for (double v : {year, month, day, hour, minute, second, ms}) {
        if (!std::isfinite(v)) return kNaN;
    }
    year = std::trunc(year); month = std::trunc(month); day = std::trunc(day);
    hour = std::trunc(hour); minute = std::trunc(minute); second = std::trunc(second); ms = std::trunc(ms);
    double y = year + std::floor(month / 12.0);
    int m = static_cast<int>(month - std::floor(month / 12.0) * 12.0);
    if (std::fabs(y) > 400000) return kNaN;
    double days = days_from_civil(y, m + 1, 1) + day - 1;
    return days * kMsPerDay + hour * 3600000.0 + minute * 60000.0 + second * 1000.0 + ms;
}

double time_clip(double t) {
    if (!std::isfinite(t) || std::fabs(t) > kMaxTime) return kNaN;
    return std::trunc(t) + 0.0;     // + 0.0 turns -0 into +0
}

// ---- Local time ------------------------------------------------------------

/// Offset of local time from UTC, in milliseconds, at the UTC instant `t`.
double local_offset_ms(double t) {
    if (!std::isfinite(t)) return 0.0;
    std::time_t seconds = static_cast<std::time_t>(std::floor(t / 1000.0));
    std::tm local{};
    if (!localtime_r(&seconds, &local)) return 0.0;
    return static_cast<double>(local.tm_gmtoff) * 1000.0;
}

std::string local_zone_name(double t) {
    std::time_t seconds = static_cast<std::time_t>(std::floor(t / 1000.0));
    std::tm local{};
    if (!localtime_r(&seconds, &local) || !local.tm_zone) return "Coordinated Universal Time";
    // The C library only has the abbreviation. Browsers print the long name;
    // without a zone-name table that can be matched for these two only.
    const std::string zone = local.tm_zone;
    if (zone == "UTC") return "Coordinated Universal Time";
    if (zone == "GMT") return "Greenwich Mean Time";
    return zone;
}

double to_local(double t) { return t + local_offset_ms(t); }

/// Converts a local wall-clock time to UTC. The offset depends on the instant,
/// so guess with the offset at the wall time and correct once.
double from_local(double local) {
    if (!std::isfinite(local)) return kNaN;
    double guess = local - local_offset_ms(local);
    return local - local_offset_ms(guess);
}

double now_ms() {
    return js_current_time_ms();
}

} // namespace

double js_current_time_ms() {
    using namespace std::chrono;
    // Wall-clock time, moved forward by however far the event loop has skipped
    // ahead: a script that sets a 100 ms timer and then reads the clock must
    // see 100 ms gone, whether or not the host really waited.
    const auto wall = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return static_cast<double>((wall + EventLoop::default_loop().clock_skew()).count());
}

namespace {

// ---- Parsing ---------------------------------------------------------------

const char* kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* kDayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

bool read_digits(const std::string& s, size_t& i, int count, int& out) {
    if (i + static_cast<size_t>(count) > s.size()) return false;
    int value = 0;
    for (int k = 0; k < count; ++k) {
        char c = s[i + static_cast<size_t>(k)];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    i += static_cast<size_t>(count);
    out = value;
    return true;
}

/// ISO 8601 as profiled by ECMA-262: YYYY[-MM[-DD]][THH:mm[:ss[.sss]]][Z|±HH:mm].
bool parse_iso(const std::string& s, double& out) {
    size_t i = 0;
    int sign = 1;
    int year = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        sign = s[i] == '-' ? -1 : 1;
        ++i;
        if (!read_digits(s, i, 6, year)) return false;
    } else if (!read_digits(s, i, 4, year)) {
        return false;
    }
    year *= sign;
    int month = 1, day = 1, hour = 0, minute = 0, second = 0, ms = 0;
    bool has_time = false;
    bool has_offset = false;
    double offset_minutes = 0;

    if (i < s.size() && s[i] == '-') {
        ++i;
        if (!read_digits(s, i, 2, month)) return false;
        if (i < s.size() && s[i] == '-') {
            ++i;
            if (!read_digits(s, i, 2, day)) return false;
        }
    }
    if (i < s.size() && (s[i] == 'T' || s[i] == 't' || s[i] == ' ')) {
        ++i;
        has_time = true;
        if (!read_digits(s, i, 2, hour)) return false;
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        if (!read_digits(s, i, 2, minute)) return false;
        if (i < s.size() && s[i] == ':') {
            ++i;
            if (!read_digits(s, i, 2, second)) return false;
            if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
                ++i;
                // Any number of fraction digits; only milliseconds are kept.
                int digits = 0;
                int value = 0;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                    if (digits < 3) value = value * 10 + (s[i] - '0');
                    ++digits;
                    ++i;
                }
                if (digits == 0) return false;
                for (; digits < 3; ++digits) value *= 10;
                ms = value;
            }
        }
        if (i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
            ++i;
            has_offset = true;
        } else if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            int offset_sign = s[i] == '-' ? -1 : 1;
            ++i;
            int oh = 0, om = 0;
            if (!read_digits(s, i, 2, oh)) return false;
            if (i < s.size() && s[i] == ':') ++i;
            if (!read_digits(s, i, 2, om)) return false;
            offset_minutes = offset_sign * (oh * 60 + om);
            has_offset = true;
        }
    }
    if (i != s.size()) return false;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 24 || minute > 59 || second > 59) return false;
    if (hour == 24 && (minute || second || ms)) return false;

    double t = make_time(year, month - 1, day, hour, minute, second, ms);
    // A date-only form is UTC; a date-time without an offset is local time.
    if (has_offset) t -= offset_minutes * 60000.0;
    else if (has_time) t = from_local(t);
    out = t;
    return true;
}

/// The legacy formats engines accept in practice: "Mar 5, 2024 10:00:00",
/// "5 March 2024", "Tue Mar 05 2024 10:00:00 GMT+0100", "2024/03/05".
bool parse_legacy(const std::string& s, double& out) {
    int year = -1, month = -1, day = -1, hour = 0, minute = 0, second = 0;
    bool has_offset = false;
    double offset_minutes = 0;
    bool pm = false, am = false;
    std::vector<int> numbers;

    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalpha(c)) {
            size_t start = i;
            while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
            std::string word = s.substr(start, i - start);
            for (char& ch : word) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (word == "am") { am = true; continue; }
            if (word == "pm") { pm = true; continue; }
            if (word == "gmt" || word == "utc" || word == "z") { has_offset = true; continue; }
            bool matched = false;
            if (word.size() >= 3) {
                for (int m = 0; m < 12; ++m) {
                    std::string name = kMonthNames[m];
                    for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (word.compare(0, 3, name) == 0) { month = m; matched = true; break; }
                }
            }
            (void)matched;      // weekday names and unknown words are ignored
            continue;
        }
        if (std::isdigit(c)) {
            size_t start = i;
            int value = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                if (value < 100000000) value = value * 10 + (s[i] - '0');
                ++i;
            }
            if (i < s.size() && s[i] == ':') {
                // hh:mm[:ss]
                hour = value;
                ++i;
                int mm = 0;
                size_t digits_start = i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) mm = mm * 10 + (s[i++] - '0');
                if (i == digits_start) return false;
                minute = mm;
                if (i < s.size() && s[i] == ':') {
                    ++i;
                    int ss = 0;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ss = ss * 10 + (s[i++] - '0');
                    second = ss;
                    if (i < s.size() && s[i] == '.') {
                        ++i;
                        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                    }
                }
                continue;
            }
            (void)start;
            numbers.push_back(value);
            continue;
        }
        if ((c == '+' || c == '-') && has_offset && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
            // GMT+0100 / UTC-05:00
            int sign = c == '-' ? -1 : 1;
            ++i;
            int digits = 0;
            int value = 0;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == ':')) {
                if (s[i] != ':') { value = value * 10 + (s[i] - '0'); ++digits; }
                ++i;
            }
            int oh = digits <= 2 ? value : value / 100;
            int om = digits <= 2 ? 0 : value % 100;
            offset_minutes = sign * (oh * 60 + om);
            continue;
        }
        if (c == '(') {
            while (i < s.size() && s[i] != ')') ++i;     // "(Central European Time)"
        }
        ++i;
    }

    if (month >= 0) {
        // "Mar 5 2024" or "5 Mar 2024": the larger number is the year.
        if (numbers.size() < 2) return false;
        if (numbers[0] > 31) { year = numbers[0]; day = numbers[1]; }
        else { day = numbers[0]; year = numbers[1]; }
    } else {
        if (numbers.size() < 3) return false;
        if (numbers[0] > 31) { year = numbers[0]; month = numbers[1] - 1; day = numbers[2]; }   // 2024/03/05
        else { month = numbers[0] - 1; day = numbers[1]; year = numbers[2]; }                   // 3/5/2024
    }
    if (year >= 0 && year < 50) year += 2000;
    else if (year >= 50 && year < 100) year += 1900;
    if (month < 0 || month > 11 || day < 1 || day > 31 || hour > 24 || minute > 59 || second > 59) return false;
    if (pm && hour < 12) hour += 12;
    if (am && hour == 12) hour = 0;

    double t = make_time(year, month, day, hour, minute, second, 0);
    out = has_offset ? t - offset_minutes * 60000.0 : from_local(t);
    return true;
}

double parse_date(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return kNaN;
    size_t end = text.find_last_not_of(" \t\r\n");
    std::string s = text.substr(begin, end - begin + 1);
    double t = kNaN;
    if (parse_iso(s, t)) return time_clip(t);
    if (parse_legacy(s, t)) return time_clip(t);
    return kNaN;
}

// ---- Formatting ------------------------------------------------------------

std::string format_year(double year) {
    char buffer[32];
    if (year >= 0 && year <= 9999) std::snprintf(buffer, sizeof(buffer), "%04d", static_cast<int>(year));
    else std::snprintf(buffer, sizeof(buffer), "%c%06d", year < 0 ? '-' : '+', static_cast<int>(std::fabs(year)));
    return buffer;
}

std::string to_iso_string(double t) {
    Civil c = civil_from_time(t);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "-%02d-%02dT%02d:%02d:%02d.%03dZ", c.month + 1, c.day, c.hour, c.minute, c.second, c.ms);
    return format_year(c.year) + buffer;
}

std::string date_part(const Civil& c) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s %s %02d ", kDayNames[c.weekday], kMonthNames[c.month], c.day);
    return buffer + (c.year >= 0 && c.year <= 9999 ? format_year(c.year) : js_number_to_string(c.year));
}

std::string time_part(double t, const Civil& c) {
    double offset = local_offset_ms(t) / 60000.0;
    int abs_offset = static_cast<int>(std::fabs(offset));
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d GMT%c%02d%02d (%s)", c.hour, c.minute, c.second,
                  offset < 0 ? '-' : '+', abs_offset / 60, abs_offset % 60, local_zone_name(t).c_str());
    return buffer;
}

std::string to_utc_string(double t) {
    Civil c = civil_from_time(t);
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s, %02d %s %s %02d:%02d:%02d GMT", kDayNames[c.weekday], c.day,
                  kMonthNames[c.month], format_year(c.year).c_str(), c.hour, c.minute, c.second);
    return buffer;
}

// ---- Date objects ----------------------------------------------------------

std::shared_ptr<double> time_slot(const JsValue& value, const char* method) {
    if (!value.object_val || value.object_val->klass != ObjectClass::Date || !value.object_val->internal) {
        js_throw_type_error(std::string("Date.prototype.") + method + " called on a value that is not a Date");
    }
    return std::static_pointer_cast<double>(value.object_val->internal);
}

JsValue make_date(double t) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().date_proto;
    cell->klass = ObjectClass::Date;
    cell->internal = std::make_shared<double>(t);
    return JsValue::make_object(cell);
}

double arg_or(Args args, size_t index, double fallback) {
    return index < args.size() ? js_to_number(args[index]) : fallback;
}

double time_from_components(Args args, bool local) {
    // ToNumber can run script or throw. Sequence each conversion explicitly:
    // C++ function arguments do not guarantee JavaScript's left-to-right order.
    double year = js_to_number(args[0]);
    const double month = arg_or(args, 1, 0);
    const double day = arg_or(args, 2, 1);
    const double hour = arg_or(args, 3, 0);
    const double minute = arg_or(args, 4, 0);
    const double second = arg_or(args, 5, 0);
    const double ms = arg_or(args, 6, 0);
    // Two-digit years mean 19xx, a compatibility rule from the first browsers.
    if (std::isfinite(year)) {
        double truncated = std::trunc(year);
        if (truncated >= 0 && truncated <= 99) year = 1900 + truncated;
    }
    double t = make_time(year, month, day, hour, minute, second, ms);
    return time_clip(local ? from_local(t) : t);
}

enum class Field { Year, Month, Day, Weekday, Hour, Minute, Second, Ms };

JsValue::NativeFn getter(const char* name, Field field, bool local) {
    return [name, field, local](Args) {
        double t = *time_slot(self(), name);
        if (std::isnan(t)) return JsValue::make_number(kNaN);
        Civil c = civil_from_time(local ? to_local(t) : t);
        switch (field) {
            case Field::Year: return JsValue::make_number(c.year);
            case Field::Month: return JsValue::make_number(c.month);
            case Field::Day: return JsValue::make_number(c.day);
            case Field::Weekday: return JsValue::make_number(c.weekday);
            case Field::Hour: return JsValue::make_number(c.hour);
            case Field::Minute: return JsValue::make_number(c.minute);
            case Field::Second: return JsValue::make_number(c.second);
            case Field::Ms: return JsValue::make_number(c.ms);
        }
        return JsValue::make_number(kNaN);
    };
}

/// Setters replace `count` consecutive fields starting at `first` with the
/// arguments supplied and leave the rest as they are.
JsValue::NativeFn setter(const char* name, Field first, int max_args, bool local) {
    return [name, first, max_args, local](Args args) {
        auto slot = time_slot(self(), name);
        double t = *slot; // Snapshot before ToNumber: conversion hooks can mutate the Date.
        if (args.empty()) {
            *slot = kNaN;
            return JsValue::make_number(kNaN);
        }

        // The largest setter is setHours (four arguments). Even an invalid
        // Date must convert every supplied argument before the NaN return.
        double converted[4];
        converted[0] = js_to_number(args[0]);
        if (first == Field::Year) {
            // FullYear recovers from NaN with calendar +0, NOT LocalTime(+0).
            // For local FullYear, LocalTime precedes optional month/day conversion.
            t = std::isnan(t) ? 0 : (local ? to_local(t) : t);
        }
        size_t count = 1;
        for (; count < static_cast<size_t>(max_args) && count < args.size(); ++count) {
            converted[count] = js_to_number(args[count]);
        }
        if (first != Field::Year) {
            // Do not overwrite a mutation made by a hook if the snapshot was NaN.
            if (std::isnan(t)) return JsValue::make_number(kNaN);
            if (local) t = to_local(t);
        }
        Civil c = civil_from_time(t);
        double fields[7] = {c.year, static_cast<double>(c.month), static_cast<double>(c.day),
                            static_cast<double>(c.hour), static_cast<double>(c.minute),
                            static_cast<double>(c.second), static_cast<double>(c.ms)};
        int start = 0;
        switch (first) {
            case Field::Year: start = 0; break;
            case Field::Month: start = 1; break;
            case Field::Day: start = 2; break;
            case Field::Hour: start = 3; break;
            case Field::Minute: start = 4; break;
            case Field::Second: start = 5; break;
            case Field::Ms: start = 6; break;
            default: break;
        }
        for (size_t i = 0; i < count; ++i) {
            fields[start + i] = converted[i];
        }
        double result = make_time(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6]);
        *slot = time_clip(local ? from_local(result) : result);
        return JsValue::make_number(*slot);
    };
}

std::string to_date_string(double t) {
    if (std::isnan(t)) return "Invalid Date";
    Civil c = civil_from_time(to_local(t));
    return date_part(c) + " " + time_part(t, c);
}

} // namespace

double js_parse_date_string(const std::string& text) {
    return parse_date(text);
}

void install_date_builtins(JsIntrinsics& realm) {
    JsObject* dp = realm.date_proto;

    JsValue date_ctor = js_make_native("Date", 7, [](Args args) {
        // Called as a function, Date() ignores its arguments and returns a string.
        if (!JsValue::current_is_construct()) return JsValue::make_string(to_date_string(now_ms()));
        if (args.empty()) return make_date(time_clip(now_ms()));
        if (args.size() == 1) {
            const JsValue& value = args[0];
            if (value.object_val && value.object_val->klass == ObjectClass::Date && value.object_val->internal) {
                return make_date(*std::static_pointer_cast<double>(value.object_val->internal));
            }
            JsValue primitive = js_to_primitive(value);
            if (primitive.type == JsType::String) return make_date(parse_date(primitive.string_val.str()));
            return make_date(time_clip(js_to_number(primitive)));
        }
        return make_date(time_from_components(args, true));
    });
    date_ctor.define_property("prototype", js_object_value(dp), 0);
    js_define_hidden(dp, "constructor", date_ctor);

    js_define_method(date_ctor, "now", 0, [](Args) { return JsValue::make_number(now_ms()); });
    js_define_method(date_ctor, "parse", 1, [](Args args) {
        return JsValue::make_number(parse_date(js_to_string(js_arg(args, 0))));
    });
    js_define_method(date_ctor, "UTC", 7, [](Args args) {
        if (args.empty()) return JsValue::make_number(kNaN);
        return JsValue::make_number(time_from_components(args, false));
    });
    realm.add_global("Date", date_ctor);

    // ---- Value access -----------------------------------------------------
    js_define_method(dp, "getTime", 0, [](Args) { return JsValue::make_number(*time_slot(self(), "getTime")); });
    js_define_method(dp, "valueOf", 0, [](Args) { return JsValue::make_number(*time_slot(self(), "valueOf")); });
    js_define_method(dp, "setTime", 1, [](Args args) {
        auto slot = time_slot(self(), "setTime");
        *slot = time_clip(js_to_number(js_arg(args, 0)));
        return JsValue::make_number(*slot);
    });
    js_define_method(dp, "getTimezoneOffset", 0, [](Args) {
        double t = *time_slot(self(), "getTimezoneOffset");
        if (std::isnan(t)) return JsValue::make_number(kNaN);
        return JsValue::make_number(-local_offset_ms(t) / 60000.0);
    });
    js_define_method(dp, "getYear", 0, [](Args) {
        double t = *time_slot(self(), "getYear");
        if (std::isnan(t)) return JsValue::make_number(kNaN);
        return JsValue::make_number(civil_from_time(to_local(t)).year - 1900);
    });

    struct Accessor { const char* local; const char* utc; Field field; };
    static const Accessor getters[] = {
        {"getFullYear", "getUTCFullYear", Field::Year},   {"getMonth", "getUTCMonth", Field::Month},
        {"getDate", "getUTCDate", Field::Day},            {"getDay", "getUTCDay", Field::Weekday},
        {"getHours", "getUTCHours", Field::Hour},         {"getMinutes", "getUTCMinutes", Field::Minute},
        {"getSeconds", "getUTCSeconds", Field::Second},   {"getMilliseconds", "getUTCMilliseconds", Field::Ms},
    };
    for (const auto& g : getters) {
        js_define_method(dp, g.local, 0, getter(g.local, g.field, true));
        js_define_method(dp, g.utc, 0, getter(g.utc, g.field, false));
    }

    struct Mutator { const char* local; const char* utc; Field field; int max_args; };
    static const Mutator setters[] = {
        {"setFullYear", "setUTCFullYear", Field::Year, 3},   {"setMonth", "setUTCMonth", Field::Month, 2},
        {"setDate", "setUTCDate", Field::Day, 1},            {"setHours", "setUTCHours", Field::Hour, 4},
        {"setMinutes", "setUTCMinutes", Field::Minute, 3},   {"setSeconds", "setUTCSeconds", Field::Second, 2},
        {"setMilliseconds", "setUTCMilliseconds", Field::Ms, 1},
    };
    for (const auto& s : setters) {
        js_define_method(dp, s.local, s.max_args, setter(s.local, s.field, s.max_args, true));
        js_define_method(dp, s.utc, s.max_args, setter(s.utc, s.field, s.max_args, false));
    }

    // ---- Formatting -------------------------------------------------------
    js_define_method(dp, "toISOString", 0, [](Args) {
        double t = *time_slot(self(), "toISOString");
        if (std::isnan(t)) js_throw_range_error("Invalid time value");
        return JsValue::make_string(to_iso_string(t));
    });
    js_define_method(dp, "toJSON", 1, [](Args) {
        // §21.4.4.37 is deliberately generic: conversion precedes looking up
        // toISOString, and only a non-finite *Number* suppresses that call.
        JsValue object = js_to_object(self());
        JsValue primitive = js_to_primitive(object, JsPrimitiveHint::Number);
        if (primitive.type == JsType::Number && !std::isfinite(primitive.number_val)) {
            return JsValue::make_null();
        }
        return js_call(object.get_property("toISOString"), object, {});
    });
    js_define_method(dp, "toString", 0, [](Args) {
        return JsValue::make_string(to_date_string(*time_slot(self(), "toString")));
    });
    js_define_method(dp, "toDateString", 0, [](Args) {
        double t = *time_slot(self(), "toDateString");
        if (std::isnan(t)) return JsValue::make_string("Invalid Date");
        return JsValue::make_string(date_part(civil_from_time(to_local(t))));
    });
    js_define_method(dp, "toTimeString", 0, [](Args) {
        double t = *time_slot(self(), "toTimeString");
        if (std::isnan(t)) return JsValue::make_string("Invalid Date");
        return JsValue::make_string(time_part(t, civil_from_time(to_local(t))));
    });
    auto utc_string = [](Args) {
        double t = *time_slot(self(), "toUTCString");
        if (std::isnan(t)) return JsValue::make_string("Invalid Date");
        return JsValue::make_string(to_utc_string(t));
    };
    js_define_method(dp, "toUTCString", 0, utc_string);
    js_define_method(dp, "toGMTString", 0, utc_string);

    // No locale data: an en-US style rendering of local time.
    auto locale_date = [](double t) {
        Civil c = civil_from_time(to_local(t));
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%d/%d/%s", c.month + 1, c.day, js_number_to_string(c.year).c_str());
        return std::string(buffer);
    };
    auto locale_time = [](double t) {
        Civil c = civil_from_time(to_local(t));
        char buffer[48];
        int hour12 = c.hour % 12 == 0 ? 12 : c.hour % 12;
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d %s", hour12, c.minute, c.second, c.hour < 12 ? "AM" : "PM");
        return std::string(buffer);
    };
    js_define_method(dp, "toLocaleDateString", 0, [locale_date](Args) {
        double t = *time_slot(self(), "toLocaleDateString");
        return JsValue::make_string(std::isnan(t) ? "Invalid Date" : locale_date(t));
    });
    js_define_method(dp, "toLocaleTimeString", 0, [locale_time](Args) {
        double t = *time_slot(self(), "toLocaleTimeString");
        return JsValue::make_string(std::isnan(t) ? "Invalid Date" : locale_time(t));
    });
    js_define_method(dp, "toLocaleString", 0, [locale_date, locale_time](Args) {
        double t = *time_slot(self(), "toLocaleString");
        return JsValue::make_string(std::isnan(t) ? "Invalid Date" : locale_date(t) + ", " + locale_time(t));
    });
}

} // namespace litebird
