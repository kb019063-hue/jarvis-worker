// Phase 455, finding 455: small function frames reserve their known bindings.
TEST(function_call_environment_reservation_preserves_bindings) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function check(name, actual, expected) {
            if (actual !== expected) failures.push(name + ': got ' + actual + ', want ' + expected);
        }

        function addWithLocal(a, b) {
            var total = a + b;
            function readTotal() { return total; }
            return readTotal();
        }
        check('parameters, var and hoisted function', addWithLocal(4, 5), 9);

        function duplicateSimpleParameter(a, a) { return a; }
        check('duplicate simple parameters keep the last value', duplicateSimpleParameter(2, 7), 7);

        function redeclaredParameter(a) { var a; return a; }
        check('var redeclaration keeps parameter value', redeclaredParameter(8), 8);

        function readsArguments(a, b) { return arguments[0] + arguments[1] + a + b; }
        check('arguments binding remains available', readsArguments(3, 4), 14);

        function capture(start) {
            var value = start;
            return function () { value++; return value; };
        }
        var next = capture(10);
        check('captured frame first update', next(), 11);
        check('captured frame persists', next(), 12);

        function withDefault(a = 5) { var b = 2; return a + b; }
        check('parameter-expression frame stays correct', withDefault(), 7);

        var arrow = (value) => value + 1;
        check('arrow parameter frame', arrow(8), 9);
    )JS");
    assert(js.eval("failures.join('; ')").to_string().empty());
}
