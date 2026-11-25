// JSON.rawJSON and JSON.isRawJSON write a number no double can hold.
//
// ES2025 §25.5.3.1 and §25.5.3.2 (Stage 4, "JSON.parse source text access").
// `JSON.stringify(1e1000)` is `"null"`, because 1e1000 is not a finite
// `double`; the point of `JSON.rawJSON` is that a number written as text
// survives a round trip untouched, so `JSON.stringify(JSON.rawJSON("1e1000"))`
// is `"1e1000"`. Neither method existed: `typeof JSON.rawJSON` was
// `"undefined"`.
//
// What is asserted here is not only that the text comes out. The slot is a
// real internal one, so the suite separates it from the three fakes a
// property-shaped implementation would accept: an object that has `rawJSON`
// of its own, one that inherits the name, and one a script gave a
// `%`-prefixed property to. `JSON.stringify` must ignore all three and
// `JSON.isRawJSON` must answer false for all three. A version that read the
// property would pass every other assertion in this file and fail those.
//
// Every expected value below is Node v22.22.3's, by evaluating the same
// expression there.
TEST(a_raw_number_survives_json_stringify) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // The two methods exist, with the standard's length and name.
    assert(run("typeof JSON.rawJSON") == "function");
    assert(run("typeof JSON.isRawJSON") == "function");
    assert(run("JSON.rawJSON.length") == "1");
    assert(run("JSON.isRawJSON.length") == "1");
    assert(run("JSON.rawJSON.name") == "rawJSON");
    assert(run("JSON.isRawJSON.name") == "isRawJSON");

    // The number 1e1000 is text a `double` cannot hold. Without the method the
    // engine writes `null` for it, which is why the case is worth having.
    assert(run("JSON.stringify(1e1000)") == "null");
    assert(run("JSON.stringify(JSON.rawJSON('1e1000'))") == "1e1000");
    assert(run("JSON.stringify({a: JSON.rawJSON('1e1000')})") == "{\"a\":1e1000}");
    assert(run("JSON.stringify([{x: JSON.rawJSON('1e999')}])") == "[{\"x\":1e999}]");

    // The text is written as it stands: `1.0` keeps its trailing zero and a
    // quoted string keeps its quotes, because neither is re-serialised.
    assert(run("JSON.stringify(JSON.rawJSON('1.0'))") == "1.0");
    assert(run("JSON.stringify(JSON.rawJSON('1E+5'))") == "1E+5");
    assert(run("JSON.stringify(JSON.rawJSON('\"a\"'))") == "\"a\"");
    assert(run("JSON.stringify(JSON.rawJSON('null'))") == "null");
    assert(run("JSON.stringify(JSON.rawJSON('true'))") == "true");

    // §25.5.3.1's object: a null prototype, one enumerable property that is
    // neither writable nor configurable, sealed, and not extensible.
    assert(run("Object.getPrototypeOf(JSON.rawJSON('42')) === null") == "true");
    assert(run("Object.isFrozen(JSON.rawJSON('42'))") == "true");
    assert(run("Object.isExtensible(JSON.rawJSON('42'))") == "false");
    assert(run("Object.getOwnPropertyNames(JSON.rawJSON('1')).join()") == "rawJSON");
    assert(run("(function(){var d = Object.getOwnPropertyDescriptor(JSON.rawJSON('42'), 'rawJSON');"
               "return d.value + ',' + d.writable + ',' + d.enumerable + ',' + d.configurable;})()")
           == "42,false,true,false");
    // The tag is Object's, not a class of its own.
    assert(run("Object.prototype.toString.call(JSON.rawJSON('1'))") == "[object Object]");
    // Two calls are two objects.
    assert(run("JSON.rawJSON('1') === JSON.rawJSON('1')") == "false");

    // The slot is internal, and this is what says so. A plain object with the
    // same property, one that inherits it, and one given the engine's own
    // hidden name are each *not* a raw JSON object:
    assert(run("JSON.isRawJSON(JSON.rawJSON('1'))") == "true");
    assert(run("JSON.isRawJSON({rawJSON: '1'})") == "false");
    assert(run("JSON.isRawJSON(Object.create({rawJSON: '1'}))") == "false");
    assert(run("(function(){var o = {};"
               "Object.defineProperty(o, '%RawJSON', {value: '1', enumerable: false});"
               "return JSON.isRawJSON(o);})()") == "false");
    // and `stringify` ignores them rather than trusting the property:
    assert(run("JSON.stringify({a: {rawJSON: '1'}})") == "{\"a\":{\"rawJSON\":\"1\"}}");
    // A copy is an ordinary object, because the slot does not spread.
    assert(run("JSON.stringify(Object.assign({}, JSON.rawJSON('1')))") == "{\"rawJSON\":\"1\"}");

    // The grammar: a JSON number, `true`, `false`, `null` or a JSON string, with
    // no space around it, and nothing else. `{}` and `[]` are refused — a
    // parser that accepted any JSON value would take them.
    const char* refused[] = {
        "''", "'.5'", "'5.'", "'0x10'", "'+1'", "'01'", "'1.'", "'1e'",
        "'Infinity'", "'NaN'", "' 1'", "'1 '", "'truex'", "'nullx'",
        "'{}'", "'[]'", "'{\"a\":1}'", "'[1]'", "'\"a'",
    };
    for (const char* argument : refused) {
        const std::string source =
            std::string("(function(){try{JSON.rawJSON(") + argument +
            ");return 'accepted';}catch(e){return e.name;}})()";
        assert(run(source.c_str()) == "SyntaxError");
    }
    // The argument is converted first, so an object that stringifies to a
    // number is accepted and no argument at all is `"undefined"`, refused.
    assert(run("JSON.stringify(JSON.rawJSON({toString:function(){return '7';}}))") == "7");
    assert(run("(function(){try{JSON.rawJSON();}catch(e){return e.name;}})()") == "SyntaxError");

    // `isRawJSON` answers for anything, including primitives, and says false.
    assert(run("JSON.isRawJSON(42)") == "false");
    assert(run("JSON.isRawJSON(null)") == "false");
    assert(run("JSON.isRawJSON(undefined)") == "false");
    assert(run("JSON.isRawJSON('1')") == "false");

    // The walk: a raw object is written where it stands, and `toJSON` and a
    // replacer each get their turn before the slot is asked — so a replacer
    // that replaces one wins, and one that returns a new raw object is honoured.
    assert(run("JSON.stringify(JSON.rawJSON('1'), function(k, v){return 2;})") == "2");
    assert(run("JSON.stringify({a: 1}, function(k, v){return k === 'a' ? JSON.rawJSON('99') : v;})")
           == "{\"a\":99}");
    assert(run("typeof JSON.rawJSON('1').toJSON") == "undefined");
    // `space` does not reformat the text, and an array keeps them in place.
    assert(run("JSON.stringify({a: JSON.rawJSON('1')}, null, 2)") == "{\n  \"a\": 1\n}");
    assert(run("JSON.stringify([JSON.rawJSON('1'), JSON.rawJSON('true'), 3])") == "[1,true,3]");
}
