// Phase 559, finding 559: named imports are immutable live bindings, not
// values copied into the importer when its dependency finishes evaluating.
TEST(module_named_imports_remain_live_and_immutable) {
    using namespace litebird;
    namespace fs = std::filesystem;

    auto wait_for = [](JsInterpreter& js, const std::string& expression) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (js.eval(expression).is_truthy()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return js.eval(expression).is_truthy();
    };

    const fs::path root = fs::temp_directory_path() /
        ("litebird_live_named_imports_" + std::to_string(static_cast<long>(::getpid())));
    fs::remove_all(root);
    fs::create_directories(root);
    {
        std::ofstream state(root / "state.js");
        state << "export let value = 1;\n"
                 "export function increment() { value = value + 1; return this === undefined; }\n";
    }
    {
        std::ofstream reexport(root / "reexport.js");
        reexport << "export { value as forwarded } from './state.js';\n";
    }
    {
        std::ofstream entry(root / "entry.js");
        entry << "import { value, increment as advance } from './state.js';\n"
                 "import { forwarded } from './reexport.js';\n"
                 "import * as state from './state.js';\n"
                 "let before = value;\n"
                 "let unboundThis = advance();\n"
                 "let after = value;\n"
                 "let namespaceAfter = state.value;\n"
                 "let reexportAfter = forwarded;\n"
                 "let blockShadow; { let value = 80; blockShadow = value; }\n"
                 "function parameterShadow(value) { return value; }\n"
                 "let assignment = '';\n"
                 "try { value = 99; } catch (error) { assignment = error.name; }\n"
                 "postMessage({ before: before, after: after, unboundThis: unboundThis, "
                 "namespaceAfter: namespaceAfter, "
                 "reexportAfter: reexportAfter, "
                 "blockShadow: blockShadow, parameterShadow: parameterShadow(7), "
                 "propertyName: ({ value: 11 }).value, assignment: assignment });\n";
    }

    ResourceLoader::set_origin_root_for_testing("https://live-imports.example.test", root.string());
    auto doc = std::make_shared<Document>();
    doc->set_url("https://live-imports.example.test/app/page.html");
    JsInterpreter js(doc);
    js.load_page("<!doctype html><body></body>", true);
    js.eval(R"js(
        var liveImportReply = null, liveImportError = '';
        var liveImportWorker = new Worker('/entry.js', { type: 'module' });
        liveImportWorker.onmessage = function(event) { liveImportReply = event.data; };
        liveImportWorker.onerror = function(event) { liveImportError = event.message; };
    )js");

    assert(wait_for(js, "liveImportReply !== null || liveImportError !== ''"));
    assert(js.eval("liveImportError").to_string().empty());
    assert(js.eval("liveImportReply.before").to_number() == 1.0);
    assert(js.eval("liveImportReply.after").to_number() == 2.0);
    assert(js.eval("liveImportReply.unboundThis").is_truthy());
    assert(js.eval("liveImportReply.namespaceAfter").to_number() == 2.0);
    assert(js.eval("liveImportReply.reexportAfter").to_number() == 2.0);
    assert(js.eval("liveImportReply.blockShadow").to_number() == 80.0);
    assert(js.eval("liveImportReply.parameterShadow").to_number() == 7.0);
    assert(js.eval("liveImportReply.propertyName").to_number() == 11.0);
    assert(js.eval("liveImportReply.assignment").to_string() == "TypeError");

    js.eval("liveImportWorker.terminate();");
    WorkerObject::shutdown_all_workers();
    ResourceLoader::clear_origin_roots_for_testing();
    fs::remove_all(root);
}
