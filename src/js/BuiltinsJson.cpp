// JSON.parse and JSON.stringify (ECMA-262 25.5).

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/common/StackLimit.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    JsValue parse() {
        skip_space();
        JsValue value = parse_value();
        skip_space();
        if (m_pos != m_text.size()) fail();
        return value;
    }

private:
    [[noreturn]] void fail() const {
        if (m_pos >= m_text.size()) {
            js_throw_error(JsErrorKind::SyntaxError, "Unexpected end of JSON input");
        }
        // Report the offending character, taking care not to split a UTF-8 sequence.
        size_t len = 1;
        unsigned char c = static_cast<unsigned char>(m_text[m_pos]);
        if (c >= 0xF0) len = 4; else if (c >= 0xE0) len = 3; else if (c >= 0xC0) len = 2;
        js_throw_error(JsErrorKind::SyntaxError, "Unexpected token '" + m_text.substr(m_pos, len) +
                                                     "' in JSON at position " + std::to_string(m_pos));
    }

    void skip_space() {
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++m_pos;
            else break;
        }
    }

    bool consume_literal(const char* literal) {
        size_t n = std::char_traits<char>::length(literal);
        if (m_text.compare(m_pos, n, literal) != 0) return false;
        m_pos += n;
        return true;
    }

    JsValue parse_value() {
        // Hostile input can nest arbitrarily deep; stop before the native stack does.
        if (stack_is_exhausted()) js_throw_range_error("JSON is nested too deeply");
        if (m_pos >= m_text.size()) fail();
        switch (m_text[m_pos]) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return JsValue::make_string(parse_string());
            case 't': if (consume_literal("true")) return JsValue::make_boolean(true); fail();
            case 'f': if (consume_literal("false")) return JsValue::make_boolean(false); fail();
            case 'n': if (consume_literal("null")) return JsValue::make_null(); fail();
            default: return parse_number();
        }
    }

    JsValue parse_number() {
        size_t start = m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '-') ++m_pos;
        if (m_pos >= m_text.size()) fail();
        if (m_text[m_pos] == '0') {
            ++m_pos;                                   // a leading zero stands alone
        } else if (m_text[m_pos] >= '1' && m_text[m_pos] <= '9') {
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        } else {
            fail();
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            if (m_pos >= m_text.size() || !std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) fail();
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos;
            if (m_pos >= m_text.size() || !std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) fail();
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        }
        return JsValue::make_number(std::strtod(m_text.substr(start, m_pos - start).c_str(), nullptr));
    }

    unsigned read_hex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (m_pos >= m_text.size()) fail();
            char c = m_text[m_pos];
            int digit = (c >= '0' && c <= '9') ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (digit < 0) fail();
            value = value * 16 + static_cast<unsigned>(digit);
            ++m_pos;
        }
        return value;
    }

    std::string parse_string() {
        ++m_pos;        // opening quote
        std::string out;
        while (true) {
            if (m_pos >= m_text.size()) fail();
            unsigned char c = static_cast<unsigned char>(m_text[m_pos]);
            if (c == '"') {
                ++m_pos;
                return out;
            }
            if (c < 0x20) fail();              // raw control characters are not allowed
            if (c != '\\') {
                out += static_cast<char>(c);
                ++m_pos;
                continue;
            }
            ++m_pos;
            if (m_pos >= m_text.size()) fail();
            char esc = m_text[m_pos++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = read_hex4();
                    // Join a surrogate pair written as two escapes.
                    if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 1 < m_text.size() &&
                        m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                        size_t save = m_pos;
                        m_pos += 2;
                        uint32_t low = read_hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else m_pos = save;
                    }
                    js_append_code_point(out, cp);
                    break;
                }
                default:
                    --m_pos;
                    fail();
            }
        }
    }

    JsValue parse_array() {
        ++m_pos;
        JsValue array = JsValue::make_array();
        skip_space();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return array;
        }
        while (true) {
            skip_space();
            array.object_val->array_elements.push_back(parse_value());
            skip_space();
            if (m_pos >= m_text.size()) fail();
            if (m_text[m_pos] == ',') { ++m_pos; continue; }
            if (m_text[m_pos] == ']') { ++m_pos; return array; }
            fail();
        }
    }

    JsValue parse_object() {
        ++m_pos;
        JsValue object = JsValue::make_object();
        skip_space();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return object;
        }
        while (true) {
            skip_space();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') fail();
            std::string key = parse_string();
            skip_space();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') fail();
            ++m_pos;
            skip_space();
            // define, not set: "__proto__" must become an ordinary own property.
            object.define_property(key, parse_value(), PropertyMap::Default);
            skip_space();
            if (m_pos >= m_text.size()) fail();
            if (m_text[m_pos] == ',') { ++m_pos; continue; }
            if (m_text[m_pos] == '}') { ++m_pos; return object; }
            fail();
        }
    }

    const std::string& m_text;
    size_t m_pos{0};
};

