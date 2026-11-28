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
    js.eval(
        "var classicPathReply = '', classicPathError = '';"
        "var classicPathWorker = new Worker('data:text/javascript,' + encodeURIComponent(workerPathSource));"
        "classicPathWorker.onmessage = function(e){ classicPathReply = e.data; };"
        "classicPathWorker.onerror = function(e){ classicPathError = e.message; };"
        "var modulePathReply = '', modulePathError = '';"
        "var modulePathWorker = new Worker('data:text/javascript,' + encodeURIComponent(workerPathSource), {type:'module'});"
        "modulePathWorker.onmessage = function(e){ modulePathReply = e.data; };"
        "modulePathWorker.onerror = function(e){ modulePathError = e.message; };"
    );

    const bool replied = wait_for(js,
        "(classicPathReply !== '' || classicPathError !== '') && "
        "(modulePathReply !== '' || modulePathError !== '')");
    if (!replied) {
        std::cerr << "classic Path2D worker error: " << js.eval("classicPathError").to_string()
                  << "; module error: " << js.eval("modulePathError").to_string() << "\n";
    }
    assert(replied);
    const std::string classic_error = js.eval("classicPathError").to_string();
    const std::string module_error = js.eval("modulePathError").to_string();
    if (!classic_error.empty() || !module_error.empty()) {
        std::cerr << "Path2D worker errors: classic=" << classic_error
                  << "; module=" << module_error << "\n";
    }
    assert(classic_error.empty());
    assert(module_error.empty());

    const std::string expected =
        "types=function,function|self=true,true|document=undefined|"
        "methods=function,function,function,function|"
        "fill=255,0,0,255|hit=true,false|svg=0,255,0,255|"
        "stroke=0,0,255,255|clip=255,255,0,255,0,0,0,0|copy=false,true";
    assert(js.eval("classicPathReply").to_string() == expected);
    assert(js.eval("modulePathReply").to_string() == expected);

    js.eval("classicPathWorker.terminate(); modulePathWorker.terminate();");
    WorkerObject::shutdown_all_workers();
}
