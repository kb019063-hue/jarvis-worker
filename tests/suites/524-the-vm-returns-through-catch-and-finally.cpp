// Phase 524, finding 524: returns unwind catch and finally handlers in order.
TEST(the_bytecode_vm_returns_through_catch_and_finally) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The original refusal comes first.
    agree("function f(){try{try{return 1}catch(e){}finally{}}finally{}}f()", "1");
    agree("function f(){try{return 1}catch(e){}finally{}}f()", "1");
    agree("function f(){try{return 1}catch(e){}} f()", "1");
    agree("var s='';function f(){try{try{return 4}catch(e){s+='c'}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "4/io");
    agree("var s='';function f(){try{try{return missing_name}catch(e){s+=e.name}finally{s+='i'}}finally{s+='o'}return 9}String(f()+'/'+s)", "9/ReferenceErrorio");
    agree("var s='';function f(){try{try{try{return 1}finally{s+='i';throw 7}}catch(e){s+=e}finally{s+='o'}}finally{s+='p'}return 9}String(f()+'/'+s)", "9/i7op");
    agree("var s='';function f(){try{try{return 1}catch(e){s+='c'}finally{s+='i';throw 7}}catch(e){s+=e}finally{s+='o'}return 9}String(f()+'/'+s)", "9/i7o");
    agree("var s='';function f(){try{try{return 1}catch(e){s+='c'}finally{s+='i';throw 7}}finally{s+='o'}}try{f()}catch(e){s+=e}String(s)", "io7");
    agree("function f(){try{let a=1;try{let b=2;return {v:a+b}}catch(e){}finally{gc()}}finally{gc()}}String(f().v)", "3");
    agree("var s='';function f(){let x=1;try{let x=2;try{let x=3;return x}catch(e){}finally{s+=x}}catch(e){}finally{s+=x}}String(f()+'/'+s)", "3/21");
    agree("var n=0;function f(v){try{if(v)return n++;return 5}catch(e){n=8}finally{n++}}String(f(true)+'/'+f(false)+'/'+n)", "0/5/3");
    agree("var s='';function f(){try{try{return 1}finally{s+='i'}}catch(e){s+='c'}finally{s+='o'}}String(f()+'/'+s)", "1/io");
    agree("function f(){try{return;}catch{}finally{gc()}}String(f())", "undefined");
    agree("var s='';function f(){try{return 3}catch(e){s+='c'}}try{s+=f();throw 7}catch(e){s+=e}String(s)", "37");
    agree("var s='';function f(){try{try{return 1}catch(e){s+='c'}finally{s+='i'}}finally{s+='o'}}var o={};Object.defineProperty(o,'x',{get:f});String(o.x+'/'+s)", "1/io");
    agree("var s='';function f(){try{try{return 1}finally{throw 7}}catch(e){s+=e}return 9}String(f()+'/'+s)", "9/7");
    agree("var s='';function f(){try{try{return 1}catch(e){s+='a'}finally{s+='i'}}catch(e){s+='b'}finally{s+='o'}}String(f()+'/'+s)", "1/io");
    // Interleaved iterator cleanup and return from a finalizer have distinct
    // completion rules and stay refused.
    for (const char* source : {"function f(){try{for(var x of [1]){try{return x}finally{}}}catch(e){}}f()",
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
