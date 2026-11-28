// Phase 493, finding 493: Date and Date.UTC convert components in source order.
TEST(date_components_convert_left_to_right_and_stop_on_abrupt_completion) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        (function () {
            var failures = [], checks = 0;
            var values = [2000, 1, 29, 12, 34, 56, 789];
            function check(name, fn) {
                checks++;
                try {
                    if (fn() !== true) failures.push(name + ': wrong result');
                } catch (e) { failures.push(name + ': unexpected ' + e.name); }
            }
            function invoke(local, args) {
                return local ? new Date(...args).getTime() : Date.UTC(...args);
            }
            function prefix(length) {
                var out = [];
                for (var i = 0; i < length; i++) out.push(i);
                return out.join(',');
            }
            function argumentsFor(log, convert, exotic) {
                return values.map(function (value, index) {
                    var object = {};
                    if (exotic) {
                        object[Symbol.toPrimitive] = function (hint) {
                            if (hint !== 'number') throw 'wrong hint';
                            log.push(index);
                            return convert(index, value);
                        };
                    } else {
                        object.valueOf = function () {
                            log.push(index);
                            return convert(index, value);
                        };
                    }
                    return object;
                });
            }
            function unchanged(index, value) { return value; }
            [false, true].forEach(function (local) {
                var label = local ? 'constructor ' : 'UTC ';
                [false, true].forEach(function (exotic) {
                    check(label + 'ordered conversion ' + exotic, function () {
                        var log = [];
                        var args = argumentsFor(log, unchanged, exotic);
                        args.push({ valueOf: function () { throw 'extra argument converted'; } });
                        return invoke(local, args) === invoke(local, values) && log.join(',') === prefix(7);
                    });
                });
                for (let length = 2; length <= 7; length++) {
                    check(label + 'omitted defaults ' + length, function () {
                        var log = [];
                        var args = argumentsFor(log, unchanged, false).slice(0, length);
                        return invoke(local, args) === invoke(local, values.slice(0, length)) &&
                            log.join(',') === prefix(length);
                    });
                }
                for (let position = 0; position < 7; position++) {
                    check(label + 'throw at ' + position, function () {
                        var log = [], marker = {};
                        var args = argumentsFor(log, function (index, value) {
                            if (index === position) throw marker;
                            return value;
                        }, false);
                        try { invoke(local, args); } catch (e) {
                            return e === marker && log.join(',') === prefix(position + 1);
                        }
                        return false;
                    });
                    check(label + 'symbol at ' + position, function () {
                        var log = [];
                        var args = argumentsFor(log, function (index, value) {
                            return index === position ? Symbol('not numeric') : value;
                        }, true);
                        try { invoke(local, args); } catch (e) {
                            return e instanceof TypeError && log.join(',') === prefix(position + 1);
                        }
                        return false;
                    });
                    [NaN, Infinity, -Infinity, undefined].forEach(function (invalid) {
                        check(label + 'invalid component still converts later fields ' + position + ' ' + invalid, function () {
                            var log = [];
                            var args = argumentsFor(log, function (index, value) {
                                return index === position ? invalid : value;
                            }, false);
                            return Number.isNaN(invoke(local, args)) && log.join(',') === prefix(7);
                        });
                    });
                }
                check(label + 'earlier hooks change later values', function () {
                    var year = 2000, month = 1, day = 29;
                    var args = [
                        { valueOf: function () { month = 5; return year; } },
                        { valueOf: function () { year = 2024; day = 25; return month; } },
                        { valueOf: function () { return day; } }
                    ];
                    return invoke(local, args) === invoke(local, [2000, 5, 25]);
                });
                [0, 42, 99, 100, -1, 2000.75].forEach(function (year) {
                    check(label + 'year mapping after conversion ' + year, function () {
                        var log = [];
                        var args = argumentsFor(log, function (index, value) {
                            return index === 0 ? year : value;
                        }, false);
                        var expected = values.slice(); expected[0] = year;
                        return invoke(local, args) === invoke(local, expected) && log.join(',') === prefix(7);
                    });
                });
            });
            check('UTC single year defaults', function () {
                var count = 0;
                var year = { valueOf: function () { count++; return 2000; } };
                return Date.UTC(year) === 946684800000 && count === 1 && Number.isNaN(Date.UTC());
            });
            check('Date called without new does not convert', function () {
                var poison = { valueOf: function () { throw 'must not convert'; } };
                return typeof Date(poison, poison, poison) === 'string';
            });
            return failures.length ? failures.join('; ') : 'ok ' + checks;
        })();
    )JS");
    // Assert the script result as well as its type: uncaught JS errors cannot pass.
    assert(result.type == JsType::String);
    if (result.string_val.str() != "ok 116") std::cerr << result.to_string() << '\n';
    assert(result.string_val.str() == "ok 116");
}
