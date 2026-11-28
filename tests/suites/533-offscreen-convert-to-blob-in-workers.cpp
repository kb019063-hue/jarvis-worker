// Phase 533, finding 533: OffscreenCanvas.convertToBlob uses a worker realm's Promise.
//
// Phase 499 made OffscreenCanvas reachable in workers, but convertToBlob still
// searched only realm.window for Promise. A worker has no Window, so successful
// conversion and every specified rejection returned undefined. This suite asks
// classic and module workers for real PNG bytes and two rejected promises.

TEST(offscreen_canvas_convert_to_blob_uses_promises_in_classic_and_module_workers) {
    using namespace litebird;

    // Worker cells belong to their own agents. Keep the owner page out of agent
    // 0 as well, so this oracle cannot perturb later suites' GC thresholds.
    const uint32_t previous_agent = Heap::current_agent();
    Heap::enter_new_agent();
    {
        TestPage page;
        JsInterpreter& js = page.js;

        const auto wait_for = [](JsInterpreter& interpreter, const std::string& expression) {
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
                EventLoop::default_loop().tick();
                EventLoop::default_loop().run_microtasks();
                if (interpreter.eval(expression).is_truthy()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return interpreter.eval(expression).is_truthy();
        };

        std::string source = R"JS(
            function rejectedName(p) {
                return p.then(function () { return 'resolved'; },
                              function (e) { return e.name; });
            }
            function u32(bytes, at) {
                return bytes[at] * 16777216 + bytes[at + 1] * 65536 +
                       bytes[at + 2] * 256 + bytes[at + 3];
            }

            var canvas = new OffscreenCanvas(3, 2);
            var context = canvas.getContext('2d');
            context.fillStyle = '#ff0000';
            context.fillRect(0, 0, 3, 2);

            var converted = canvas.convertToBlob();
            var convertedIsPromise = converted instanceof Promise;
            var good = converted.then(function (blob) {
                var bytesPromise = blob.arrayBuffer();
                var bytesIsPromise = bytesPromise instanceof Promise;
                return bytesPromise.then(function (buffer) {
                    var bytes = new Uint8Array(buffer);
                    var signature = Array.from(bytes.slice(0, 8)).join(',');
                    return 'blob=' + blob.type + ',' + (blob.size > 24) + ',' +
                           bytesIsPromise + ',' + signature + ',' +
                           u32(bytes, 16) + 'x' + u32(bytes, 20);
                });
            });
            var badType = rejectedName(canvas.convertToBlob({type: 'image/jpeg'}));
            var badSize = rejectedName(new OffscreenCanvas(0, 2).convertToBlob());

            Promise.all([good, badType, badSize]).then(function (answers) {
                postMessage('promise=' + convertedIsPromise + '|' + answers.join('|'));
            }, function (error) {
                postMessage('unexpected=' + error.name + ':' + error.message);
            });
        )JS";
        for (char& c : source) if (c == '\n' || c == '\r') c = ' ';
        js.eval("var convertBlobWorkerSource = " + js_quote_for_suite(source) + ";");

        const std::string expected =
            "promise=true|blob=image/png,true,true,137,80,78,71,13,10,26,10,3x2|"
            "EncodingError|IndexSizeError";

        const auto run_worker = [&](const std::string& kind, const std::string& options) {
            js.eval(
                "var convertBlobReply = '', convertBlobError = '';"
                "var convertBlobWorker = new Worker('data:text/javascript,' + "
                "encodeURIComponent(convertBlobWorkerSource)" + options + ");"
                "convertBlobWorker.onmessage = function(e){ convertBlobReply = e.data; };"
                "convertBlobWorker.onerror = function(e){ convertBlobError = e.message; };"
            );
            const bool replied = wait_for(js,
                "convertBlobReply !== '' || convertBlobError !== ''");
            const std::string error = js.eval("convertBlobError").to_string();
            if (!replied || !error.empty()) {
                std::cerr << kind << " convertToBlob worker error: " << error << "\n";
            }
            assert(replied);
            assert(error.empty());
            const std::string reply = js.eval("convertBlobReply").to_string();
            js.eval(
                "convertBlobWorker.terminate(); convertBlobWorker = null;"
                "convertBlobReply = ''; convertBlobError = '';"
            );
            WorkerObject::shutdown_all_workers();
            EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
            Heap::default_heap().collect_garbage();
            return reply;
        };

        assert(run_worker("classic", "") == expected);
        assert(run_worker("module", ", {type:'module'}") == expected);
        js.eval("convertBlobWorkerSource = '';");
    }

    ResourceLoader::clear_origin_roots_for_testing();
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
    Heap::default_heap().collect_garbage();
    Heap::join_agent(previous_agent);
}
