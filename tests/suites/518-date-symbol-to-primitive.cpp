// Phase 518, finding 518: Date's primitive-conversion method is generic.
TEST(date_symbol_to_primitive_validates_hints_and_uses_ordinary_conversion) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        (function () {
            var method = Date.prototype[Symbol.toPrimitive];
            if (typeof method !== 'function') return 'missing Date.prototype[Symbol.toPrimitive]';
            var failures = [], checks = 0;
            function check(name, fn) {
                checks++;
                try {
                    if (fn() !== true) failures.push(name + ': wrong result');
                } catch (e) { failures.push(name + ': unexpected ' + e.name); }
            }
            function throwsTypeError(fn) {
                try { fn(); } catch (e) { return e instanceof TypeError; }
                return false;
            }
            check('method metadata', function () {
                return method.name === '[Symbol.toPrimitive]' && method.length === 1 &&
                    !Object.prototype.hasOwnProperty.call(method, 'prototype');
            });
            check('symbol property descriptor', function () {
                var d = Object.getOwnPropertyDescriptor(Date.prototype, Symbol.toPrimitive);
                return d.value === method && !d.writable && !d.enumerable && d.configurable;
            });
            ['name', 'length'].forEach(function (key) {
                check('function descriptor ' + key, function () {
                    var d = Object.getOwnPropertyDescriptor(method, key);
                    return !d.writable && !d.enumerable && d.configurable;
                });
            });
            check('not a constructor', function () {
                return throwsTypeError(function () { new method('default'); });
            });
            ['default', 'string', 'number'].forEach(function (hint) {
                var first = hint === 'number' ? 'valueOf' : 'toString';
                var second = hint === 'number' ? 'toString' : 'valueOf';
                check(hint + ' short circuits after first primitive', function () {
                    var receiver = {}, log = [];
                    Object.defineProperty(receiver, first, { get: function () {
                        log.push('get');
                        return function () {
                            'use strict';
                            log.push(this === receiver && arguments.length === 0 ? 'call' : 'wrong call');
                            return 17;
                        };
                    }});
                    Object.defineProperty(receiver, second, { get: function () { throw 'must not read'; }});
                    return method.call(receiver, hint, 'ignored') === 17 && log.join(',') === 'get,call';
                });
                check(hint + ' fallback order', function () {
                    var receiver = {}, log = [];
                    Object.defineProperty(receiver, first, { get: function () {
                        log.push('get first');
                        return function () { log.push('call first'); return {}; };
                    }});
                    Object.defineProperty(receiver, second, { get: function () {
                        log.push('get second');
                        return function () {
                            'use strict';
                            log.push(this === receiver && arguments.length === 0 ? 'call second' : 'wrong call');
                            return 'fallback';
                        };
                    }});
                    return method.call(receiver, hint) === 'fallback' &&
                        log.join(',') === 'get first,call first,get second,call second';
                });
                [undefined, null, 0, 'not callable', {}].forEach(function (value, index) {
                    check(hint + ' skips noncallable first ' + index, function () {
                        var receiver = {}, calls = 0;
                        receiver[first] = value;
                        receiver[second] = function () { calls++; return 23; };
                        return method.call(receiver, hint) === 23 && calls === 1;
                    });
                });
                check(hint + ' both noncallable', function () {
                    return throwsTypeError(function () { method.call({ toString: 0, valueOf: null }, hint); });
                });
                check(hint + ' both return objects', function () {
                    var log = [], receiver = {};
                    receiver[first] = function () { log.push(first); return {}; };
                    receiver[second] = function () { log.push(second); return function () {}; };
                    return throwsTypeError(function () { method.call(receiver, hint); }) &&
                        log.join(',') === first + ',' + second;
                });
                ['get first', 'call first', 'get second', 'call second'].forEach(function (stage) {
                    check(hint + ' abrupt ' + stage, function () {
                        var receiver = {}, log = [], marker = {};
                        function step(name) { log.push(name); if (name === stage) throw marker; }
                        Object.defineProperty(receiver, first, { get: function () {
                            step('get first'); return function () { step('call first'); return {}; };
                        }});
                        Object.defineProperty(receiver, second, { get: function () {
                            step('get second'); return function () { step('call second'); return 0; };
                        }});
                        try { method.call(receiver, hint); } catch (e) {
                            var order = ['get first', 'call first', 'get second', 'call second'];
                            return e === marker && log.join(',') === order.slice(0, order.indexOf(stage) + 1).join(',');
                        }
                        return false;
                    });
                });
                [undefined, null, true, 17, 'text', Symbol('primitive'), NaN, -0].forEach(function (primitive, index) {
                    check(hint + ' returns primitive unchanged ' + index, function () {
                        var receiver = {};
                        receiver[first] = function () { return primitive; };
                        receiver[second] = function () { throw 'must not call'; };
                        return Object.is(method.call(receiver, hint), primitive);
                    });
                });
            });
            [undefined, null, true, 0, Symbol('number'), {}, new String('number'), 'NUMBER', '', ' string'].forEach(function (hint, index) {
                check('rejects hint without conversion ' + index, function () {
                    var reads = 0, receiver = {};
                    Object.defineProperty(receiver, 'toString', { get: function () { reads++; throw 'must not read'; }});
                    Object.defineProperty(receiver, 'valueOf', { get: function () { reads++; throw 'must not read'; }});
                    return throwsTypeError(function () { method.call(receiver, hint); }) && reads === 0;
                });
            });
            check('hint object conversion is never attempted', function () {
                var calls = 0;
                var hint = { toString: function () { calls++; throw 'must not coerce'; } };
                return throwsTypeError(function () { method.call({}, hint); }) && calls === 0;
            });
            [undefined, null, true, 1, 'text', Symbol('receiver')].forEach(function (receiver, index) {
                check('rejects primitive receiver ' + index, function () {
                    return throwsTypeError(function () { method.call(receiver, 'default'); });
                });
            });
            check('callable receiver is an object', function () {
                var receiver = function () {};
                receiver.toString = function () { return 'function receiver'; };
                return method.call(receiver, 'default') === 'function receiver';
            });
            check('arrays and wrappers are objects', function () {
                return method.call([1, 2], 'default') === '1,2' &&
                    method.call(new Number(17), 'number') === 17 &&
                    method.call(new String('text'), 'string') === 'text';
            });
            check('inherited Date conversion uses the hint, not a Date brand', function () {
                var receiver = Object.create(Date.prototype);
                receiver.toString = function () { return 'string result'; };
                receiver.valueOf = function () { return 17; };
                return receiver + '' === 'string result' && +receiver === 17 && String(receiver) === 'string result';
            });
            check('ordinary Date implicit conversions', function () {
                var date = new Date(0);
                date.toString = function () { return 'date text'; };
                date.valueOf = function () { return 42; };
                return date + '' === 'date text' && String(date) === 'date text' && +date === 42;
            });
            [undefined, null].forEach(function (shadow, index) {
                check('Date without exotic method uses ordinary default hint ' + index, function () {
                    var date = new Date(0);
                    Object.defineProperty(date, Symbol.toPrimitive, { value: shadow });
                    date.toString = function () { return 'date text'; };
                    date.valueOf = function () { return 42; };
                    return date + 1 === 43 && String(date) === 'date text' && +date === 42;
                });
            });
            check('conversion does not redispatch the exotic method', function () {
                var receiver = { toString: function () { return 'ordinary'; } };
                receiver[Symbol.toPrimitive] = function () { throw 'must not recurse'; };
                return method.call(receiver, 'default') === 'ordinary';
            });
            check('conversion survives collection in a fallback hook', function () {
                var receiver = {
                    toString: function () { if (typeof gc === 'function') gc(); return {}; },
                    valueOf: function () { if (typeof gc === 'function') gc(); return 19; }
                };
                return method.call(receiver, 'default') === 19;
            });
            return failures.length ? failures.join('; ') : 'ok ' + checks;
        })();
    )JS");
    assert(result.type == JsType::String);
    if (result.string_val.str() != "ok 93") std::cerr << result.to_string() << '\n';
    assert(result.string_val.str() == "ok 93");
}
