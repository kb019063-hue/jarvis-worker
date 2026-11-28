// Phase 506, finding 506: invalid dates do not bypass setter conversions.
TEST(invalid_date_setters_convert_arguments_before_handling_nan_time) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        (function () {
            var failures = [], checks = 0;
            var methods = [
                ['setFullYear', [2020, 5, 15]], ['setUTCFullYear', [2020, 5, 15]],
                ['setMonth', [5, 15]], ['setUTCMonth', [5, 15]],
                ['setDate', [15]], ['setUTCDate', [15]],
                ['setHours', [5, 6, 7, 8]], ['setUTCHours', [5, 6, 7, 8]],
                ['setMinutes', [6, 7, 8]], ['setUTCMinutes', [6, 7, 8]],
                ['setSeconds', [7, 8]], ['setUTCSeconds', [7, 8]],
                ['setMilliseconds', [8]], ['setUTCMilliseconds', [8]]
            ];
            function check(name, fn) {
                checks++;
                try {
                    if (fn() !== true) failures.push(name + ': wrong result');
                } catch (e) { failures.push(name + ': unexpected ' + e.name); }
            }
            function same(a, b) { return a === b || (Number.isNaN(a) && Number.isNaN(b)); }
            function prefix(length) {
                var out = [];
                for (var i = 0; i < length; i++) out.push(i);
                return out.join(',');
            }
            function argumentsFor(values, log, convert, exotic) {
                return values.map(function (value, index) {
                    var object = {};
                    var hook = function (hint) {
                        if (exotic && hint !== 'number') throw 'wrong hint';
                        log.push(index);
                        return convert(value, index);
                    };
                    if (exotic) object[Symbol.toPrimitive] = hook;
                    else object.valueOf = hook;
                    return object;
                });
            }
            function unchanged(value) { return value; }
            function recovery(name, values) {
                // Invalid FullYear setters use +0 as calendar time, not LocalTime(+0).
                var epoch = name === 'setUTCFullYear' ? new Date(0) : new Date(1970, 0, 1);
                return epoch[name](...values);
            }
            methods.forEach(function (entry) {
                var name = entry[0], values = entry[1];
                var recovers = name === 'setFullYear' || name === 'setUTCFullYear';
                [false, true].forEach(function (exotic) {
                    check(name + ' converts invalid receiver arguments ' + exotic, function () {
                        var date = new Date(NaN), log = [];
                        var args = argumentsFor(values, log, unchanged, exotic);
                        args.push({ valueOf: function () { throw 'extra argument converted'; } });
                        var result = date[name](...args);
                        var expected = recovers ? recovery(name, values) : NaN;
                        return same(result, expected) && same(date.getTime(), expected) &&
                            log.join(',') === prefix(values.length);
                    });
                });
                for (let position = 0; position < values.length; position++) {
                    check(name + ' abrupt conversion at ' + position, function () {
                        var date = new Date(NaN), log = [], marker = {};
                        var args = argumentsFor(values, log, function (value, index) {
                            if (index === position) throw marker;
                            return value;
                        }, false);
                        try { date[name](...args); } catch (e) {
                            return e === marker && log.join(',') === prefix(position + 1) &&
                                Number.isNaN(date.getTime());
                        }
                        return false;
                    });
                    check(name + ' Symbol conversion at ' + position, function () {
                        var date = new Date(NaN), log = [];
                        var args = argumentsFor(values, log, function (value, index) {
                            return index === position ? Symbol('not numeric') : value;
                        }, true);
                        try { date[name](...args); } catch (e) {
                            return e instanceof TypeError && log.join(',') === prefix(position + 1);
                        }
                        return false;
                    });
                }
                for (let length = 1; length <= values.length; length++) {
                    check(name + ' omitted defaults ' + length, function () {
                        var date = new Date(NaN), log = [], partial = values.slice(0, length);
                        var args = argumentsFor(partial, log, unchanged, false);
                        var expected = recovers ? recovery(name, partial) : NaN;
                        return same(date[name](...args), expected) && same(date.getTime(), expected) &&
                            log.join(',') === prefix(length);
                    });
                }
                [NaN, Infinity].forEach(function (invalid) {
                    check(name + ' numeric invalidity does not skip conversion ' + invalid, function () {
                        var date = new Date(NaN), log = [];
                        var args = argumentsFor(values, log, function (value, index) {
                            return index === 0 ? invalid : value;
                        }, false);
                        return Number.isNaN(date[name](...args)) && log.join(',') === prefix(values.length);
                    });
                });
                check(name + ' invalid snapshot keeps hook mutations unless recovering', function () {
                    var date = new Date(NaN), log = [], mutation = Date.UTC(2044, 10, 20, 21, 22, 23, 24);
                    var args = argumentsFor(values, log, function (value, index) {
                        if (index === 0) date.setTime(mutation);
                        return value;
                    }, false);
                    var result = date[name](...args);
                    return same(result, recovers ? recovery(name, values) : NaN) &&
                        date.getTime() === (recovers ? recovery(name, values) : mutation) &&
                        log.join(',') === prefix(values.length);
                });
                check(name + ' valid snapshot survives hook invalidation', function () {
                    var time = Date.UTC(2001, 1, 3, 4, 5, 6, 7), date = new Date(time), log = [];
                    var control = new Date(time), expected = control[name](...values);
                    var args = argumentsFor(values, log, function (value, index) {
                        if (index === 0) date.setTime(NaN);
                        return value;
                    }, false);
                    return date[name](...args) === expected && date.getTime() === expected &&
                        log.join(',') === prefix(values.length);
                });
                check(name + ' abrupt hook preserves mutation', function () {
                    var date = new Date(NaN), marker = {};
                    try {
                        date[name]({ valueOf: function () { date.setTime(42); throw marker; } });
                    } catch (e) { return e === marker && date.getTime() === 42; }
                    return false;
                });
                check(name + ' receiver check precedes conversion', function () {
                    var calls = 0;
                    try {
                        Date.prototype[name].call({}, { valueOf: function () { calls++; return 0; } });
                    } catch (e) { return e instanceof TypeError && calls === 0; }
                    return false;
                });
                check(name + ' missing first argument invalidates', function () {
                    var date = new Date(0);
                    return Number.isNaN(date[name]()) && Number.isNaN(date.getTime());
                });
            });
            return failures.length ? failures.join('; ') : 'ok ' + checks;
        })();
    )JS");
    assert(result.type == JsType::String);
    if (result.string_val.str() != "ok 222") std::cerr << result.to_string() << '\n';
    assert(result.string_val.str() == "ok 222");
}
