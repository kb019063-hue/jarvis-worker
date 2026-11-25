// Phase 387, finding 387: console text is filtered only at output.
TEST(console_output_does_not_emit_page_terminal_controls) {
    JsInterpreter js(nullptr);
    const std::string bad = "A\xc4\x9b\x1b]0;OWN\x07\r\xc2\x9bZ";
    std::string other = bad;
    other[3] = '\x07'; // Distinct source label, same visible replacement.
    js.global_env()->define("bad", JsValue::make_string(bad));
    js.global_env()->define("other", JsValue::make_string(other));

    const std::string clean = "A\xc4\x9b\xef\xbf\xbd]0;OWN\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbdZ";
    std::ostringstream output, errors;
    auto* old_out = std::cout.rdbuf(output.rdbuf());
    auto* old_err = std::cerr.rdbuf(errors.rdbuf());
    js.eval("console.log(bad, 'tail'); console.info(bad); console.debug(bad);"
            "console.table(bad); console.trace(bad); console.dir(bad);"
            "console.group(bad); console.groupCollapsed(bad); console.warn(bad);"
            "console.error(bad); console.assert(false, bad); console.assert(true, bad);"
            "console.count(bad); console.count(other); console.count(bad);"
            "console.countReset(other); console.count(other); console.count(bad);");
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    std::string aliases;
    for (int i = 0; i < 7; ++i) aliases += clean + "\n";
    assert(output.str() == clean + " tail\n" + aliases +
                           "\033[33m[WARN] " + clean + " \033[0m\n" +
                           clean + ": 1\n" + clean + ": 1\n" + clean + ": 2\n" +
                           clean + ": 1\n" + clean + ": 3\n");
    assert(errors.str() == "\033[31m[ERROR] " + clean + " \033[0m\n" +
                           "Assertion failed: " + clean + "\n");
    assert(js.eval("bad").to_string() == bad);
    assert(js.eval("other").to_string() == other);

    // The bytecode VM has a separate console implementation.
    VM vm;
    JsValue console = vm.get_global("console");
    output.str(""); output.clear(); errors.str(""); errors.clear();
    old_out = std::cout.rdbuf(output.rdbuf());
    old_err = std::cerr.rdbuf(errors.rdbuf());
    console.get_property("log").call({JsValue::make_string(bad)});
    console.get_property("warn").call({JsValue::make_string(bad)});
    console.get_property("error").call({JsValue::make_string(bad)});
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    assert(output.str() == clean + "\n\033[33m[WARN] " + clean + " \033[0m\n");
    assert(errors.str() == "\033[31m[ERROR] " + clean + " \033[0m\n");
}
