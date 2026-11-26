// Phase 464, finding 464: finally runs once on both normal and thrown paths.
TEST(the_bytecode_vm_runs_finally) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The original refusal comes first.
    agree("try { 1; } finally { 2; }", "2");
    agree("try{1;}finally{}", "1");
    agree("var x=0;try{x=1}finally{x+=2}String(x)", "3");
    agree("try{try{throw 7}finally{2}}catch(e){e}", "7");
    agree("try{try{throw 7}finally{throw 8}}catch(e){e}", "8");
    agree("var x='';try{throw 1}catch(e){x+=e}finally{x+='f'}String(x)", "1f");
    agree("try{try{throw 1}catch(e){throw 3}finally{2}}catch(e){e}", "3");
    agree("try{try{throw 1}catch(e){throw 3}finally{throw 4}}catch(e){e}", "4");
    agree("var x='';try{try{throw 1}finally{x+='a'}}catch(e){x+=e}finally{x+='b'}String(x)", "a1b");
    agree("try{try{throw {v:8}}finally{let a=2;gc();a;}}catch(e){e.v}", "8");
    agree("function f(){try{var x=1}finally{x+=2}return x}String(f())", "3");
    agree("try{throw 2}catch(e){e}finally{let e=4;e}", "4");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};try{try{for(var x of a){throw 1}}finally{s+='f'}}catch(e){s+=e}String(s)", "rf1");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};try{for(var x of a){try{throw 1}finally{s+='f'}}}catch(e){s+=e}String(s)", "fr1");
    agree("var n=0;try{try{throw 1}finally{n++;throw 2}}catch(e){n+=e}String(n)", "3");
    agree("try{try{Object.create(42)}finally{throw 5}}catch(e){e}", "5");
    agree("try{try{Object.create(42)}finally{let y=1}}catch(e){e.name}", "TypeError");

    // When the exception reaches the host, the finalizer still runs once,
    // and its thrown value replaces the original. Observe both externally.
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
            if (bytecode) page.js.eval_bytecode("try{throw 1}finally{record('f');throw 2}");
            else { page.js.eval("try{throw 1}finally{record('f');throw 2}"); failed = page.js.last_eval_failed(); }
        } catch (const JsThrow& e) { failed = true; assert(e.value.to_string() == "2"); }
        assert(failed);
        assert(trace == "f");
    }

    // Abrupt jumps require a general completion/unwind path, not implemented.
    for (const char* source : {"function f(){try{return 1}finally{}}f()",
                               "function f(){try{}finally{return 2}}f()",
                               "while(true){try{}finally{break}}",
                               "try{}finally{let x=2;var f=function(){return x};f()}",
                               "function f(){try{throw 1}catch(e){return e}finally{}}f()"}) {
        TestPage page;
        bool refused = false;
        try { page.js.eval_bytecode(source); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("not supported by the bytecode compiler") != std::string::npos;
        }
        assert(refused);
    }
}
