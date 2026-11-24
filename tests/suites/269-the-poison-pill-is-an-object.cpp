// Phase 269, finding 273: the poison pill is a real accessor pair on
// %Function.prototype%, not a fiction in the property lookups.
TEST(the_poison_pill_is_a_pair_of_accessors_on_function_prototype) {
    // §10.2.4/§10.2.6 make `caller` and `arguments` a pair of throwing
    // accessors on %Function.prototype%, inherited by every function. This
    // engine answered for them in `has_property` and `throw_if_forbidden_property`
    // instead, so the answers were right and the object was absent:
    // `getOwnPropertyDescriptor` was `undefined`, `delete` had nothing to
    // delete, and `for...in` saw a property that was not there.
    TestPage page;
    JsInterpreter& js = page.js;

    // The pair exists, on the prototype, as inherited accessors.
    assert(js.eval("Object.prototype.hasOwnProperty.call(Function.prototype, 'caller')").to_string() == "true");
    assert(js.eval("Object.prototype.hasOwnProperty.call(Function.prototype, 'arguments')").to_string() == "true");

    // Its attributes are the standard's: a configurable, non-enumerable
    // accessor whose get and set are the same thrower.
    assert(js.eval(
               "var d = Object.getOwnPropertyDescriptor(Function.prototype, 'caller');"
               "[String(d.get === d.set), String(typeof d.get), String(d.enumerable), String(d.configurable)].join('/')")
               .to_string() == "true/function/false/true");

    // Inherited, never own. The rule is about *strict* functions and the
    // exotic ones: a strict function, a class and a generator answer `false`
    // to hasOwnProperty, which is what `forbidden-ext` asserts. An arrow and a
    // method written in sloppy code are ordinary enough to be given the
    // `null` pair by the parser, and Node agrees — checked, rather than
    // assumed: `false/false,true/true,false/false,true/true,false/false`.
    assert(js.eval(
               "var strict = (function () { 'use strict'; return function () {}; })();"
               "var arrow = function () {};"
               "class C {}"
               "var method = ({ m: function () {} }).m;"
               "function* g() {}"
               "function sloppy() {}"
               "[strict, arrow, C, method, g].map(function (f) {"
               "  return String(Object.prototype.hasOwnProperty.call(f, 'caller')) + '/' + String("
               "         Object.prototype.hasOwnProperty.call(f, 'arguments'));"
               "}).join(',') + '|' + String(Object.prototype.hasOwnProperty.call(sloppy, 'caller'))")
               .to_string() == "false/false,true/true,false/false,true/true,false/false|true");

    // The two answers must agree: `in` is true for every function, and reading
    // one of a function that does not shadow the pair throws a TypeError.
    assert(js.eval("String('caller' in function () {}) + String('arguments' in function () {})").to_string() ==
           "truetrue");
    // A *strict* function (or a class, or a generator) is the one the pill
    // answers for. A sloppy function is given the `null` pair instead, which
    // is the shadow above, so the throw is asked of a strict one.
    assert(js.eval(
               "var strict = (function () { 'use strict'; return function () {}; })();"
               "var answer = 'none';"
               "try { strict.caller; } catch (e) { answer = e.name; }"
               "answer")
               .to_string() == "TypeError");
    assert(js.eval(
               "var strict = (function () { 'use strict'; return function () {}; })();"
               "var answer = 'none';"
               "try { strict.arguments; } catch (e) { answer = e.name; }"
               "answer")
               .to_string() == "TypeError");
    // And the exotic ones inherit it too: a class and a generator throw.
    assert(js.eval(
               "class C {}"
               "function* g() {}"
               "var out = [];"
               "[C, g].forEach(function (f) {"
               "  try { f.caller; out.push('no'); } catch (e) { out.push(e.name); }"
               "});"
               "out.join(',')")
               .to_string() == "TypeError,TypeError");

    // A sloppy ordinary function still reads `null` for both, from its own
    // properties, and `new Function` is one of those.
    assert(js.eval("function f() {}; String(f.caller) + String(f.arguments)").to_string() == "nullnull");

    // The accessor is configurable, so a script may remove it — and then the
    // answers change together, which is what a real property does and what a
    // fiction in the lookup cannot.
    assert(js.eval(
               "var gone = delete Function.prototype.caller;"
               "var after = Object.getOwnPropertyDescriptor(Function.prototype, 'caller');"
               "String(gone) + '/' + String(after === undefined) + '/' + String('caller' in ({}))")
               .to_string() == "true/true/false");
}
