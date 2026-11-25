// Phase 453, finding 453: `sort` collects inherited places before comparing,
// writes the sorted list through indexed setters, and performs no writeback when
// the comparator throws. These are the three `precise-*` cases named by the
// still-open AGENTS.md row; all pass on this tree, so this suite keeps that
// observed behavior from regressing.
TEST(array_sort_observes_indexed_accessor_order) {
    TestPage page;
    JsInterpreter& js = page.js;

    JsValue inherited_data = js.eval(R"JS(
        (function () {
            Object.defineProperty(Object.prototype, "2", {
                value: 4, writable: true, configurable: true
            });
            var array = [undefined, 3, , 2, undefined, , 1];
            array.sort();
            delete Object.prototype["2"];
            return [String(array[0]), String(array[1]), String(array[2]),
                    String(array[3]), String(array[4]), String(array[5]),
                    String(array[6]), array.hasOwnProperty("2"),
                    array.hasOwnProperty("6"), 6 in array, array.length].join("|");
        })()
    )JS");
    assert(inherited_data.type == JsType::String);
    assert(inherited_data.string_val.str() ==
           "1|2|3|4|undefined|undefined|undefined|true|false|false|7");

    JsValue accessor_writeback = js.eval(R"JS(
        (function () {
            var log = [];
            Object.defineProperty(Object.prototype, "2", {
                get: function () { log.push("get"); return 4; },
                set: function (value) { log.push("set with " + value); },
                configurable: true
            });
            var array = [undefined, 3, , 2, undefined, , 1];
            array.sort();
            var result = log.join("|") + ";" +
                [String(array[0]), String(array[1]), String(array[3]),
                 String(array[4]), String(array[5]), array.hasOwnProperty("2"),
                 6 in array, array.length].join("|");
            delete Object.prototype["2"];
            return result;
        })()
    )JS");
    assert(accessor_writeback.type == JsType::String);
    assert(accessor_writeback.string_val.str() ==
           "get|set with 3;1|2|4|undefined|undefined|false|false|7");

    JsValue comparator_throw = js.eval(R"JS(
        (function () {
            var log = [];
            Object.defineProperty(Object.prototype, "2", {
                get: function () { log.push("get"); return 4; },
                set: function (value) { log.push("set with " + value); },
                configurable: true
            });
            var array = [undefined, 3, , 2, undefined, , 1];
            var count = 0;
            try {
                array.sort(function (a, b) {
                    if (++count === 3) throw new Error("lolwat");
                    return b - a;
                });
            } catch (error) {
                log.push(error.message);
            }
            var events = log.join("|");
            delete Object.prototype["2"];
            return events + ";" +
                [String(array[0]), String(array[1]), String(array[2]),
                 String(array[3]), String(array[4]), String(array[5]),
                 String(array[6])].join("|") + ";" +
                [array.hasOwnProperty("0"), array.hasOwnProperty("1"),
                 array.hasOwnProperty("2"), array.hasOwnProperty("3"),
                 array.hasOwnProperty("4"), array.hasOwnProperty("5"),
                 array.hasOwnProperty("6"), array.length].join("|");
        })()
    )JS");
    assert(comparator_throw.type == JsType::String);
    assert(comparator_throw.string_val.str() ==
           "get|lolwat;undefined|3|undefined|2|undefined|undefined|1;"
           "true|true|false|true|true|false|true|7");
}
