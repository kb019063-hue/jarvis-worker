// Phase 392: cube-root refinement must not overflow on finite inputs.
TEST(math_cbrt_finite_extremes_and_special_values) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("var x=Number.MAX_VALUE, r=Math.cbrt(x);"
                   "isFinite(r)&&r>0&&Math.abs(r/5.643803094122362e102-1)<1e-14")
               .to_string() == "true");
    assert(js.eval("var x=-Number.MAX_VALUE, r=Math.cbrt(x);"
                   "isFinite(r)&&r<0&&Math.abs(r/-5.643803094122362e102-1)<1e-14")
               .to_string() == "true");
    assert(js.eval("var r=Math.cbrt(1.7976931348623155e308);"
                   "isFinite(r)&&r>0").to_string() == "true");
    assert(js.eval("Math.cbrt(27)===3 && Math.cbrt(-27)===-3 && "
                   "Math.cbrt(64)===4 && 1/Math.cbrt(-0)===-Infinity && "
                   "Math.cbrt(Infinity)===Infinity && Math.cbrt(-Infinity)===-Infinity && "
                   "isNaN(Math.cbrt(NaN))").to_string() == "true");
    assert(js.eval("var r=Math.cbrt(5e-324);r>0 && "
                   "Math.abs(r/1.7031839360032603e-108-1)<1e-14").to_string() == "true");
}
