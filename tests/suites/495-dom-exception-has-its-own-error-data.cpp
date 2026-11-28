// Phase 495 — DOMException has its own [[DOMExceptionData]] and is not an Error.

TEST(dom_exception_has_its_own_error_data) {
    TestPage page;
    JsInterpreter &js = page.js;
    auto run = [&](const char *src) { return js.eval(src).to_string(); };
    auto is_true = [&](const char *src) { return js.eval(src).is_truthy(); };

    // 1. DOMException is not an Error.
    assert(run("Error.isError(new DOMException('m','SyntaxError'))") == "false");
    assert(run("Error.isError(new DOMException('m','NotFoundError'))") == "false");

    // 2. But it is still a DOMException and still an instance of Error's
    // prototype chain for stack inheritance — toString tag.
    assert(run("Object.prototype.toString.call(new DOMException('x','SyntaxError'))") == "[object DOMException]");
    assert(run("Object.prototype.toString.call(new Error('x'))") == "[object Error]");

    // 3. Stack accessor still works for DOMException (shares shape).
    assert(is_true("new DOMException('x','SyntaxError').stack !== undefined"));
    assert(run("typeof new DOMException('x','SyntaxError').stack") == "string");

    // 4. And still works for Error.
    assert(is_true("new Error('x').stack !== undefined"));

    // 5. structuredClone of DOMException keeps tag and properties.
    assert(is_true("(function(){ var d=new DOMException('hey','SyntaxError'); var c=structuredClone(d); return Object.prototype.toString.call(c)==='[object DOMException]' && c.message==='hey' && c.name==='SyntaxError'; })()"));

    // 6. Error.isError still true for real Errors, cross-realm.
    assert(is_true("Error.isError(new TypeError('bad'))"));
    {
        static const char *page_src = R"html(<!doctype html><html><body>
          <iframe id="f" src="data:text/html,<html></html>"></iframe>
        </body></html>)html";
        js.load_page(page_src, true);
        js.notify_document_loaded();
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(2));
        assert(run("var w=document.getElementById('f').contentWindow; Error.isError(new w.DOMException('m','SyntaxError'))") == "false");
        assert(is_true("var w=document.getElementById('f').contentWindow; Error.isError(new w.TypeError('m'))"));
        js.eval("w=null;");
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
    }
}
