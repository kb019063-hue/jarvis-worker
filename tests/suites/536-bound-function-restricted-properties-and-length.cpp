// Phase 536, finding 536: Bound functions enforce caller/arguments and correct length/name.
TEST(bound_function_restricted_properties_and_length) {
    TestPage page;
    auto must = [&](const char* src, const char* expected) {
        JsValue result = page.js.eval(src);
        std::string got = result.to_string();
        if (got != expected) {
            std::cerr << "FAIL: " << src << "\n  expected: " << expected << "\n  got:      " << got << "\n";
        }
        assert(got == expected);
    };

    // caller/arguments poison pill on bound function: get throws, set throws, no own property
    must("var f=function(){};var b=f.bind(null);var r=(function(){try{b.caller;return 'no'}catch(e){return e.name}})();r", "TypeError");
    must("var f=function(){};var b=f.bind(null);var r=(function(){try{b.arguments;return 'no'}catch(e){return e.name}})();r", "TypeError");
    must("var f=function(){};var b=f.bind(null);var r=(function(){try{b.caller=1;return 'no'}catch(e){return e.name}})();r+','+b.hasOwnProperty('caller')", "TypeError,false");
    must("var f=function(){};var b=f.bind(null);var r=(function(){try{b.arguments=1;return 'no'}catch(e){return e.name}})();r+','+b.hasOwnProperty('arguments')", "TypeError,false");
    must("var f=function(){};var b=f.bind(null);b.hasOwnProperty('caller')+','+b.hasOwnProperty('arguments')", "false,false");
    must("var f=function(){};var b=f.bind(null);Object.getOwnPropertyDescriptor(b,'caller')===undefined", "true");
    // delete inherited accessor allows shadowing
    must("var f=function(){};var b=f.bind(null);var proto=Object.getPrototypeOf(b);var desc=Object.getOwnPropertyDescriptor(proto,'caller');var has=proto.hasOwnProperty('caller');has+','+(typeof desc.get==='function')", "true,true");
    // strict function also inherits pill
    must("var g=function(){'use strict';};var r=(function(){try{g.caller;return 'no'}catch(e){return e.name}})();r+','+('caller' in g)+','+g.hasOwnProperty('caller')", "TypeError,true,false");
    // sloppy function has own null data properties shadowing pill? litebird installs null for sloppy, but strict does not
    // Ensure bound function of sloppy still throws (bound functions are not sloppy's own null)
    must("var f=function(){};var b=f.bind(null);var r=(function(){'use strict';try{b.caller=1;return 'no'}catch(e){return e.name}})();r", "TypeError");

    // length: own number with ToInteger
    must("var fn=function(){};Object.defineProperty(fn,'length',{value:3.66});String(fn.bind().length)", "3");
    must("var fn=function(){};Object.defineProperty(fn,'length',{value:-0.77});String(fn.bind().length)", "0");
    must("var fn=function(){};Object.defineProperty(fn,'length',{value:NaN});String(fn.bind().length)", "0");
    must("var fn=function(){};Object.defineProperty(fn,'length',{value:Infinity});String(fn.bind().length)", "Infinity");
    must("var fn=function(){};Object.defineProperty(fn,'length',{value:-Infinity});String(fn.bind().length)", "0");
    must("var fn=function(a,b,c){};String(fn.bind(null,1).length)", "2");
    must("var fn=function(){};Object.defineProperty(fn,'length',{value:3.66});String(fn.bind(null,1).length)", "2");
    // non-own length => 0
    must("var bar=function(){};Object.setPrototypeOf(bar,{length:42});delete bar.length;String(Function.prototype.bind.call(bar,null,1).length)", "0");
    // non-number length => 0
    must("var foo=function(){};Object.defineProperty(foo,'length',{value:Symbol('1')});String(foo.bind(null,1).length)", "0");
    must("var foo=function(){};Object.defineProperty(foo,'length',{value:'1'});String(foo.bind(null,1).length)", "0");
    must("var foo=function(){};Object.defineProperty(foo,'length',{value:null});String(foo.bind(null,1).length)", "0");
    // name: non-string => \"bound \"
    must("var t=Object.defineProperty(function(){},'name',{value:Symbol('s')});t.bind().name", "bound ");
    must("var t=Object.defineProperty(function(){},'name',{value:23});t.bind().name", "bound ");
    must("var t=Object.defineProperty(function(){},'name',{value:undefined});t.bind().name", "bound ");
    must("var t=Object.defineProperty(function(){},'name',{value:{}});t.bind().name", "bound ");
    must("var fn=function foo(){};fn.bind().name", "bound foo");
}
