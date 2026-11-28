// Phase 540, finding 540: catch returns still run finalizers in order.
TEST(the_bytecode_vm_returns_from_catch_through_finally) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The original refusal first.
    agree("function f(){try{throw 1}catch(e){return e}finally{}}f()", "1");
    agree("var s='';function f(){try{throw 3}catch(e){s+='c';return e}finally{s+='f'}}String(f()+'/'+s)", "3/cf");
    agree("var s='';function f(){try{throw 2}catch(e){try{return e}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "2/io");
    agree("var s='';function f(){try{throw 2}catch(e){try{return e}catch(x){s+='c'}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "2/io");
    agree("var s='';function f(){try{throw 2}catch(e){return missing_name}finally{s+='f'}}try{f()}catch(e){s+=e.name}String(s)", "fReferenceError");
    agree("var s='';function f(){try{throw 2}catch(e){return e}finally{s+='f';throw 7}}try{f()}catch(e){s+=e}String(s)", "f7");
    agree("var e=8,s='';function f(){try{throw 2}catch(e){return e}finally{s+=e}}String(f()+'/'+s)", "2/8");
    agree("var s='';function f(){try{throw 2}catch(e){try{throw 4}catch(e){return e}finally{s+=e}}finally{s+='o'}}String(f()+'/'+s)", "4/2o");
    agree("var s='';function f(){try{try{throw 2}catch(e){return e}finally{s+='i';throw 7}}catch(e){s+=e;return e+1}finally{s+='o'}}String(f()+'/'+s)", "8/i7o");
    agree("function f(){try{throw {x:3}}catch(e){return e}finally{gc()}}String(f().x)", "3");
    agree("var s='';function f(){try{throw 2}catch(e){try{return e}catch(x){s+='c'}finally{e=9;s+=e}}finally{s+='o'}}String(f()+'/'+s)", "2/9o");
    agree("function f(){try{throw 2}catch{return;}finally{gc()}}String(f())", "undefined");
    agree("var s='';function f(){try{throw 2}catch(e){return e}finally{s+='f'}}var o={};Object.defineProperty(o,'x',{get:f});String(o.x+'/'+s)", "2/f");
    agree("var s='';function f(){try{throw 1}catch(e){try{return e}finally{try{throw 3}catch(e){s+=e}}}finally{s+='o'}}String(f()+'/'+s)", "1/3o");
    agree("var s='';function f(){try{try{throw 2}catch(e){return missing_name}finally{s+='i'}}catch(e){s+=e.name;return 7}finally{s+='o'}}String(f()+'/'+s)", "7/iReferenceErroro");
    agree("var s='';function f(){try{throw 1}catch(e){try{try{throw 3}catch(x){return x}finally{s+='a'}}finally{s+='b'}}finally{s+='c'}}String(f()+'/'+s)", "3/abc");
    agree("var s='';function f(){try{throw 1}catch(e){let x={v:e};return x}finally{gc()}}var a=f();String(a.v+'/'+s)", "1/");
    agree("var s='';function f(){try{throw 5}catch(e){if(e==5)return e;return 7}finally{s+='f'}}String(f()+'/'+s)", "5/f");
    agree("var s='';function f(){try{throw 1}catch(e){try{return missing_name}catch(x){s+=x.name}finally{s+='i'}}finally{s+='o'}return 9}String(f()+'/'+s)", "9/ReferenceErrorio");
    // Interleaved iterator/finalizer and finalizer-return unwinding remain refused.
    for (const char* source : {"function f(){try{for(var x of [1]){try{throw x}catch(e){return e}finally{}}}finally{}}f()",
                               "function f(){try{throw 1}catch(e){return e}finally{return 2}}f()"}) {
        TestPage page;
        bool refused = false;
        try { page.js.eval_bytecode(source); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("not supported by the bytecode compiler") != std::string::npos;
        }
        assert(refused);
    }
}
