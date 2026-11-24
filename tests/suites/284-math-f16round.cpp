// Phase 284: Math.f16round rounds binary64 straight to binary16.
TEST(math_f16round_rounds_without_double_rounding) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("typeof Math.f16round + '/' + Math.f16round.name + '/' + Math.f16round.length").to_string() ==
           "function/f16round/1");
    assert(js.eval("var d = Object.getOwnPropertyDescriptor(Math,'f16round');"
                   "[d.writable,d.enumerable,d.configurable].join('/')").to_string() == "true/false/true");
    assert(js.eval("var t; try { new Math.f16round(1); } catch(e) { t=e.name; } t").to_string() == "TypeError");
    assert(js.eval("var t; try { Math.f16round(Symbol()); } catch(e) { t=e.name; } t").to_string() == "TypeError");
    assert(js.eval("var n=0; var x={valueOf:function(){n++;return '1.5'}};"
                   "String(Math.f16round(x))+'/'+n").to_string() == "1.5/1");
    assert(js.eval("String(isNaN(Math.f16round()))+'/'+String(isNaN(Math.f16round(NaN)))").to_string() == "true/true");
    assert(js.eval("[1/Math.f16round(-0),1/Math.f16round(-1e-20),Math.f16round(Infinity),"
                   "Math.f16round(-Infinity)].join('/')").to_string() == "-Infinity/-Infinity/Infinity/-Infinity");
    // At exact halfway, even significands win; either neighbor is distinguishable.
    assert(js.eval("[Math.f16round(1.00048828125),Math.f16round(1.00146484375),"
                   "Math.f16round(1.0004882812500002),Math.f16round(1.0004882812499998)].join('/')")
               .to_string() == "1/1.001953125/1.0009765625/1");
    // Half the smallest subnormal ties to zero; the next binary64 does not.
    assert(js.eval("[Math.f16round(2.9802322387695312e-8),Math.f16round(2.980232238769532e-8),"
                   "Math.f16round(65519),Math.f16round(65520)].join('/')")
               .to_string() == "0/5.960464477539063e-8/65504/Infinity");
}
