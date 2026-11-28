// Phase 538, finding 538: ToPropertyKey preserves a converted Symbol.
TEST(object_property_keys_preserve_symbols_and_convert_only_once) {
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
                { name: 'read', run: function (key, expected) { var o = {}; o[expected] = 17; return o[key] === 17; } },
                { name: 'write', run: function (key, expected) { var o = {}; o[key] = 17; return o[expected] === 17; } },
                { name: 'in', run: function (key, expected) { var o = {}; o[expected] = 17; return key in o; } },
                { name: 'delete', run: function (key, expected) { var o = {}; o[expected] = 17; return delete o[key] && !(expected in o); } },
                { name: 'defineProperty', run: function (key, expected) {
                    var o = {}; Object.defineProperty(o, key, { value: 17 }); return o[expected] === 17;
                } },
                { name: 'getOwnPropertyDescriptor', run: function (key, expected) {
                    var o = {}; o[expected] = 17; return Object.getOwnPropertyDescriptor(o, key).value === 17;
                } },
                { name: 'hasOwn', run: function (key, expected) { var o = {}; o[expected] = 17; return Object.hasOwn(o, key); } },
                { name: 'hasOwnProperty', run: function (key, expected) {
                    var o = {}; o[expected] = 17; return Object.prototype.hasOwnProperty.call(o, key);
                } },
                { name: 'propertyIsEnumerable', run: function (key, expected) {
                    var o = {}; o[expected] = 17; return Object.prototype.propertyIsEnumerable.call(o, key);
                } },
                { name: 'Reflect.get', run: function (key, expected) { var o = {}; o[expected] = 17; return Reflect.get(o, key) === 17; } },
                { name: 'Reflect.set', run: function (key, expected) { var o = {}; return Reflect.set(o, key, 17) && o[expected] === 17; } },
                { name: 'Reflect.has', run: function (key, expected) { var o = {}; o[expected] = 17; return Reflect.has(o, key); } },
                { name: 'Reflect.deleteProperty', run: function (key, expected) {
                    var o = {}; o[expected] = 17; return Reflect.deleteProperty(o, key) && !(expected in o);
                } },
                { name: 'Reflect.defineProperty', run: function (key, expected) {
                    var o = {}; return Reflect.defineProperty(o, key, { value: 17 }) && o[expected] === 17;
                } },
                { name: 'Reflect.getOwnPropertyDescriptor', run: function (key, expected) {
                    var o = {}; o[expected] = 17; return Reflect.getOwnPropertyDescriptor(o, key).value === 17;
                } },
                { name: 'fromEntries', run: function (key, expected) { return Object.fromEntries([[key, 17]])[expected] === 17; } },
                { name: 'computed object name', run: function (key, expected) { return ({ [key]: 17 })[expected] === 17; } },
                { name: 'computed class method', run: function (key, expected) {
                    class C { [key]() { return 17; } } return new C()[expected]() === 17;
                } }
            ];
            operations.forEach(function (op) {
                check(op.name + ' exotic Symbol result', function () {
                    var symbol = Symbol('key'), key = {}, reads = 0, calls = 0, valid = true;
                    Object.defineProperty(key, Symbol.toPrimitive, { get: function () {
                        reads++;
                        return function (hint) {
                            'use strict';
                            calls++;
                            valid = this === key && arguments.length === 1 && hint === 'string';
                            return symbol;
                        };
                    }});
                    key.toString = key.valueOf = function () { throw 'must not fall back'; };
                    return op.run(key, symbol) && reads === 1 && calls === 1 && valid;
                });
                check(op.name + ' ordinary first Symbol result', function () {
                    var symbol = Symbol('key'), key = {}, log = [];
                    Object.defineProperty(key, 'toString', { get: function () {
                        log.push('get'); return function () {
                            'use strict';
                            log.push(this === key && arguments.length === 0 ? 'call' : 'bad call');
                            return symbol;
                        };
                    }});
                    Object.defineProperty(key, 'valueOf', { get: function () { throw 'must not read'; } });
                    return op.run(key, symbol) && log.join(',') === 'get,call';
                });
                check(op.name + ' ordinary fallback Symbol result', function () {
                    var symbol = Symbol('key'), key = {}, log = [];
                    key.toString = function () { log.push('toString'); return {}; };
                    key.valueOf = function () { log.push('valueOf'); return symbol; };
                    return op.run(key, symbol) && log.join(',') === 'toString,valueOf';
                });
                check(op.name + ' null-prototype exotic key', function () {
                    var symbol = Symbol('key'), key = Object.create(null);
                    key[Symbol.toPrimitive] = function () { return symbol; };
                    return op.run(key, symbol);
                });
                check(op.name + ' primitive Symbol', function () {
                    var symbol = Symbol('primitive'); return op.run(symbol, symbol);
                });
                [undefined, null, true, 17, NaN, -0, 'text'].forEach(function (primitive, index) {
                    check(op.name + ' non-Symbol primitive ' + index, function () {
                        var calls = 0, key = {};
                        key[Symbol.toPrimitive] = function (hint) {
                            calls++; if (hint !== 'string') throw 'wrong hint'; return primitive;
                        };
                        return op.run(key, String(primitive)) && calls === 1;
                    });
                });
                check(op.name + ' getter exception', function () {
                    var marker = {}, key = {};
                    Object.defineProperty(key, Symbol.toPrimitive, { get: function () { throw marker; } });
                    try { op.run(key, 'irrelevant'); } catch (e) { return e === marker; }
                    return false;
                });
                check(op.name + ' call exception', function () {
                    var marker = {}, key = {};
                    key[Symbol.toPrimitive] = function () { throw marker; };
                    try { op.run(key, 'irrelevant'); } catch (e) { return e === marker; }
                    return false;
                });
                check(op.name + ' object exotic result', function () {
                    var calls = 0, key = {};
                    key[Symbol.toPrimitive] = function () { calls++; return {}; };
                    key.toString = key.valueOf = function () { calls += 100; return 'fallback'; };
                    return throwsTypeError(function () { op.run(key, 'fallback'); }) && calls === 1;
                });
                check(op.name + ' noncallable exotic method', function () {
                    var key = {}, calls = 0;
                    key[Symbol.toPrimitive] = 0;
                    key.toString = function () { calls++; return 'fallback'; };
                    return throwsTypeError(function () { op.run(key, 'fallback'); }) && calls === 0;
                });
                check(op.name + ' collection inside hook', function () {
                    var symbol = Symbol('gc'), key = {};
                    key[Symbol.toPrimitive] = function () { if (typeof gc === 'function') gc(); return symbol; };
                    return op.run(key, symbol);
                });
            });
            check('Symbol identity is not its description', function () {
                var first = Symbol('same'), second = Symbol('same'), object = {};
                var key = {}; key[Symbol.toPrimitive] = function () { return first; };
                object[key] = 17;
                return object[first] === 17 && !Object.hasOwn(object, second) && !Object.hasOwn(object, 'Symbol(same)') &&
                    Object.getOwnPropertySymbols(object)[0] === first && Object.keys(object).length === 0;
            });
            check('well-known Symbol survives object conversion', function () {
                var key = {}; key[Symbol.toPrimitive] = function () { return Symbol.iterator; };
                var object = {}; object[key] = 17;
                return object[Symbol.iterator] === 17 && Object.getOwnPropertySymbols(object)[0] === Symbol.iterator;
            });
            check('string conversion of the same object still throws', function () {
                var key = {}; key[Symbol.toPrimitive] = function () { return Symbol('key'); };
                return throwsTypeError(function () { String(key); });
            });
            return failures.length ? failures.join('; ') : 'ok ' + checks;
        })();
    )JS");
    assert(result.type == JsType::String);
    if (result.string_val.str() != "ok 309") std::cerr << result.to_string() << '\n';
    assert(result.string_val.str() == "ok 309");

    // Exercise the VM's separate OpGetIndex/OpSetIndex callers as well.
    const std::vector<std::string> vm_sources = {
        "var s=Symbol('vm'); var k={}; k[Symbol.toPrimitive]=function(){return s;}; var o={}; o[s]=17; o[k]===17;",
        "var s=Symbol('vm'); var k={}; k[Symbol.toPrimitive]=function(){return s;}; var o={}; o[k]=17; o[s]===17;",
        "var s=Symbol('vm'); var k={}; k.toString=function(){return s;}; var o={}; o[s]=17; o[k]===17;",
        "var s=Symbol('vm'); var k={}; k.toString=function(){return {};}; k.valueOf=function(){return s;}; var o={}; o[k]=17; o[s]===17;"
    };
    for (const auto& source : vm_sources) {
        TestPage interpreted;
        TestPage compiled;
        assert(interpreted.js.eval(source).to_string() == "true");
        assert(compiled.js.eval_bytecode(source).to_string() == "true");
    }
}
