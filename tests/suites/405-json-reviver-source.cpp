// A reviver is told the text each value was written as.
//
// ES2025 §25.5.1 and §25.5.5 (Stage 4, "JSON.parse source text access"): the
// third argument of a reviver is a context Record with one field, `source`,
// holding the text that value came from. Without it there is no way to write
// `"1e1000"` back out — the double it parses to is `Infinity`, and
// `JSON.stringify` writes `null` for that — so this is the half of the
// proposal that makes `JSON.rawJSON` useful. Phase 393 added `JSON.rawJSON`
// and recorded this as the other half.
//
// Only a primitive gets a `source`: §25.5.1 makes the Record for a value that
// is not an Object, so an object and an array are given `{}`. The root is the
// exception in the other direction — §25.5.5 makes its context itself, from
// the whole text — so `JSON.parse('{"a":1}', …)` gives the root
// `{"source":"{\"a\":1}"}` and the member `{}`.
//
// Every expected value below is Node v22.22.3's, by evaluating the same
// expression there.
TEST(a_reviver_is_told_the_text_each_value_was_written_as) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // The reviver is called with three arguments, and `JSON.parse` still
    // reports the standard's length of 2.
    assert(run("(function(){var n; JSON.parse('1', function(){n = arguments.length;"
               "return arguments[1];}); return n;})()") == "3");
    assert(run("JSON.parse.length") == "2");

    // A primitive's `source` is the text it was written as, verbatim: `1.0`
    // keeps its point, `-0` its sign and `1e1000` its exponent, which is the
    // whole reason the field exists — `1e1000` is `Infinity` once parsed.
    assert(run("(function(){var s; JSON.parse('1.0', function(k, v, c){s = c.source; return v;});"
               "return s;})()") == "1.0");
    assert(run("(function(){var s; JSON.parse('-0', function(k, v, c){s = c.source; return v;});"
               "return s;})()") == "-0");
    assert(run("(function(){var s; JSON.parse('1e1000', function(k, v, c){s = c.source; return v;});"
               "return s;})()") == "1e1000");
    assert(run("(function(){var s; JSON.parse('123456789012345678901234567890',"
               "function(k, v, c){s = c.source; return v;}); return s;})()")
           == "123456789012345678901234567890");
    // A string keeps the escape it was written with, not the character it means.
    // The JSON text is the four characters `\u00e9` inside quotes, so the
    // JavaScript string holding it needs the backslash escaped once.
    assert(run("(function(){var s; JSON.parse(\"\\\"\\\\u00e9\\\"\", function(k, v, c){"
               "s = c.source; return v;}); return s;})()") == "\"\\u00e9\"");
    assert(run("(function(){var s; JSON.parse('\"a\\\\nb\"', function(k, v, c){s = c.source;"
               "return v;}); return s;})()") == "\"a\\nb\"");
    // A primitive, `true`, `false` and `null` all carry one.
    assert(run("(function(){var s; JSON.parse('null', function(k, v, c){s = c.source; return v;});"
               "return s;})()") == "null");
    // The root is visited too, and an array has no `source`, so the last entry
    // is the empty string — this is Node's own answer, checked there.
    assert(run("(function(){var s = []; JSON.parse('[true,false]', function(k, v, c){"
               "s.push(c.source); return v;}); return s.join();})()") == "true,false,");

    // An object and an array get a context too, and it has no `source`: the
    // Record is only made for a value that is not an Object.
    // `[1]`'s element 0 *is* a primitive and does have one; it is the
    // container that must not.
    assert(run("(function(){var ok; JSON.parse('[[]]', function(k, v, c){"
               "if (k === '0') ok = !('source' in c); return v;}); return ok;})()") == "true");
    assert(run("(function(){var ok; JSON.parse('[{}]', function(k, v, c){"
               "if (k === '0') ok = !('source' in c); return v;}); return ok;})()") == "true");
    assert(run("(function(){var ok; JSON.parse('[1]', function(k, v, c){"
               "if (k === '0') ok = 'source' in c; return v;}); return ok;})()") == "true");
    assert(run("(function(){var s; JSON.parse('{\"a\":[1]}', function(k, v, c){"
               "if (k === 'a') s = Object.getOwnPropertyNames(c).join(); return v;}); return s;})()")
           == "");
    // The root of a JSON object is an object, so it has no `source` either —
    // but the root of a *primitive* does, and it is the whole text.
    assert(run("(function(){var s; JSON.parse('  1  ', function(k, v, c){s = c.source; return v;});"
               "return s;})()") == "1");

    // §25.5.5 step 9: an ordinary object from `%Object.prototype%`, with a
    // writable enumerable configurable `source` and nothing else.
    assert(run("(function(){var ok; JSON.parse('1', function(k, v, c){"
               "ok = Object.getPrototypeOf(c) === Object.prototype; return v;}); return ok;})()")
           == "true");
    assert(run("(function(){var n; JSON.parse('1', function(k, v, c){"
               "n = Object.getOwnPropertyNames(c).join(); return v;}); return n;})()") == "source");
    assert(run("(function(){var d; JSON.parse('1', function(k, v, c){"
               "d = Object.getOwnPropertyDescriptor(c, 'source'); return v;});"
               "return d.value + ',' + d.writable + ',' + d.enumerable + ',' + d.configurable;})()")
           == "1,true,true,true");
    assert(run("(function(){var f; JSON.parse('1', function(k, v, c){f = Object.isFrozen(c);"
               "return v;}); return f;})()") == "false");
    // A fresh context for every call: the same primitive twice is two objects.
    assert(run("(function(){var s = []; JSON.parse('[1,2]', function(k, v, c){s.push(c); return v;});"
               "return s[0] === s[1];})()") == "false");

    // The walk is unchanged: bottom-up, `this` is the holder, and a key the
    // reviver removed before its turn is not visited.
    assert(run("(function(){var s = []; JSON.parse('{\"a\":{\"b\":1}}', function(k, v){"
               "s.push(k || '<root>'); return v;}); return s.join();})()") == "b,a,<root>");
    assert(run("(function(){var r; JSON.parse('[1]', function(k, v, c){"
               "if (k === '0') r = Object.prototype.toString.call(this); return v;}); return r;})()")
           == "[object Array]");
    assert(run("(function(){var seen = []; JSON.parse('{\"a\":1,\"b\":2}', function(k, v, c){"
               "seen.push(k); if (k === 'a') this.b = undefined; return v;});"
               "return seen.join();})()") == "a,b,");

    // And the reason for all of it: a number too large for a double goes back
    // out as the text it came in as. `JSON.stringify(1e1000)` is `null`.
    assert(run("JSON.stringify(1e1000)") == "null");
    assert(run("JSON.stringify(JSON.parse('{\"a\":1e1000}', function(k, v, c){"
               "return typeof v === 'number' ? JSON.rawJSON(c.source) : v;}))")
           == "{\"a\":1e1000}");

    // With no reviver, nothing changes.
    assert(run("JSON.stringify(JSON.parse('[1,{\"a\":2}]'))") == "[1,{\"a\":2}]");
    assert(run("JSON.stringify(JSON.parse('[1]', null))") == "[1]");
}