/// Applies a reviver bottom-up, as JSON.parse(text, reviver) requires.
JsValue internalize(JsValue holder, const std::string& key, const JsValue& reviver) {
    if (stack_is_exhausted()) js_throw_range_error("JSON is nested too deeply");
    JsValue value = holder.get_property(key);
    if (value.is_object_like() && !value.is_function()) {
        std::vector<std::string> keys;
        if (!litebird::dom::cross_origin_own_keys(value, false, keys)) keys = value.own_keys();
        for (const auto& child : keys) {
            JsValue revived = internalize(value, child, reviver);
            if (revived.is_undefined()) value.delete_property(child);
            else value.set_property(child, revived);
        }
    }
    return reviver.call_with_this(holder, {JsValue::make_string(key), value});
}

// ---------------------------------------------------------------------------
// Serialising
// ---------------------------------------------------------------------------

void quote_into(std::string& out, std::string_view s) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else if (c == 0xED && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) >= 0xA0) {
                    // A lone surrogate (stored as three bytes) is written as an
                    // escape so the output stays well-formed.
                    unsigned unit = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                                    (static_cast<unsigned char>(s[i + 2]) & 0x3F);
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", unit);
                    out += buffer;
                    i += 2;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

class JsonWriter {
public:
    JsonWriter(const JsValue& replacer, const JsValue& space) {
        if (replacer.is_function()) {
            m_replacer = replacer;
        } else if (replacer.is_array()) {
            m_has_allow_list = true;
            for (const auto& item : replacer.object_val->array_elements) {
                if (item.type == JsType::String || item.type == JsType::Number) {
                    std::string key = js_to_string(item);
                    if (std::find(m_allow_list.begin(), m_allow_list.end(), key) == m_allow_list.end()) {
                        m_allow_list.push_back(key);
                    }
                }
            }
        }
        if (space.type == JsType::Number) {
            int count = static_cast<int>(std::min(10.0, std::max(0.0, js_to_integer(space.number_val))));
            m_gap.assign(static_cast<size_t>(count), ' ');
        } else if (space.type == JsType::String) {
            m_gap = space.string_val.substring(0, 10).str();      // the first ten code units
        }
    }

    /// Returns false when the value has no JSON form (undefined, functions).
    bool write_property(const JsValue& holder, const std::string& key, std::string& out) {
        JsValue value = holder.get_property(key);
        return write_value(holder, key, value, out);
    }

    bool write_value(const JsValue& holder, const std::string& key, JsValue value, std::string& out) {
        if (stack_is_exhausted()) js_throw_range_error("Value is nested too deeply to serialise");
        if (value.is_object_like()) {
            JsValue to_json = value.get_property("toJSON");
            if (to_json.is_function()) value = to_json.call_with_this(value, {JsValue::make_string(key)});
        }
        if (m_replacer.is_function()) {
            value = m_replacer.call_with_this(holder, {JsValue::make_string(key), value});
        }
        // `new String("a")` and friends serialise as the value they wrap.
        if (const JsValue* inner = js_wrapped_primitive(value); inner && inner->type != JsType::Symbol) {
            value = *inner;
        }

        switch (value.type) {
            case JsType::Null: out += "null"; return true;
            case JsType::Boolean: out += value.bool_val ? "true" : "false"; return true;
            case JsType::String: quote_into(out, value.string_val.view()); return true;
            case JsType::Number:
                out += std::isfinite(value.number_val) ? js_number_to_string(value.number_val) : "null";
                return true;
            case JsType::Undefined:
            case JsType::Symbol:
            case JsType::Function:
            case JsType::NativeFunction:
                return false;
            default:
                break;
        }

        const void* id = value.identity();
        if (std::find(m_stack.begin(), m_stack.end(), id) != m_stack.end()) {
            js_throw_type_error("Converting circular structure to JSON");
        }
        m_stack.push_back(id);
        struct Pop { std::vector<const void*>& s; ~Pop() { s.pop_back(); } } pop{m_stack};

        const std::string outer_indent = m_indent;
        m_indent += m_gap;
        if (value.is_array()) {
            const size_t length = value.object_val->array_elements.size();
            if (length == 0) {
                out += "[]";
            } else {
                out += '[';
                for (size_t i = 0; i < length; ++i) {
                    if (i) out += ',';
                    newline(out, m_indent);
                    JsValue element = i < value.object_val->array_elements.size()
                        ? value.object_val->array_elements[i] : JsValue::make_undefined();
                    if (!write_value(value, std::to_string(i), element, out)) out += "null";
                }
                newline(out, outer_indent);
                out += ']';
            }
        } else {
            std::vector<std::string> keys;
            if (m_has_allow_list) {
                keys = m_allow_list;
            } else if (!litebird::dom::cross_origin_own_keys(value, false, keys)) {
                keys = value.own_keys();
            }
            bool any = false;
            out += '{';
            for (const auto& child : keys) {
                if (m_has_allow_list && !value.has_own_property(child)) continue;
                std::string member;
                if (!write_property(value, child, member)) continue;
                if (any) out += ',';
                any = true;
                newline(out, m_indent);
                quote_into(out, child);
                out += m_gap.empty() ? ":" : ": ";
                out += member;
            }
            if (any) newline(out, outer_indent);
            out += '}';
        }
        m_indent = outer_indent;
        return true;
    }

private:
    void newline(std::string& out, const std::string& indent) const {
        if (m_gap.empty()) return;
        out += '\n';
        out += indent;
    }

    JsValue m_replacer;
    bool m_has_allow_list{false};
    std::vector<std::string> m_allow_list;
    std::string m_gap;
    std::string m_indent;
    std::vector<const void*> m_stack;
};

} // namespace

void install_json_builtins(JsIntrinsics& realm) {
    JsValue json = JsValue::make_object();

    js_define_method(json, "parse", 2, [](Args args) {
        std::string text = js_to_string(js_arg(args, 0));
        JsonParser parser(text);
        JsValue value = parser.parse();
        const JsValue& reviver = js_arg(args, 1);
        if (reviver.is_function()) {
            JsValue root = JsValue::make_object();
            root.define_property("", value, PropertyMap::Default);
            return internalize(root, "", reviver);
        }
        return value;
    });

    js_define_method(json, "stringify", 3, [](Args args) {
        JsonWriter writer(js_arg(args, 1), js_arg(args, 2));
        JsValue wrapper = JsValue::make_object();
        wrapper.define_property("", js_arg(args, 0), PropertyMap::Default);
        std::string out;
        if (!writer.write_property(wrapper, "", out)) return JsValue::make_undefined();
        return JsValue::make_string(out);
    });

    json.define_property("@@toStringTag", JsValue::make_string("JSON"), PropertyMap::Configurable);
    realm.add_global("JSON", json);
}

} // namespace litebird
