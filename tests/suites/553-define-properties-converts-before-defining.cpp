// Phase 553, finding 553: convert all property descriptors before applying any.
TEST(object_define_properties_converts_every_descriptor_before_defining) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var target = {};
        var first_was_present_during_second_conversion = null;
        var descriptors = {
            first: {
                enumerable: true,
                value: 1
            },
            second: {
                get enumerable() {
                    first_was_present_during_second_conversion =
                        Object.prototype.hasOwnProperty.call(target, 'first');
                    return true;
                },
                value: 2
            }
        };
        Object.defineProperties(target, descriptors);

        var throwing_target = {};
        var caught = false;
        var throwing_descriptors = {
            first: { enumerable: true, value: 3 },
            second: {
                get enumerable() { throw new Error('stop descriptor conversion'); }
            }
        };
        try {
            Object.defineProperties(throwing_target, throwing_descriptors);
        } catch (e) {
            caught = true;
        }
    )JS");

    assert(js.eval("first_was_present_during_second_conversion === false").to_string() == "true");
    assert(js.eval("target.first === 1 && target.second === 2").to_string() == "true");
    assert(js.eval("caught === true").to_string() == "true");
    assert(js.eval("Object.prototype.hasOwnProperty.call(throwing_target, 'first') === false").to_string() == "true");
    assert(js.eval("Object.keys(throwing_target).length === 0").to_string() == "true");
}
