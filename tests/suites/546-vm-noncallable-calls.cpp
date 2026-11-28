// Phase 546, finding 546: a VM call of a non-callable value throws TypeError.
TEST(the_bytecode_vm_rejects_noncallable_calls) {
    using namespace litebird;
    auto agree = [](const char* source, const char* expected) {
        {
            TestPage tree;
            assert(tree.js.eval(source).to_string() == expected);
        }
        {
            TestPage bytecode;
            assert(bytecode.js.eval_bytecode(source).to_string() == expected);
        }
    };

    agree("try{(0)()}catch(e){e.name}", "TypeError");
    agree("try{null()}catch(e){e.name}", "TypeError");
    agree("try{undefined()}catch(e){e.name}", "TypeError");
    agree("try{false()}catch(e){e.name}", "TypeError");
    agree("try{('x')()}catch(e){e.name}", "TypeError");
    agree("try{({})()}catch(e){e.name}", "TypeError");
    agree("try{([])()}catch(e){e.name}", "TypeError");
    agree("try{Symbol('x')()}catch(e){e.name}", "TypeError");
    agree("try{(0)()}catch(e){String(Object.getPrototypeOf(e)===TypeError.prototype)}", "true");
    // All arguments are evaluated before IsCallable is tested; a throwing
    // argument takes precedence over the non-callable TypeError.
    agree("var s='';try{(0)(s+='a',s+='b')}catch(e){s+=e.name}String(s)", "abTypeError");
    agree("function arg(){throw 'argument'}try{(0)(arg())}catch(e){e}", "argument");
    // Rejecting an object must not coerce it or execute its toString method.
    agree("var s='',o={toString:function(){s+='c';return 'x'}};try{o()}catch(e){s+=e.name}String(s)", "TypeError");
    agree("var o={toString:function(){throw 'converted'}};try{o()}catch(e){e.name}", "TypeError");
    // A TypeError crosses a VM frame and closes an active iterator.
    agree("function f(){(0)()}try{f()}catch(e){e.name}", "TypeError");
    agree("var s='',it={};it[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};try{for(var x of it){(0)()}}catch(e){s+=e.name}String(s)", "rTypeError");
    agree("var s='';try{try{(0)()}finally{s+='f'}}catch(e){s+=e.name}String(s)", "fTypeError");
    agree("function f(x){return x+1}String(f(2))", "3");

    TestPage uncaught;
    bool threw = false;
    try { uncaught.js.eval_bytecode("(0)();"); }
    catch (const JsThrow& error) {
        threw = true;
        assert(js_describe_thrown(error.value) == "TypeError: expression is not a function");
    }
    assert(threw);
}
