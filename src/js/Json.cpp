#include "litebird/js/Json.hpp"
#include "litebird/js/Heap.hpp"
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iomanip>

namespace litebird {

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : m_src(input) {}

    JsValue parse() {
        skip_whitespace();
        if (m_pos >= m_src.size()) {
            throw std::runtime_error("Unexpected end of JSON input");
        }
        JsValue res = parse_value();
        skip_whitespace();
        return res;
    }

private:
    std::string_view m_src;
    size_t m_pos{0};

    char peek() const {
        return (m_pos < m_src.size()) ? m_src[m_pos] : '\0';
    }

    char advance() {
        return (m_pos < m_src.size()) ? m_src[m_pos++] : '\0';
    }

    void skip_whitespace() {
        while (m_pos < m_src.size() && (m_src[m_pos] == ' ' || m_src[m_pos] == '\t' ||
                                        m_src[m_pos] == '\r' || m_src[m_pos] == '\n')) {
            m_pos++;
        }
    }

    JsValue parse_value() {
        js_check_stack();
        skip_whitespace();
        char c = peek();
        if (c == '"') return parse_string();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_boolean();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        throw std::runtime_error(std::string("Unexpected character in JSON: '") + c + "'");
    }

    JsValue parse_string() {
        advance(); // skip opening quote
        std::string s;
        while (m_pos < m_src.size()) {
            char c = advance();
            if (c == '"') {
                return JsValue::make_string(s);
            }
            if (c == '\\') {
                if (m_pos >= m_src.size()) throw std::runtime_error("Unterminated escape sequence in JSON string");
                char esc = advance();
                switch (esc) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u': {
                        if (m_pos + 4 > m_src.size()) throw std::runtime_error("Invalid unicode escape");
                        std::string hex_str(m_src.substr(m_pos, 4));
                        m_pos += 4;
                        try {
                            int code = std::stoi(hex_str, nullptr, 16);
                            if (code < 128) s += static_cast<char>(code);
                            else s += '?';
                        } catch (...) {
                            s += '?';
                        }
                        break;
                    }
                    default: s += esc; break;
                }
            } else {
                s += c;
            }
        }
        throw std::runtime_error("Unterminated JSON string");
    }

    JsValue parse_number() {
        size_t start = m_pos;
        if (peek() == '-') advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        if (peek() == '.') {
            advance();
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }

        std::string num_str(m_src.substr(start, m_pos - start));
        try {
            double val = std::stod(num_str);
            return JsValue::make_number(val);
        } catch (...) {
            throw std::runtime_error("Invalid number in JSON: " + num_str);
        }
    }

    JsValue parse_boolean() {
        if (m_src.substr(m_pos, 4) == "true") {
            m_pos += 4;
            return JsValue::make_boolean(true);
        }
        if (m_src.substr(m_pos, 5) == "false") {
            m_pos += 5;
            return JsValue::make_boolean(false);
        }
        throw std::runtime_error("Invalid boolean literal in JSON");
    }

    JsValue parse_null() {
        if (m_src.substr(m_pos, 4) == "null") {
            m_pos += 4;
            return JsValue::make_null();
        }
        throw std::runtime_error("Invalid null literal in JSON");
    }

    JsValue parse_array() {
        advance(); // skip '['
        skip_whitespace();
        std::vector<JsValue> elements;
        if (peek() == ']') {
            advance();
            return JsValue::make_array(elements);
        }

        while (true) {
            elements.push_back(parse_value());
            skip_whitespace();
            if (peek() == ']') {
                advance();
                break;
            }
            if (peek() == ',') {
                advance();
                skip_whitespace();
            } else {
                throw std::runtime_error("Expected ',' or ']' in JSON array");
            }
        }
        return JsValue::make_array(elements);
    }

    JsValue parse_object() {
        advance(); // skip '{'
        skip_whitespace();
        JsValue obj = JsValue::make_object();
        if (peek() == '}') {
            advance();
            return obj;
        }

        while (true) {
            skip_whitespace();
            if (peek() != '"') throw std::runtime_error("Expected string key in JSON object");
            JsValue key_val = parse_string();
            std::string key = key_val.string_val.str();

            skip_whitespace();
            if (peek() != ':') throw std::runtime_error("Expected ':' after key in JSON object");
            advance(); // skip ':'

            JsValue val = parse_value();
            obj.set_property(key, val);

            skip_whitespace();
            if (peek() == '}') {
                advance();
                break;
            }
            if (peek() == ',') {
                advance();
                skip_whitespace();
            } else {
                throw std::runtime_error("Expected ',' or '}' in JSON object");
            }
        }
        return obj;
    }
};

JsValue Json::parse(std::string_view json_str) {
    JsonParser parser(json_str);
    return parser.parse();
}

static std::string escape_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 32) {
                    std::stringstream ss;
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    out += ss.str();
                } else {
                    out += c;
                }
                break;
        }
    }
    out += '"';
    return out;
}

std::string Json::stringify(const JsValue& value) {
    // No cycle detection here: a structure that contains itself ends in this
    // check, as a RangeError, instead of running off the end of the stack.
    js_check_stack();
    switch (value.type) {
        case JsType::Undefined:
        case JsType::Null:
            return "null";
        case JsType::Boolean:
            return value.bool_val ? "true" : "false";
        case JsType::Number: {
            if (value.number_val == static_cast<int64_t>(value.number_val)) {
                return std::to_string(static_cast<int64_t>(value.number_val));
            }
            std::stringstream ss;
            ss << value.number_val;
            return ss.str();
        }
        case JsType::String:
            return escape_string(value.string_val.str());
        case JsType::Object: {
            if (value.object_val && value.object_val->is_array) {
                std::string res = "[";
                for (size_t i = 0; i < value.object_val->array_elements.size(); ++i) {
                    res += stringify(value.object_val->array_elements[i]);
                    if (i + 1 < value.object_val->array_elements.size()) res += ",";
                }
                res += "]";
                return res;
            }

            std::string res = "{";
            bool first = true;
            if (value.object_val) {
                for (const auto& [k, v] : value.object_val->properties) {
                    if (k.rfind("__", 0) == 0) continue; // skip internal metadata
                    if (v.type == JsType::Undefined || v.type == JsType::Function || v.type == JsType::NativeFunction) continue;
                    if (!first) res += ",";
                    first = false;
                    res += escape_string(k) + ":" + stringify(v);
                }
            }
            res += "}";
            return res;
        }
        default:
            return "null";
    }
}

} // namespace litebird
