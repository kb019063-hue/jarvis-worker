#include "litebird/dom/FormState.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/TreeWalk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace litebird {

FormControlState& Element::ensure_form_state() {
    if (!m_form_state) m_form_state = std::make_shared<FormControlState>();
    return *m_form_state;
}

bool Element::is_defined() const {
    if (is_foreign()) return true;
    if (m_custom_state == CustomElementState::Custom) return true;
    // A built-in element that no definition was to customize is defined by
    // being one. An element that answers to a custom element name, or that a
    // definition was to customize (`<button is="x-y">`), is defined only once
    // its upgrade has returned the element: one whose constructor failed is
    // not `:defined`, whatever it is called.
    if (!is_value().empty()) return false;
    return !CustomElementRegistry::is_valid_custom_element_name(m_tag_name);
}

namespace forms {

bool (*pattern_matcher)(const std::string& pattern, const std::string& value) = nullptr;

namespace {

bool is_html(const Element& element, const char* tag) {
    return !element.is_foreign() && element.tag_name() == tag;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string strip(const std::string& text) {
    std::size_t start = 0, end = text.size();
    while (start < end && is_space(text[start])) ++start;
    while (end > start && is_space(text[end - 1])) --end;
    return text.substr(start, end - start);
}

std::string strip_and_collapse(const std::string& text) {
    std::string out;
    bool pending = false;
    for (char c : text) {
        if (is_space(c)) { pending = !out.empty(); continue; }
        if (pending) { out += ' '; pending = false; }
        out += c;
    }
    return out;
}

std::string strip_newlines(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) if (c != '\n' && c != '\r') out += c;
    return out;
}

// The root of the tree `node` is in. Kept alive by the returned pointer.
std::shared_ptr<const Node> tree_root(const Node& node) {
    std::shared_ptr<const Node> at = node.shared_from_this();
    for (;;) {
        auto parent = at->parent();
        if (!parent) return at;
        at = std::move(parent);
    }
}

std::shared_ptr<Element> shared(const Element& element) {
    return std::static_pointer_cast<Element>(const_cast<Element&>(element).shared_from_this());
}

const Element* ancestor(const Element& element, const char* tag) {
    for (auto at = element.parent(); at; at = at->parent()) {
        if (at->is_element() && is_html(static_cast<const Element&>(*at), tag)) return static_cast<const Element*>(at.get());
    }
    return nullptr;
}

bool is_descendant_of(const Node& node, const Node& ancestor_node) {
    for (auto at = node.parent(); at; at = at->parent()) {
        if (at.get() == &ancestor_node) return true;
    }
    return false;
}

const char* const kInputTypes[] = {
    "hidden", "text", "search", "tel", "url", "email", "password", "date", "month", "week", "time",
    "datetime-local", "number", "range", "color", "checkbox", "radio", "file", "submit", "image", "reset", "button"
};

enum class ValueMode { Value, Default, DefaultOn, Filename };

ValueMode value_mode(const std::string& type) {
    if (type == "hidden" || type == "submit" || type == "image" || type == "reset" || type == "button") return ValueMode::Default;
    if (type == "checkbox" || type == "radio") return ValueMode::DefaultOn;
    if (type == "file") return ValueMode::Filename;
    return ValueMode::Value;
}

bool is_number_type(const std::string& type) { return type == "number" || type == "range"; }

bool is_datetime_type(const std::string& type) {
    return type == "date" || type == "time" || type == "month" || type == "week" || type == "datetime-local";
}

bool is_steppable_type(const std::string& type) { return is_number_type(type) || is_datetime_type(type); }

constexpr double kMsPerDay = 86400000.0;
constexpr double kMsPerWeek = 604800000.0;

bool is_leap_year(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int days_in_month(int64_t y, unsigned m) {
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    return m == 2 && is_leap_year(y) ? 29 : kDays[m - 1];
}

int64_t floor_div_i64(int64_t a, int64_t b) {
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

int64_t floor_mod_i64(int64_t a, int64_t b) { return a - floor_div_i64(a, b) * b; }

// Howard Hinnant's civil-from-days / days-from-civil (1970-01-01 == 0).
int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}

int iso_weekday(int64_t days) { return static_cast<int>(floor_mod_i64(days + 3, 7)); } // 0=Mon..6=Sun

int iso_weeks_in_year(int64_t y) {
    const int wd_jan1 = iso_weekday(days_from_civil(y, 1, 1));
    return (wd_jan1 == 3 || (is_leap_year(y) && wd_jan1 == 2)) ? 53 : 52;
}

int64_t days_from_iso_week(int64_t y, unsigned w) {
    const int64_t jan4 = days_from_civil(y, 1, 4);
    const int64_t week1_monday = jan4 - iso_weekday(jan4);
    return week1_monday + static_cast<int64_t>(w - 1) * 7;
}

void iso_week_from_days(int64_t days, int64_t& iso_year, unsigned& iso_week) {
    const int64_t thu_days = days + (3 - iso_weekday(days));
    unsigned m = 0, d = 0;
    civil_from_days(thu_days, iso_year, m, d);
    const int64_t jan1 = days_from_civil(iso_year, 1, 1);
    iso_week = static_cast<unsigned>((thu_days - jan1) / 7 + 1);
}

bool parse_uint_range(const std::string& text, std::size_t from, std::size_t count, int64_t& out) {
    if (from + count > text.size() || count == 0) return false;
    int64_t v = 0;
    for (std::size_t i = from; i < from + count; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        if (v > 1000000000LL) return false;
        v = v * 10 + (text[i] - '0');
    }
    out = v;
    return true;
}

bool all_digits(const std::string& text, std::size_t from, std::size_t count) {
    if (from + count > text.size()) return false;
    for (std::size_t i = from; i < from + count; ++i) if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

bool parse_date_parts(const std::string& text, int64_t& y, unsigned& m, unsigned& d) {
    // HTML spec valid date string: YYYY-MM-DD (at least 4 year digits, year > 0).
    const std::size_t dash1 = text.find('-');
    if (dash1 == std::string::npos || dash1 < 4) return false;
    if (text.size() != dash1 + 6 || text[dash1 + 3] != '-') return false;
    int64_t year = 0, month = 0, day = 0;
    if (!parse_uint_range(text, 0, dash1, year) || year < 1) return false;
    if (!parse_uint_range(text, dash1 + 1, 2, month) || month < 1 || month > 12) return false;
    if (!parse_uint_range(text, dash1 + 4, 2, day) || day < 1 || day > days_in_month(year, static_cast<unsigned>(month))) {
        return false;
    }
    y = year;
    m = static_cast<unsigned>(month);
    d = static_cast<unsigned>(day);
    return true;
}

bool parse_month_parts(const std::string& text, int64_t& y, unsigned& m) {
    const std::size_t dash = text.find('-');
    if (dash == std::string::npos || dash < 4 || text.size() != dash + 3) return false;
    int64_t year = 0, month = 0;
    if (!parse_uint_range(text, 0, dash, year) || year < 1) return false;
    if (!parse_uint_range(text, dash + 1, 2, month) || month < 1 || month > 12) return false;
    y = year;
    m = static_cast<unsigned>(month);
    return true;
}

bool parse_week_parts(const std::string& text, int64_t& y, unsigned& w) {
    const std::size_t dash = text.find('-');
    if (dash == std::string::npos || dash < 4 || text.size() != dash + 4 || text[dash + 1] != 'W') return false;
    int64_t year = 0, week = 0;
    if (!parse_uint_range(text, 0, dash, year) || year < 1) return false;
    if (!parse_uint_range(text, dash + 2, 2, week) || week < 1 || week > iso_weeks_in_year(year)) return false;
    y = year;
    w = static_cast<unsigned>(week);
    return true;
}

bool parse_time_parts(const std::string& text, int& hh, int& mm, int& ss, int& ms) {
    if (text.size() < 5 || !all_digits(text, 0, 2) || text[2] != ':' || !all_digits(text, 3, 2)) return false;
    int64_t h = 0, m = 0, s = 0;
    if (!parse_uint_range(text, 0, 2, h) || h > 23) return false;
    if (!parse_uint_range(text, 3, 2, m) || m > 59) return false;
    int msec = 0;
    if (text.size() > 5) {
        if (text.size() < 8 || text[5] != ':' || !all_digits(text, 6, 2)) return false;
        if (!parse_uint_range(text, 6, 2, s) || s > 59) return false;
        if (text.size() > 8) {
            if (text[8] != '.' || text.size() < 10 || text.size() > 12 || !all_digits(text, 9, text.size() - 9)) {
                return false;
            }
            const std::size_t frac_len = text.size() - 9;
            int64_t frac = 0;
            parse_uint_range(text, 9, frac_len, frac);
            if (frac_len == 1) msec = static_cast<int>(frac * 100);
            else if (frac_len == 2) msec = static_cast<int>(frac * 10);
            else msec = static_cast<int>(frac);
        }
    }
    hh = static_cast<int>(h);
    mm = static_cast<int>(m);
    ss = static_cast<int>(s);
    ms = msec;
    return true;
}

bool parse_datetime_local_parts(const std::string& text, int64_t& y, unsigned& m, unsigned& d,
                                int& hh, int& mm, int& ss, int& ms, std::string* normalized_out = nullptr) {
    std::size_t sep = text.find('T');
    if (sep == std::string::npos) sep = text.find(' ');
    if (sep == std::string::npos) return false;
    const std::string date_part = text.substr(0, sep);
    const std::string time_part = text.substr(sep + 1);
    if (!parse_date_parts(date_part, y, m, d) || !parse_time_parts(time_part, hh, mm, ss, ms)) return false;
    if (normalized_out) *normalized_out = date_part + "T" + time_part;
    return true;
}

std::string format_year(int64_t y) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04lld", static_cast<long long>(y));
    return buf;
}

std::string format_date_days(int64_t days) {
    int64_t y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(days, y, m, d);
    if (y < 1 || y > 275760) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s-%02u-%02u", format_year(y).c_str(), m, d);
    return buf;
}

std::string format_month_count(int64_t months) {
    const int64_t y = 1970 + floor_div_i64(months, 12);
    const unsigned m = static_cast<unsigned>(floor_mod_i64(months, 12) + 1);
    if (y < 1 || y > 275760) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s-%02u", format_year(y).c_str(), m);
    return buf;
}

std::string format_week_days(int64_t days) {
    int64_t y = 0;
    unsigned w = 0;
    iso_week_from_days(days, y, w);
    if (y < 1 || y > 275760) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s-W%02u", format_year(y).c_str(), w);
    return buf;
}

std::string format_time_ms(int64_t ms_of_day) {
    const int64_t wrapped = floor_mod_i64(ms_of_day, 86400000);
    const int ms = static_cast<int>(wrapped % 1000);
    const int total_sec = static_cast<int>(wrapped / 1000);
    const int hh = total_sec / 3600;
    const int mm = (total_sec / 60) % 60;
    const int ss = total_sec % 60;
    char buf[32];
    if (ss == 0 && ms == 0) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    } else if (ms == 0) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mm, ss);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", hh, mm, ss, ms);
        std::string out(buf);
        while (!out.empty() && out.back() == '0') out.pop_back();
        return out;
    }
    return buf;
}

