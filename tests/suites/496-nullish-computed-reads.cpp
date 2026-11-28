// Phase 496, finding 496: a nullish computed read never coerces its key.
TEST(nullish_computed_reads_evaluate_the_key_but_do_not_convert_it) {
    auto answer = [](const char* source, const char* expected) {
        TestPage page;
        assert(page.js.eval(source).to_string() == expected);
    };

    // The original Phase 474 witness: Node says TypeError, not cTypeError.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{null[k]}catch(e){s+=e.name}String(s)", "TypeError");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{undefined[k]}catch(e){s+=e.name}String(s)", "TypeError");
    // The key expression executes first; conversion does not. A failing key
    // expression wins over the TypeError, but a failing conversion does not.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{null[(s+='k',k)]}catch(e){s+=e.name}String(s)", "kTypeError");
    answer("var k={toString:function(){throw 'converted'}};try{undefined[k]}catch(e){e.name}", "TypeError");
    answer("function key(){throw 'expression'}try{null[key()]}catch(e){e}", "expression");
    // A computed method call is also a read: the argument does not run after
    // the base fails, and object-key conversion must not run either.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{null[(s+='k',k)](s+='a')}catch(e){s+=e.name}String(s)", "kTypeError");
    answer("var s='';try{null?.[(s+='k')]}catch(e){s+='bad'}String(s)", "");
    // For a non-nullish receiver conversion still happens exactly once, and a
    // computed method call retains the receiver as `this`.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};var o={x:7};String(o[k])+'/'+s", "7/c");
    answer("var s='';var k={toString:function(){s+='c';return 'm'}};var o={x:7,m:function(){return this.x}};String(o[k]())+'/'+s", "7/c");
    answer("try{null['foo']}catch(e){e.message}", "Cannot read properties of null (reading 'foo')");
    answer("try{undefined['bar']()}catch(e){e.message}", "Cannot read properties of undefined (reading 'bar')");
}
