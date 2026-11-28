// Phase 522, finding 522: a rejected promise does not outlive its interpreter.
//
// The GC audit row carried a clause from Phase 415: an interpreter whose
// generator is aborted mid-walk was measured leaving its cells reachable after
// the interpreter was gone. This suite is the shape that clause described --
// and it passes: the aborted patched generator leaves nothing behind. What the
// measurement had actually caught is the company that shape keeps: a rejected
// promise's unhandled-rejection report is a task on the process-wide event
// loop, and that task captured the promise and rooted its result, so every
// rejection a dead interpreter left behind stayed marked for the life of the
// process. A tick frees it; a host that never ticks again never does.
TEST(a_rejected_promise_does_not_outlive_its_interpreter) {
    using namespace litebird;

    auto alive_now = []() {
        return Heap::default_heap().collect_garbage({}).alive_cells;
    };
    const std::string page = "<!doctype html><html><body></body></html>";

    // ------------------------------------------------------------------
    // The pin: two thousand rejected promises out of a dead interpreter
    // leave no residue. With the defect open this measured about 2,000
    // cells; the report task now holds the promise weakly and roots
    // nothing, so the collector takes the whole graph.
    // ------------------------------------------------------------------
    const size_t baseline = alive_now();
    {
        auto doc = HtmlParser::parse(page);
        JsInterpreter interp(doc);
        const JsValue res = interp.eval(
            "for (var i = 0; i < 2000; i++) Promise.reject(new Error('r' + i));"
            "'rejected 2000';");
        (void)res;
    }
    const size_t after = alive_now();
    if (after > baseline + 512) {
        std::cerr << "RESIDUE rejected promises left " << (after - baseline)
                  << " cells behind a dead interpreter\n";
    }
    assert(after <= baseline + 512);

    // ------------------------------------------------------------------
    // The aborted patched generator, which is the shape the Phase 415
    // clause described: closes on the mapper's throw, `return` patched, the
    // frame parked until the interpreter tears it down. Nothing is left.
    // ------------------------------------------------------------------
    {
        auto doc = HtmlParser::parse(page);
        JsInterpreter interp(doc);
        const JsValue res = interp.eval(
            "var closed = 0, fin = 0;"
            "function* src() { try { yield 1; yield 2; yield 3; } finally { fin++; } }"
            "var it = src();"
            "it.return = function() { closed++; return { done: true }; };"
            "try { Array.from(it, function(v) { if (v === 1) throw new Error('stop'); return v; }); }"
            "catch (e) { }"
            "closed + '|' + fin");
        assert(res.to_string() == "1|0");  // the patched return ran; no `finally`
    }
    assert(alive_now() <= baseline + 512);

    // ------------------------------------------------------------------
    // The report itself is not lost: an unhandled rejection whose promise
    // is still reachable is still counted when the task runs. The output
    // is captured so the run's own stream stays clean; suite 269 owns the
    // exact wording.
    // ------------------------------------------------------------------
    {
        auto doc = HtmlParser::parse(page);
        JsInterpreter interp(doc);
        const int before = js_uncaught_error_count();
        std::ostringstream sink;
        auto* previous_buffer = std::cerr.rdbuf(sink.rdbuf());
        interp.eval("var p = Promise.reject(new Error('still here'));");
        EventLoop::default_loop().tick();  // the report's task runs now
        std::cerr.rdbuf(previous_buffer);
        assert(js_uncaught_error_count() == before + 1);
    }

    // ------------------------------------------------------------------
    // And a report for a promise that is already gone says nothing: the
    // task finds the weak state empty and keeps quiet. Under the old
    // capture this tick reported the rejection of an interpreter that no
    // longer exists.
    // ------------------------------------------------------------------
    {
        const int before = js_uncaught_error_count();
        {
            auto doc = HtmlParser::parse(page);
            JsInterpreter interp(doc);
            interp.eval("Promise.reject(new Error('gone'));");  // unreachable at once
        }
        Heap::default_heap().collect_garbage({});  // the promise is collected here
        std::ostringstream sink;
        auto* previous_buffer = std::cerr.rdbuf(sink.rdbuf());
        EventLoop::default_loop().tick();
        std::cerr.rdbuf(previous_buffer);
        assert(js_uncaught_error_count() == before);
    }
}
