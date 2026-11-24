// Phase 285, finding 290: AggregateError, and the Promise.any that faked it.
//
// §20.5.7 has eight error types, and this engine had seven: `typeof
// AggregateError` was `undefined` where Node v22.22.3 answers `function`, and
// all 25 tests of test262's `built-ins/AggregateError` failed. What hid behind
// that was `Promise.any`: when every input rejected, it made a plain `Error`,
// renamed it with a hidden `name` property and hung `errors` off it, so the
// value was not an `AggregateError` at all and `e instanceof AggregateError`
// threw on the missing constructor. Its `errors` was also a dense array with a
// hole at every index that never rejected -- `[undefined]` where Node has `[]`
// -- because the value slots started undefined and were never told apart.
// Node v22.22.3 is the oracle for every answer below, and test262
// `built-ins/AggregateError` moves from 0 of 25 to 21 of 25 (the four left
// need `Proxy`, `$262` or a global that can be deleted).
TEST(aggregate_error_collects_the_reasons) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function ok(what, got, want) {
            if (String(got) !== String(want)) failures.push(what + ': got ' + got + ', expected ' + want);
        }
        function okThrows(what, fn, name) {
            try { fn(); failures.push(what + ': no throw, expected ' + name); }
            catch (e) { if (e.name !== name) failures.push(what + ': threw ' + e.name + ', expected ' + name); }
        }

        // ---- the constructor is the eighth error type -----------------------
        ok('AggregateError exists', typeof AggregateError, 'function');
        ok('its length', AggregateError.length, 2);
        ok('its name', AggregateError.name, 'AggregateError');
        ok('its [[Prototype]] is Error', Object.getPrototypeOf(AggregateError) === Error, true);
        ok('its prototype is an Error', Object.getPrototypeOf(AggregateError.prototype) === Error.prototype, true);
        ok('the prototype names itself', AggregateError.prototype.name, 'AggregateError');
        ok('the prototype message is empty', AggregateError.prototype.message, '');
        // §20.5.7.3: `errors` is not on the prototype, it is only on an instance.
        ok('no errors on the prototype', AggregateError.prototype.hasOwnProperty('errors'), false);

        // ---- an instance ----------------------------------------------------
        var err = new AggregateError([1, 2], 'boom', { cause: 42 });
        ok('instanceof AggregateError', err instanceof AggregateError, true);
        ok('instanceof Error', err instanceof Error, true);
        ok('name', err.name, 'AggregateError');
        ok('message', err.message, 'boom');
        ok('errors', JSON.stringify(err.errors), '[1,2]');
        ok('cause', err.cause, 42);
        ok('toString', err.toString(), 'AggregateError: boom');
        // §20.5.7.1 step 7: CreateNonEnumerableDataPropertyOrThrow.
        var descriptor = Object.getOwnPropertyDescriptor(err, 'errors');
        ok('errors is a data property', typeof descriptor.value, 'object');
        ok('errors is not enumerable', descriptor.enumerable, false);
        ok('errors is writable', descriptor.writable, true);
        ok('errors is configurable', descriptor.configurable, true);
        ok('Object.keys is empty', Object.keys(err).join(','), '');

        // Callable without `new`, as every error type is.
        ok('callable', AggregateError([3]).errors[0], 3);
        ok('called without new is an instance', AggregateError([3]) instanceof AggregateError, true);

        // ---- the message is there only when one was given -------------------
        // Step 4 defines `message` for a message that is not undefined, so
        // `new AggregateError([])` has none of its own and inherits the empty
        // string; `null` and `0` are messages.
        ok('no message argument', new AggregateError([]).hasOwnProperty('message'), false);
        ok('undefined message', new AggregateError([], undefined).hasOwnProperty('message'), false);
        ok('null message', new AggregateError([], null).message, 'null');
        ok('zero message', new AggregateError([], 0).message, '0');
        ok('empty message is a message', new AggregateError([], '').hasOwnProperty('message'), true);

        // ---- errors is IterableToList(errors), and read before anything else
        ok('an empty list', new AggregateError([]).errors.length, 0);
        ok('a string is iterable', JSON.stringify(new AggregateError('ab').errors), '["a","b"]');
        ok('a Set', JSON.stringify(new AggregateError(new Set([1, 1, 2])).errors), '[1,2]');
        (function () {
            var seen = false;
            var iterable = {};
            iterable[Symbol.iterator] = function () { seen = true; return [][Symbol.iterator](); };
            var thrown = null;
            try { new AggregateError(iterable, { toString: function () { throw new TypeError('m'); } }); }
            catch (e) { thrown = e; }
            ok('a bad message throws', thrown && thrown.name, 'TypeError');
            ok('and it throws before iterating', seen, false);
        })();
        okThrows('a non-iterable list', function () { new AggregateError(5); }, 'TypeError');

        // ---- a subclass gets its own prototype ------------------------------
        class Sub extends AggregateError {}
        var sub = new Sub([1], 'x');
        ok('subclass instanceof', sub instanceof Sub, true);
        ok('subclass is an AggregateError', sub instanceof AggregateError, true);
        ok('subclass is an Error', sub instanceof Error, true);
        ok('subclass errors', JSON.stringify(sub.errors), '[1]');

        // ---- Promise.any rejects with one -----------------------------------
        var out = [];
        Promise.any([Promise.reject('a'), Promise.reject('b')])
            .catch(function (e) {
                out.push('instanceof ' + (e instanceof AggregateError));
                out.push('errors ' + JSON.stringify(e.errors));
                out.push('message ' + e.message);
                out.push('name ' + e.name);
            });
        // The reasons are the ones that rejected, in the order given.
        Promise.any([new Promise(function (_, r) { setTimeout(function () { r('c'); }, 3); }),
                     Promise.reject('a'),
                     new Promise(function (_, r) { setTimeout(function () { r('b'); }, 1); })])
            .catch(function (e) { out.push('order ' + JSON.stringify(e.errors)); });
        // An undefined reason is a reason: it must not be dropped.
        Promise.any([Promise.reject(undefined)])
            .catch(function (e) { out.push('undefined reason ' + JSON.stringify(e.errors) + '/' + e.errors.length); });
        // Nothing rejected nothing: an empty list still rejects with one.
        Promise.any([])
            .catch(function (e) { out.push('empty ' + (e instanceof AggregateError) + '/' + e.errors.length); });
        // One that fulfils decides it, and no error is made.
        Promise.any([Promise.reject(1), Promise.resolve('won')])
            .then(function (v) { out.push('fulfilled ' + v); });
        // The reason keeps its identity.
        var reason = { code: 7 };
        Promise.any([Promise.reject(reason)])
            .catch(function (e) { out.push('identity ' + (e.errors[0] === reason)); });

        setTimeout(function () {
            ok('Promise.any answered everything', out.length, 9);
            ok('Promise.any answers', out.slice().sort().join(' | '), [
                'empty true/0', 'errors ["a","b"]', 'fulfilled won', 'identity true',
                'instanceof true', 'message All promises were rejected', 'name AggregateError',
                'order ["c","a","b"]', 'undefined reason [null]/1'
            ].sort().join(' | '));
            failures.length ? failures.join('\n') : 'ok';
        }, 10);
    )JS");
    // The Promise.any cases settle on timers, and the last of them asks
    // the questions above. Nothing here depends on how long they took: the
    // loop is run until there is no work left.
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    const std::string failed = js.eval("failures.length ? failures.join('\\n') : ''").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());
}
