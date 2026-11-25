// Phase 399, finding 399: a symbol's description is not a property of it, and
// a symbol made with no description has none at all.
//
// §20.4.1.1: `Symbol()` with no argument makes a symbol with no description,
// and §20.4.3.2 answers `undefined` for it. `Symbol("")` is a different thing:
// a symbol whose description is the empty string. Both print as `Symbol()`.
//
// Every assertion was compared against Node v22.22.3 first.
TEST(a_symbols_description_is_not_one_of_its_properties) {
    TestPage page;
    JsInterpreter& js = page.js;

    // §20.4.3.2: no description answers `undefined`, and the empty string is a
    // description the script asked for. Before this phase both answered `""`.
    assert(js.eval("typeof Symbol().description").to_string() == "undefined");
    assert(js.eval("String(Symbol().description)").to_string() == "undefined");
    assert(js.eval("typeof Symbol('').description").to_string() == "string");
    assert(js.eval("Symbol('').description.length").to_string() == "0");
    assert(js.eval("Symbol('').description === ''").to_string() == "true");
    assert(js.eval("Symbol('x').description").to_string() == "x");
    // An explicit `undefined` argument is the same as no argument.
    assert(js.eval("typeof Symbol(undefined).description").to_string() == "undefined");

    // The description is not an own property of the symbol. It used to live in
    // the symbol's property table under the key "description", which made this
    // `true` and made `getOwnPropertyNames` answer `description`.
    assert(js.eval("Object.prototype.hasOwnProperty.call(Symbol('x'), 'description')")
               .to_string() == "false");
    assert(js.eval("Object.prototype.hasOwnProperty.call(Symbol(), 'description')")
               .to_string() == "false");
    assert(js.eval("Object.getOwnPropertyNames(Symbol('x')).join()").to_string() == "");

    // A symbol carries no own property at all, so `Reflect.ownKeys` on one
    // throws rather than answering anything (Symbol is not an object).
    assert(js.eval("(function () { try { Reflect.ownKeys(Symbol()); return 'made'; }"
                   " catch (e) { return e.name; } })()")
               .to_string() == "TypeError");

    // Printing is unchanged for all three: `Symbol()` and `Symbol('')` both
    // print as `Symbol()`, and the named one keeps its name.
    assert(js.eval("Symbol().toString()").to_string() == "Symbol()");
    assert(js.eval("Symbol('').toString()").to_string() == "Symbol()");
    assert(js.eval("Symbol('x').toString()").to_string() == "Symbol(x)");
    assert(js.eval("String(Symbol())").to_string() == "Symbol()");

    // The other kinds of symbol still have their descriptions: a registered
    // one and a well-known one.
    assert(js.eval("Symbol.for('k').description").to_string() == "k");
    assert(js.eval("Symbol.iterator.description").to_string() == "Symbol.iterator");
    assert(js.eval("Symbol.keyFor(Symbol())").to_string() == "undefined");
    assert(js.eval("Symbol.keyFor(Symbol.for('k'))").to_string() == "k");

    // The accessor is on the prototype, with a getter and no setter, and it
    // refuses a receiver that is not a symbol.
    assert(js.eval("typeof Object.getOwnPropertyDescriptor(Symbol.prototype, 'description').get")
               .to_string() == "function");
    assert(js.eval("Object.getOwnPropertyDescriptor(Symbol.prototype, 'description').set === undefined")
               .to_string() == "true");
    assert(js.eval(
               "(function () {"
               "  try { Object.getOwnPropertyDescriptor(Symbol.prototype, 'description').get.call({});"
               "        return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");

    // A description computed from an object is `ToString` of it, and a
    // description is fixed when the symbol is made: nothing can change it
    // afterwards, because it is not a property anyone can write.
    assert(js.eval("Symbol({ toString: function () { return 'from-object'; } }).description")
               .to_string() == "from-object");
    assert(js.eval(
               "var s = Symbol('first');"
               "try { s.description = 'second'; } catch (e) {}"
               "s.description")
               .to_string() == "first");

    // A symbol used as a property key still works the way it did: the value
    // stored under it survives, and its description is what a hint names it
    // with.
    assert(js.eval(
               "var s = Symbol('k');"
               "var o = {};"
               "o[s] = 1;"
               "o[s]")
               .to_string() == "1");
    assert(js.eval(
               "var s = Symbol('named');"
               "var o = {}; o[s] = 1;"
               "Object.getOwnPropertySymbols(o).length")
               .to_string() == "1");
}
