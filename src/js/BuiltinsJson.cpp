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
#include <map>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

/// The text a value was written as, for §25.5.1's reviver context.
///
/// It is the slice of the source between the value's first and last character,
/// so a number keeps `1.0` and `-0` and a string keeps its escapes. An object
/// or an array has no `source` at all: §25.5.1's Record is only made for a
/// value that is not an Object, which is what `primitive` records.
struct ParsedSource {
    std::string text;        ///< empty when the value is an object or an array
    bool primitive{false};
};

/// Where a value was written, keyed by the holder it is read from and the key
/// it is read under — which is the pair `InternalizeJSONProperty` walks by, and
/// is what a primitive needs: it has no identity of its own to key on, and the
/// property that holds it does.
///
/// A side table rather than a slot on each value: the parser makes ordinary
/// objects and arrays, and a field on every `JsObject` for a feature only
/// `JSON.parse` uses would be paid for by the whole engine. The entries live
/// until the call returns.
using SourceKey = std::pair<const void*, std::string>;
using SourceMap = std::map<SourceKey, ParsedSource>;

/// §25.5.2's InternalizeJSONProperty, with the context of §25.5.1.
///
/// `source` is the text this value was written as, or null for an object or an
/// array. §25.5.1 makes the context a Record with one field, `source`, and
/// §25.5.5 step 9 builds it as `OrdinaryObjectCreate(%Object.prototype%)` with
/// `CreateDataPropertyOrThrow`, which is what the descriptor below is.
class JsonParser {
public:
    /// `record_sources` is false for a parse without a reviver: the spans are
    /// only read by the reviver walk, so §25.5.5's step order — ToString,
    /// then IsCallable, then the parse — lets the parser skip them entirely.
    /// A 2.1 MB document (200,000 primitives) parsed without one measured
    /// 4,570 ms for ten parses with the recording on and about 1,900 ms
    /// without; a parse with a reviver keeps the recording and is unchanged.
    explicit JsonParser(const std::string& text, bool record_sources = true)
        : m_text(text), m_record(record_sources) {}

    JsValue parse() {
        skip_space();
        JsValue value = parse_value();
        m_root_source = last_source;        // the root's, before `skip_space` moves m_pos
        skip_space();
        if (m_pos != m_text.size()) fail();
        return value;
    }

    /// The source of the root value, read after `parse()`.
    const ParsedSource& root_source() const { return m_root_source; }

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

    /// Reads one value and remembers the span of text it was written as.
    JsValue parse_value() {
        // Hostile input can nest arbitrarily deep; stop before the native stack does.
        if (stack_is_exhausted()) js_throw_range_error("JSON is nested too deeply");
        if (m_pos >= m_text.size()) fail();
        const size_t start = m_pos;
        const char first = m_text[m_pos];
        JsValue value = parse_one(first);
        // An object or an array is not a primitive, so it has no `source`:
        // `JSON.parse('{"a":1}', ...)` gives the reviver `{}` for it and the
        // `{"a":1}` only for the root, which §25.5.1 makes separately. This
        // engine's parser is the one place that knows which is which.
        if (first == '{' || first == '[') {
            last_source = ParsedSource{{}, false};
        } else {
            last_source = ParsedSource{m_text.substr(start, m_pos - start), true};
        }
        return value;
    }

    JsValue parse_one(char first) {
        switch (first) {
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
            JsValue element = parse_value();
            const size_t index = array.object_val->array_elements.size();
            array.object_val->array_elements.push_back(element);
            remember(array, std::to_string(index));
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
            remember(object, key);
            skip_space();
            if (m_pos >= m_text.size()) fail();
            if (m_text[m_pos] == ',') { ++m_pos; continue; }
            if (m_text[m_pos] == '}') { ++m_pos; return object; }
            fail();
        }
    }

    const std::string& m_text;
    size_t m_pos{0};
public:
    /// The span of the value `parse_value` last returned; `parse_into` reads it
    /// immediately, before the next value overwrites it.
    ParsedSource last_source;
    /// The spans recorded so far, keyed by the holder and key each value was
    /// found under. Handed to the reviver walk by `take_sources`.
    SourceMap take_sources() { return std::move(m_sources); }
private:
    /// Records the value just parsed under the holder it has been put into.
    /// A duplicate key's span is the **last** one recorded, because the
    /// property the reviver walks holds the last writer's value: the
    /// reviver is visited once per property, with that value, and its
    /// context says what *that* value was written as. Node v22.22.3
    /// measured: `{"a":1,"a":22}` gives value 22 with source "22";
    /// `{"a":null,"a":5}` gives 5 with "5"; three duplicates keep the
    /// third. (Phase 471 asserted the first and cited this same Node —
    /// finding 541 has the probes.) A discarded earlier object leaves its
    /// spans keyed by its own identity, which no walk visits.
    void remember(const JsValue& holder, const std::string& key) {
        if (!m_record) return;
        if (const void* id = holder.identity()) {
            m_sources[SourceKey{id, key}] = last_source;
        }
    }

