// Phase 465: significant-digit formatting rounds exact ties up in magnitude.
TEST(number_significant_formatting_rounds_decimal_ties_up) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("[(1.25).toExponential(1),(-1.25).toExponential(1),"
                   "(0.125).toPrecision(2),(-0.125).toPrecision(2),"
                   "(2.5).toExponential(0),(2.5).toPrecision(1)].join('/')")
               .to_string() == "1.3e+0/-1.3e+0/0.13/-0.13/3e+0/3");
    assert(js.eval("[(9.5).toExponential(0),(99.5).toPrecision(2),"
                   "(9.95).toExponential(1),(9.95).toPrecision(2)].join('/')")
               .to_string() == "1e+1/1.0e+2/9.9e+0/9.9");
    assert(js.eval("[(1.2499999999999998).toExponential(1),"
                   "(1.2500000000000002).toExponential(1),"
                   "(-0).toPrecision(2),(0).toExponential(0)].join('/')")
               .to_string() == "1.2e+0/1.3e+0/0.0/0e+0");
    assert(js.eval("[(Infinity).toExponential(2),NaN.toPrecision(2),"
                   "(1.25).toExponential(),(1.25).toPrecision()].join('/')")
               .to_string() == "Infinity/NaN/1.25e+0/1.25");
}
