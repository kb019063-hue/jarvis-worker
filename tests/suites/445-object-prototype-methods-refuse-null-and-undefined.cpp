TEST(object_prototype_methods_refuse_null_and_undefined) {
    // §20.1.3.2, §20.1.3.3, §20.1.3.4, §20.1.3.7 all begin with ToObject(this)
    // and must throw TypeError on null or undefined, while wrapping primitives
    // and preserving step-1 order in hasOwnProperty and isPrototypeOf.
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    JsValue out = js.eval(R"JS(
        var errs = [];
        function check(name, fn) {
            try {
                var r = fn();
                if (r !== true) errs.push(name + ": got " + String(r));
            } catch (e) {
                errs.push(name + ": threw " + e.name + ": " + e.message);
            }
        }
        function throwsTypeError(name, fn) {
            try {
                var r = fn();
                errs.push(name + ": expected TypeError, got " + String(r));
            } catch (e) {
                if (e.name !== "TypeError") errs.push(name + ": expected TypeError, got " + e.name);
            }
        }

        var P = Object.prototype;
        throwsTypeError("hasOwnProperty(null)", function () { return P.hasOwnProperty.call(null, "a"); });
        throwsTypeError("hasOwnProperty(undefined)", function () { return P.hasOwnProperty.call(undefined, "a"); });
        throwsTypeError("propertyIsEnumerable(null)", function () { return P.propertyIsEnumerable.call(null, "a"); });
        throwsTypeError("propertyIsEnumerable(undefined)", function () { return P.propertyIsEnumerable.call(undefined, "a"); });
        throwsTypeError("isPrototypeOf(null, {})", function () { return P.isPrototypeOf.call(null, {}); });
        throwsTypeError("isPrototypeOf(undefined, {})", function () { return P.isPrototypeOf.call(undefined, {}); });
        throwsTypeError("valueOf(null)", function () { return P.valueOf.call(null); });
        throwsTypeError("valueOf(undefined)", function () { return P.valueOf.call(undefined); });

        // Step-1 ordering: ToPropertyKey before ToObject in hasOwnProperty;
        // non-object argument returns false before ToObject in isPrototypeOf.
        var keyOrder = [];
        try {
            P.hasOwnProperty.call(null, { toString: function () { keyOrder.push("key"); return "a"; } });
        } catch (e) {
            keyOrder.push(e.name);
        }
        check("hasOwnProperty key before receiver", function () { return keyOrder.join(",") === "key,TypeError"; });
        check("isPrototypeOf(null, 1)", function () { return P.isPrototypeOf.call(null, 1) === false; });
        check("isPrototypeOf(undefined, null)", function () { return P.isPrototypeOf.call(undefined, null) === false; });

        // Primitives are wrapped, not refused.
        check("hasOwnProperty('ab', 1)", function () { return P.hasOwnProperty.call("ab", 1) === true; });
        check("propertyIsEnumerable('ab', 1)", function () { return P.propertyIsEnumerable.call("ab", 1) === true; });
        check("valueOf(1) is object", function () {
            var v = P.valueOf.call(1);
            return typeof v === "object" && Number(v) === 1;
        });

        // toString on null/undefined remains exempt (§20.1.3.6).
        check("toString(null)", function () { return P.toString.call(null) === "[object Null]"; });
        check("toString(undefined)", function () { return P.toString.call(undefined) === "[object Undefined]"; });

        errs.join("; ");
    )JS");
    assert(out.type == JsType::String);
    assert(out.string_val.str().empty());
}
