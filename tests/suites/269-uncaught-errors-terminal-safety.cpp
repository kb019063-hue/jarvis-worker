// Phase 269, finding 283: diagnostic text, not thrown values, is filtered.
TEST(uncaught_js_diagnostics_cannot_inject_terminal_controls) {
    JsInterpreter js(nullptr);
    JsValue text = js.eval("var s=''; for (var i=0;i<256;i++) s+=String.fromCharCode(i); s;");
    auto previous_handler = js_uncaught_handler;
    js_uncaught_handler = nullptr;
    std::ostringstream output;
    auto* previous_buffer = std::cerr.rdbuf(output.rdbuf());

    js_report_uncaught(text);
    js_report_unhandled_rejection(text);
    JsThrow wrapped(text);
    js_report_uncaught(static_cast<const std::exception&>(wrapped));
    std::string bytes;
    for (int c = 1; c < 256; ++c) bytes += static_cast<char>(c);
    const std::runtime_error native(bytes);
    const JsSyntaxError syntax(bytes, 7);
    js_report_uncaught(native);
    js_report_uncaught(syntax);

    std::cerr.rdbuf(previous_buffer);
    js_uncaught_handler = previous_handler;
    std::string expected_text(32, '?');
    for (int c = 0x20; c <= 0x7e; ++c) expected_text += static_cast<char>(c);
    expected_text += std::string(1 + 128 * 2, '?');
    std::string expected_native(31, '?');
    for (int c = 0x20; c <= 0x7e; ++c) expected_native += static_cast<char>(c);
    expected_native += std::string(129, '?');
    assert(output.str() == "Uncaught " + expected_text + "\n" +
                           "Uncaught (in promise) " + expected_text + "\n" +
                           "Uncaught " + expected_text + "\n" +
                           "Uncaught " + expected_native + "\n" +
                           "Uncaught SyntaxError: " + expected_native + " (line 7)\n");
    // The original JS value is not changed by either output path.
    assert(text.to_string() == js.eval("s").to_string());
}
