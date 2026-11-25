// A symbol is refused by structuredClone with a DataCloneError, not a TypeError.
//
// HTML 2.7.4 StructuredSerializeInternal has the step before any object is
// looked at:
//
//     step 6: If value is a Symbol, then throw a "DataCloneError" DOMException.
//
// It is its own step, separate from step 19's "If value has a [[Call]]" that
// refuses a function, and it fires for a symbol *anywhere* in the graph: a
// nested value, an array element, a Map's key. This engine threw a TypeError
// with a message of its own, and its comment said that was the standard's
// split. It was a misreading: Node v22.22.3 answers a DataCloneError
// DOMException with `e.code` 25, for a symbol exactly as for a function.
//
// The engine was already right for every other refusal — a function, a WeakMap,
// a Promise all answer DataCloneError — so the defect was one branch, and a
// test for "structuredClone refuses the unserialisable" that only ever tried a
// function could not see it.
//
// Every expected value below is Node v22.22.3's, by evaluating the same
// expression there.
TEST(a_symbol_is_a_data_clone_error_not_a_type_error) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // The name and the class, which is what a script branches on.
    assert(run("(function(){try{structuredClone(Symbol('x'));return 'no throw';}"
               "catch(e){return e.name;}})()") == "DataCloneError");
    assert(run("(function(){try{structuredClone(Symbol('x'));return 'no throw';}"
               "catch(e){return e instanceof DOMException;}})()") == "true");
    // `code` 25 is DATA_CLONE_ERR, and it is what the DOMException carries.
    assert(run("(function(){try{structuredClone(Symbol());}catch(e){return e.code;}})()") == "25");
    // Not a TypeError. This is the assertion the engine failed.
    assert(run("(function(){try{structuredClone(Symbol());}catch(e){return e instanceof TypeError;}})()")
           == "false");

    // The message names the symbol, as Node's does: `Symbol(x)` with a
    // description and `Symbol()` without.
    assert(run("(function(){try{structuredClone(Symbol('x'));}catch(e){return e.message;}})()")
           == "Symbol(x) could not be cloned.");
    assert(run("(function(){try{structuredClone(Symbol());}catch(e){return e.message;}})()")
           == "Symbol() could not be cloned.");

    // Anywhere in the graph, because the refusal is inside the walk and not a
    // check on the outermost value.
    assert(run("(function(){try{structuredClone({a: Symbol()});}catch(e){return e.name;}})()")
           == "DataCloneError");
    assert(run("(function(){try{structuredClone([1, Symbol()]);}catch(e){return e.name;}})()")
           == "DataCloneError");
    assert(run("(function(){try{structuredClone({a:{b:{c:[Symbol('y')]}}});}catch(e){return e.name;}})()")
           == "DataCloneError");
    assert(run("(function(){try{structuredClone(new Map([[Symbol('k'), 1]]));}catch(e){return e.name;}})()")
           == "DataCloneError");
    assert(run("(function(){try{structuredClone(new Set([Symbol()]));}catch(e){return e.name;}})()")
           == "DataCloneError");
    // The deep case names the symbol it found, not the outer object.
    assert(run("(function(){try{structuredClone({a:{b:[Symbol('deep')]}});}catch(e){return e.message;}})()")
           == "Symbol(deep) could not be cloned.");

    // Every other refusal was already right, and must stay right: this is the
    // set a test written against the engine as it was would have had.
    assert(run("(function(){try{structuredClone(function(){});}catch(e){return e.name;}})()")
           == "DataCloneError");
    assert(run("(function(){try{structuredClone(new WeakMap());}catch(e){return e.name;}})()")
           == "DataCloneError");
    assert(run("(function(){try{structuredClone(new WeakSet());}catch(e){return e.name;}})()")
           == "DataCloneError");
    assert(run("(function(){try{structuredClone(Promise.resolve());}catch(e){return e.name;}})()")
           == "DataCloneError");

    // A getter that throws is not caught and replaced: the exception the
    // property raised is the one the caller sees.
    assert(run("(function(){try{structuredClone({get a(){throw new RangeError('boom');}});}"
               "catch(e){return e.name;}})()") == "RangeError");

    // And the clone itself still works, so nothing above was bought by
    // refusing more than the standard does.
    assert(run("JSON.stringify(structuredClone({a: 1, b: [2]}))") == "{\"a\":1,\"b\":[2]}");
    assert(run("(function(){var s = Symbol; return typeof structuredClone(1);})()") == "number");
}
