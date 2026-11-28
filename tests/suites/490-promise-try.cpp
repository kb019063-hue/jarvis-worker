// Promise.try follows ECMA-262 §27.2.4.9: invoke now, resolve the result or
// reject on a thrown completion, and preserve the constructor receiver.

TEST(promise_try_invokes_and_settles_through_its_constructor) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    auto text = [&js](const char* source) { return js.eval(source).to_string(); };

    assert(text("typeof Promise.try") == "function");
    assert(text("Promise.try.length") == "1");
    // The call is synchronous and receives all trailing arguments; reactions
    // still wait for the promise job queue.
    assert(text(R"JS(
        var try_log = [];
        var try_value = Promise.try(function (a, b) {
            try_log.push('callback:' + a + ':' + b);
            return a + b;
        }, 4, 7);
        try_log.push('returned:' + (try_value instanceof Promise));
        try_value.then(function (value) { try_log.push('value:' + value); });
        try_log.join('|');
    )JS") == "callback:4:7|returned:true");
    EventLoop::default_loop().run_microtasks();
    assert(text("try_log.join('|')") == "callback:4:7|returned:true|value:11");

    // A thrown value, including undefined, and a non-callable callback become
    // rejections rather than escaping the Promise.try call.
    text(R"JS(
        var try_rejections = [];
        var try_thrown = Promise.try(function () { throw undefined; });
        try_thrown.catch(function (value) { try_rejections.push(value === undefined ? 'undefined' : 'wrong'); });
        var try_non_callable = Promise.try(1);
        try_non_callable.catch(function (error) { try_rejections.push(error instanceof TypeError ? 'TypeError' : 'wrong'); });
    )JS");
    EventLoop::default_loop().run_microtasks();
    assert(text("try_rejections.join('|')") == "undefined|TypeError");

    // PromiseResolve adopts thenables and returns an existing promise of the
    // same constructor unchanged.
    assert(text(R"JS(
        (function () {
            var original = Promise.resolve(19);
            return Promise.try(function () { return original; }) === original;
        })()
    )JS") == "true");
    text(R"JS(
        var try_thenable_value = 'pending';
        Promise.try(function () { return { then: function (resolve) { resolve('adopted'); } }; })
            .then(function (value) { try_thenable_value = value; });
    )JS");
    EventLoop::default_loop().run_microtasks();
    assert(text("try_thenable_value") == "adopted");

    // Generic receiver: callback runs before NewPromiseCapability, and both
    // fulfillment and rejection results are instances of the subclass.
    text(R"JS(
        var try_order = [];
        class TryPromise extends Promise {
            constructor(executor) { try_order.push('construct'); super(executor); }
        }
        var try_subclass_value = TryPromise.try(function () {
            try_order.push('callback');
            return 23;
        });
        var try_subclass_error = TryPromise.try(function () {
            try_order.push('throw');
            throw 'subclass reason';
        });
        var try_subclass_rejected = 'pending';
    )JS");
    assert(text("try_subclass_value instanceof TryPromise") == "true");
    assert(text("try_order.join('|')") == "callback|construct|throw|construct");
    text("try_subclass_error.catch(function (value) { try_subclass_rejected = value; });");
    EventLoop::default_loop().run_microtasks();
    assert(text("try_subclass_rejected") == "subclass reason");

    // A primitive receiver is rejected before the callback can run. A plain
    // object receiver reaches PromiseResolve only after a successful callback.
    assert(text(R"JS(
        (function () {
            var called = false;
            try { Promise.try.call(null, function () { called = true; }); }
            catch (error) { return error instanceof TypeError && !called; }
            return false;
        })()
    )JS") == "true");
    assert(text(R"JS(
        (function () {
            var order = [];
            try {
                Promise.try.call({}, function () { order.push('callback'); return 1; });
            } catch (error) { order.push(error instanceof TypeError ? 'TypeError' : 'wrong'); }
            return order.join('|');
        })()
    )JS") == "callback|TypeError");
    assert(text(R"JS(
        (function () {
            var this_value = 'not checked';
            Promise.try(function () { 'use strict'; this_value = this; });
            return this_value === undefined;
        })()
    )JS") == "true");
}
