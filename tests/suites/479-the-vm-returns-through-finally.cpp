// Phase 479, finding 479: save a return value while the finalizer runs.
TEST(the_bytecode_vm_returns_through_finally) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The oracle's original refusal is first.
    agree("function f(){try{return 1}finally{}}f()", "1");
    agree("var s='';function f(){try{return 1}finally{s+='f'}}String(f()+'/'+s)", "1/f");
    agree("function f(){var x=3;try{return x}finally{x=7}}String(f())", "3");
    agree("function f(){try{{let a=2;let b=3;return a+b}}finally{let z=7;z}}String(f())", "5");
    agree("var s=0;function f(){let x=9;try{let x=3;return x}finally{s=x}}String(f()+'/'+s)", "3/9");
    agree("function f(){try{return {x:7}}finally{gc()}}String(f().x)", "7");
    agree("function f(){try{return 1}finally{throw 3}}try{f()}catch(e){e}", "3");
    agree("function f(){try{return missing_name}finally{throw 8}}try{f()}catch(e){e}", "8");
    agree("var s='';function f(){try{return missing_name}finally{s+='f'}}try{f()}catch(e){s+=e.name}String(s)", "fReferenceError");
    agree("function f(x){try{if(x)return 3;return 5}finally{x=2}}String(f(true)+'/'+f(false))", "3/5");
    agree("var s='';function f(){try{return 3}finally{try{throw 5}catch(e){s+=e}}}String(f()+'/'+s)", "3/5");
    agree("function f(){try{return;}finally{gc()}}String(f())", "undefined");
    agree("function f(){var a={x:1};try{return a}finally{a.x=7}}String(f().x)", "7");
    agree("var n=0;function f(){try{return n++}finally{n++}}String(f()+'/'+n)", "0/2");
    agree("var s='';function f(){try{return 2}finally{let [a,b]=[3,4];s+=a+b}}String(f()+'/'+s)", "2/7");
    agree("var s='';function f(){try{return 2}finally{s+='f'}}var o={};Object.defineProperty(o,'x',{get:f});String(o.x+'/'+s)", "2/f");

    // General completion unwinding is not implemented; refuse rather than
    // miss an outer handler, cursor close or return from a finalizer.
    for (const char* source : {"function f(){for(var x of [1]){try{return x}finally{}}}f()",
                               "function f(){try{}finally{return 2}}f()"}) {
        TestPage page;
        bool refused = false;
        try { page.js.eval_bytecode(source); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("not supported by the bytecode compiler") != std::string::npos;
        }
        assert(refused);
    }
}
