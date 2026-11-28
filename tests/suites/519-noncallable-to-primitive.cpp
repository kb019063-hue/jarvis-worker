// Phase 519, finding 519: GetMethod rejects noncallable conversion hooks.
TEST(noncallable_to_primitive_throws_without_ordinary_fallback) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        (function () {
            var checks = 0, failures = [];
            function check(name, fn) {
                checks++;
                try { if (fn() !== true) failures.push(name + ': wrong result'); }
                catch (e) { failures.push(name + ': unexpected ' + e.name); }
            }
            function throwsTypeError(fn) {
                try { fn(); } catch (e) { return e instanceof TypeError; }
                return false;
            }
            var operations = [
                { name: 'Number', hint: 'number', run: function (o) { return Number(o) === 7; } },
                { name: 'String', hint: 'string', run: function (o) { return String(o) === '7'; } },
                { name: 'addition', hint: 'default', run: function (o) { return o + 1 === 8; } },
                { name: 'subtraction', hint: 'number', run: function (o) { return o - 1 === 6; } },
                { name: 'relational', hint: 'number', run: function (o) { return o < 8; } },
                { name: 'equality', hint: 'default', run: function (o) { return o == 7; } },
                { name: 'Date constructor', hint: 'default', run: function (o) { return new Date(o).getTime() === 7; } },
                { name: 'Date setter', hint: 'number', run: function (o) { return new Date(0).setTime(o) === 7; } },
                { name: 'Math', hint: 'number', run: function (o) { return Math.abs(o) === 7; } },
                { name: 'isFinite', hint: 'number', run: function (o) { return isFinite(o) === true; } },
                { name: 'isNaN', hint: 'number', run: function (o) { return isNaN(o) === false; } }
            ];
            var invalidMethods = [false, true, 0, NaN, '', 'number', Symbol('method'), {}, [], new Number(7), new String('number')];
            operations.forEach(function (op) {
                invalidMethods.forEach(function (value, index) {
                    check(op.name + ' rejects noncallable ' + index, function () {
                        var reads = 0, ordinaryReads = 0, receiver = {};
                        Object.defineProperty(receiver, Symbol.toPrimitive, { get: function () { reads++; return value; } });
                        ['valueOf', 'toString'].forEach(function (key) {
                            Object.defineProperty(receiver, key, { get: function () { ordinaryReads++; return function () { return 7; }; } });
                        });
                        return throwsTypeError(function () { op.run(receiver); }) && reads === 1 && ordinaryReads === 0;
                    });
                });
                [undefined, null].forEach(function (value, index) {
                    check(op.name + ' allows nullish opt out ' + index, function () {
                        var reads = 0, log = [], receiver = {};
                        Object.defineProperty(receiver, Symbol.toPrimitive, { get: function () { reads++; return value; } });
                        ['valueOf', 'toString'].forEach(function (key) {
                            Object.defineProperty(receiver, key, { get: function () {
                                log.push('get ' + key);
                                return function () {
                                    'use strict';
                                    log.push(this === receiver && arguments.length === 0 ? 'call ' + key : 'bad call');
                                    return key === 'toString' ? '7' : 7;
                                };
                            }});
                        });
                        var first = op.hint === 'string' ? 'toString' : 'valueOf';
                        return op.run(receiver) && reads === 1 && log.join(',') === 'get ' + first + ',call ' + first;
                    });
                });
                check(op.name + ' callable hook receiver and hint', function () {
                    var receiver = {}, reads = 0, calls = 0, valid = true;
                    Object.defineProperty(receiver, Symbol.toPrimitive, { get: function () {
                        reads++;
                        return function (hint) {
                            'use strict';
                            calls++;
                            valid = this === receiver && arguments.length === 1 && hint === op.hint;
                            return 7;
                        };
                    }});
                    receiver.valueOf = receiver.toString = function () { throw 'must not fall back'; };
                    return op.run(receiver) && reads === 1 && calls === 1 && valid;
                });
                check(op.name + ' getter exception identity', function () {
                    var marker = {}, receiver = {};
                    Object.defineProperty(receiver, Symbol.toPrimitive, { get: function () { throw marker; } });
                    try { op.run(receiver); } catch (e) { return e === marker; }
                    return false;
                });
                check(op.name + ' callable exception identity', function () {
                    var marker = {}, receiver = {};
                    receiver[Symbol.toPrimitive] = function () { throw marker; };
                    try { op.run(receiver); } catch (e) { return e === marker; }
                    return false;
                });
                check(op.name + ' object result never falls back', function () {
                    var receiver = {}, calls = 0;
                    receiver[Symbol.toPrimitive] = function () { calls++; return {}; };
                    receiver.toString = receiver.valueOf = function () { calls += 100; return 7; };
                    return throwsTypeError(function () { op.run(receiver); }) && calls === 1;
                });
                check(op.name + ' inherited invalid method', function () {
                    var proto = {}, reads = 0, receiver;
                    Object.defineProperty(proto, Symbol.toPrimitive, { get: function () {
                        if (this === receiver) reads++;
                        return false;
                    }});
                    receiver = Object.create(proto);
                    receiver.toString = receiver.valueOf = function () { throw 'must not fall back'; };
                    return throwsTypeError(function () { op.run(receiver); }) && reads === 1;
                });
                check(op.name + ' getter is not reread after mutation', function () {
                    var receiver = {}, reads = 0;
                    Object.defineProperty(receiver, Symbol.toPrimitive, { configurable: true, get: function () {
                        reads++;
                        Object.defineProperty(receiver, Symbol.toPrimitive, { value: function () { return 7; } });
                        return 0;
                    }});
                    return throwsTypeError(function () { op.run(receiver); }) && reads === 1 && op.run(receiver);
                });
                check(op.name + ' collection during invalid getter', function () {
                    var receiver = {};
                    Object.defineProperty(receiver, Symbol.toPrimitive, { get: function () {
                        if (typeof gc === 'function') gc();
                        return {};
                    }});
                    return throwsTypeError(function () { op.run(receiver); });
                });
            });
            check('Date inherited method can be shadowed with a noncallable', function () {
                var date = new Date(0);
                Object.defineProperty(date, Symbol.toPrimitive, { value: 0 });
                return throwsTypeError(function () { return date + ''; });
            });
            check('symbol wrapper method can be shadowed with a noncallable', function () {
                var wrapper = Object(Symbol('wrapped'));
                Object.defineProperty(wrapper, Symbol.toPrimitive, { value: false });
                return throwsTypeError(function () { return wrapper == 7; });
            });
            check('right operand conversion is skipped after failed left conversion', function () {
                var log = [], left = {}, right = {};
                left[Symbol.toPrimitive] = 0;
                right[Symbol.toPrimitive] = function () { log.push('right'); return 1; };
                return throwsTypeError(function () { return left + right; }) && log.length === 0;
            });
            return failures.length ? failures.join('; ') : 'ok ' + checks;
        })();
    )JS");
    assert(result.type == JsType::String);
    if (result.string_val.str() != "ok 223") std::cerr << result.to_string() << '\n';
    assert(result.string_val.str() == "ok 223");
}
