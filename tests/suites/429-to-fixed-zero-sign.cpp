// Phase 429: toFixed preserves the sign of negative nonzero inputs.
TEST(number_to_fixed_rounded_zero_keeps_negative_sign) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("(-0.0001).toFixed(2)+'/'+(-0.49).toFixed(0)+'/'+"
                   "(-5e-324).toFixed(6)").to_string() == "-0.00/-0/-0.000000");
    assert(js.eval("(-0).toFixed(0)+'/'+(-0).toFixed(2)+'/'+(0).toFixed(2)+'/'+"
                   "(0.0001).toFixed(2)").to_string() == "0/0.00/0.00/0.00");
    assert(js.eval("(-0.5).toFixed(0)+'/'+(-1.25).toFixed(1)+'/'+"
                   "(0.5).toFixed(0)").to_string() == "-1/-1.3/1");
    assert(js.eval("(-5e-324).toFixed(100)").to_string() == "-0." + std::string(100, '0'));
    assert(js.eval("NaN.toFixed(2)+'/'+Infinity.toFixed(2)+'/'+(-Infinity).toFixed(2)")
               .to_string() == "NaN/Infinity/-Infinity");
}
