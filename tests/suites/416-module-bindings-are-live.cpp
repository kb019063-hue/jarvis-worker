// Phase 416, finding 416: a module's exported bindings are live.
//
// §16.2.1.6 makes a module namespace answer for the *binding*, not for the
// value it held when the module finished evaluating. The wrapper this engine
// builds for a module ended with `exports.n = n`, copies of the values as they
// were when the module finished. Phase 105 made the *namespace* a set of
// getters over that bag, so the getters were live and the bag was a snapshot:
// a name read again after `bump()` still answered 1.
//
// Each export is now a getter closed over the module's own scope, and the
// wrapper hands its exports back instead of leaving them on a global. Both
// halves are load-bearing: the getter is what makes the binding live, and the
// global is what used to pin every module the worker ever evaluated (it kept
// the scope alive for the worker's whole life, which showed up as a much
// larger live heap in the run -- finding 416 has the measurement).
//
// The importers read the *name* (`ns.n`), not a function that closes over it:
// a function such as `get()` reads `n` out of its own scope and answers 2
// whether or not the export is live, so it cannot tell the two apart. Every
// expected answer below is Node v22.22.3's.
TEST(module_bindings_are_live) {
    using namespace litebird;
    namespace fs = std::filesystem;

    const fs::path root = fs::temp_directory_path() / "litebird416";
    fs::remove_all(root);
    fs::create_directories(root);
    {
        std::ofstream f(root / "counter.js");
        f << "export let n = 1;\n"
             "export function bump() { n = n + 1; }\n";
    }
    {
        // A namespace import straight at the exporting module.
        std::ofstream f(root / "direct.js");
        f << "import * as ns from './counter.js';\n"
             "var before = ns.n;\n"
             "ns.bump();\n"
             "var after = ns.n;\n"
             "postMessage({ before: before, after: after });\n";
    }
    {
        std::ofstream f(root / "via_star.js");
        f << "export * from './counter.js';\n";
    }
    {
        // The same binding one `export *` away.
        std::ofstream f(root / "star.js");
        f << "import * as ns from './via_star.js';\n"
             "var before = ns.n;\n"
             "ns.bump();\n"
             "var after = ns.n;\n"
             "postMessage({ before: before, after: after });\n";
    }

    std::string failure, direct, star;
    {
        ResourceLoader::set_origin_root_for_testing("https://modules416.test", root.string());
        auto doc = std::make_shared<Document>();
        doc->set_url("https://modules416.test/app/page.html");
        JsInterpreter js(doc);
        js.load_page("<!doctype html><body></body>", true);
        js.eval(
            "var direct = null, star = null, err = '';"
            "function spawn(path, take) {"
            "  var w = new Worker(path, { type: 'module' });"
            "  w.onmessage = function (e) { take(e.data); };"
            "  w.onerror = function (e) { err = String(e.message); };"
            "  return w;"
            "}"
            "var a = spawn('/direct.js', function (d) { direct = d; });"
            "var b = spawn('/star.js', function (d) { star = d; });"
            "1;");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (js.eval("(direct === null || star === null) && err === ''").is_truthy() &&
               std::chrono::steady_clock::now() < deadline) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        failure = js.eval("err").to_string();
        direct = js.eval("JSON.stringify(direct)").to_string();
        star = js.eval("JSON.stringify(star)").to_string();
        js.eval("a.terminate(); b.terminate();");
        WorkerObject::shutdown_all_workers();
    }
    ResourceLoader::clear_origin_roots_for_testing();
    fs::remove_all(root);

    // Terminate, drop the interpreter, then collect: a worker's own cells go
    // with it, and the suites after this one measure what they measured before
    // it. Measured at ~1,000 cells for a suite of this shape.
    Heap::default_heap().collect_garbage();

    assert(failure.empty());
    // Before the fix both answered {"before":1,"after":1}.
    assert(direct == R"({"before":1,"after":2})");
    assert(star == R"({"before":1,"after":2})");
}
