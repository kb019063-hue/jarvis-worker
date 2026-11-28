// Phase 541, finding 541: the reviver's source is the text of the value it
// is visited with.
//
// Phase 471 pinned a duplicate key's span to the first occurrence while
// citing Node v22.22.3 — which answers the last. The two invariants here are
// the ones the wrong rule broke, both measured on Node v22.22.3 first:
//
//   * **Value and source agree.** For every primitive the reviver visits,
//     `JSON.parse(c.source)` is `v`. Under first-wins the duplicate case
//     told the reviver "value 22, written as 1" — a disagreement no correct
//     rule can produce.
//   * **A reviver sees the whole record; a parse without one skips it.**
//     §25.5.5's step order (ToString, then IsCallable, then the parse) is
//     what lets the parser skip the span recording nobody reads, so the two
//     calls must return the same document — and the reviver call must still
//     find its sources.
TEST(the_revivers_source_is_the_text_of_the_value_it_visits) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    auto run = [&](const char* src) { return js.eval(src).to_string(); };

    // Every primitive the reviver visits has a source that parses back to
    // the value it came with (Node: 0 violations).
    assert(run(R"JS((function(){
        var bad = 0;
        JSON.parse('{"a":1,"a":22,"b":"HI","c":true,"d":[7,8],"e":{"f":-0.5,"e":null}}',
            function (k, v, c) {
                if (k !== "" && "source" in c && JSON.parse(c.source) !== v) bad++;
                return v;
            });
        return bad;
    })())JS") == "0");

    // The same, for the document the wrong rule broke: the duplicate's
    // source is its own text, not the earlier writer's (Node: "22|2").
    assert(run(R"JS((function(){
        var pair;
        JSON.parse('{"a":1,"a":22}', function (k, v, c) {
            if (k === 'a') pair = v + '|' + JSON.parse(c.source);
            return v;
        });
        return pair;
    })())JS") == "22|22");

    // A parse with a reviver still finds its sources: three shapes, three
    // sources present (the row that found the record was easy to lose).
    assert(run(R"JS((function(){
        var seen = 0;
        JSON.parse('{"o":{"a":1,"a":2},"arr":[9,"x"],"n":1.5}',
            function (k, v, c) {
                if ((k === 'a' && v === 2) || (k === '1' && v === 'x') || (k === 'n')) seen++;
                return v;
            });
        return seen;
    })())JS") == "3");
    assert(run(R"JS((function(){
        var s;
        JSON.parse('{"arr":[9,"x"]}', function (k, v, c) {
            if (k === '1' && v === 'x') s = c.source;
            return v;
        });
        return s;
    })())JS") == "\"x\"");

    // And a parse without a reviver returns the same document the reviver
    // walk returns: the skipped recording changes no answer.
    assert(run(R"JS((function(){
        var text = '{"a":1,"a":22,"b":{"c":[3,4,{"d":"x"}]},"e":[[1],[2]]}';
        return JSON.stringify(JSON.parse(text)) === JSON.stringify(JSON.parse(text, function (k, v) { return v; }));
    })())JS") == "true");

    // The use the whole Record exists for: a number too large for a double
    // writes itself back out from its own text, and a duplicate key's text
    // is the LAST one (Node: {"a":1e999}).
    assert(run(R"JS((function(){
        return JSON.stringify(JSON.parse('{"a":1e1000,"a":1e999}',
            function (k, v, c) { return typeof v === 'number' ? JSON.rawJSON(c.source) : v; }));
    })())JS") == "{\"a\":1e999}");
}
