// Phase 507 — Map.prototype.getOrInsert and getOrInsertComputed (and WeakMap).

TEST(map_and_weakmap_get_or_insert) {
    TestPage page;
    JsInterpreter &js = page.js;
    auto run = [&](const char *src) { return js.eval(src).to_string(); };
    auto is_true = [&](const char *src) { return js.eval(src).is_truthy(); };

    // Map getOrInsert — inserts when missing, returns existing when present.
    assert(run("let m=new Map(); m.getOrInsert('k', 42)") == "42");
    assert(run("let m=new Map([['k',1]]); m.getOrInsert('k', 99)") == "1");
    assert(is_true("let m=new Map(); let a=m.getOrInsert('k', 42); m.get('k')===42 && m.size===1"));
    assert(is_true("let m=new Map([['k',1]]); let v=m.getOrInsert('k', 99); v===1 && m.get('k')===1 && m.size===1"));
    // -0 and +0 are same key, NaN equals NaN.
    assert(run("let m=new Map(); m.getOrInsert(0, 'a'); m.getOrInsert(-0, 'b')") == "a");
    assert(run("let m=new Map(); m.getOrInsert(NaN, 1); m.getOrInsert(NaN, 2)") == "1");
    // Object identity.
    assert(run("let m=new Map(); let o={}; m.getOrInsert(o, 1); m.getOrInsert({}, 2)") == "2");

    // Map getOrInsertComputed — callback only when missing.
    assert(run("let m=new Map(); m.getOrInsertComputed('k', k=>k+'!')") == "k!");
    assert(is_true("let m=new Map([['k',1]]); let called=false; m.getOrInsertComputed('k', ()=>{called=true; return 2;}); !called"));
    assert(is_true("let m=new Map(); let c=0; m.getOrInsertComputed('k', ()=>{c++; return 42;}); c===1 && m.getOrInsertComputed('k', ()=>{c++; return 99;})===42 && c===1"));
    assert(run("let m=new Map(); try{m.getOrInsertComputed('k', null)}catch(e){e.name}") == "TypeError");
    // Callback receives key and this is undefined (strict).
    assert(is_true("let m=new Map(); let got=null; m.getOrInsertComputed('k', function(k){ got=k; return 1;}); got==='k'"));
    assert(is_true("let m=new Map(); let t='x'; m.getOrInsertComputed('k', function(){ 'use strict'; t=this; return 1;}); t===undefined"));

    // WeakMap — object keys only, same semantics.
    assert(run("let w=new WeakMap(); let o={}; w.getOrInsert(o, 42)") == "42");
    assert(run("let w=new WeakMap(); let o={}; w.set(o,1); w.getOrInsert(o, 99)") == "1");
    assert(is_true("let w=new WeakMap(); let o={}; w.getOrInsert(o, 1); w.get(o)===1"));
    assert(run("let w=new WeakMap(); try{w.getOrInsert(42,1)}catch(e){e.name}") == "TypeError");
    assert(run("let w=new WeakMap(); let o={}; w.getOrInsertComputed(o, k=>k) === o") == "true");
    assert(run("let w=new WeakMap(); let o={}; try{w.getOrInsertComputed(o, null)}catch(e){e.name}") == "TypeError");
    assert(run("let w=new WeakMap(); try{w.getOrInsertComputed(42, ()=>1)}catch(e){e.name}") == "TypeError");

    // Type checks: receiver must be Map/WeakMap.
    assert(run("try{Map.prototype.getOrInsert.call({}, 'k', 1)}catch(e){e.name}") == "TypeError");
    assert(run("try{WeakMap.prototype.getOrInsert.call(new Map(), {}, 1)}catch(e){e.name}") == "TypeError");

    // Method exists with correct length.
    assert(run("Map.prototype.getOrInsert.length") == "2");
    assert(run("Map.prototype.getOrInsertComputed.length") == "2");
    assert(run("WeakMap.prototype.getOrInsert.length") == "2");
    assert(run("WeakMap.prototype.getOrInsertComputed.length") == "2");
}
