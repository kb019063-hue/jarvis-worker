// Phase 403, finding 403: a class's `extends` clause checks that its parent's
// `prototype` is an object or null, and refuses anything else.
//
// §15.7.14 (ClassDefinitionEvaluation) step 5: `protoParent = ? Get(superclass,
// "prototype")`, then "if protoParent is not an Object and protoParent is not
// null, throw a TypeError". The engine read the property and quietly ignored a
// missing one, so a class could extend a *bound* function — a constructor with
// no `prototype` at all — and its instances ended up inheriting straight from
// `%Object.prototype%`.
//
// Every assertion was compared against Node v22.22.3 first.
TEST(extends_refuses_a_parent_whose_prototype_is_not_an_object) {
    TestPage page;
    JsInterpreter& js = page.js;

    // The case the row named: a bound function is a constructor (it has
    // [[Construct]]) but `prototype` is not one of its properties.
    assert(js.eval("function f() {}")
               .to_string() == "undefined");
    assert(js.eval("'prototype' in f.bind(null)").to_string() == "false");
    assert(js.eval("Object.prototype.hasOwnProperty.call(f.bind(null), 'prototype')")
               .to_string() == "false");
    assert(js.eval(
               "(function () {"
               "  try { eval('class X extends f.bind(null) {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");

    // A parent whose `prototype` is `undefined` or a primitive is refused the
    // same way, and for the same step.
    assert(js.eval(
               "function g() {} g.prototype = undefined;"
               "(function () {"
               "  try { eval('class B extends g {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");
    assert(js.eval(
               "function h() {} h.prototype = 5;"
               "(function () {"
               "  try { eval('class C extends h {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");

    // What must keep working: an ordinary constructor, a class, and an
    // `extends null` are all still accepted, and a bound function is still a
    // constructor that can be called with `new` — the refusal is about
    // extending it, not about what it is.
    assert(js.eval(
               "(function () {"
               "  try { eval('class Y extends f {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "made");
    assert(js.eval(
               "(function () {"
               "  try { eval('class A extends null {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "made");
    assert(js.eval(
               "function n() { this.x = 1; }"
               "var B = n.bind(null);"
               "new B().x")
               .to_string() == "1");

    // The check is a `Get` of `prototype`, so an accessor installed on the
    // *parent object itself* is asked. (It cannot be done on an ordinary
    // function: `prototype` there is non-configurable, so `defineProperty` on
    // it throws before `extends` ever runs — Node says the same.) A parent
    // that is not a constructor is refused earlier, by the `IsConstructor`
    // check, whichever of the two answers its `prototype` gives.
    assert(js.eval(
               "var withGetter = { get prototype() { return {}; } };"
               "(function () {"
               "  try { eval('class F extends withGetter {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");
    // And a value that is a constructor but whose `prototype` answers `null`
    // is accepted by step 5 and then cannot be constructed.
    assert(js.eval(
               "function k() {} k.prototype = null;"
               "(function () {"
               "  try { eval('class D extends k {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "made");

    // ... and constructing it is a ReferenceError, because `super()` has
    // nothing to call into. That is Node's answer too.
    assert(js.eval(
               "function k2() {} k2.prototype = null;"
               "eval('class D2 extends k2 {}');"
               "(function () {"
               "  try { new D2(); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "ReferenceError");

    // A non-constructor parent is still refused by the earlier check, with its
    // own message; an arrow has no [[Construct]].
    assert(js.eval(
               "(function () {"
               "  try { eval('class Z extends (() => {}) {}'); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");

    // And the chain a refused `extends` must not have produced: an accepted
    // class's instances do inherit from the parent's prototype.
    assert(js.eval(
               "class P { greet() { return 'hi'; } }"
               "class Q extends P {}"
               "new Q().greet()")
               .to_string() == "hi");
    assert(js.eval(
               "class P2 {}"
               "Object.getPrototypeOf(Object.getPrototypeOf(new (class R extends P2 {})())) === P2.prototype")
               .to_string() == "true");
}
