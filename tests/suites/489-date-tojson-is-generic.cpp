// Phase 489, finding 489: §21.4.4.37 is generic, not a Date-slot lookup.
TEST(date_tojson_converts_then_invokes_the_receivers_iso_method) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        (function () {
            var toJSON = Date.prototype.toJSON;
            var failures = [], checks = 0;
            function check(name, fn) {
                checks++;
                try {
                    if (fn() !== true) failures.push(name + ': wrong result');
                } catch (e) {
                    failures.push(name + ': unexpected ' + e.name);
                }
            }
            function throwsTypeError(fn) {
                try { fn(); } catch (e) { return e instanceof TypeError; }
                return false;
            }
            check('ordinary date', function () {
                return new Date(0).toJSON() === '1970-01-01T00:00:00.000Z';
            });
            check('invalid date', function () { return new Date(NaN).toJSON() === null; });
            check('valid date override and ignored key', function () {
                var date = new Date(0);
                date.toISOString = function () {
                    return this === date && arguments.length === 0 ? 'override' : 'bad call';
                };
                return date.toJSON('ignored') === 'override';
            });
            check('stringify dispatches override', function () {
                var date = new Date(0);
                date.toISOString = function () { return 'custom'; };
                return JSON.stringify({ date: date }) === '{"date":"custom"}';
            });
            check('generic object', function () {
                return toJSON.call({ toISOString: function () { return 17; } }) === 17;
            });
            check('return object unchanged', function () {
                var marker = {};
                return toJSON.call({ toISOString: function () { return marker; } }) === marker;
            });
            check('return undefined unchanged', function () {
                return toJSON.call({ toISOString: function () {} }) === undefined;
            });
            check('return null unchanged', function () {
                return toJSON.call({ toISOString: function () { return null; } }) === null;
            });
            [NaN, Infinity, -Infinity].forEach(function (number) {
                check('nonfinite ' + number, function () {
                    var object = { valueOf: function () { return number; } };
                    Object.defineProperty(object, 'toISOString', {
                        get: function () { throw 'must not read'; }
                    });
                    return toJSON.call(object) === null;
                });
            });
            ['NaN', 'Infinity', undefined, null, false, Symbol('primitive')].forEach(function (primitive) {
                check('nonnumeric primitive ' + typeof primitive, function () {
                    return toJSON.call({
                        valueOf: function () { return primitive; },
                        toISOString: function () { return 'called'; }
                    }) === 'called';
                });
            });
            check('conversion hint and invocation order', function () {
                var log = [], receiver = {};
                receiver[Symbol.toPrimitive] = function (hint) {
                    log.push(hint);
                    return 0;
                };
                Object.defineProperty(receiver, 'toISOString', {
                    get: function () {
                        log.push('get');
                        return function () {
                            log.push(this === receiver ? 'this' : 'wrong');
                            log.push(arguments.length);
                            return 'done';
                        };
                    }
                });
                return toJSON.call(receiver, 'key') === 'done' && log.join(',') === 'number,get,this,0';
            });
            check('ordinary numeric conversion order', function () {
                var log = [];
                var receiver = {
                    valueOf: function () { log.push('valueOf'); return {}; },
                    toString: function () { log.push('toString'); return 'text'; },
                    toISOString: function () { log.push('iso'); return 0; }
                };
                return toJSON.call(receiver) === 0 && log.join(',') === 'valueOf,toString,iso';
            });
            check('date valueOf override before iso', function () {
                var date = new Date(0);
                date.valueOf = function () { return Infinity; };
                date.toISOString = function () { throw 'must not call'; };
                return date.toJSON() === null;
            });
            check('invalid date with finite conversion invokes iso', function () {
                var date = new Date(NaN);
                date.valueOf = function () { return 0; };
                date.toISOString = function () { return 'rescued'; };
                return date.toJSON() === 'rescued';
            });
            [null, undefined, 0, true, 'text', Symbol('receiver'), {}].forEach(function (receiver) {
                check('missing method or nullish ' + typeof receiver, function () {
                    return throwsTypeError(function () { toJSON.call(receiver); });
                });
            });
            [undefined, null, 1, 'iso', {}].forEach(function (method) {
                check('noncallable iso ' + typeof method, function () {
                    return throwsTypeError(function () { toJSON.call({ toISOString: method }); });
                });
            });
            check('nonfinite primitive receivers', function () {
                return toJSON.call(NaN) === null && toJSON.call(Infinity) === null && toJSON.call(-Infinity) === null;
            });
            check('finite number receiver is boxed', function () {
                Number.prototype.toISOString = function () {
                    'use strict';
                    return typeof this === 'object' && this.valueOf() === 23 && arguments.length === 0;
                };
                try { return toJSON.call(23) === true; }
                finally { delete Number.prototype.toISOString; }
            });
            ['convert', 'get', 'call'].forEach(function (stage) {
                check('abrupt ' + stage, function () {
                    var marker = {}, log = [];
                    var receiver = {
                        valueOf: function () {
                            log.push('convert');
                            if (stage === 'convert') throw marker;
                            return 0;
                        }
                    };
                    Object.defineProperty(receiver, 'toISOString', {
                        get: function () {
                            log.push('get');
                            if (stage === 'get') throw marker;
                            return function () { log.push('call'); throw marker; };
                        }
                    });
                    try { toJSON.call(receiver); } catch (e) {
                        return e === marker && log.join(',') === (
                            stage === 'convert' ? 'convert' : stage === 'get' ? 'convert,get' : 'convert,get,call');
                    }
                    return false;
                });
            });
            check('nonprimitive exotic conversion refuses before iso', function () {
                var receiver = { toISOString: function () { throw 'must not call'; } };
                receiver[Symbol.toPrimitive] = function () { return {}; };
                return throwsTypeError(function () { toJSON.call(receiver); });
            });
            return failures.length ? failures.join('; ') : 'ok ' + checks;
        })();
    )JS");
    // An uncaught script exception must not be mistaken for a passing suite.
    assert(result.type == JsType::String);
    if (result.string_val.str() != "ok 39") std::cerr << result.to_string() << '\n';
    assert(result.string_val.str() == "ok 39");
}
