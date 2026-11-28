// Phase 510, finding 510: nullish computed references do not convert keys.
TEST(nullish_computed_writes_and_delete_do_not_convert_the_key) {
    auto answer = [](const char* source, const char* expected) {
        TestPage page;
        assert(page.js.eval(source).to_string() == expected);
    };
    // For plain assignment the key expression and RHS run, then PutValue
    // throws; an object key's conversion never runs.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};function key(){s+='k';return k}try{null[key()]=(s+='r')}catch(e){s+=e.name}String(s)", "krTypeError");
    answer("var s='';var k={toString:function(){throw 'converted'}};try{undefined[k]=(s+='r')}catch(e){s+=e.name}String(s)", "rTypeError");
    answer("var s='';var k={};Object.defineProperty(k,Symbol.toPrimitive,{get:function(){s+='g';return function(){return 'x'}}});try{null[k]=(s+='r')}catch(e){s+=e.name}String(s)", "rTypeError");
    answer("function key(){throw 'key'}try{null[key()]=(function(){throw 'rhs'})()}catch(e){e}", "key");
    answer("var k={toString:function(){throw 'converted'}};try{null[k]=(function(){throw 'rhs'})()}catch(e){e}", "rhs");
    // Reads in compound, logical and update operations throw before RHS.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};function key(){s+='k';return k}try{null[key()]+=(s+='r')}catch(e){s+=e.name}String(s)", "kTypeError");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{undefined[k]||=(s+='r')}catch(e){s+=e.name}String(s)", "TypeError");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{null[k]++}catch(e){s+=e.name}String(s)", "TypeError");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{++undefined[k]}catch(e){s+=e.name}String(s)", "TypeError");
    // Destructuring evaluates the reference before its default or iterator
    // step, and closes the iterator on the eventual abrupt completion.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};try{[null[k]=(s+='r')]=[undefined]}catch(e){s+=e.name}String(s)", "rTypeError");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};var it={};it[Symbol.iterator]=function(){return {next:function(){s+='n';return {value:1,done:false}},return:function(){s+='x';return {}}}};try{[null[k]]=it}catch(e){s+=e.name}String(s)", "nxTypeError");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};var it={};it[Symbol.iterator]=function(){return {next:function(){s+='n';throw 'step'},return:function(){s+='x';return {}}}};try{[null[k]]=it}catch(e){s+=e}String(s)", "nstep");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};var it={};it[Symbol.iterator]=function(){var n=0;return {next:function(){s+='n';return {value:++n,done:n>2}},return:function(){s+='x';return {}}}};try{[...null[k]]=it}catch(e){s+=e.name}String(s)", "nnnTypeError");
    // delete evaluates the expression but does not convert on a nullish base.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};function key(){s+='k';return k}try{delete null[key()]}catch(e){s+=e.name}String(s)", "kTypeError");
    answer("var k={toString:function(){throw 'converted'}};try{delete undefined[k]}catch(e){e.name}", "TypeError");
    answer("function key(){throw 'key'}try{delete null[key()]}catch(e){e}", "key");
    // Ordinary bases still convert and retain existing mutation semantics.
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};var o={x:1};o[k]=2;String(o.x)+'/'+s", "2/c");
    answer("var s='';var k={toString:function(){s+='c';return 'x'}};var o={x:1};delete o[k];String('x' in o)+'/'+s", "false/c");
    answer("try{null['x']=1}catch(e){e.message}", "Cannot set properties of null (setting 'x')");
}