std::string format_datetime_local_ms(int64_t ms) {
    const int64_t days = floor_div_i64(ms, 86400000);
    const int64_t rem = floor_mod_i64(ms, 86400000);
    const std::string date_str = format_date_days(days);
    if (date_str.empty()) return {};
    return date_str + "T" + format_time_ms(rem);
}

bool parse_type_value(const std::string& type, const std::string& text, double& out) {
    if (is_number_type(type)) return parse_number(text, out);
    if (type == "date") {
        int64_t y = 0; unsigned m = 0, d = 0;
        if (!parse_date_parts(text, y, m, d)) return false;
        out = static_cast<double>(days_from_civil(y, m, d)) * kMsPerDay;
        return true;
    }
    if (type == "month") {
        int64_t y = 0; unsigned m = 0;
        if (!parse_month_parts(text, y, m)) return false;
        out = static_cast<double>((y - 1970) * 12 + static_cast<int64_t>(m) - 1);
        return true;
    }
    if (type == "week") {
        int64_t y = 0; unsigned w = 0;
        if (!parse_week_parts(text, y, w)) return false;
        out = static_cast<double>(days_from_iso_week(y, w)) * kMsPerDay;
        return true;
    }
    if (type == "time") {
        int hh = 0, mm = 0, ss = 0, ms = 0;
        if (!parse_time_parts(text, hh, mm, ss, ms)) return false;
        out = static_cast<double>(((hh * 60 + mm) * 60 + ss) * 1000 + ms);
        return true;
    }
    if (type == "datetime-local") {
        int64_t y = 0; unsigned m = 0, d = 0;
        int hh = 0, mm = 0, ss = 0, ms = 0;
        if (!parse_datetime_local_parts(text, y, m, d, hh, mm, ss, ms)) return false;
        const int64_t day_ms = days_from_civil(y, m, d) * 86400000LL;
        const int64_t tod_ms = ((hh * 60LL + mm) * 60LL + ss) * 1000LL + ms;
        out = static_cast<double>(day_ms + tod_ms);
        return true;
    }
    return false;
}

