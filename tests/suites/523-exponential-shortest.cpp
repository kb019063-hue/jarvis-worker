// Phase 523: shortest exponential exponents belong to the shortest digits.
TEST(number_to_exponential_shortest_uses_its_own_exponent) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("[(1e23).toExponential(),(-1e23).toExponential(),"
                   "(1e-6).toExponential(),(1e-7).toExponential(),"
                   "(1e-323).toExponential()].join('/')")
               .to_string() == "1e+23/-1e+23/1e-6/1e-7/1e-323");
    assert(js.eval("[(0).toExponential(),(-0).toExponential(),"
                   "(12.34).toExponential(),(0.01234).toExponential(),"
                   "(1e20).toExponential(),(5e-324).toExponential()].join('/')")
               .to_string() == "0e+0/0e+0/1.234e+1/1.234e-2/1e+20/5e-324");
    assert(js.eval("[(1.25).toExponential(1),(9.5).toExponential(0),"
                   "Infinity.toExponential(),NaN.toExponential()].join('/')")
               .to_string() == "1.3e+0/1e+1/Infinity/NaN");
}
