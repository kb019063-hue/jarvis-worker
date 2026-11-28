// Phase 552, finding 552: dotted assignment resolves its base before its RHS.
TEST(the_bytecode_vm_rejects_nullish_dotted_writes) {
    using namespace litebird;
    auto agree = [](const char* source, const char* expected) {
        { TestPage page; assert(page.js.eval(source).to_string() == expected); }
        { TestPage page; assert(page.js.eval_bytecode(source).to_string() == expected); }
    };

    agree("try{null.x=1}catch(e){e.name}", "TypeError");
    agree("try{undefined.x=1}catch(e){e.name}", "TypeError");
    agree("try{null.x=1}catch(e){Object.getPrototypeOf(e)===TypeError.prototype}", "true");
    agree("try{null.x=1}catch(e){e.message}", "Cannot set properties of null (setting 'x')");
    agree("try{undefined.y=1}catch(e){e.message}", "Cannot set properties of undefined (setting 'y')");
    // Resolving a property reference precedes evaluating the RHS, whereas its
    // nullish-base error is deferred until PutValue after the RHS.
    agree("var s='';function base(){s+='b';return null}function rhs(){s+='r';return 1}try{base().x=rhs()}catch(e){s+=e.name}String(s)", "brTypeError");
    agree("var s='';function base(){s+='b';throw 'base'}function rhs(){s+='r';return 1}try{base().x=rhs()}catch(e){s+=e}String(s)", "bbase");
    agree("var s='';function base(){s+='b';return null}function rhs(){s+='r';throw 'rhs'}try{base().x=rhs()}catch(e){s+=e}String(s)", "brrhs");
    agree("var s='';function base(){s+='b';return undefined}try{base().x+=(s+='r')}catch(e){s+=e.name}String(s)", "bTypeError");
    agree("var s='';function base(){s+='b';return null}try{base().x++}catch(e){s+=e.name}String(s)", "bTypeError");
    // A successful store retains the original base through RHS mutation/GC.
    agree("var s='',o={x:1},saved=o;function base(){s+='b';return o}function rhs(){s+='r';o=null;gc();return 7}var v=(base().x=rhs());String(s+'/'+saved.x+'/'+v)", "br/7/7");
    agree("var a={},b={};var v=(a.x=b.y=4);String(a.x+'/'+b.y+'/'+v)", "4/4/4");
    agree("var o={x:1};String((o.x=3)+'/'+o.x)", "3/3");
    // A thrown write crosses frames, executes finalizers and closes iterators.
    agree("function f(){null.x=1}try{f()}catch(e){e.name}", "TypeError");
    agree("var s='';try{try{undefined.x=(s+='r')}finally{s+='f'}}catch(e){s+=e.name}String(s)", "rfTypeError");
    agree("var s='',it={};it[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='c';return {}}}};try{for(var x of it){null.x=1}}catch(e){s+=e.name}String(s)", "cTypeError");

    TestPage uncaught;
    bool threw = false;
    try { uncaught.js.eval_bytecode("null.x=1;"); }
    catch (const JsThrow& error) {
        threw = true;
        assert(js_describe_thrown(error.value) == "TypeError: Cannot set properties of null (setting 'x')");
    }
    assert(threw);
}