std::string format_type_value(const std::string& type, double value) {
    if (!std::isfinite(value)) return {};
    if (is_number_type(type)) return number_to_string(value);
    if (type == "date") {
        const double days = std::floor(value / kMsPerDay);
        if (std::fabs(days) > 1e8) return {};
        return format_date_days(static_cast<int64_t>(days));
    }
    if (type == "month") {
        const double months = std::trunc(value);
        if (std::fabs(months) > 1e7) return {};
        return format_month_count(static_cast<int64_t>(months));
    }
    if (type == "week") {
        // Round half-day differences away or floor to UTC day representing the week's Monday.
        const double days = std::floor(value / kMsPerDay);
        if (std::fabs(days) > 1e8) return {};
        return format_week_days(static_cast<int64_t>(days));
    }
    if (type == "time") {
        const double trunc_ms = std::floor(value);
        if (std::fabs(trunc_ms) > 1e15) return {};
        return format_time_ms(static_cast<int64_t>(trunc_ms));
    }
    if (type == "datetime-local") {
        const double trunc_ms = std::floor(value);
        if (std::fabs(trunc_ms) > 1e15) return {};
        return format_datetime_local_ms(static_cast<int64_t>(trunc_ms));
    }
    return {};
}

double attr_number(const Element& element, const char* name, double fallback) {
    const std::string* text = element.attribute(name);
    double out = 0;
    return text && parse_number(*text, out) ? out : fallback;
}

bool has_attr_type_value(const Element& element, const char* name, const std::string& type, double& out) {
    const std::string* text = element.attribute(name);
    return text && parse_type_value(type, *text, out);
}

double default_step_for(const std::string& type) {
    return (type == "time" || type == "datetime-local") ? 60.0 : 1.0;
}

double step_scale_factor(const std::string& type) {
    if (type == "date") return kMsPerDay;
    if (type == "week") return kMsPerWeek;
    if (type == "time" || type == "datetime-local") return 1000.0;
    return 1.0;
}

// "Allowed value step" of a steppable input; 0 for step="any".
double step_of(const Element& input, const std::string& type) {
    const double def = default_step_for(type);
    const double scale = step_scale_factor(type);
    const std::string* text = input.attribute("step");
    if (!text) return def * scale;
    if (lower(*text) == "any") return 0;
    double value = 0;
    if (!parse_number(*text, value) || value <= 0) return def * scale;
    return value * scale;
}

double step_base(const Element& input, const std::string& type) {
    double out = 0;
    if (has_attr_type_value(input, "min", type, out)) return out;
    if (has_attr_type_value(input, "value", type, out)) return out;
    if (type == "week") return -259200000.0; // 1969-12-29T00:00:00Z (Monday of 1970-W01)
    return 0.0;
}

