// Phase 446, finding 446: for-of pulls lazily and closes unfinished iterators.
TEST(the_bytecode_vm_iterates_for_of) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The original refusal is first: it must fail loudly without the fix.
    agree("var s = \"\"; for (var k of [1,2]) { s += k; } String(s)", "12");
    agree("var s=\"\"; for(var x of [1,undefined,3]){s+=String(x)+\",\";} String(s)", "1,undefined,3,");
    agree("var s=\"before\"; for(var x of []) {s=\"bad\";} String(s)", "before");
    agree("var s=\"\"; for(var x of \"a💡b\") {s+=x+\"/\";} String(s)", "a/💡/b/");
    agree("var n=0;for(var x of [1,2,3,4]) {if(x==2)continue;if(x==4)break;n+=x;}String(n)", "4");
    agree("var s=\"\";for(var x of [1,2]){for(var y of [3,4]){s+=x*y+\",\";}}String(s)", "3,4,6,8,");
    agree("var a=9; function f(a){for(var a of [1,2]){}return a;} String(f(6)+\"/\"+a)", "2/9");
    agree("var x=0;for(x of [4,5]){}String(x)", "5");
    agree("var n=0;var a=[1,2];a[Symbol.iterator]=function(){return {next:function(){n++;return {value:9,done:n>2};}}};for(var x of a){n+=10;break;}String(x+\"/\"+n)", "9/11");
    agree("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){n++;return {}}}};for(var x of a){break;}String(n)", "1");
    agree("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){n++;return {value:n,done:n>3}},return:function(){n+=100;return {}}}};for(var x of a){if(x<3)continue;break;}String(n)", "103");
    agree("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){n++;return {}}}};function f(){for(var x of a){return 7;}}String(f()+\"/\"+n)", "7/1");
    agree("var s=0;for(var x of [1,2,3]){{let y=x*2;if(x==2)continue;s+=y;}}String(s)", "8");
    agree("var s=0;for(var x of [1,2,3]){{let y=x*2;if(x==2)break;s+=y;}}String(s)", "2");
    agree("var s=\"\";for(var x of [1,2,3]){switch(x){case 2:continue;default:s+=x;}}String(s)", "13");
    agree("var a=[];for(var x of [1,2]) {a.push(x);}String(a)", "1,2");
    agree("var a=[];a.length=3;var r=\"\";for(var x of a){r+=String(x)+\",\"}String(r)", "undefined,undefined,undefined,");
    agree("var a=[1,2];var s=\"\";for(var x of a){s+=x;if(x==1)a.push(3)}String(s)", "123");
    agree("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){n++;return {done:n>2,value:n}},return:function(){n+=10;return {}}}};for(var x of a){}String(n)", "3");
    agree("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){n++;return {done:false,value:n}},return:function(){n+=10;return {}}}};function f(){for(var x of a){for(var y of a){return x+y;}}}String(f()+\"/\"+n)", "3/22");
    agree("var n=0;for(var x of [1,2]){var [v]=[x];n+=v;}String(n)", "3");

    // A grown sparse length can name an accessor beyond stored elements.
    agree("var a=[]; a.length=2; Object.defineProperty(a,\"1\",{get:function(){return 9;},configurable:true}); var [u,v]=a; String(v)", "9");
    agree("var a=[];a.length=2;Object.defineProperty(a,\"1\",{get:function(){return 9;},configurable:true});var s=\"\";for(var x of a){s+=String(x)+\",\"}String(s)", "undefined,9,");

    agree("for(var k of []){} String(k)", "undefined");
    agree("var k=7;for(var k of []){} String(k)", "7");

    // The original error wins over IteratorClose. A step failure never calls
    // return; a failure in the body closes it once; an invalid normal close
    // is itself an error. Trace with a native so both paths are observable.
    auto abrupt = [](const char* source, const std::string& expected) {
        for (bool bytecode : {false, true}) {
            TestPage page;
            std::string trace;
            page.js.global_env()->define("record", JsValue::make_native_function(
                [&trace](const std::vector<JsValue>& args) {
                    if (!args.empty()) trace += args[0].to_string();
                    return JsValue::make_undefined();
                }));
            bool failed = false;
            try {
                if (bytecode) page.js.eval_bytecode(source);
                else { page.js.eval(source); failed = page.js.last_eval_failed(); }
            } catch (const JsThrow&) { failed = true; }
              catch (const std::exception&) { failed = true; }
            assert(failed);
            assert(trace == expected);
        }
    };
    abrupt("var s={}; s[Symbol.iterator]=function(){return {next:function(){record('n');return Object.create(42);},return:function(){record('r');return {};}}};for(var x of s){}", "n");
    abrupt("var s={}; s[Symbol.iterator]=function(){return {next:function(){record('n');return 1;},return:function(){record('r');return {};}}};for(var x of s){}", "n");
    abrupt("var s={}; s[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false};},return:function(){record('r');return {};}}};for(var x of s){record('b');Object.create(42);}", "br");
    abrupt("var s={}; s[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false};},return:function(){record('r');return 1;}}};for(var x of s){break;}", "r");
    abrupt("var s={}; s[Symbol.iterator]=function(){return {next:function(){return Object.defineProperty({}, 'done', {get:function(){record('d');return Object.create(42);}});},return:function(){record('r');return {};}}};for(var x of s){}", "d");

    // The compiler has no per-iteration lexical environments or assignment
    // references for patterns, so these remain explicit refusals.
    for (const char* source : {"for(let x of [1,2]){}", "for(var [x] of [[1]]){}",
                               "for(var x in [1,2]){}"}) {
        TestPage page;
        bool refused = false;
        try { page.js.eval_bytecode(source); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("not supported by the bytecode compiler") != std::string::npos;
        }
        assert(refused);
    }
}
