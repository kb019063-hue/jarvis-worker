#pragma once

// Installers for the built-in library. Each one populates the prototypes of a
// realm and registers its global bindings; they run once per realm.

#include "litebird/js/JsOps.hpp"

namespace litebird {

void install_object_builtins(JsIntrinsics& realm);      // Object, Function.prototype, Boolean, Symbol, Reflect, globals
void install_error_builtins(JsIntrinsics& realm);       // Error and its subclasses
void install_array_builtins(JsIntrinsics& realm);       // Array, Array.prototype, array iterators
void install_string_builtins(JsIntrinsics& realm);      // String, String.prototype
void install_number_builtins(JsIntrinsics& realm);      // Number, Math, parseInt and friends
void install_collection_builtins(JsIntrinsics& realm);  // Map, Set, WeakMap, WeakSet
void install_regexp_builtins(JsIntrinsics& realm);      // RegExp
void install_date_builtins(JsIntrinsics& realm);        // Date
void install_json_builtins(JsIntrinsics& realm);        // JSON
void install_promise_builtins(JsIntrinsics& realm);     // Promise

/// Milliseconds since the Unix epoch on the clock scripts see: wall-clock time
/// plus whatever the current event loop has skipped (EventLoop::clock_skew).
double js_current_time_ms();

/// Creates an iterator object of the kind `proto` stands for, whose state is
/// `step`: it fills `value` and returns true, or returns false when the
/// sequence is exhausted. `roots` lists the values `step` captured, so the
/// collector keeps them alive. The object's own properties are none: `next` is
/// on `proto`, where a script can replace it, and `proto` must have had
/// js_install_iterator_prototype() called on it.
JsValue js_make_iterator(std::function<bool(JsValue& value)> step, std::vector<JsValue> roots = {},
                         JsObject* proto = nullptr);

/// Gives an iterator prototype its `next` — the one function every iterator of
/// that kind uses, which reads the state from `this` — and, when `tag` is not
/// null, a `Symbol.toStringTag` naming the kind, so that
/// `Object.prototype.toString.call(it)` is `[object Array Iterator]`.
void js_install_iterator_prototype(JsObject* proto, const char* tag);

/// A date and time as `Date.parse` reads it: milliseconds since the epoch,
/// NaN for text that is not a date.
double js_parse_date_string(const std::string& text);

// --- RegExp entry points used by String.prototype methods -----------------
// The subject is passed as the string value's own text, so that a pattern
// applied again and again to one long string (an exec() loop) does not copy
// or convert the string on every call.
inline constexpr const char* kSymbolMatchKey    = "@@match";
inline constexpr const char* kSymbolMatchAllKey = "@@matchAll";
inline constexpr const char* kSymbolReplaceKey  = "@@replace";
inline constexpr const char* kSymbolSearchKey   = "@@search";
inline constexpr const char* kSymbolSplitKey    = "@@split";
inline constexpr const char* kSymbolSpeciesKey  = "@@species";

bool js_is_regexp(const JsValue& value);
/// Validates a RegExp pattern and flags at parse time (no JS heap allocation).
/// Returns true if valid; otherwise writes the SyntaxError reason into `error_out`.
bool js_regexp_validate(const std::string& pattern, const std::string& flags, std::string& error_out);
JsValue js_regexp_create(const std::string& pattern, const std::string& flags);
/// String.prototype.replace / replaceAll with a regular expression.
JsValue js_regexp_replace(const JsValue& regex, const JsString& subject, const JsValue& replacement);
/// String.prototype.match.
JsValue js_regexp_match(const JsValue& regex, const JsString& subject);
/// String.prototype.matchAll: an iterator of match arrays.
JsValue js_regexp_match_all(const JsValue& regex, const JsString& subject);
/// String.prototype.search: index of the first match in UTF-16 units, or -1.
double js_regexp_search(const JsValue& regex, const JsString& subject);
/// String.prototype.split with a regular expression separator.
JsValue js_regexp_split(const JsValue& regex, const JsString& subject, double limit);

/// Expands `$&`, `$1`, `$<name>`, "$`", `$'` and `$$` in a replacement template.
/// `captures[0]` is the whole match; an unmatched group is marked absent.
struct JsReplaceCapture {
    bool matched{false};
    std::u16string text;
};
std::u16string js_expand_replacement(const std::u16string& replacement, const Utf16View& subject,
                                     size_t position, const std::vector<JsReplaceCapture>& captures,
                                     const std::vector<std::pair<std::string, size_t>>& named_groups);

} // namespace litebird
