// Phase 281, finding 286: an arguments object is an ordinary object with the
// array iteration protocol, not an Array.
TEST(arguments_is_an_object_with_the_array_iteration_protocol) {
    // §10.4.4: an arguments object's [[Prototype]] is %Object.prototype%, not
    // %Array.prototype%. It was `make_array`, so the prototype was the wrong
    // one and every inherited name came out of the wrong place: `#<Array>`
    // where V8 says `#<Object>` in a delete refusal's message, and
    // `typeof arguments.push` a function.
    //
    // §10.4.4.7 is the other half and the reason the prototype alone is not
    // enough: the object gets its *own* `@@iterator`, the same function object
    // as %Array.prototype.values%, so `[...arguments]` and `for…of` keep
    // working once the prototype no longer supplies one. Doing only the first
    // half broke 75 test262 cases.
    TestPage page;
    JsInterpreter& js = page.js;

    // The prototype, and what follows from it.
    assert(js.eval("Object.getPrototypeOf((function () { return arguments; })()) === Object.prototype")
               .to_string() == "true");
    assert(js.eval("Array.isArray((function () { return arguments; })())").to_string() == "false");
    assert(js.eval("typeof (function () { return arguments; })().push").to_string() == "undefined");
    assert(js.eval("String((function () { return arguments; })())").to_string() == "[object Arguments]");

    // The iteration protocol still works, from the object's own @@iterator.
    assert(js.eval("[...(function () { return arguments; })(1, 2, 3)].join()").to_string() == "1,2,3");
    assert(js.eval(
               "var s = '';"
               "for (var v of (function () { return arguments; })('a', 'b')) s += v;"
               "s")
               .to_string() == "ab");
    assert(js.eval("typeof (function () { return arguments; })()[Symbol.iterator]").to_string() == "function");

    // §10.4.4.7: it is *the same function object* as %Array.prototype.values%.
    assert(js.eval("(function () { return arguments; })()[Symbol.iterator] === Array.prototype.values")
               .to_string() == "true");

    // The message the row named: a strict delete of a non-configurable place
    // says `#<Object>` now, because that is what the receiver is.
    assert(js.eval(
               "var args;"
               "(function () { args = arguments; })();"
               "Object.defineProperty(args, '0', { value: 1, configurable: false });"
               "var answer = 'none';"
               "try { (function () { 'use strict'; delete args[0]; })(); } catch (e) { answer = e.message; }"
               "answer")
               .to_string() == "Cannot delete property '0' of #<Object>");

    // The index places and `length` are unaffected: this is a prototype
    // change, not a shape change.
    // The places and `length`, which this change must not have moved.
    //
    // `callee` is *not* asserted here: §10.4.4.6 gives a sloppy arguments
    // object its own `callee` (Node: `typeof a.callee === 'function'`, and
    // own), and this engine answers `undefined` and not-own — before this
    // phase as well as after it. A separate gap, named in the phase doc's
    // "still open", not something a prototype change should be asked to fix.
    assert(js.eval(
               "var a = (function () { return arguments; })(7, 8);"
               "[a.length, a[0], a[1], a[5]].join('/')")
               .to_string() == "2/7/8/");
}
