// Phase 499, finding 499: Path2D reaches an OffscreenCanvas in worker realms.
//
// Phase 426's note blamed a reduced OffscreenCanvas context. The context already
// came from CanvasRenderingContext2D::to_js_value and had full path dispatch. The
// worker could not reach it because **both constructors** were absent:
//
//   typeof OffscreenCanvas  -> "undefined"
//   typeof Path2D           -> "undefined"
//
// This suite runs real classic and module workers. It checks bare names and the
// engine's separately modelled `self`, then asks the context to fill, stroke,
// clip, hit-test, parse SVG path data, and copy a Path2D. Constructor exposure
// without usable pixels cannot pass.

TEST(path2d_draws_through_offscreen_canvas_in_classic_and_module_workers) {
    using namespace litebird;

    // Keep this worker-realm oracle in its own test agent. Worker cells belong
    // to their agents, and must not raise agent 0's automatic-GC threshold for
    // suites that run later in this process.
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

        // The window remains the control. This also proves installing constructors in
        // non-document interpreters did not replace the Window interface constructors.
        assert(js.eval("typeof OffscreenCanvas + ',' + typeof Path2D").to_string() ==
               "function,function");
        assert(js.eval(
            "(function(){var c=new OffscreenCanvas(12,12),g=c.getContext('2d'),p=new Path2D();"
            "p.rect(2,2,6,6);g.fillStyle='#ff0000';g.fill(p);"
            "return g.getImageData(4,4,1,1).data.join(',');})()"
        ).to_string() == "255,0,0,255");

        // One source is run as both classic and module code. No DOM object crosses the
        // agent boundary; the worker sends only the observations and pixel strings.
        std::string source = R"JS(
            function rgba(g, x, y) {
                return g.getImageData(x, y, 1, 1).data.join(',');
            }
            function probe() {
                var out = [];
                out.push('types=' + typeof OffscreenCanvas + ',' + typeof Path2D);
                out.push('self=' + (self.OffscreenCanvas === OffscreenCanvas) + ',' +
                                   (self.Path2D === Path2D));
                out.push('document=' + typeof document);

                var c = new OffscreenCanvas(32, 32);
                var g = c.getContext('2d');
                out.push('methods=' + [typeof g.fill, typeof g.stroke, typeof g.clip,
                                        typeof g.isPointInPath].join(','));

                var box = new Path2D();
                box.rect(2, 2, 8, 8);
                g.fillStyle = '#ff0000';
                g.fill(box);
                out.push('fill=' + rgba(g, 4, 4));
                out.push('hit=' + g.isPointInPath(box, 4, 4) + ',' +
                                  g.isPointInPath(box, 14, 14));

                var svg = new Path2D('M12 2 L18 2 L18 8 L12 8 Z');
                g.fillStyle = '#00ff00';
                g.fill(svg);
                out.push('svg=' + rgba(g, 14, 4));

                var line = new Path2D();
                line.moveTo(2, 20); line.lineTo(10, 20);
                g.strokeStyle = '#0000ff'; g.lineWidth = 2; g.stroke(line);
                out.push('stroke=' + rgba(g, 5, 20));

                var clip = new Path2D(); clip.rect(22, 2, 8, 8);
                g.save(); g.clip(clip); g.fillStyle = '#ffff00';
                g.fillRect(0, 0, 32, 32); g.restore();
                out.push('clip=' + rgba(g, 24, 4) + ',' + rgba(g, 20, 4));

                var copy = new Path2D(box);
                box.rect(12, 12, 4, 4);
                out.push('copy=' + g.isPointInPath(copy, 13, 13) + ',' +
                                   g.isPointInPath(box, 13, 13));
                return out.join('|');
            }
            postMessage(probe());
        )JS";
        // `js_quote_for_suite` escapes quotes and slashes but intentionally leaves
        // line endings alone; a JavaScript string literal cannot contain those raw.
        for (char& c : source) if (c == '\n' || c == '\r') c = ' ';

        js.eval("var workerPathSource = " + js_quote_for_suite(source) + ";");
        const std::string expected =
            "types=function,function|self=true,true|document=undefined|"
            "methods=function,function,function,function|"
            "fill=255,0,0,255|hit=true,false|svg=0,255,0,255|"
            "stroke=0,0,255,255|clip=255,255,0,255,0,0,0,0|copy=false,true";

        // Run the agents one at a time. Apart from reducing peak resources, this
        // releases each parent-side worker wrapper and its queued callbacks before
        // the next realm starts.
        const auto run_worker = [&](const std::string& kind, const std::string& options) {
            js.eval(
                "var workerPathReply = '', workerPathError = '';"
                "var workerPathWorker = new Worker('data:text/javascript,' + "
                "encodeURIComponent(workerPathSource)" + options + ");"
                "workerPathWorker.onmessage = function(e){ workerPathReply = e.data; };"
                "workerPathWorker.onerror = function(e){ workerPathError = e.message; };"
            );
            const bool replied = wait_for(js,
                "workerPathReply !== '' || workerPathError !== ''");
            const std::string error = js.eval("workerPathError").to_string();
            if (!replied || !error.empty()) {
                std::cerr << kind << " Path2D worker error: " << error << "\n";
            }
            assert(replied);
            assert(error.empty());
            const std::string reply = js.eval("workerPathReply").to_string();
            js.eval(
                "workerPathWorker.terminate(); workerPathWorker = null;"
                "workerPathReply = ''; workerPathError = '';"
            );
            WorkerObject::shutdown_all_workers();
            EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
            Heap::default_heap().collect_garbage();
            return reply;
        };

        assert(run_worker("classic", "") == expected);
        assert(run_worker("module", ", {type:'module'}") == expected);
        js.eval("workerPathSource = '';");
    }

    // A module worker keeps its import-resolution document in the loader's test
    // roots on its owner agent. Drop those roots and the page before collecting,
    // so later suites can deliberately measure automatic collection thresholds.
    ResourceLoader::clear_origin_roots_for_testing();
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
    Heap::default_heap().collect_garbage();
    Heap::join_agent(previous_agent);
}
