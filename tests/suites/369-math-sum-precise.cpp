// Phase 369: Math.sumPrecise adds exactly, then rounds once.
TEST(math_sum_precise_sums_exactly_and_closes_iterators) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("typeof Math.sumPrecise+'/'+Math.sumPrecise.name+'/'+Math.sumPrecise.length").to_string() ==
           "function/sumPrecise/1");
    assert(js.eval("var d=Object.getOwnPropertyDescriptor(Math,'sumPrecise');"
                   "[d.writable,d.enumerable,d.configurable].join('/')").to_string() == "true/false/true");
    assert(js.eval("[1/Math.sumPrecise([]),1/Math.sumPrecise([-0,-0]),1/Math.sumPrecise([0,-0]),"
                   "1/Math.sumPrecise([1,-1])].join('/')").to_string() == "-Infinity/-Infinity/Infinity/Infinity");
    assert(js.eval("Math.sumPrecise([1e16,1,-1e16])").to_string() == "1");
    assert(js.eval("Math.sumPrecise([Number.MAX_VALUE,Number.MAX_VALUE,-Number.MAX_VALUE]) === Number.MAX_VALUE")
               .to_string() == "true");
    assert(js.eval("Math.sumPrecise([Number.MAX_VALUE,Number.MAX_VALUE]) === Infinity").to_string() == "true");
    assert(js.eval("[Math.sumPrecise([5e-324,5e-324,-5e-324])===5e-324,"
                   "isNaN(Math.sumPrecise([Infinity,-Infinity])),isNaN(Math.sumPrecise([NaN,1]))].join('/')")
               .to_string() == "true/true/true");
    assert(js.eval("var r; try { new Math.sumPrecise([]) } catch(e) { r=e.name } r").to_string() == "TypeError");
    assert(js.eval("var r; try { Math.sumPrecise([new Number(1)]) } catch(e) { r=e.name } r").to_string() == "TypeError");
    assert(js.eval("var r; try { Math.sumPrecise([,]) } catch(e) { r=e.name } r").to_string() == "TypeError");
    // NaN must not stop iteration or hide a later type error; an abrupt value
    // closes the iterator before the TypeError reaches the caller.
    assert(js.eval("var seen=[],i=0,o={};o[Symbol.iterator]=function(){return {"
                   "next:function(){seen.push('next');i++;return i===1?{value:NaN,done:false}:{value:'bad',done:false}},"
                   "return:function(){seen.push('return');return {done:true}}}};"
                   "try {Math.sumPrecise(o)} catch(e) {seen.push(e.name)} seen.join('/')")
               .to_string() == "next/next/return/TypeError");
    // An exception from next itself must not trigger IteratorClose.
    assert(js.eval("var seen=[],o={};o[Symbol.iterator]=function(){return {"
                   "next:function(){seen.push('next');throw 7},"
                   "return:function(){seen.push('return');return {done:true}}}};"
                   "try{Math.sumPrecise(o)}catch(e){seen.push(e)} seen.join('/')")
               .to_string() == "next/7");
}
