// Phase 463, finding 463: TextDecoder and TextEncoder work in module and nested worker realms.
TEST(text_codecs_work_in_module_and_nested_worker_realms) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    auto wait_for = [](JsInterpreter& interpreter, const std::string& expr) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (interpreter.eval(expr).is_truthy()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return interpreter.eval(expr).is_truthy();
    };

    // The window is the control: the same round trip must work in the page,
    // the module worker's own interpreter, and a child worker it creates.
    assert(js.eval(
        "typeof TextDecoder + ',' + typeof TextEncoder + ',' + "
        "new TextDecoder().decode(new TextEncoder().encode('window €'))"
    ).to_string() == "function,function,window €");

    const std::string module_source =
        "function codecProbe(text) {"
        "  var encoder = new TextEncoder(), decoder = new TextDecoder();"
        "  return typeof TextDecoder + ',' + typeof TextEncoder + ',' + decoder.decode(encoder.encode(text));"
        "}"
        "var moduleProbe = codecProbe('module €');"
        "var childSource = \"var encoder = new TextEncoder(), decoder = new TextDecoder(); "
        "postMessage(typeof TextDecoder + ',' + typeof TextEncoder + ',' + decoder.decode(encoder.encode('nested €')));\";"
        "var child = new Worker('data:text/javascript,' + encodeURIComponent(childSource));"
        "child.onmessage = function(e) {"
        "  postMessage({module: moduleProbe, nested: e.data});"
        "  child.terminate();"
        "};"
        "child.onerror = function(e) {"
        "  postMessage({module: moduleProbe, nested: 'ERROR:' + e.message});"
        "  child.terminate();"
        "};";
    js.eval("var codecModuleSource = " + js_quote_for_suite(module_source) + ";");
    js.eval(
        "var codecModuleReply = null, codecModuleError = '';"
        "var codecModule = new Worker('data:text/javascript,' + encodeURIComponent(codecModuleSource), { type: 'module' });"
        "codecModule.onmessage = function(e) { codecModuleReply = e.data; };"
        "codecModule.onerror = function(e) { codecModuleError = e.message; };"
    );

    const bool replied = wait_for(js, "codecModuleReply !== null || codecModuleError !== ''");
    if (!replied) std::cerr << "module worker codec error: " << js.eval("codecModuleError").to_string() << "\n";
    assert(replied);
    assert(js.eval("codecModuleError").to_string().empty());
    assert(js.eval("codecModuleReply.module").to_string() == "function,function,module €");
    assert(js.eval("codecModuleReply.nested").to_string() == "function,function,nested €");

    js.eval("codecModule.terminate();");
    WorkerObject::shutdown_all_workers();
}
