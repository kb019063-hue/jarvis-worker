// Phase 407: non-finite receivers skip the formatting precision range check.
TEST(number_format_special_values_convert_before_returning) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("[NaN.toExponential(-1),Infinity.toExponential(101),"
                   "(-Infinity).toExponential(Infinity),NaN.toPrecision(0),"
                   "Infinity.toPrecision(-1),(-Infinity).toPrecision(101)].join('/')")
               .to_string() == "NaN/Infinity/-Infinity/NaN/Infinity/-Infinity");
    for (const char* method : {"toExponential", "toPrecision"}) {
        const std::string call = std::string("Number.prototype.") + method;
        assert(js.eval("var log=[];var arg={valueOf:function(){log.push('convert');return 101}};"
                       "var result=" + call + ".call(Infinity,arg);log.join('/')+'/'+result")
                   .to_string() == "convert/Infinity");
        assert(js.eval("var log=[];var arg={valueOf:function(){log.push('convert');throw 'boom'}};"
                       "try{" + call + ".call(NaN,arg)}catch(e){log.push(e)}log.join('/')")
                   .to_string() == "convert/boom");
        assert(js.eval("try{" + call + ".call(NaN,Symbol())}catch(e){e.name}")
                   .to_string() == "TypeError");
        assert(js.eval("try{" + call + ".call(1,101)}catch(e){e.name}")
                   .to_string() == "RangeError");
        assert(js.eval("var log=[];try{" + call + ".call({}, {valueOf:function(){log.push('wrong');return 0}})}"
                       "catch(e){log.push(e.name)}log.join('/')").to_string() == "TypeError");
    }
    assert(js.eval("try{NaN.toFixed(101)}catch(e){e.name}").to_string() == "RangeError");
    assert(js.eval("(1).toExponential(0)+'/'+(1).toPrecision(1)+'/'+(1).toFixed(0)")
               .to_string() == "1e+0/1/1");
}
