// Phase 248, finding 252: only the worker diagnostic is terminal-safe.
TEST(worker_error_diagnostic_cannot_inject_terminal_controls) {
    JsInterpreter js(nullptr);
    std::ostringstream diagnostic;
    auto* previous = std::cerr.rdbuf(diagnostic.rdbuf());

    // The unhandled message arrives asynchronously on the owner's event loop.
    js.eval(R"js(
        var worker = new Worker('data:text/javascript,var s=""; for (var i=0;i<256;i++) s+=String.fromCharCode(i); throw s;');
    )js");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (diagnostic.str().empty() && std::chrono::steady_clock::now() < deadline) {
        EventLoop::default_loop().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    js.eval("worker.terminate()");
    WorkerObject::shutdown_all_workers();
    std::cerr.rdbuf(previous);
    const std::string text = diagnostic.str();
    assert(text.rfind("Uncaught (in worker) ", 0) == 0);
    assert(!text.empty() && text.back() == '\n');
    std::string expected = "Uncaught (in worker) " + std::string(32, '?');
    for (int c = 0x20; c <= 0x7e; ++c) expected += static_cast<char>(c);
    // DEL is one byte, and U+0080..U+00FF use two UTF-8 bytes each.
    expected += std::string(1 + 128 * 2, '?');
    expected += '\n';
    assert(text == expected);
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        assert(c >= 0x20 && c <= 0x7e);
    }

    // A listener still sees the original control characters, not the output
    // representation, and suppresses the unhandled-error diagnostic.
    std::ostringstream handled_diagnostic;
    previous = std::cerr.rdbuf(handled_diagnostic.rdbuf());
    js.eval(R"js(
        var seen = '';
        var handled = new Worker('data:text/javascript,throw "\\x1b]0;worker\\x07";');
        handled.onerror = function(e) { seen = e.message; };
    )js");
    const auto handled_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!js.eval("seen.length > 0").is_truthy() && std::chrono::steady_clock::now() < handled_deadline) {
        EventLoop::default_loop().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(js.eval("seen === '\\x1b]0;worker\\x07'").is_truthy());
    js.eval("handled.terminate()");
    WorkerObject::shutdown_all_workers();
    std::cerr.rdbuf(previous);
    assert(handled_diagnostic.str().empty());
}
