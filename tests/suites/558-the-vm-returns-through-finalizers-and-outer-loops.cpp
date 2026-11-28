// Phase 558, finding 558: inner finalizers run before enclosing iterator close.
TEST(the_bytecode_vm_returns_through_finalizers_and_outer_loops) {
    auto agree = [](const char* source, const char* expected) {
        TestPage walked;
        TestPage compiled;
        assert(walked.js.eval(source).to_string() == expected);
        assert(compiled.js.eval_bytecode(source).to_string() == expected);
    };
    // The original refusal is first.
    agree("function f(){for(var x of [1]){try{throw x}catch(e){return e}finally{}}}f()", "1");
    agree("function f(){for(var x of [1]){try{return x}finally{}}}f()", "1");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){s+='c';return e}finally{s+='f'}}}String(f()+'/'+s)", "5/cfr");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{return x}finally{s+='f'}}}String(f()+'/'+s)", "5/fr");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{try{throw x}catch(e){return e}finally{s+='i'}}finally{s+='o'}}}String(f()+'/'+s)", "5/ior");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){return {v:e}}finally{gc();s+='f'}}}String(f().v+'/'+s)", "5/fr");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){return missing_name}finally{s+='f'}}}try{f()}catch(e){s+=e.name}String(s)", "frReferenceError");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';throw 'close'}}};function f(){for(var x of a){try{throw x}catch(e){return e}finally{s+='f'}}}try{f()}catch(e){s+=e}String(s)", "frclose");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';throw 'close'}}};function f(){for(var x of a){try{throw x}catch(e){return e}finally{s+='f';throw 'fin'}}}try{f()}catch(e){s+=e}String(s)", "frfin");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};var b={};b[Symbol.iterator]=function(){return {next:function(){return {value:7,done:false}},return:function(){s+='b';return {}}}};function f(){for(var x of a){for(var y of b){try{throw y}catch(e){return e}finally{s+='f'}}}}String(f()+'/'+s)", "7/fbr");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){try{return e}finally{s+='i'}}finally{s+='o'}}}String(f()+'/'+s)", "5/ior");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){return e}finally{s+='f'}}}var o={};Object.defineProperty(o,'x',{get:f});String(o.x+'/'+s)", "5/fr");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){let k=2;try{throw x}catch(e){let v=e+k;return v}finally{s+=k}}}String(f()+'/'+s)", "7/2r");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){return e}finally{let [u,v]=[3,4];s+=u+v}}}String(f()+'/'+s)", "5/7r");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:5,done:false}},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){return e}finally{try{throw 3}catch(q){s+=q}}}}String(f()+'/'+s)", "5/3r");
    agree("var s='';function f(){while(true){try{throw 1}catch(e){return e}finally{s+='f'}}}String(f()+'/'+s)", "1/f");
    agree("var s='';function f(){switch(1){case 1:try{throw 1}catch(e){return e}finally{s+='f'}}}String(f()+'/'+s)", "1/f");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='a';return {}}}};var b={};b[Symbol.iterator]=function(){return {next:function(){return {value:2,done:false}},return:function(){s+='b';throw 'B'}}};function f(){for(var x of a){for(var y of b){try{throw y}catch(e){return e}finally{s+='f'}}}}try{f()}catch(e){s+=e}String(s)", "fbaB");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';gc();return {}}}};function f(){for(var x of a){try{throw {v:x}}catch(e){return e}finally{s+='f';gc()}}}String(f().v+'/'+s)", "1/fr");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return 3}}};function f(){for(var x of a){try{throw x}catch(e){return e}finally{s+='f'}}}try{f()}catch(e){s+=e.name}String(s)", "frTypeError");
    agree("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){s+='n';throw 4},return:function(){s+='r';return {}}}};function f(){for(var x of a){try{throw x}catch(e){return e}finally{s+='f'}}}try{f()}catch(e){s+=e}String(s)", "n4");
    agree("var s='';function f(){for(var x of [2]){try{return missing_name}catch(e){s+=e.name;return 7}finally{s+='f'}}}String(f()+'/'+s)", "7/ReferenceErrorf");
    // When handlers and loops interleave, their completion order differs.
    for (const char* source : {"function f(){try{for(var x of [1]){try{throw x}catch(e){return e}finally{}}}finally{}}f()",
                               "function f(){try{for(var x of [1]){try{return x}finally{}}}catch(e){}}f()",
                               "function f(){for(var x of [1]){try{return x}finally{return 2}}}f()"}) {
        TestPage page;
        bool refused = false;
        try { page.js.eval_bytecode(source); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("not supported by the bytecode compiler") != std::string::npos;
        }
        assert(refused);
    }
}
