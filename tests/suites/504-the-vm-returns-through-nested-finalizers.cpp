// Phase 504, finding 504: a return visits nested finalizers inside out.
TEST(the_bytecode_vm_returns_through_nested_finalizers) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The original oracle refusal is first.
    agree("function f(){try{try{return 1}finally{}}finally{}}f()", "1");
    agree("var s='';function f(){try{try{return 4}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "4/io");
    agree("var s='';function f(){try{try{try{return 7}finally{s+='a'}}finally{s+='b'}}finally{s+='c'}}String(f()+'/'+s)", "7/abc");
    agree("var s='';function f(){try{let a=3;try{let b=4;return a+b}finally{s+=a}}finally{s+='o'}}String(f()+'/'+s)", "7/3o");
    agree("var s='';function f(){let x=1;try{let x=2;try{let x=3;return x}finally{s+=x}}finally{s+=x}}String(f()+'/'+s)", "3/21");
    agree("var s='';function f(){try{try{return 1}finally{s+='i';throw 7}}finally{s+='o'}}try{f()}catch(e){s+=e}String(s)", "io7");
    agree("var s='';function f(){try{try{return 1}finally{s+='i'}}finally{s+='o';throw 8}}try{f()}catch(e){s+=e}String(s)", "io8");
    agree("var s='';function f(){try{try{return missing_name}finally{s+='i'}}finally{s+='o'}}try{f()}catch(e){s+=e.name}String(s)", "ioReferenceError");
    agree("function f(){try{try{return {x:2}}finally{gc()}}finally{gc()}}String(f().x)", "2");
    agree("function f(){var x={v:1};try{try{return x}finally{x.v=2}}finally{x.v+=3}}String(f().v)", "5");
    agree("function f(v){try{try{if(v)return 1;return 2}finally{v=3}}finally{v=4}}String(f(true)+'/'+f(false))", "1/2");
    agree("function f(){try{try{return 1}finally{try{throw 2}catch(e){}}}finally{}}String(f())", "1");
    agree("var s='';function f(){try{try{return 1}finally{s+='i';throw 7}}finally{s+='o';throw 8}}try{f()}catch(e){s+=e}String(s)", "io8");
    agree("var s='';function f(){try{try{return 2}finally{s+='i'}}finally{s+='o'}}var o={};Object.defineProperty(o,'x',{get:f});String(o.x+'/'+s)", "2/io");
    agree("var n=0;function f(){try{try{return n++}finally{n++}}finally{n++}}String(f()+'/'+n)", "0/3");
    // Interleaved cursor closes and returns from finalizers need their
    // own completion rules and remain refusals rather than silent jumps.
    for (const char* source : {"function f(){try{for(var x of [1]){try{return x}finally{}}}finally{}}f()",
                               "function f(){try{try{return 1}finally{return 2}}finally{}}f()"}) {
        TestPage page;
        bool refused = false;
        try { page.js.eval_bytecode(source); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("not supported by the bytecode compiler") != std::string::npos;
        }
        assert(refused);
    }
}