    SourceMap m_sources;
    ParsedSource m_root_source;
    bool m_record{true};
};

JsValue internalize(JsValue holder, const std::string& key, const JsValue& reviver,
                    const SourceMap& sources) {
    if (stack_is_exhausted()) js_throw_range_error("JSON is nested too deeply");
    JsValue value = holder.get_property(key);
    const ParsedSource* source = nullptr;
    if (const void* holder_id = holder.identity()) {
        const auto found = sources.find(SourceKey{holder_id, key});
        if (found != sources.end()) source = &found->second;
    }
    if (value.is_object_like() && !value.is_function()) {
        std::vector<std::string> keys;
        if (!litebird::dom::cross_origin_own_keys(value, false, keys)) keys = value.own_keys();
        for (const auto& child : keys) {
            JsValue revived = internalize(value, child, reviver, sources);
            if (revived.is_undefined()) value.delete_property(child);
            else value.set_property(child, revived);
        }
    }
    JsValue context = JsValue::make_object();
    if (source && source->primitive) {
        context.define_property("source", JsValue::make_string(source->text), PropertyMap::Default);
    }
    return reviver.call_with_this(holder, {JsValue::make_string(key), value, context});
}

// ---------------------------------------------------------------------------
// Serialising
/// The text a `JSON.rawJSON` object carries, and whether `value` is one.
///
/// The slot is a real internal one, in the heap cell, not a property: a plain
/// object that happens to have `rawJSON` of its own is not one (Node says
/// `JSON.isRawJSON({rawJSON: "1"})` is false), nor is one that inherits the
/// name, nor one a script gives a `%`-prefixed property to. Only the object
/// `JSON.rawJSON` made has it, which is what §25.5.3.1's `[[IsRawJSON]]`
/// means by an internal slot.
struct RawJsonText {
    std::string text;
};

/// Whether `value` is an object `JSON.rawJSON` made.
bool is_raw_json(const JsValue& value) {
    // `internal` is a `shared_ptr<void>`: the owner names the concrete type,
    // which is why every other slot in the engine is read the same way — by
    // knowing what it holds. A cell that some other built-in made has a
    // different `internal`, and `rawJSON` of its own was never a slot.
    JsObject* cell = value.object_val;
    return cell != nullptr && static_cast<bool>(cell->internal) &&
           cell->raw_json_slot;
}

/// The text of a raw JSON object; the caller has checked `is_raw_json`.
const std::string& raw_json_text(const JsValue& value) {
    return static_cast<const RawJsonText*>(value.object_val->internal.get())->text;
}

