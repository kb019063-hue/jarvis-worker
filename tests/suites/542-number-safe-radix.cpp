// Phase 542: safe integer radix conversion already agrees with an exact oracle.
TEST(number_safe_integer_radix_digits) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        [
            (9007199254740991).toString(2),
            (9007199254740991).toString(3),
            (9007199254740991).toString(7),
            (9007199254740991).toString(36),
            (-9007199254740991).toString(16),
            (9007199254740992).toString(3),
            (4503599627370497).toString(35),
            (0).toString(36),
            (-0).toString(2)
        ].join('|')
    )JS");
    assert(result.type == JsType::String);
    assert(result.string_val.str() ==
        "11111111111111111111111111111111111111111111111111111|"
        "1121202011211211122211100012101111|5350140446150306053|"
        "2gosa7pa2gv|-1fffffffffffff|1121202011211211122211100012101112|"
        "1m4wmr2p2xh|0|0");
}
