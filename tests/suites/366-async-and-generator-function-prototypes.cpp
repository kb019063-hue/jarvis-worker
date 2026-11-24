// Phase 366, finding 366: an async function and a generator function each have
// a prototype of their own between them and %Function.prototype%, and a native
// function defines `length` before `name`.
//
// Every assertion below was checked field for field against Node v22.22.3
// first, and three of them were written the wrong way round before that: the
// kind's prototype is a *callable object*, not a language function (`typeof` is
// "object"), the two constructors are not globals, and this engine spells
// symbol keys as "@@name" strings, so `@@toStringTag` is an own property of the
// prototype under that name.
TEST(async_and_generator_functions_are_not_ordinary_functions) {
    TestPage page;
    JsInterpreter& js = page.js;

    // §20.2.3.2 and §27.3.3.2: the prototype of each kind is its own object,
    // between the function and %Function.prototype%, not %Function.prototype%
    // itself. Before this phase both kinds hung directly under it, so nothing
    // could tell the three apart: `typeof` says "function" for all of them.
    assert(js.eval("Object.getPrototypeOf(async function () {}) === Function.prototype")
               .to_string() == "false");
    assert(js.eval("Object.getPrototypeOf(function* () {}) === Function.prototype")
               .to_string() == "false");
    assert(js.eval("Object.getPrototypeOf(async function () {}) === Object.getPrototypeOf(function* () {})")
               .to_string() == "false");
    // Both are one step below %Function.prototype% and no further.
    assert(js.eval(
               "Object.getPrototypeOf(Object.getPrototypeOf(async function () {})) === Function.prototype")
               .to_string() == "true");
    assert(js.eval(
               "Object.getPrototypeOf(Object.getPrototypeOf(function* () {})) === Function.prototype")
               .to_string() == "true");

    // The kind is a callable object, not a language function: Node answers
    // "object" to `typeof` and "function" to `typeof p.call`, because the
    // prototype has no [[FunctionKind]] of its own.
    assert(js.eval("typeof Object.getPrototypeOf(async function () {})").to_string() == "object");
    assert(js.eval("typeof Object.getPrototypeOf(function* () {})").to_string() == "object");

    // §20.1.3.6 step 15: @@toStringTag is read *before* the built-in tag, so
    // each kind answers its own name. This is the half that needed the tag to
    // exist; before it, `object_tag` returned "Function" for every function
    // before it ever looked for a tag.
    assert(js.eval("Object.prototype.toString.call(async function () {})").to_string()
           == "[object AsyncFunction]");
    assert(js.eval("Object.prototype.toString.call(function* () {})").to_string()
           == "[object GeneratorFunction]");

    // An ordinary function and a native one carry no tag and still answer
    // "Function" — the fallthrough did not eat them.
    assert(js.eval("Object.prototype.toString.call(function () {})").to_string()
           == "[object Function]");
    assert(js.eval("Object.prototype.toString.call(Math.max)").to_string()
           == "[object Function]");
    assert(js.eval("Object.prototype.toString.call(function () {})").to_string()
           != "[object AsyncFunction]");

    // The tag is neither writable nor enumerable but is configurable, which is
    // Node's descriptor exactly: {value, writable: false, enumerable: false,
    // configurable: true}.
    assert(js.eval(
               "var d = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(async function () {}),"
               " '@@toStringTag');"
               "String(d.writable) + '/' + String(d.enumerable) + '/' + String(d.configurable)")
               .to_string() == "false/false/true");
    assert(js.eval("Object.getOwnPropertyDescriptor(Object.getPrototypeOf(function* () {}),"
                   " '@@toStringTag').value")
               .to_string() == "GeneratorFunction");

    // Each prototype has a `constructor` back to a function of its kind's name,
    // and neither constructor is a global (Node: `typeof AsyncFunction` is
    // "undefined"), which is why they are reachable only from here.
    assert(js.eval("Object.getPrototypeOf(async function () {}).constructor.name").to_string()
           == "AsyncFunction");
    assert(js.eval("Object.getPrototypeOf(function* () {}).constructor.name").to_string()
           == "GeneratorFunction");
    assert(js.eval("typeof this.AsyncFunction").to_string() == "undefined");
    assert(js.eval("typeof this.GeneratorFunction").to_string() == "undefined");

    // §27.3.3.2: the generator kind's prototype also has a `prototype` of its
    // own, the object a generator makes, tagged "Generator".
    assert(js.eval("Object.getOwnPropertyNames(Object.getPrototypeOf(function* () {})).join()")
               .to_string() == "prototype,constructor"
           || js.eval("Object.getOwnPropertyNames(Object.getPrototypeOf(function* () {})).indexOf('prototype') >= 0")
                      .to_string() == "true");
    assert(js.eval(
               "Object.prototype.toString.call(Object.getOwnPropertyDescriptor("
               "Object.getPrototypeOf(function* () {}), 'prototype').value)")
               .to_string() == "[object Generator]");

    // §10.2.4 and §10.2.5 define `length` before `name`, and the order is
    // visible in `Object.getOwnPropertyNames`. Every native function was
    // `name,length`; the three test262 property-order cases named this.
    assert(js.eval("Object.getOwnPropertyNames(Math.max).join()").to_string() == "length,name");
    assert(js.eval("Object.getOwnPropertyNames(Array.prototype.push).join()").to_string()
           == "length,name");
    // A script function already had the right order and must keep it. It has
    // more own names than these two (Node lists length,name,arguments,caller,
    // prototype), so only the first two are asked about.
    assert(js.eval(
               "Object.getOwnPropertyNames(function (a, b) {}).slice(0, 2).join()")
               .to_string() == "length,name");

    // Nothing here made an async or a generator function itself constructible:
    // `new` on one is a TypeError, and always was.
    assert(js.eval("(function () { try { new (function* () {})(); return 'made'; }"
                   " catch (e) { return e.name; } })()")
               .to_string() == "TypeError");
    assert(js.eval("(function () { try { new (async function () {})(); return 'made'; }"
                   " catch (e) { return e.name; } })()")
               .to_string() == "TypeError");

    // A generator still yields and an async function still settles: the
    // prototype moved, the behaviour did not.
    assert(js.eval(
               "var out = '';"
               "for (var v of (function* () { yield 1; yield 2; })()) out += v;"
               "out")
               .to_string() == "12");
    assert(js.eval("typeof (function* () {})()").to_string() == "object");
    assert(js.eval("typeof (async function () {})().then").to_string() == "function");
}
