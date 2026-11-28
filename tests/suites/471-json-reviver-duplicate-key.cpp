// JSON.parse reviver keeps first span on duplicate keys (Phase 471).
//
// The parser records the span for each value under (holder identity, key).
// For {"a":1,"a":2} the reviver for "a" must see the first occurrence's text
// "1", not the second's "2". Before the fix the map overwrote, so the last
// won. First wins is insertion order and matches Node v22.22.3.

TEST(json_reviver_keeps_first_span_on_duplicate_keys) {
    using namespace litebird;
    TestPage page;
    JsInterpreter &js = page.js;
    auto run = [&](const char *src) { return js.eval(src).to_string(); };

    // Duplicate key: first span wins.
    assert(run("(function(){var s; JSON.parse('{\"a\":1,\"a\":2}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "1");
    // Triple duplicate keeps the very first.
    assert(run("(function(){var s; JSON.parse('{\"a\":1,\"a\":2,\"a\":3}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "1");
    // Value itself is last writer's value (2), but source is first writer's text (1).
    assert(run("(function(){var v2; JSON.parse('{\"a\":1,\"a\":2}', (k,v)=>{if(k==='a') v2=v; return v;}); return String(v2);})()") == "2");
    // Re-check source still first after checking value.
    assert(run("(function(){var s; JSON.parse('{\"a\":1,\"a\":2}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "1");
    // Nested object duplicate: inner object's a keeps its first.
    assert(run("(function(){var s; JSON.parse('{\"o\":{\"a\":1,\"a\":2}}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "1");
    // Non-duplicate keys keep their own spans.
    assert(run("(function(){var a,b; JSON.parse('{\"a\":1,\"b\":2}', (k,v,c)=>{if(k==='a') a=c.source; if(k==='b') b=c.source; return v;}); return a+','+b;})()") == "1,2");
    // Array indices are distinct keys: "0" vs "1" — not a duplicate.
    assert(run("(function(){var s=[]; JSON.parse('[1,1]', (k,v,c)=>{if(k==='0'||k==='1') s.push(c.source); return v;}); return s.join(',');})()") == "1,1");
    // Duplicate in different holders: each holder's first wins separately.
    assert(run("(function(){var s1,s2; JSON.parse('{\"x\":{\"a\":1,\"a\":2},\"y\":{\"a\":3,\"a\":4}}', (k,v,c)=>{if(k==='a' && v===2) s1=c.source; if(k==='a' && v===4) s2=c.source; return v;}); return s1+','+s2;})()") == "1,3");
    // Root is unaffected: JSON.parse(' 1 ') still gives "1" for root (holder "" on root object).
    assert(run("(function(){var s; JSON.parse(' 1 ', (k,v,c)=>{if(k==='') s=c.source; return v;}); return s;})()") == "1");
}
