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

/// Creates an iterator object whose next() calls `step`. `step` fills `value`
/// and returns true, or returns false when the sequence is exhausted. `roots`
/// lists the values `step` captured, so the collector keeps them alive.
JsValue js_make_iterator(std::function<bool(JsValue& value)> step, std::vector<JsValue> roots = {});

// --- RegExp entry points used by String.prototype methods -----------------
bool js_is_regexp(const JsValue& value);
JsValue js_regexp_create(const std::string& pattern, const std::string& flags);
/// String.prototype.replace / replaceAll with a regular expression.
JsValue js_regexp_replace(const JsValue& regex, const std::string& subject, const JsValue& replacement);
/// String.prototype.match.
JsValue js_regexp_match(const JsValue& regex, const std::string& subject);
/// String.prototype.matchAll: an iterator of match arrays.
JsValue js_regexp_match_all(const JsValue& regex, const std::string& subject);
/// String.prototype.search: index of the first match in UTF-16 units, or -1.
double js_regexp_search(const JsValue& regex, const std::string& subject);
/// String.prototype.split with a regular expression separator.
JsValue js_regexp_split(const JsValue& regex, const std::string& subject, double limit);

/// Expands `$&`, `$1`, `$<name>`, "$`", `$'` and `$$` in a replacement template.
/// `captures[0]` is the whole match; an unmatched group is marked absent.
struct JsReplaceCapture {
    bool matched{false};
    std::u16string text;
};
std::u16string js_expand_replacement(const std::u16string& replacement, const std::u16string& subject,
                                     size_t position, const std::vector<JsReplaceCapture>& captures,
                                     const std::vector<std::pair<std::string, size_t>>& named_groups);

} // namespace litebird
