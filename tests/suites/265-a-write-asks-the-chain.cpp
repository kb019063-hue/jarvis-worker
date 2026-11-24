// Phase 265, finding 269: an array's index asks its chain, and a build does not.
TEST(a_write_to_an_array_index_asks_the_chain) {
    // A place the array does not own -- a hole, or past the end -- is a key
    // its prototype chain may answer for (ECMAScript 10.1.9.2,
    // OrdinarySetWithOwnDescriptor). The three answers, and the two ways this
    // was wrong before: the chain was never asked (so a setter never ran), and
    // when it was, a *writable data* property was reported as handled and the
    // write refused where it should have been shadowed.
    TestPage page;
    JsInterpreter& js = page.js;

    // `[value an array ends up holding, whether it owns the place]`, with one
    // descriptor installed on Array.prototype[0] and taken away again.
    auto write_with = [&](const char* descriptor) -> std::string {
        std::string probe =
            "Object.defineProperty(Array.prototype, '0', " + std::string(descriptor) + ");"
            "var a = [];"
            "a[0] = 5;"
            "var answer = String(a[0]) + '/' + String(Object.prototype.hasOwnProperty.call(a, '0'));"
            "delete Array.prototype[0];"
            "answer";
        try {
            return js.eval(probe).to_string();
        } catch (const JsThrow& thrown) {
            return "threw: " + thrown.value.to_string();
        }
    };

    // A setter runs instead of the write, and the place stays a hole. Both the
    // setter-and-getter and the setter-alone shapes: with no getter the read
    // reaches nothing and answers `undefined`, which is still not 5 and still
    // not the array's own place.
    assert(write_with("{ set: function (v) {}, get: function () { return 'p'; }, configurable: true }") ==
           "p/false");
    assert(write_with("{ set: function (v) {}, configurable: true }") == "undefined/false");

    // An accessor with no setter refuses the write, so the getter's value is
    // what reads back and the array stays empty.
    assert(write_with("{ get: function () { return 'p'; }, configurable: true }") == "p/false");

    // A non-writable data property refuses it too. This is the case that needs
    // the walk to be gated on every index-named key rather than only on
    // accessors: there is no accessor anywhere in this program.
    assert(write_with("{ value: 'p', writable: false, configurable: true }") == "p/false");

    // A *writable* data property is shadowed by the element write -- the bug
    // #193 shipped, where `handled` was set before it knew what it had found.
    assert(write_with("{ value: 'p', writable: true, configurable: true }") == "5/true");

    // The strict-mode half: a refused write must throw rather than be dropped.
    assert(js.eval(
               "Object.defineProperty(Array.prototype, '0', { value: 'p', writable: false, configurable: true });"
               "var threw = 'none';"
               "try { (function () { 'use strict'; var a = []; a[0] = 5; })(); } catch (e) { threw = e.name; }"
               "delete Array.prototype[0];"
               "threw")
               .to_string() == "TypeError");

    // An array with nothing on its chain still writes its own elements: the
    // gate is a load, and the answer must not change with the count.
    assert(js.eval("var b = []; b[0] = 1; b[3] = 4; String(b[0]) + String(b[1]) + String(b[3])").to_string() ==
           "1undefined4");
    assert(js.eval("var c = [1, 2, 3]; c[1] = 9; c.join()").to_string() == "1,9,3");

    // The half-in-each-place case: an index defined on an array the elements
    // did not reach used to keep its value in the element and its attributes
    // in the map, so `hasOwnProperty` said false while the read said 'p'.
    assert(js.eval(
               "var arr = [];"
               "Object.defineProperty(arr, '3', { value: 'p', writable: false, configurable: true });"
               "var d = Object.getOwnPropertyDescriptor(arr, '3');"
               "[String(Object.prototype.hasOwnProperty.call(arr, '3')), arr[3], arr.length,"
               " String(d.writable), d.value, String(d.enumerable), Object.keys(arr).length].join('/')")
               .to_string() == "true/p/4/false/p/false/0");

    // And over an element the array already had: the attributes still follow
    // the descriptor, and the place keeps the *enumerable* it already had,
    // because a define that does not mention it leaves it alone.
    assert(js.eval(
               "var arr2 = []; arr2[5] = 'x';"
               "Object.defineProperty(arr2, '5', { value: 'p', writable: false, configurable: true });"
               "var d2 = Object.getOwnPropertyDescriptor(arr2, '5');"
               "String(d2.writable) + '/' + d2.value + '/' + String(d2.enumerable) + '/' + arr2[5]")
               .to_string() == "false/p/true/p");

    // The build half: a method that *makes* an array defines its places
    // (7.3.6), so a setter the chain answers for cannot swallow a result.
    // `Array.from` and the array `splice` returns were still Set until this
    // phase, which is invisible until a write to a hole asks the chain.
    assert(js.eval(
               "Object.defineProperty(Array.prototype, '0', { set: function (v) {}, get: function () { return 'p'; },"
               " configurable: true });"
               "var f = Array.from([7]);"
               "var s = [7, 8].splice(0, 2);"
               "var m = [7].map(function (x) { return x; });"
               "var answer = [f[0], Object.prototype.hasOwnProperty.call(f, '0'),"
               "              s[0], Object.prototype.hasOwnProperty.call(s, '0'),"
               "              m[0], Object.prototype.hasOwnProperty.call(m, '0')].join('/');"
               "delete Array.prototype[0];"
               "answer")
               .to_string() == "7/true/7/true/7/true");
}
