// Phase 383: binary Math methods convert operands in ECMAScript order.
TEST(math_atan2_and_pow_convert_left_before_right) {
    TestPage page;
    JsInterpreter& js = page.js;
    for (const char* method : {"atan2", "pow"}) {
        const std::string call = std::string("Math.") + method;
        assert(js.eval("var log=[],a={valueOf:function(){log.push('a');return 2}},"
                       "b={valueOf:function(){log.push('b');return 3}};" + call +
                       "(a,b);log.join('/')").to_string() == "a/b");
        assert(js.eval("var log=[],a={valueOf:function(){log.push('a');throw 'first'}},"
                       "b={valueOf:function(){log.push('b');throw 'second'}};"
                       "try{" + call + "(a,b)}catch(e){log.push(e)}log.join('/')").to_string() == "a/first");
        assert(js.eval("var log=[],a={valueOf:function(){log.push('a');return 2}},"
                       "b={valueOf:function(){log.push('b');throw 'second'}};"
                       "try{" + call + "(a,b)}catch(e){log.push(e)}log.join('/')").to_string() == "a/b/second");
        assert(js.eval("var log=[],b={valueOf:function(){log.push('old');return 3}},"
                       "a={valueOf:function(){log.push('a');b.valueOf=function(){log.push('new');return 4};return 2}};"
                       "var r=" + call + "(a,b);log.join('/')+':'+String(r===" + call + "(2,4))")
                   .to_string() == "a/new:true");
        assert(js.eval("var log=[],b={valueOf:function(){log.push('b');return 3}};"
                       "try{" + call + "(Symbol(),b)}catch(e){log.push(e.name)}log.join('/')")
                   .to_string() == "TypeError");
        assert(js.eval("var log=[],a={valueOf:function(){log.push('a');return NaN}},"
                       "b={valueOf:function(){log.push('b');return 3}};"
                       "var r=" + call + "(a,b);log.join('/')+':'+String(isNaN(r))")
                   .to_string() == "a/b:true");
    }
    assert(js.eval("[Math.pow(2,3),1/Math.pow(-0,3),1/Math.atan2(-0,1),"
                   "Math.atan2(0,-1)===Math.PI].join('/')").to_string() == "8/-Infinity/-Infinity/true");
}