bool is_valid_color(const std::string& text) {
    if (text.size() != 7 || text[0] != '#') return false;
    for (std::size_t i = 1; i < 7; ++i) {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool is_valid_date(const std::string& text) {
    int64_t y = 0; unsigned m = 0, d = 0;
    return parse_date_parts(text, y, m, d);
}

bool is_valid_time(const std::string& text) {
    int hh = 0, mm = 0, ss = 0, ms = 0;
    return parse_time_parts(text, hh, mm, ss, ms);
}

std::string sanitize(const Element& input, const std::string& type, const std::string& raw) {
    if (type == "text" || type == "search" || type == "tel" || type == "password") return strip_newlines(raw);
    if (type == "url" || type == "email") return strip(strip_newlines(raw));
    if (type == "number") {
        double ignored = 0;
        return parse_number(raw, ignored) ? raw : std::string();
    }
    if (type == "range") {
        const double min = attr_number(input, "min", 0);
        double max = attr_number(input, "max", 100);
        if (max < min) max = min;
        double value = 0;
        if (!parse_number(raw, value)) value = max < min ? min : min + (max - min) / 2;
        value = std::min(std::max(value, min), max);
        const double step = step_of(input, "range");
        if (step > 0) {
            const double base = min;
            double aligned = base + std::round((value - base) / step) * step;
            if (aligned > max) aligned -= step;
            if (aligned < min) aligned = min;
            value = aligned;
        }
        return number_to_string(value);
    }
    if (type == "color") return is_valid_color(raw) ? lower(raw) : std::string("#000000");
    if (type == "date") return is_valid_date(raw) ? raw : std::string();
    if (type == "time") return is_valid_time(raw) ? raw : std::string();
    if (type == "datetime-local") {
        int64_t y = 0; unsigned m = 0, d = 0;
        int hh = 0, mm = 0, ss = 0, ms = 0;
        std::string normalized;
        return parse_datetime_local_parts(raw, y, m, d, hh, mm, ss, ms, &normalized) ? normalized : std::string();
    }
    if (type == "month") {
        int64_t y = 0; unsigned m = 0;
        return parse_month_parts(raw, y, m) ? raw : std::string();
    }
    if (type == "week") {
        int64_t y = 0; unsigned w = 0;
        return parse_week_parts(raw, y, w) ? raw : std::string();
    }
    return raw;
}

// The value of an input in value mode: what the control holds.
std::string input_value(const Element& input, const std::string& type) {
    const FormControlState* state = input.form_state();
    const std::string raw = state && state->value_dirty ? state->value : input.get_attribute("value");
    return sanitize(input, type, raw);
}

std::string normalize_newlines(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            out += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool is_valid_email(const std::string& text) {
    const auto at = text.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= text.size() || text.find('@', at + 1) != std::string::npos) return false;
    static const std::string local_extra = ".!#$%&'*+/=?^_`{|}~-";
    for (std::size_t i = 0; i < at; ++i) {
        const char c = text[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && local_extra.find(c) == std::string::npos) return false;
    }
    std::size_t start = at + 1;
    while (start <= text.size()) {
        std::size_t dot = text.find('.', start);
        if (dot == std::string::npos) dot = text.size();
        if (dot == start || dot - start > 63) return false;
        for (std::size_t i = start; i < dot; ++i) {
            const char c = text[i];
            const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!alnum && !(c == '-' && i != start && i + 1 != dot)) return false;
        }
        start = dot + 1;
        if (dot == text.size()) break;
    }
    return true;
}

bool is_absolute_url(const std::string& text) {
    std::size_t i = 0;
    if (text.empty() || !((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z'))) return false;
    while (i < text.size() && text[i] != ':') {
        const char c = text[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        if (!ok) return false;
        ++i;
    }
    if (i >= text.size()) return false;
    const std::string scheme = lower(text.substr(0, i));
    const bool special = scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "ws" || scheme == "wss" || scheme == "file";
    if (!special) return true;
    // A special scheme needs a host.
    std::size_t rest = i + 1;
    while (rest < text.size() && (text[rest] == '/' || text[rest] == '\\')) ++rest;
    return scheme == "file" || rest < text.size();
}

// The first <legend> child of a fieldset: controls inside it stay enabled.
const Element* first_legend(const Element& fieldset) {
    for (const auto& child : fieldset.children()) {
        if (child->is_element() && is_html(static_cast<const Element&>(*child), "legend")) {
            return static_cast<const Element*>(child.get());
        }
    }
    return nullptr;
}

void collect_options(const Element& select, std::vector<std::shared_ptr<Element>>& out) {
    for (const auto& child : select.children()) {
        if (!child->is_element()) continue;
        auto element = std::static_pointer_cast<Element>(child);
        if (is_html(*element, "option")) out.push_back(element);
        else if (is_html(*element, "optgroup")) {
            for (const auto& inner : element->children()) {
                if (inner->is_element() && is_html(static_cast<const Element&>(*inner), "option")) {
                    out.push_back(std::static_pointer_cast<Element>(inner));
                }
            }
        }
    }
}

bool option_disabled(const Element& option) {
    if (option.has_attribute("disabled")) return true;
    auto parent = option.parent();
    return parent && parent->is_element() && is_html(static_cast<const Element&>(*parent), "optgroup") &&
           static_cast<const Element&>(*parent).has_attribute("disabled");
}

// What the option itself says, before a single select picks one.
bool raw_selected(const Element& option) {
    const FormControlState* state = option.form_state();
    if (state && state->has_selected) return state->selected;
    return option.has_attribute("selected");
}

// Whether the "nothing selected" a script asked for still stands.
bool none_still_holds(const Element& select, const std::vector<std::shared_ptr<Element>>& options) {
    const FormControlState* state = select.form_state();
    if (!state || !state->select_none) return false;
    if (state->none_options.size() != options.size()) return false;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (state->none_options[i] != options[i].get()) return false;
    }
    return true;
}

// The index of the one selected option of a single select, or -1.
int single_selection(const Element& select, const std::vector<std::shared_ptr<Element>>& options) {
    int picked = -1;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (raw_selected(*options[i])) picked = static_cast<int>(i);
    }
    if (picked >= 0 || none_still_holds(select, options)) return picked;
    // A drop-down always shows something: the first option that can be chosen.
    int size = 0;
    if (const std::string* text = select.attribute("size")) size = std::atoi(text->c_str());
    if (size > 1) return -1;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (!option_disabled(*options[i])) return static_cast<int>(i);
    }
    return -1;
}

void clear_none(Element& select) {
    if (FormControlState* state = select.form_state()) {
        state->select_none = false;
        state->none_options.clear();
    }
}

} // namespace

// --- Classification ---------------------------------------------------------------

bool is_listed(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "input" || tag == "button" || tag == "select" || tag == "textarea" || tag == "fieldset" ||
           tag == "output" || tag == "object";
}

bool is_submittable(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "input" || tag == "button" || tag == "select" || tag == "textarea";
}

bool is_labelable(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    if (tag == "input") return input_type(element) != "hidden";
    return tag == "button" || tag == "select" || tag == "textarea" || tag == "meter" || tag == "output" || tag == "progress";
}

std::string input_type(const Element& input) {
    const std::string* text = input.attribute("type");
    if (!text) return "text";
    const std::string wanted = lower(*text);
    for (const char* known : kInputTypes) {
        if (wanted == known) return wanted;
    }
    return "text";
}

std::string button_type(const Element& button) {
    const std::string* text = button.attribute("type");
    if (!text) return "submit";
    const std::string wanted = lower(*text);
    return wanted == "reset" || wanted == "button" ? wanted : std::string("submit");
}

bool is_submit_button(const Element& element) {
    if (is_html(element, "button")) return button_type(element) == "submit";
    if (is_html(element, "input")) {
        const std::string type = input_type(element);
        return type == "submit" || type == "image";
    }
    return false;
}

bool has_text_selection(const Element& element) {
    if (is_html(element, "textarea")) return true;
    if (!is_html(element, "input")) return false;
    const std::string type = input_type(element);
    return type == "text" || type == "search" || type == "url" || type == "tel" || type == "password";
}

// --- Ownership --------------------------------------------------------------------

std::shared_ptr<Element> form_owner(const Element& control) {
    if (const std::string* id = control.attribute("form")) {
        if (is_listed(control) || is_html(control, "label") || is_html(control, "legend") || is_html(control, "option")) {
            auto root = tree_root(control);
            auto found = id->empty() ? nullptr : root->descendant_with_id(*id);
            return found && is_html(*found, "form") ? found : nullptr;
        }
    }
    for (auto at = control.parent(); at; at = at->parent()) {
        if (at->is_element() && is_html(static_cast<const Element&>(*at), "form")) {
            return std::static_pointer_cast<Element>(at);
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<Element>> form_controls(const Element& form) {
    std::vector<std::shared_ptr<Element>> out;
    auto root = tree_root(form);
    for_each_descendant(*root, [&](Node& node) {
        if (!node.is_element()) return true;
        auto& element = static_cast<Element&>(node);
        if (!is_listed(element)) return true;
        if (is_html(element, "input") && input_type(element) == "image") return true;
        // Without a form attribute anywhere near, only what is inside the
        // form can belong to it, and that is settled by one look upwards.
        if (!element.has_attribute("form") && !is_descendant_of(element, form)) return true;
        if (form_owner(element).get() == &form) out.push_back(shared(element));
        return true;
    });
    return out;
}

std::vector<std::shared_ptr<Element>> fieldset_controls(const Element& fieldset) {
    std::vector<std::shared_ptr<Element>> out;
    for_each_descendant(fieldset, [&](Node& node) {
        if (node.is_element() && is_listed(static_cast<Element&>(node))) out.push_back(shared(static_cast<Element&>(node)));
        return true;
    });
    return out;
}

std::shared_ptr<Element> label_control(const Element& label) {
    if (const std::string* id = label.attribute("for")) {
        auto root = tree_root(label);
        auto found = id->empty() ? nullptr : root->descendant_with_id(*id);
        return found && is_labelable(*found) ? found : nullptr;
    }
    std::shared_ptr<Element> found;
    for_each_descendant(label, [&](Node& node) {
        if (node.is_element() && is_labelable(static_cast<Element&>(node))) {
            found = shared(static_cast<Element&>(node));
            return false;
        }
        return true;
    });
    return found;
}

std::vector<std::shared_ptr<Element>> labels_of(const Element& control) {
    std::vector<std::shared_ptr<Element>> out;
    if (!is_labelable(control)) return out;
    auto root = tree_root(control);
    for_each_descendant(*root, [&](Node& node) {
        if (node.is_element() && is_html(static_cast<Element&>(node), "label") &&
            label_control(static_cast<Element&>(node)).get() == &control) {
            out.push_back(shared(static_cast<Element&>(node)));
        }
        return true;
    });
    return out;
}

std::vector<std::shared_ptr<Element>> radio_group(const Element& radio) {
    std::vector<std::shared_ptr<Element>> out;
    const std::string* name = radio.attribute("name");
    if (!name || name->empty()) {
        out.push_back(shared(radio));
        return out;
    }
    auto owner = form_owner(radio);
    auto root = tree_root(radio);
    for_each_descendant(*root, [&](Node& node) {
        if (!node.is_element()) return true;
        auto& element = static_cast<Element&>(node);
        if (!is_html(element, "input") || input_type(element) != "radio") return true;
        const std::string* other = element.attribute("name");
        if (!other || *other != *name) return true;
        if (form_owner(element) == owner) out.push_back(shared(element));
        return true;
    });
    if (out.empty()) out.push_back(shared(radio));       // the root itself is a radio
    return out;
}

// --- Value ------------------------------------------------------------------------

std::string textarea_raw_value(const Element& textarea) {
    const FormControlState* state = textarea.form_state();
    return state && state->value_dirty ? state->value : textarea.text_content();
}

std::string value(const Element& control) {
    if (control.is_foreign()) return control.get_attribute("value");
    const std::string& tag = control.tag_name();
    if (tag == "input") {
        const std::string type = input_type(control);
        switch (value_mode(type)) {
            case ValueMode::Value: return input_value(control, type);
            case ValueMode::Default: return control.get_attribute("value");
            case ValueMode::DefaultOn: return control.has_attribute("value") ? control.get_attribute("value") : std::string("on");
            case ValueMode::Filename: return std::string();
        }
    }
    if (tag == "textarea") return normalize_newlines(textarea_raw_value(control));
    if (tag == "select") {
        auto selected = selected_options(control);
        return selected.empty() ? std::string() : option_value(*selected.front());
    }
    if (tag == "option") return option_value(control);
    if (tag == "output") {
        const FormControlState* state = control.form_state();
        return state && state->value_dirty ? state->value : control.text_content();
    }
    return control.get_attribute("value");
}

bool set_value(Element& control, const std::string& new_value) {
    if (control.is_foreign()) {
        control.set_attribute("value", new_value);
        return true;
    }
    const std::string& tag = control.tag_name();
    if (tag == "input") {
        const std::string type = input_type(control);
        switch (value_mode(type)) {
            case ValueMode::Value: {
                FormControlState& state = control.ensure_form_state();
                state.value = sanitize(control, type, new_value);
                state.value_dirty = true;
                state.selection_set = false;
                control.mark_needs_layout();
                Node::bump_tree_version();
                return true;
            }
            case ValueMode::Default:
            case ValueMode::DefaultOn:
                control.set_attribute("value", new_value);
                return true;
            case ValueMode::Filename:
                return new_value.empty();
        }
    }
    if (tag == "textarea" || tag == "output") {
        FormControlState& state = control.ensure_form_state();
        state.value = new_value;
        state.value_dirty = true;
        state.selection_set = false;
        control.mark_needs_layout();
        Node::bump_tree_version();
        return true;
    }
    if (tag == "select") {
        auto options = select_options(control);
        int found = -1;
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (option_value(*options[i]) == new_value) { found = static_cast<int>(i); break; }
        }
        set_selected_index(control, found);
        return true;
    }
    control.set_attribute("value", new_value);
    return true;
}

std::string default_value(const Element& control) {
    if (is_html(control, "textarea")) return control.text_content();
    if (is_html(control, "output")) {
        // Remembered when the value was first changed; until then the text.
        return control.text_content();
    }
    return control.get_attribute("value");
}

// --- Checkboxes and radio buttons -------------------------------------------------

bool checked(const Element& input) {
    const FormControlState* state = input.form_state();
    if (state && state->has_checked) return state->checked;
    if (!input.has_attribute("checked")) return false;
    if (!is_html(input, "input") || input_type(input) != "radio") return true;
    // Several radio buttons of one group marked `checked` in the markup: the
    // last one is the one that is checked.
    bool after = false;
    for (const auto& member : radio_group(input)) {
        if (member.get() == &input) { after = true; continue; }
        if (!after) continue;
        const FormControlState* other = member->form_state();
        if (other && other->has_checked ? other->checked : member->has_attribute("checked")) return false;
    }
    return true;
}

void set_checked(Element& input, bool new_value, bool make_dirty) {
    const bool radio = is_html(input, "input") && input_type(input) == "radio";
    if (radio && new_value) {
        for (const auto& member : radio_group(input)) {
            if (member.get() == &input) continue;
            FormControlState& other = member->ensure_form_state();
            other.checked = false;
            other.has_checked = true;
        }
    }
    FormControlState& state = input.ensure_form_state();
    state.checked = new_value;
    state.has_checked = true;
    if (make_dirty) state.checked_dirty = true;
    input.mark_needs_layout();
    Node::bump_tree_version();
}

// --- select and option ------------------------------------------------------------

bool is_multiple_select(const Element& select) { return select.has_attribute("multiple"); }

std::vector<std::shared_ptr<Element>> select_options(const Element& select) {
    std::vector<std::shared_ptr<Element>> out;
    collect_options(select, out);
    return out;
}

std::vector<std::shared_ptr<Element>> selected_options(const Element& select) {
    std::vector<std::shared_ptr<Element>> out;
    auto options = select_options(select);
    if (is_multiple_select(select)) {
        for (const auto& option : options) if (raw_selected(*option)) out.push_back(option);
        return out;
    }
    const int index = single_selection(select, options);
    if (index >= 0) out.push_back(options[static_cast<std::size_t>(index)]);
    return out;
}

int selected_index(const Element& select) {
    auto options = select_options(select);
    if (is_multiple_select(select)) {
        for (std::size_t i = 0; i < options.size(); ++i) if (raw_selected(*options[i])) return static_cast<int>(i);
        return -1;
    }
    return single_selection(select, options);
}

void set_selected_index(Element& select, int index) {
    auto options = select_options(select);
    for (std::size_t i = 0; i < options.size(); ++i) {
        FormControlState& state = options[i]->ensure_form_state();
        state.selected = static_cast<int>(i) == index;
        state.has_selected = true;
        if (state.selected) state.selected_dirty = true;
    }
    FormControlState& state = select.ensure_form_state();
    if (index < 0 || index >= static_cast<int>(options.size())) {
        state.select_none = true;
        state.none_options.clear();
        for (const auto& option : options) state.none_options.push_back(option.get());
    } else {
        clear_none(select);
    }
    select.mark_needs_layout();
    Node::bump_tree_version();
}

std::shared_ptr<Element> option_select(const Element& option) {
    auto parent = option.parent();
    if (parent && parent->is_element() && is_html(static_cast<const Element&>(*parent), "optgroup")) parent = parent->parent();
    if (parent && parent->is_element() && is_html(static_cast<const Element&>(*parent), "select")) {
        return std::static_pointer_cast<Element>(parent);
    }
    return nullptr;
}

bool option_selected(const Element& option) {
    auto select = option_select(option);
    if (!select || is_multiple_select(*select)) return raw_selected(option);
    auto options = select_options(*select);
    const int index = single_selection(*select, options);
    return index >= 0 && options[static_cast<std::size_t>(index)].get() == &option;
}

void set_option_selected(Element& option, bool new_value) {
    auto select = option_select(option);
    if (select && !is_multiple_select(*select) && new_value) {
        for (const auto& other : select_options(*select)) {
            if (other.get() == &option) continue;
            FormControlState& state = other->ensure_form_state();
            state.selected = false;
            state.has_selected = true;
        }
    }
    FormControlState& state = option.ensure_form_state();
    state.selected = new_value;
    state.has_selected = true;
    state.selected_dirty = true;
    if (select) {
        // Unselecting the shown option of a drop-down selects the first one
        // again; choosing one ends "nothing selected".
        clear_none(*select);
        select->mark_needs_layout();
    }
    Node::bump_tree_version();
}

std::string option_text(const Element& option) {
    std::string text;
    for_each_descendant(option, [&](Node& node) {
        if (node.is_text()) {
            // Text inside a <script> in the option does not count.
            auto parent = node.parent();
            if (parent && parent->is_element() && static_cast<Element&>(*parent).tag_name() == "script") return true;
            text += node.text_content();
        }
        return true;
    });
    return strip_and_collapse(text);
}

std::string option_value(const Element& option) {
    if (const std::string* text = option.attribute("value")) return *text;
    return option_text(option);
}

std::string option_label(const Element& option) {
    if (const std::string* text = option.attribute("label")) {
        if (!text->empty()) return *text;
    }
    return option_text(option);
}

// --- Selector states --------------------------------------------------------------

bool matches_checked(const Element& element) {
    if (element.is_foreign()) return false;
    if (element.tag_name() == "input") {
        const std::string type = input_type(element);
        return (type == "checkbox" || type == "radio") && checked(element);
    }
    if (element.tag_name() == "option") return option_selected(element);
    return false;
}

bool can_be_disabled(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "button" || tag == "input" || tag == "select" || tag == "textarea" || tag == "optgroup" ||
           tag == "option" || tag == "fieldset";
}

bool is_disabled(const Element& element) {
    if (!can_be_disabled(element)) return false;
    if (element.has_attribute("disabled")) return true;
    const std::string& tag = element.tag_name();
    if (tag == "option") return option_disabled(element);
    if (tag == "optgroup") return false;
    // Inside a disabled fieldset, except inside its first legend.
    const Node* below = &element;
    for (auto at = element.parent(); at; at = at->parent()) {
        if (at->is_element()) {
            const auto& up = static_cast<const Element&>(*at);
            if (is_html(up, "fieldset") && up.has_attribute("disabled")) {
                const Element* legend = first_legend(up);
                if (!(legend && (legend == below || is_descendant_of(element, *legend)))) return true;
            }
        }
        below = at.get();
    }
    return false;
}

bool can_be_required(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "input" || tag == "select" || tag == "textarea";
}

bool is_required(const Element& element) {
    return can_be_required(element) && element.has_attribute("required");
}

bool is_read_write(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    if (tag == "textarea") return !element.has_attribute("readonly") && !is_disabled(element);
    if (tag == "input") {
        const std::string type = input_type(element);
        if (value_mode(type) != ValueMode::Value || type == "range" || type == "color") return false;
        return !element.has_attribute("readonly") && !is_disabled(element);
    }
    if (const std::string* editable = element.attribute("contenteditable")) {
        return lower(*editable) != "false";
    }
    return false;
}

bool placeholder_shown(const Element& element) {
    if (element.is_foreign() || !element.has_attribute("placeholder")) return false;
    if (element.tag_name() == "textarea") return value(element).empty();
    if (element.tag_name() != "input") return false;
    const std::string type = input_type(element);
    const bool takes_placeholder = type == "text" || type == "search" || type == "url" || type == "tel" ||
                                   type == "email" || type == "password" || type == "number";
    return takes_placeholder && value(element).empty();
}

int validity_for_selector(const Element& element) {
    if (element.is_foreign()) return 0;
    const std::string& tag = element.tag_name();
    if (tag == "form" || tag == "fieldset") return invalid_controls(element).empty() ? 1 : 2;
    if (!will_validate(element)) return 0;
    return validity(element).valid() ? 1 : 2;
}

// --- Validation -------------------------------------------------------------------

bool will_validate(const Element& control) {
    if (!is_submittable(control)) return false;
    if (is_disabled(control)) return false;
    if (ancestor(control, "datalist")) return false;
    const std::string& tag = control.tag_name();
    if (tag == "button") return button_type(control) == "submit";
    if (tag == "input") {
        const std::string type = input_type(control);
        if (type == "hidden" || type == "reset" || type == "button") return false;
        if (control.has_attribute("readonly")) return false;
    }
    if (tag == "textarea" && control.has_attribute("readonly")) return false;
    return true;
}

Validity validity(const Element& control) {
    Validity out;
    if (!is_submittable(control)) return out;
    if (const FormControlState* state = control.form_state()) out.custom_error = !state->custom_validity.empty();
    const std::string& tag = control.tag_name();
    const bool required = control.has_attribute("required");

    if (tag == "select") {
        if (required) {
            auto selected = selected_options(control);
            if (selected.empty()) {
                out.value_missing = true;
            } else if (!is_multiple_select(control)) {
                // The placeholder label option: the first option, a child of
                // the select, with an empty value.
                auto options = select_options(control);
                if (!options.empty() && selected.front() == options.front() &&
                    options.front()->parent().get() == &control && option_value(*options.front()).empty()) {
                    out.value_missing = true;
                }
            }
        }
        return out;
    }
    if (tag == "textarea") {
        out.value_missing = required && value(control).empty();
        return out;
    }
    if (tag != "input") return out;

    const std::string type = input_type(control);
    if (type == "checkbox") {
        out.value_missing = required && !checked(control);
        return out;
    }
    if (type == "radio") {
        bool group_required = false, any_checked = false;
        for (const auto& member : radio_group(control)) {
            group_required = group_required || member->has_attribute("required");
            any_checked = any_checked || checked(*member);
        }
        out.value_missing = group_required && !any_checked;
        return out;
    }
    if (type == "file") {
        out.value_missing = required;
        return out;
    }
    if (value_mode(type) != ValueMode::Value) return out;

    const std::string current = input_value(control, type);
    if (required && current.empty() && type != "range" && type != "color") out.value_missing = true;
    if (current.empty()) return out;

    if (type == "email") {
        if (control.has_attribute("multiple")) {
            std::size_t start = 0;
            while (start <= current.size()) {
                std::size_t comma = current.find(',', start);
                if (comma == std::string::npos) comma = current.size();
                if (!is_valid_email(strip(current.substr(start, comma - start)))) out.type_mismatch = true;
                start = comma + 1;
            }
        } else {
            out.type_mismatch = !is_valid_email(current);
        }
    } else if (type == "url") {
        out.type_mismatch = !is_absolute_url(current);
    }

    const bool takes_pattern = type == "text" || type == "search" || type == "url" || type == "tel" ||
                               type == "email" || type == "password";
    if (takes_pattern && pattern_matcher) {
        if (const std::string* pattern = control.attribute("pattern")) {
            out.pattern_mismatch = !pattern_matcher(*pattern, current);
        }
    }

    if (is_steppable_type(type)) {
        double number = 0;
        if (parse_type_value(type, current, number)) {
            double min = 0, max = 0;
            const bool has_min = has_attr_type_value(control, "min", type, min);
            const bool has_max = has_attr_type_value(control, "max", type, max);
            if (type == "time" && has_min && has_max && min > max) {
                if (number < min && number > max) {
                    out.range_underflow = true;
                    out.range_overflow = true;
                }
            } else {
                if (has_min && number < min) out.range_underflow = true;
                if (has_max && number > max) out.range_overflow = true;
            }
            const double step = step_of(control, type);
            if (step > 0) {
                const double steps = (number - step_base(control, type)) / step;
                if (std::fabs(steps - std::round(steps)) > 1e-9 * std::max(1.0, std::fabs(steps))) out.step_mismatch = true;
            }
        }
    }
    return out;
}

std::string validation_message(const Element& control) {
    if (!will_validate(control)) return std::string();
    const Validity v = validity(control);
    if (v.custom_error) return control.form_state()->custom_validity;
    if (v.value_missing) return "Please fill out this field.";
    if (v.type_mismatch) return "Please enter a value of the right kind.";
    if (v.pattern_mismatch) return "Please match the requested format.";
    if (v.range_underflow) return "The value is too small.";
    if (v.range_overflow) return "The value is too large.";
    if (v.step_mismatch) return "The value is not one of the allowed steps.";
    return std::string();
}

std::vector<std::shared_ptr<Element>> invalid_controls(const Element& form_or_fieldset) {
    std::vector<std::shared_ptr<Element>> out;
    auto controls = is_html(form_or_fieldset, "form") ? form_controls(form_or_fieldset) : fieldset_controls(form_or_fieldset);
    for (const auto& control : controls) {
        if (will_validate(*control) && !validity(*control).valid()) out.push_back(control);
    }
    return out;
}

// --- Numbers & Date/Time ----------------------------------------------------------

bool parse_number(const std::string& text, double& out) {
    // -?digits(.digits)?([eE][+-]?digits)? or -?.digits...
    std::size_t i = 0;
    const std::size_t n = text.size();
    if (i < n && text[i] == '-') ++i;
    std::size_t digits = 0;
    while (i < n && text[i] >= '0' && text[i] <= '9') { ++i; ++digits; }
    if (i < n && text[i] == '.') {
        ++i;
        std::size_t fraction = 0;
        while (i < n && text[i] >= '0' && text[i] <= '9') { ++i; ++fraction; }
        if (fraction == 0) return false;
        digits += fraction;
    }
    if (digits == 0) return false;
    if (i < n && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
        std::size_t exponent = 0;
        while (i < n && text[i] >= '0' && text[i] <= '9') { ++i; ++exponent; }
        if (exponent == 0) return false;
    }
    if (i != n) return false;
    out = std::strtod(text.c_str(), nullptr);
    return std::isfinite(out);
}

std::string number_to_string(double value) {
    if (value == 0) return "0";
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        return buffer;
    }
    // The shortest text that reads back as the same number.
    char buffer[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) break;
    }
    return buffer;
}

bool has_value_as_number(const std::string& type) { return is_steppable_type(type); }

bool has_value_as_date(const std::string& type) {
    return type == "date" || type == "month" || type == "week" || type == "time";
}

double value_as_number(const Element& input) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!is_html(input, "input")) return nan;
    const std::string type = input_type(input);
    if (!has_value_as_number(type)) return nan;
    double out = 0;
    if (!parse_type_value(type, input_value(input, type), out)) return nan;
    return out;
}

