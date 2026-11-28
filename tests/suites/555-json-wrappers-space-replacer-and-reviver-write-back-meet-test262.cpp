// Phase 555, finding 555: JSON.stringify's wrapper objects, space and
// replacer list, and the reviver walk's write-back meet test262.
//
// built-ins/JSON ran 130 of 166 here while 16 of the failures needed Proxy or
// $262 and 6 needed BigInt syntax — and 10 were engine defects this phase
// fixes, every expectation below measured on Node v22.22.3 first (finding 555
// has the probes):
//
//   * a Number object serialises with ToNumber, where valueOf answers first
//     and a toString override is never asked; a String object with ToString,
//     the other way round; the engine used the raw slots and skipped the
//     methods;
//   * the replacer list takes a String item, ToString's a Number item, and
//     ToString's a String or Number *object* — everything else names nothing;
//     the engine accepted only bare strings and numbers;
//   * the space argument dispatches on the wrapper's slot (Number → ToNumber,
//     String → ToString, anything else → no gap), truncates a float toward
//     zero, and caps at ten;
//   * the reviver walk's write-back is CreateDataProperty — a definition the
//     object refuses is ignored, not an error — so a property a reviver made
//     non-configurable ahead of the walk keeps the value it already holds;
//   * a value the reviver replaces forward loses its parse source: the
//     context Record describes the value the parse put there, and the walk
//     only serves it while that value is still the one it is visiting.
TEST(json_wrappers_space_replacer_and_reviver_write_back_meet_test262) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    auto run = [&](const char* src) { return js.eval(src).to_string(); };

    // ---- wrapper objects serialise through their conversion -------------
    // ToNumber(value): valueOf wins; a toString override is never called.
    assert(run(R"JS((function(){
        var num = new Number(42);
        num.toString = function() { throw new Error('toString'); };
        num.valueOf = function() { return 2; };
        var calls = '';
        JSON.stringify(['str'], function(_k, v) {
            return v === 'str' ? num : v;
        });
        return JSON.stringify(new Number(8.5));
    })())JS") == "8.5");
    // The conversion's abrupt completion propagates: a wrapper whose both
    // methods throw (Node's abruptToJSON shape) rejects the whole call.
    assert(run(R"JS((function(){
        var num = new Number(3.14);
        num.toString = function() { throw new Error('abrupt'); };
        num.valueOf = function() { throw new Error('abrupt'); };
        try {
            JSON.stringify({ key: num });
            return 'no throw';
        } catch (e) { return e.message; }
    })())JS") == "abrupt");
    assert(run(R"JS((function(){
        var num = new Number(42);
        num.toString = function() { throw new Error('toString'); };
        num.valueOf = function() { return 2; };
        return JSON.stringify(['str'], function(_k, v) { return v === 'str' ? num : v; });
    })())JS") == "[2]");
    // ToString(value): toString wins; valueOf is never called.
    assert(run(R"JS((function(){
        var str = new String('str');
        str.toString = function() { return 'toString'; };
        str.valueOf = function() { throw new Error('valueOf'); };
        return JSON.stringify({ key: { toJSON: function() { return str; } } });
    })())JS") == "{\"key\":\"toString\"}");
    // ToBoolean(value) is the slot and no method.
    assert(run("JSON.stringify(new Boolean(false))") == "false");
    // A Symbol wrapper is none of the three slots and walks as an object.
    assert(run("JSON.stringify(Object(Symbol('s')))") == "{}");

    // ---- the replacer list ----------------------------------------------
    // A String or Number wrapper object in the list is ToString'd — the
    // toString override answers, valueOf is never asked.
    assert(run(R"JS((function(){
        var num = new Number(10);
        num.toString = function() { return 'toString'; };
        num.valueOf = function() { throw new Error('valueOf'); };
        return JSON.stringify({ 10: 1, toString: 2, valueOf: 3 }, [num]);
    })())JS") == "{\"toString\":2}");
    assert(run(R"JS((function(){
        var str = new String('str');
        str.toString = function() { return 'toString'; };
        str.valueOf = function() { throw new Error('valueOf'); };
        return JSON.stringify({ str: 1, toString: 2, valueOf: 3 }, [str]);
    })())JS") == "{\"toString\":2}");
    // Anything that is not a string, a number, or such a wrapper names
    // nothing (Node: an empty object).
    assert(run("JSON.stringify({null:1, undefined:2, false:3}, [null, undefined, false, {}])") == "{}");

    // ---- the space argument ---------------------------------------------
    // A float truncates toward zero, so -1.99999 is -1 is no gap at all.
    assert(run(R"JS((function(){
        var obj = { a1: { b1: [1, 2, 3, 4] }, a2: 'a2' };
        return JSON.stringify(obj, null, -1.99999) === JSON.stringify(obj, null, -1);
    })())JS") == "true");
    assert(run("JSON.stringify({a:{b:[1]}}, null, -1.99999)") == "{\"a\":{\"b\":[1]}}");
    // A Number object's space is ToNumber'd: valueOf answers, toString is
    // never asked (Node: three spaces).
    assert(run(R"JS((function(){
        var obj = { a: { b: [1] } };
        var num = new Number(1);
        num.toString = function() { throw new Error('toString'); };
        num.valueOf = function() { return 3; };
        var text = JSON.stringify(obj, null, num);
        return text === JSON.stringify(obj, null, 3);
    })())JS") == "true");
    // A String object's space is ToString'd: toString answers.
    assert(run(R"JS((function(){
        var obj = { a: { b: [1] } };
        var str = new String('xxx');
        str.toString = function() { return '---'; };
        str.valueOf = function() { throw new Error('valueOf'); };
        return JSON.stringify(obj, null, str) === JSON.stringify(obj, null, '---');
    })())JS") == "true");
    // Any other object leaves the gap empty: the dispatch is on the slots,
    // with no ToPrimitive fallback.
    assert(run("JSON.stringify({a:{b:[1]}}, null, {})") == "{\"a\":{\"b\":[1]}}");

    // ---- the reviver walk's write-back ----------------------------------
    // A property the reviver made non-configurable ahead of the walk refuses
    // the write-back: Node keeps 2 where the walk wanted 22.
    assert(run(R"JS((function(){
        var obj = JSON.parse('{"a": 1, "b": 2}', function(key, value) {
            if (key === 'a') Object.defineProperty(this, 'b', {configurable: false});
            if (key === 'b') return 22;
            return value;
        });
        return obj.a + '|' + obj.b;
    })())JS") == "1|2");
    assert(run(R"JS((function(){
        var arr = JSON.parse('[1, 2]', function(key, value) {
            if (key === '0') Object.defineProperty(this, '1', {configurable: false});
            if (key === '1') return 22;
            return value;
        });
        return arr[0] + '|' + arr[1];
    })())JS") == "1|2");

    // ---- the forward-modified value loses its source ---------------------
    // The reviver replaces holder[1] while visiting key "0"; the walk then
    // visits the replacement's own keys before "1", and none of those visits
    // is told a source the parse never wrote (Node: key order 0,1,'' and
    // source undefined for "1").
    assert(run(R"JS((function(){
        var seen = '';
        JSON.parse('[1, 2]', function(k, v, c) {
            seen += k + ':';
            if (k === '0') this[1] = 42;
            return this[k];
        });
        return seen;
    })())JS") == "0:1::");
    assert(run(R"JS((function(){
        var sources = {};
        JSON.parse('[1, 2]', function(k, v, c) {
            sources[k] = 'source' in c ? c.source : undefined;
            if (k === '0') this[1] = { foo: 'bar' };
            return this[k];
        });
        return JSON.stringify(sources);
    })())JS") == "{\"0\":\"1\"}");  // "1" and "" were undefined: stringify drops them
}
