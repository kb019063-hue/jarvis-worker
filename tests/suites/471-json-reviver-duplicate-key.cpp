// JSON.parse reviver keeps the LAST span on duplicate keys (Phase 541; the
// suite Phase 471 wrote for the opposite rule, corrected).
//
// The parser records the span for each value under (holder identity, key),
// and a duplicate key overwrites: the property the reviver walks holds the
// LAST writer's value, and the reviver is visited once per property with
// that value, so its context says what *that* value was written as. Every
// expectation below was measured on Node v22.22.3 (finding 541 has the
// probes); Phase 471's version asserted first-wins while citing the same
// Node version, and each of its duplicate-key assertions is one Node
// refuses.

TEST(json_reviver_keeps_the_last_span_on_duplicate_keys) {
    using namespace litebird;
    TestPage page;
    JsInterpreter &js = page.js;
    auto run = [&](const char *src) { return js.eval(src).to_string(); };

    // Duplicate key: the last span wins (Node: "22").
    assert(run("(function(){var s; JSON.parse('{\"a\":1,\"a\":22}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "22");
    // Triple duplicate keeps the very last (Node: "3").
    assert(run("(function(){var s; JSON.parse('{\"a\":1,\"a\":2,\"a\":3}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "3");
    // The value itself is the last writer's value (2) — and now the source
    // says so too (Node: value 2, source "2").
    assert(run("(function(){var v2; JSON.parse('{\"a\":1,\"a\":2}', (k,v)=>{if(k==='a') v2=v; return v;}); return String(v2);})()") == "2");
    assert(run("(function(){var s; JSON.parse('{\"a\":1,\"a\":2}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "2");
    // A null first writer is no exception (Node: value 5, source "5").
    assert(run("(function(){var s; JSON.parse('{\"a\":null,\"a\":5}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "5");
    // A string keeps its quotes (Node: "\"HI\"").
    assert(run("(function(){var s; JSON.parse('{\"s\":\"\\u0041\",\"s\":\"HI\"}', (k,v,c)=>{if(k==='s') s=c.source; return v;}); return s;})()") == "\"HI\"");
    // Nested object duplicate: the inner object's a keeps its last (Node: "2").
    assert(run("(function(){var s; JSON.parse('{\"o\":{\"a\":1,\"a\":2}}', (k,v,c)=>{if(k==='a') s=c.source; return v;}); return s;})()") == "2");
    // Non-duplicate keys keep their own spans (Node: "1,2").
    assert(run("(function(){var a,b; JSON.parse('{\"a\":1,\"b\":2}', (k,v,c)=>{if(k==='a') a=c.source; if(k==='b') b=c.source; return v;}); return a+','+b;})()") == "1,2");
    // Array indices are distinct keys: "0" vs "1" — not a duplicate (Node: "1,1").
    assert(run("(function(){var s=[]; JSON.parse('[1,1]', (k,v,c)=>{if(k==='0'||k==='1') s.push(c.source); return v;}); return s.join(',');})()") == "1,1");
    // Duplicates in different holders: each holder's last wins separately
    // (Node: "2,4").
    assert(run("(function(){var s1,s2; JSON.parse('{\"x\":{\"a\":1,\"a\":2},\"y\":{\"a\":3,\"a\":4}}', (k,v,c)=>{if(k==='a' && v===2) s1=c.source; if(k==='a' && v===4) s2=c.source; return v;}); return s1+','+s2;})()") == "2,4");
    // Root is unaffected: JSON.parse(' 1 ') still gives "1" for root (holder "" on root object).
    assert(run("(function(){var s; JSON.parse(' 1 ', (k,v,c)=>{if(k==='') s=c.source; return v;}); return s;})()") == "1");
}
