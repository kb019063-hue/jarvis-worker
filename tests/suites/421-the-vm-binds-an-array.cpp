// Phase 421, finding 421: array binding uses the iterator, one pull at a time.
TEST(the_bytecode_vm_binds_an_array_pattern) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // the original refusal
    agree("var [a, b] = [1, 2]; String(a)", "1");
    // elisions still pull
    agree("var [a,,b]=[1,2,3]; String(a+','+b)", "1,3");
    // defaults only for undefined
    agree("var [a=5,b=4,c=9]=[undefined,null,0]; String(a+','+b+','+c)", "5,null,0");
    // nested patterns and rest
    agree("var [[a,b],...c]=[[1,2],3,4]; String(a+','+b+','+c)", "1,2,3,4");
    // string iteration and rest
    agree("var [a,...b]='abc'; String(a+'/'+b.length+'/'+b)", "a/2/b,c");
    // lazy protocol, receiver, and close
    agree("var src={}; var n=0; src['@@iterator']=function(){return {n:40,next:function(){n++;this.n++;return {value:this.n,done:false};},return:function(){n+=10;return {};}}}; var [a,b]=src; String(a+','+b+'/'+n)", "41,42/12");
    // function var does not overwrite global
    agree("var a=42; function f(){var [a]=[1]; return a;} String(f()+'/'+a)", "1/42");
    // parameter redeclaration
    agree("function f(a){var [a,b]=[3,4]; return String(a)+','+b;} String(f(8))", "3,4");
    // block lexical bindings
    agree("var out=''; {let [a,b]=[1,2];out=String(a)+String(b);} String(out)", "12");
    // inferred default function name
    agree("var [a=function(){}]=[]; String(a.name)", "a");
    // defaults observe earlier bindings
    agree("var [a=3,b=a+2]=[]; String(a+','+b)", "3,5");
    // rest drains the iterator
    agree("var n=0; var src={}; src['@@iterator']=function(){return {next:function(){n++;return {value:n,done:n>3};}}}; var [a,...b]=src; String(a+'/'+b+'/'+n)", "1/2,3/4");
    // array iterator override
    agree("var a=[1,2]; a['@@iterator']=function(){return {next:function(){return {value:9,done:false};}}}; var [x]=a; String(x)", "9");
    // The actual symbol key, not its printable description.
    agree("var a=[1,2]; a[Symbol.iterator]=function(){return {next:function(){return {value:9,done:false};}}}; var [x]=a; String(x)", "9");
    // function lexical scope
    agree("var a=42; function f(){let [a]=[1]; return a;} String(f()+'/'+a)", "1/42");
    // extra call arguments do not shift slots
    agree("function f(a){var [b]=[3]; return String(a)+','+b;} String(f(1,2,3))", "1,3");

    // A failed default closes an open iterator, but a throwing next does not.
    // An injected native records the effects even when script execution fails.
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
    abrupt("var s={}; s['@@iterator']=function(){return {next:function(){record('n');return {value:undefined,done:false};},return:function(){record('r');return {};}}}; var [a=Object.create(42)]=s", "nr");
    abrupt("var s={}; s['@@iterator']=function(){return {next:function(){record('n');return Object.create(42);},return:function(){record('r');return {};}}}; var [a]=s", "n");
    abrupt("var s={}; s['@@iterator']=function(){return {next:function(){return {value:1,done:false};},return:function(){record('r');return 1;}}}; var [a]=s", "r");
}