bool set_value_as_number(Element& input, double value) {
    if (!is_html(input, "input")) return false;
    const std::string type = input_type(input);
    if (!has_value_as_number(type)) return false;
    if (std::isnan(value)) {
        set_value(input, std::string());
        return true;
    }
    const std::string formatted = format_type_value(type, value);
    set_value(input, formatted);
    return true;
}

bool value_as_utc_ms(const Element& input, double& out) {
    if (!is_html(input, "input")) return false;
    const std::string type = input_type(input);
    if (!has_value_as_date(type)) return false;
    const std::string current = input_value(input, type);
    if (current.empty()) return false;
    if (type == "month") {
        int64_t y = 0; unsigned m = 0;
        if (!parse_month_parts(current, y, m)) return false;
        out = static_cast<double>(days_from_civil(y, m, 1)) * kMsPerDay;
        return true;
    }
    return parse_type_value(type, current, out);
}

bool set_value_as_utc_ms(Element& input, double ms) {
    if (!is_html(input, "input")) return false;
    const std::string type = input_type(input);
    if (!has_value_as_date(type)) return false;
    if (!std::isfinite(ms)) {
        set_value(input, std::string());
        return true;
    }
    if (type == "date") {
        const double days = std::floor(ms / kMsPerDay);
        if (std::fabs(days) > 1e8) { set_value(input, std::string()); return true; }
        set_value(input, format_date_days(static_cast<int64_t>(days)));
        return true;
    }
    if (type == "time") {
        const double trunc_ms = std::floor(ms);
        if (std::fabs(trunc_ms) > 1e15) { set_value(input, std::string()); return true; }
        set_value(input, format_time_ms(static_cast<int64_t>(trunc_ms)));
        return true;
    }
    if (type == "week") {
        const double days = std::floor(ms / kMsPerDay);
        if (std::fabs(days) > 1e8) { set_value(input, std::string()); return true; }
        set_value(input, format_week_days(static_cast<int64_t>(days)));
        return true;
    }
    if (type == "month") {
        const double days = std::floor(ms / kMsPerDay);
        if (std::fabs(days) > 1e8) { set_value(input, std::string()); return true; }
        int64_t y = 0; unsigned m = 0, d = 0;
        civil_from_days(static_cast<int64_t>(days), y, m, d);
        if (y < 1 || y > 275760) { set_value(input, std::string()); return true; }
        set_value(input, format_month_count((y - 1970) * 12 + static_cast<int64_t>(m) - 1));
        return true;
    }
    return false;
}

