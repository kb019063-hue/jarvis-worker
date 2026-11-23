// Phase 233, finding 237: Array.prototype[Symbol.unscopables] exists, and is
// the standard's table.
//
// §23.1.3.34: an ordinary object with a null prototype, `true` under each of
// the sixteen names a `with (array)` block must not resolve to the array (the
// twelve the 2015 edition named, plus findLast/findLastIndex in 2022 and
// toReversed/toSorted/toSpliced in 2023), every one of them writable,
// enumerable and configurable — while the table itself is neither writable nor
// enumerable on Array.prototype, and configurable. Nothing of it was there, so
// `typeof Array.prototype[Symbol.unscopables]` was `undefined` and test262's
// `built-ins/Array/prototype/Symbol.unscopables` was 0/5; it is 5/5 now.
TEST(array_prototype_unscopables_is_the_standards_table) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function ok(what, got, want) {
            if (String(got) !== String(want)) failures.push(what + ': got ' + got + ', expected ' + want);
        }

        // The property on the prototype: there, an object, not writable, not
        // enumerable, configurable. (§23.1.3.34 step 1, and the attributes
        // every built-in method's @@unscopables has had since ES2015.)
        ok('present', typeof Array.prototype[Symbol.unscopables], 'object');
        var own = Object.getOwnPropertyDescriptor(Array.prototype, Symbol.unscopables);
        ok('not writable', own.writable, 'false');
        ok('not enumerable', own.enumerable, 'false');
        ok('configurable', own.configurable, 'true');

        var u = Array.prototype[Symbol.unscopables];

        // A null prototype: the lookups a `with` block makes must not run into
        // Object.prototype's names.
        ok('null prototype', Object.getPrototypeOf(u), null);

        // Exactly the standard's names, and nothing else. A method that is not
        // in the table is one a `with` block would still find on the array;
        // an extra name is one a page could not shadow.
        var names = Object.getOwnPropertyNames(u).join();
        ok('the names', names,
           'at,copyWithin,entries,fill,find,findIndex,findLast,findLastIndex,flat,flatMap,' +
           'includes,keys,toReversed,toSorted,toSpliced,values');
        ok('sixteen', Object.getOwnPropertyNames(u).length, '16');

        // Each one is `true`, and writable, enumerable and configurable — which
        // is what makes a page able to turn one of them off again.
        var allTrue = true, allAttributes = true;
        for (var i = 0; i < 16; i++) {
            var name = Object.getOwnPropertyNames(u)[i];
            if (u[name] !== true) allTrue = false;
            var d = Object.getOwnPropertyDescriptor(u, name);
            if (!d.writable || !d.enumerable || !d.configurable) allAttributes = false;
        }
        ok('every value true', allTrue, 'true');
        ok('every property plain', allAttributes, 'true');

        // The methods that are NOT in the table: the mutating ones a page of
        // 2014 could have had a variable called `push` for.
        ok('push is not there', u.push, 'undefined');
        ok('map is not there', u.map, 'undefined');
        ok('sort is not there', u.sort, 'undefined');
        ok('length is not there', u.length, 'undefined');
        ok('with is not a name', Object.prototype.hasOwnProperty.call(u, 'with'), 'false');

        // Other prototypes do not have one (Object.prototype is the only one
        // the standard gives a table besides the arrays and the typed arrays).
        ok('not on Object.prototype', Object.prototype[Symbol.unscopables], 'undefined');

        // The symbol itself is a well-known symbol: registered, and spelled.
        ok('a symbol', typeof Symbol.unscopables, 'symbol');
        ok('its description', String(Symbol.unscopables), 'Symbol(Symbol.unscopables)');
    )JS");
    assert(js.eval("failures.length").to_string() == "0");
    assert(js.eval("failures.join('; ')").to_string().empty());
}
