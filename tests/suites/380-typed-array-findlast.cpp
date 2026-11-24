// A typed array can be searched from the end.
//
// `Array.prototype` has had `findLast` and `findLastIndex` since the ES2023
// family landed, and `%TypedArray%.prototype` did not: every other
// `Array.prototype` method the language defines for typed arrays is installed
// there, so `new Uint8Array(3).findLast` answered `undefined` while
// `[1,2,3].findLast` answered a function — found by asking every method of
// §23.2.3 for its type on a `Uint8Array` and listing what was not a function.
// The answers below are Node v22.22.3's.
TEST(a_typed_array_can_be_searched_from_the_end) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // Both are methods, with the standard's length and name.
    assert(run("typeof new Uint8Array(3).findLast") == "function");
    assert(run("typeof new Uint8Array(3).findLastIndex") == "function");
    assert(run("new Uint8Array(3).findLast.length") == "1");
    assert(run("new Uint8Array(3).findLastIndex.length") == "1");
    assert(run("new Uint8Array(3).findLast.name") == "findLast");
    assert(run("new Uint8Array(3).findLastIndex.name") == "findLastIndex");

    // The value and the index of the first match from the end, not the first
    // from the start: `find` answers 1 and 0 here where these answer 3 and 2.
    assert(run("new Uint8Array([1,2,3,4]).findLast(function(x){return x<4;})") == "3");
    assert(run("new Uint8Array([1,2,3,4]).findLastIndex(function(x){return x<4;})") == "2");
    assert(run("new Uint8Array([1,2,3,4]).find(function(x){return x<4;})") == "1");
    assert(run("new Uint8Array([1,2,3,4]).findIndex(function(x){return x<4;})") == "0");

    // Nothing matches: `undefined` and -1, each of the right type.
    assert(run("typeof new Uint8Array([1,2,3]).findLast(function(x){return x>99;})") == "undefined");
    assert(run("new Uint8Array([1,2,3]).findLastIndex(function(x){return x>99;})") == "-1");
    assert(run("String(new Uint8Array(0).findLast(function(){return true;}))") == "undefined");
    assert(run("new Uint8Array(0).findLastIndex(function(){return true;})") == "-1");

    // The walk is downwards and stops at the first match: an element above the
    // match must not be visited after it, and nothing below it at all.
    assert(run("(function(){var seen=[];"
               "new Uint8Array([1,2,3,4]).findLastIndex(function(v,i){seen.push(i);return v===3;});"
               "return seen.join(',');})()") == "3,2");
    assert(run("(function(){var seen=[];"
               "new Uint8Array([10,20,30]).findLast(function(v,i,obj){"
               "seen.push(v+'@'+i+':'+(obj instanceof Uint8Array));return false;});"
               "return seen.join(' ');})()") == "30@2:true 20@1:true 10@0:true");

    // `thisArg` reaches the callback.
    assert(run("(function(){var ctx={};var got;"
               "new Uint8Array([1]).findLast(function(){got=(this===ctx);return false;},ctx);"
               "return String(got);})()") == "true");

    // A callback that is not callable is a TypeError, thrown before any
    // element is read.
    assert(run("(function(){try{new Uint8Array([1]).findLast(5);return 'no throw';}"
               "catch(e){return e.constructor.name;}})()") == "TypeError");
    assert(run("(function(){try{new Uint8Array([1]).findLastIndex(5);return 'no throw';}"
               "catch(e){return e.constructor.name;}})()") == "TypeError");

    // Every element type, through its own conversion: an Int32Array holds the
    // number it stored, and a Float64Array's `-0` is the `-0` it stored.
    assert(run("new Int32Array([7,8,9]).findLast(function(x){return x<9;})") == "8");
    assert(run("new Float64Array([1.5,2.5]).findLast(function(x){return x<2;})") == "1.5");
    assert(run("new Uint8ClampedArray([1,2,300]).findLastIndex(function(x){return x<255;})") == "1");
}