bool step(Element& input, int count) {
    if (!is_html(input, "input")) return false;
    const std::string type = input_type(input);
    if (!is_steppable_type(type)) return false;
    const double step_size = step_of(input, type);
    if (step_size <= 0) return false;
    double min = 0, max = 0;
    bool has_min = has_attr_type_value(input, "min", type, min);
    bool has_max = has_attr_type_value(input, "max", type, max);
    if (type == "range") {
        has_min = true;
        has_max = true;
        min = attr_number(input, "min", 0);
        max = attr_number(input, "max", 100);
    }
    if (has_min && has_max && min > max && type != "time") return true;
    const double base = step_base(input, type);
    double number = 0;
    if (!parse_type_value(type, input_value(input, type), number)) number = 0;

    const double steps = (number - base) / step_size;
    const bool aligned = std::fabs(steps - std::round(steps)) <= 1e-9 * std::max(1.0, std::fabs(steps));
    if (!aligned) {
        // Not on a step: go to the nearest one in the direction asked for.
        number = base + (count > 0 ? std::ceil(steps) : std::floor(steps)) * step_size;
    } else {
        number += count * step_size;
    }
    if (type == "time" && has_min && has_max && min > max) {
        // Wrap within 24h for reversed time range; clamp only into [min, 86400000) U [0, max].
        number = std::fmod(std::fmod(number, kMsPerDay) + kMsPerDay, kMsPerDay);
    } else {
        if (has_min && number < min) number = base + std::ceil((min - base) / step_size - 1e-9) * step_size;
        if (has_max && number > max) number = base + std::floor((max - base) / step_size + 1e-9) * step_size;
    }
    const std::string formatted = format_type_value(type, number);
    if (!formatted.empty() || is_number_type(type)) set_value(input, formatted);
    return true;
}

