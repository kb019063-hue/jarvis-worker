// Phase 456, finding 456: a handler catches a thrown value and unwinds the VM.
TEST(the_bytecode_vm_catches_exceptions) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The original refusal first.
    agree("try { 1; } catch (e) { 2; }", "1");
    agree("try { throw 7; } catch (e) { e; }", "7");
    agree("try { throw 3; } catch { 7; }", "7");
    agree("var x=7;try{throw 3}catch(x){x+=2}String(x)", "7");
    agree("var s='';try{throw 3}catch(e){s+=e;}String(s)", "3");
    agree("try { Object.create(42); } catch(e) { e.name; }", "TypeError");
    agree("function f(){throw 8} try{f()}catch(e){e}", "8");
    agree("try{try{throw 1}catch(e){throw e+2}}catch(x){x}", "3");
    agree("try{1;throw 2}catch(e){}", "1");
    agree("try{throw 1}catch(e){e+2};", "3");
    agree("5;try{}catch(e){}", "5");
    agree("try{1;while(false){}}catch(e){}", "undefined");
    agree("try{throw 1}catch(e){let x=e; x+2}", "3");
    agree("function f(){try{throw 3}catch(e){return e+1}}String(f())", "4");
    agree("var n=0;try{throw 2}catch(e){n=e}try{throw 3}catch(e){n+=e}String(n)", "5");
    // The cursor is closed while the original thrown value is kept.
    agree("var s='';var it={};it[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};try{for(var x of it){throw 'x'}}catch(e){s+=e}String(s)", "rx");
    // A failing step does not close; a handler inside a loop keeps its cursor.
    agree("var s='';var it={};it[Symbol.iterator]=function(){return {next:function(){throw 'x'},return:function(){s+='r';return {}}}};try{for(var x of it){}}catch(e){s+=e}String(s)", "x");
    agree("var s='';var it={};it[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};for(var x of it){try{throw 'x'}catch(e){s+=e}break}String(s)", "xr");
    // A compiled getter runs via the native bridge, which may grow frames.
    agree("var o={};Object.defineProperty(o,'x',{get:function(){throw 12}});try{o.x}catch(e){e}", "12");
    agree("try{throw {x:7}}catch(e){gc();e.x}", "7");
    agree("function f(){try{throw 1}catch(e){return e}}try{throw f()}catch(e){e+1}", "2");
    agree("var it={};it[Symbol.iterator]=function(){throw 'iter'};try{var [x]=it}catch(e){e}", "iter");
    // These require finalizer completions, pattern binding or control-flow
    // unwinding that is not implemented, so the compiler refuses them.
    for (const char* source : {"try{1}finally{2}", "try{throw [1]}catch([e]){e}",
                               "function f(){try{return 1}catch(e){}}f()",
                               "while(true){try{throw 1}catch(e){break}}",
                               "try{throw 3}catch(e){function f(){return e}f()}"}) {
        TestPage page;
        bool refused = false;
        try { page.js.eval_bytecode(source); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("not supported by the bytecode compiler") != std::string::npos;
        }
        assert(refused);
    }
}
