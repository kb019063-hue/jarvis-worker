// Phase 551, finding 551: the ISO parser must not lose an expanded year's sign.
TEST(date_parsing_rejects_negative_zero_without_losing_valid_year_zero) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        (function () {
            var checks = 0, failures = [];
            function check(name, fn) {
                checks++;
                try { if (fn() !== true) failures.push(name + ': wrong result'); }
                catch (e) { failures.push(name + ': unexpected ' + e.name); }
            }
            var suffixes = [
                '', '-03', '-03-31', '-03-31T00:45Z', '-03-31T01:45',
                '-03-31T01:45:00+01:00', '-01-01T00:00:00.000Z',
                '-12-31T24:00:00.000Z', '-06-15T23:59:59.999-05:30',
                '-01-01T00:00:00+00:00', '-01-01T00:00:00-00:00',
                '-01-01T00:00:00+23:59', '-01-01T00:00:00-23:59'
            ];
            suffixes.forEach(function (suffix) {
                var invalid = '-000000' + suffix;
                check('parse rejects ' + invalid, function () { return Number.isNaN(Date.parse(invalid)); });
                check('constructor rejects ' + invalid, function () { return Number.isNaN(new Date(invalid).getTime()); });
                check('parse rejects boxed ' + invalid, function () { return Number.isNaN(Date.parse(new String(invalid))); });
                check('constructor rejects boxed ' + invalid, function () { return Number.isNaN(new Date(new String(invalid)).getTime()); });
                check('positive year zero parses ' + suffix, function () {
                    var expected = Date.parse('0000' + suffix);
                    return Number.isFinite(expected) && Date.parse('+000000' + suffix) === expected;
                });
                check('positive year zero constructs ' + suffix, function () {
                    var expected = new Date('0000' + suffix).getTime();
                    return Number.isFinite(expected) && new Date('+000000' + suffix).getTime() === expected;
                });
            });
            [' ', '\t', '\n\r'].forEach(function (padding, index) {
                var text = padding + '-000000-03-31T00:45Z' + padding;
                check('trimmed invalid parse ' + index, function () { return Number.isNaN(Date.parse(text)); });
                check('trimmed invalid constructor ' + index, function () { return Number.isNaN(new Date(text).getTime()); });
            });
            [
                ['0000-01-01T00:00:00.000Z', -62167219200000],
                ['+000000-01-01T00:00:00.000Z', -62167219200000],
                ['-000001-01-01T00:00:00.000Z', -62198755200000],
                ['+000001-01-01T00:00:00.000Z', -62135596800000],
                ['+010000-01-01T00:00:00.000Z', 253402300800000],
                ['-271821-04-20T00:00:00.000Z', -8640000000000000],
                ['+275760-09-13T00:00:00.000Z', 8640000000000000]
            ].forEach(function (pair) {
                check('exact UTC parse ' + pair[0], function () { return Date.parse(pair[0]) === pair[1]; });
                check('exact UTC constructor ' + pair[0], function () { return new Date(pair[0]).getTime() === pair[1]; });
            });
            ['-271821-04-19T23:59:59.999Z', '+275760-09-13T00:00:00.001Z'].forEach(function (text) {
                check('outside TimeClip parse ' + text, function () { return Number.isNaN(Date.parse(text)); });
                check('outside TimeClip constructor ' + text, function () { return Number.isNaN(new Date(text).getTime()); });
            });
            [
                { name: 'parse', hint: 'string', run: function (value) { return Date.parse(value); } },
                { name: 'constructor', hint: 'default', run: function (value) { return new Date(value).getTime(); } }
            ].forEach(function (op) {
                check(op.name + ' converts object once with original receiver', function () {
                    var source = {}, reads = 0, calls = 0, valid = true;
                    Object.defineProperty(source, Symbol.toPrimitive, { get: function () {
                        reads++;
                        return function (hint) {
                            'use strict';
                            calls++;
                            valid = this === source && arguments.length === 1 && hint === op.hint;
                            if (typeof gc === 'function') gc();
                            return '-000000-03-31T00:45Z';
                        };
                    }});
                    source.toString = source.valueOf = function () { throw 'must not fall back'; };
                    return Number.isNaN(op.run(source)) && reads === 1 && calls === 1 && valid;
                });
                check(op.name + ' getter exception is not masked', function () {
                    var source = {}, marker = {};
                    Object.defineProperty(source, Symbol.toPrimitive, { get: function () { throw marker; } });
                    try { op.run(source); } catch (e) { return e === marker; }
                    return false;
                });
                check(op.name + ' hook exception is not masked', function () {
                    var source = {}, marker = {};
                    source[Symbol.toPrimitive] = function () { throw marker; };
                    try { op.run(source); } catch (e) { return e === marker; }
                    return false;
                });
                check(op.name + ' ordinary fallback order is unchanged', function () {
                    var source = {}, log = [];
                    source.toString = function () { log.push('toString'); return {}; };
                    source.valueOf = function () {
                        log.push('valueOf');
                        return op.hint === 'string' ? '-000000-03-31T00:45Z' : {};
                    };
                    if (op.hint === 'default') {
                        // Default hint is number-first: valueOf must fail before toString succeeds.
                        source.toString = function () { log.push('toString'); return '-000000-03-31T00:45Z'; };
                    }
                    return Number.isNaN(op.run(source)) && log.join(',') ===
                        (op.hint === 'string' ? 'toString,valueOf' : 'valueOf,toString');
                });
            });
            check('parse ignores additional arguments', function () {
                var calls = 0, extra = { toString: function () { calls++; throw 'unused'; } };
                return Number.isNaN(Date.parse('-000000-03-31T00:45Z', extra)) && calls === 0;
            });
            return failures.length ? failures.join('; ') : 'ok ' + checks;
        })();
    )JS");
    assert(result.type == JsType::String);
    if (result.string_val.str() != "ok 111") std::cerr << result.to_string() << '\n';
    assert(result.string_val.str() == "ok 111");
}