// --- Reset and submission ---------------------------------------------------------

void reset_control(Element& control) {
    if (control.is_foreign()) return;
    const std::string& tag = control.tag_name();
    FormControlState* state = control.form_state();
    if (tag == "input" || tag == "textarea" || tag == "output") {
        if (state) {
            state->value.clear();
            state->value_dirty = false;
            state->checked_dirty = false;
            state->has_checked = false;
            state->selection_set = false;
        }
    } else if (tag == "select") {
        for (const auto& option : select_options(control)) {
            if (FormControlState* option_state = option->form_state()) {
                option_state->has_selected = false;
                option_state->selected_dirty = false;
            }
        }
        clear_none(control);
    }
    control.mark_needs_layout();
    Node::bump_tree_version();
}

std::vector<Entry> entry_list(const Element& form, const Element* submitter) {
    std::vector<Entry> out;
    for (const auto& control : form_controls(form)) {
        const Element& field = *control;
        if (ancestor(field, "datalist") || is_disabled(field)) continue;
        const std::string& tag = field.tag_name();
        std::string type;
        if (tag == "input") type = input_type(field);
        const bool is_button = tag == "button" || type == "submit" || type == "reset" || type == "button" || type == "image";
        if (is_button && &field != submitter) continue;
        if ((type == "checkbox" || type == "radio") && !checked(field)) continue;
        if (tag == "fieldset" || tag == "output" || tag == "object") continue;
        const std::string name = field.get_attribute("name");
        if (name.empty()) continue;
        if (tag == "select") {
            for (const auto& option : selected_options(field)) {
                if (!option_disabled(*option)) out.push_back({name, option_value(*option), false});
            }
        } else if (type == "file") {
            out.push_back({name, std::string(), true});
        } else {
            out.push_back({name, value(field), false});
        }
    }
    return out;
}

