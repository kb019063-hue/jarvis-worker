// Phase 545, finding 545: ImageData is a worker-global constructor.
//
// Phase 499 made ImageData returned by an OffscreenCanvas usable in workers,
// but scripts could not name or construct the interface there. This suite runs
// the same real constructor and pixel round trip in classic and module workers.

TEST(image_data_constructor_works_in_classic_and_module_workers) {
    using namespace litebird;

    // Worker cells belong to their own agents. Keep the owner page out of agent
    // 0 so this oracle cannot change a later suite's automatic-GC threshold.
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

        // Installing the interface in non-document interpreters must not replace
        // or weaken the Window realm's existing constructor.
        assert(js.eval(
            "(function(){var i=new ImageData(2,1); return [typeof ImageData,"
            "i instanceof ImageData,i.width,i.height,i.data instanceof Uint8ClampedArray,"
            "i.data.join(',')].join('|');})()"
        ).to_string() == "function|true|2|1|true|0,0,0,0,0,0,0,0");

        std::string source = R"JS(
            function caughtName(body) {
                try { body(); return 'no throw'; } catch (e) { return e.name; }
            }
            function probe() {
                var out = [];
                out.push('types=' + typeof ImageData + ',' + typeof OffscreenCanvas);
                out.push('self=' + (self.ImageData === ImageData));
                out.push('document=' + typeof document);

                var blank = new ImageData(2, 2);
                out.push('blank=' + [blank instanceof ImageData,
                                      Object.getPrototypeOf(blank) === ImageData.prototype,
                                      blank.width, blank.height, blank.colorSpace,
                                      blank.data instanceof Uint8ClampedArray,
                                      blank.data.length, blank.data.join(',')].join(','));

                var bytes = new Uint8ClampedArray([
                    255, 0, 0, 255, 0, 255, 0, 255,
                    0, 0, 255, 255, 255, 255, 0, 255
                ]);
                var made = new ImageData(bytes, 2);
                out.push('data=' + [made instanceof ImageData, made.data === bytes,
                                     made.width, made.height].join(','));

                /* Prove the constructed object is accepted by the worker's real
                   OffscreenCanvas path, not merely branded like ImageData. */
                var canvas = new OffscreenCanvas(3, 3);
                var context = canvas.getContext('2d');
                context.putImageData(made, 1, 1);
                var read = context.getImageData(1, 1, 2, 2);
                out.push('pixels=' + [read instanceof ImageData,
                                       read.data instanceof Uint8ClampedArray,
                                       read.data.join(',')].join(','));

                var created = context.createImageData(1, 2);
                out.push('context=' + [created instanceof ImageData,
                                        created.width, created.height,
                                        created.data.length].join(','));
                out.push('errors=' + caughtName(function () { new ImageData(0, 1); }) + ',' +
                                     caughtName(function () {
                                         new ImageData(new Uint8ClampedArray(6), 1);
                                     }));
                return out.join('|');
            }
            postMessage(probe());
        )JS";
        for (char& c : source) if (c == '\n' || c == '\r') c = ' ';
        js.eval("var imageDataWorkerSource = " + js_quote_for_suite(source) + ";");

        const std::string expected =
            "types=function,function|self=true|document=undefined|"
            "blank=true,true,2,2,srgb,true,16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0|"
            "data=true,true,2,2|"
            "pixels=true,true,255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255|"
            "context=true,1,2,8|errors=IndexSizeError,IndexSizeError";

        const auto run_worker = [&](const std::string& kind, const std::string& options) {
            js.eval(
                "var imageDataReply = '', imageDataError = '';"
                "var imageDataWorker = new Worker('data:text/javascript,' + "
                "encodeURIComponent(imageDataWorkerSource)" + options + ");"
                "imageDataWorker.onmessage = function(e){ imageDataReply = e.data; };"
                "imageDataWorker.onerror = function(e){ imageDataError = e.message; };"
            );
            const bool replied = wait_for(js, "imageDataReply !== '' || imageDataError !== ''");
            const std::string error = js.eval("imageDataError").to_string();
            if (!replied || !error.empty()) {
                std::cerr << kind << " ImageData worker error: " << error << "\n";
            }
            assert(replied);
            assert(error.empty());
            const std::string reply = js.eval("imageDataReply").to_string();
            js.eval(
                "imageDataWorker.terminate(); imageDataWorker = null;"
                "imageDataReply = ''; imageDataError = '';"
            );
            WorkerObject::shutdown_all_workers();
            EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
            Heap::default_heap().collect_garbage();
            return reply;
        };

        assert(run_worker("classic", "") == expected);
        assert(run_worker("module", ", {type:'module'}") == expected);
        js.eval("imageDataWorkerSource = '';");
    }

    ResourceLoader::clear_origin_roots_for_testing();
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
    Heap::default_heap().collect_garbage();
    Heap::join_agent(previous_agent);
}