/// §25.5.1.1's grammar, for the text `JSON.rawJSON` accepts.
///
/// The standard asks for a JSON *number*, `true`, `false`, `null` or a JSON
/// *string* — not an object and not an array — with no space around it.
/// `JsonParser` is not reused for the whole job: it reads any JSON value, so
/// it would accept `{}` and `[1]`, both of which Node refuses here ("Unexpected
/// token '{'"). It is not needed for the string case either, where the answer
/// is the quoted text as it stands.
bool raw_json_accepts(const std::string& text) {
    if (text.empty()) return false;
    if (text == "true" || text == "false" || text == "null") return true;
    // A JSON string: the escape grammar is `JsonParser`'s, and parsing the
    // whole text as one value is what holds it to it.
    if (text.front() == '"') {
        try {
            return JsonParser(text).parse().type == JsType::String;
        } catch (...) {
            return false;
        }
    }
    // A JSON number: `-?` `0` | `[1-9][0-9]*` (`.` `[0-9]+`)? (`[eE]` `[+-]?` `[0-9]+`)?
    // `+1`, `01`, `.5`, `5.`, `1e` and `Infinity` are the cases this rejects
    // and Node rejects too.
    size_t i = 0;
    if (text[i] == '-') ++i;
    if (i >= text.size()) return false;
    if (text[i] == '0') {
        ++i;                                    // a leading zero is the whole integer part
    } else if (text[i] >= '1' && text[i] <= '9') {
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
    } else {
        return false;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        const size_t digits = i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        if (i == digits) return false;          // `1.` is not a number
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        const size_t digits = i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        if (i == digits) return false;          // `1e` is not a number
    }
    return i == text.size();                    // and nothing may follow it
}

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
        // §25.5.2's SerializeJSONProperty asks for `[[IsRawJSON]]` here, after
        // `toJSON` and the replacer have each had their turn (a replacer that
        // returns another value replaces the raw object first) and before
        // `[[IsCallable]]`-style branches. Nothing is quoted and nothing is
        // reformatted: the text the object was made with is what goes out, so
        // `1e1000` — a number a `double` cannot hold — survives the round trip.
        if (is_raw_json(value)) { out += raw_json_text(value); return true; }

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
            // The array's length, not its storage: a length that names places
            // nothing was written to holds no elements (Phase 288), and JSON
            // writes those places as `null` exactly as it writes a hole.
            const size_t length = static_cast<size_t>(
                js_to_number(value.get_property("length")));
            if (length == 0) {
                out += "[]";
            } else {
                out += '[';
                for (size_t i = 0; i < length; ++i) {
                    if (i) out += ',';
                    newline(out, m_indent);
                    // Each place is read as `a[i]` reads it, which is the
                    // standard's `Get`. A hole reads `undefined` unless the
                    // index has an accessor of its own or the prototype has
                    // the index, and `undefined` has nothing to write and
                    // becomes null.
                    JsValue element = js_array_get(value, i);
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
        // §25.5.5's order: the text is read, the reviver is asked, then the
        // text is parsed. Asking first is what lets the parser skip the
        // span recording a parse without a reviver would never read.
        const JsValue& reviver = js_arg(args, 1);
        const bool with_reviver = reviver.is_function();
        JsonParser parser(text, with_reviver);
        JsValue value = parser.parse();
        if (with_reviver) {
            // The root's context is built here, from the text the whole call
            // was given; every other value's is remembered by the parser as it
            // went. `parse_value` recorded the root before `parse` skipped the
            // trailing space, so `JSON.parse(' 1 ')` gives the reviver `"1"`.
            SourceMap sources = parser.take_sources();
            JsValue root = JsValue::make_object();
            if (const void* id = root.identity()) sources[SourceKey{id, ""}] = parser.root_source();
            root.define_property("", value, PropertyMap::Default);
            return internalize(root, "", reviver, sources);
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

    // §25.5.3.1. The text is held to the grammar *before* the object is made,
    // so a rejected call leaves nothing behind: the answer is a SyntaxError
    // and never a TypeError, which is Node's answer too.
    js_define_method(json, "rawJSON", 1, [](Args args) {
        const std::string text = js_to_string(js_arg(args, 0));
        if (!raw_json_accepts(text)) {
            js_throw_error(JsErrorKind::SyntaxError, "\"" + text + "\" is not valid JSON");
        }
        // The prototype is null: Node answers `Object.getPrototypeOf(JSON.rawJSON("42"))`
        // with `null`, and it is why `JSON.stringify` spreads one as its own
        // `rawJSON` property and nothing else.
        JsValue result = JsValue::make_object();
        result.object_val->prototype = nullptr;
        result.define_property("rawJSON", JsValue::make_string(text),
                               static_cast<unsigned>(PropertyMap::Enumerable));
        // Non-writable, non-configurable and enumerable, as Node reports it;
        // sealed against anything being added. The prototype and the cell's
        // other fields are set directly rather than through `Object.freeze`,
        // which would ask the realm for the function and could throw.
        result.object_val->extensible = false;
        result.object_val->sealed = true;
        result.object_val->frozen = true;
        result.object_val->internal = std::make_shared<RawJsonText>(RawJsonText{text});
        result.object_val->raw_json_slot = true;
        return result;
    });

    // §25.5.3.2. Asks the internal slot, never the property: a forged
    // `{rawJSON: "1"}` is false, and so is one that inherits the name.
    js_define_method(json, "isRawJSON", 1, [](Args args) {
        return JsValue::make_boolean(is_raw_json(js_arg(args, 0)));
    });

    json.define_property("@@toStringTag", JsValue::make_string("JSON"), PropertyMap::Configurable);
    realm.add_global("JSON", json);
}

} // namespace litebird
