// Phase 500: hypot retains many small contributions after a large argument.
TEST(math_hypot_compensates_multi_argument_sum) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("var a=[1];for(var i=0;i<100;i++)a.push(1e-8);"
                   "var r=Math.hypot.apply(null,a);r>1 && Math.abs(r-1.000000000000005)<4.5e-16")
               .to_string() == "true");
    assert(js.eval("var a=[1e-200];for(var i=0;i<100;i++)a.push(1e-208);"
                   "var r=Math.hypot.apply(null,a);r>1e-200 && Math.abs(r/1e-200-1.000000000000005)<4.5e-16")
               .to_string() == "true");
    assert(js.eval("var a=[1];for(var i=0;i<100;i++)a.push(1e-8);"
                   "var first=Math.hypot.apply(null,a);a.reverse();"
                   "Math.abs(first-Math.hypot.apply(null,a))<4.5e-16")
               .to_string() == "true");
    assert(js.eval("Math.hypot(3,4)===5 && 1/Math.hypot(-0,-0,-0)===Infinity && "
                   "Math.hypot(Infinity,NaN,1)===Infinity && isNaN(Math.hypot(NaN,1,2))")
               .to_string() == "true");
    assert(js.eval("var log=[],a={valueOf:function(){log.push('a');return Infinity}},"
                   "b={valueOf:function(){log.push('b');throw 'bad'}};"
                   "try{Math.hypot(a,b,1)}catch(e){log.push(e)}log.join('/')")
               .to_string() == "a/b/bad");
}