void attribute_changed(Element& element, const std::string& name, bool had_old, const std::string& old_value) {
    if (element.is_foreign()) return;
    const std::string& tag = element.tag_name();
    FormControlState* state = element.form_state();
    if (tag == "input") {
        if (name == "checked") {
            // The attribute is the default; a box nobody has touched follows it.
            if (state && state->checked_dirty) return;
            if (state) state->has_checked = false;
            if (element.has_attribute("checked") && input_type(element) == "radio") {
                set_checked(element, true, false);
                element.ensure_form_state().has_checked = true;
            }
        } else if (name == "type") {
            // Changing the kind of control: what it holds moves between the
            // attribute and the state as the standard lays down.
            Element before("input");
            if (had_old) before.m_attributes["type"] = old_value;
            const ValueMode old_mode = value_mode(input_type(before));
            const ValueMode new_mode = value_mode(input_type(element));
            if (old_mode == new_mode) return;
            if (old_mode == ValueMode::Value && state && state->value_dirty && !state->value.empty() &&
                (new_mode == ValueMode::Default || new_mode == ValueMode::DefaultOn)) {
                const std::string keep = state->value;
                state->value.clear();
                state->value_dirty = false;
                element.set_attribute("value", keep);
            } else if (state && new_mode != ValueMode::Value) {
                state->value.clear();
                state->value_dirty = false;
            } else if (state && old_mode != ValueMode::Value && new_mode == ValueMode::Value) {
                state->value.clear();
                state->value_dirty = false;
            }
        }
    } else if (tag == "option" && name == "selected") {
        if (state && state->selected_dirty) return;
        if (state) state->has_selected = false;
        if (element.has_attribute("selected")) {
            if (auto select = option_select(element)) {
                clear_none(*select);
                if (!is_multiple_select(*select)) {
                    for (const auto& other : select_options(*select)) {
                        if (other.get() == &element) continue;
                        if (FormControlState* other_state = other->form_state()) {
                            if (other_state->has_selected && other_state->selected) other_state->selected = false;
                        }
                    }
                }
            }
        }
    }
}

} // namespace forms

// --- The shorthands on Element ---------------------------------------------------

std::string Element::value() const { return forms::value(*this); }
void Element::set_value(std::string val) { forms::set_value(*this, val); }
bool Element::checked() const {
    return m_tag_name == "option" ? forms::option_selected(*this) : forms::checked(*this);
}
void Element::set_checked(bool c) { forms::set_checked(*this, c); }
bool Element::disabled() const { return forms::is_disabled(*this); }

} // namespace litebird
