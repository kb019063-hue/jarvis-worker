// Phase 483 — Error.isError answers whether a value has [[ErrorData]].

TEST(error_is_error_answers_whether_value_has_error_data) {
    TestPage page;
    JsInterpreter &js = page.js;
    auto run = [&](const char *src) { return js.eval(src).to_string(); };
    auto is_true = [&](const char *src) { return js.eval(src).is_truthy(); };

    // 1. The method exists on Error with length 1.
    assert(run("typeof Error.isError") == "function");
    assert(run("Error.isError.length") == "1");

    // 2. Primitives are not errors.
    assert(run("Error.isError(null)") == "false");
    assert(run("Error.isError(undefined)") == "false");
    assert(run("Error.isError(42)") == "false");
    assert(run("Error.isError('oops')") == "false");

    // 3. Plain objects are not errors.
    assert(run("Error.isError({})") == "false");
    assert(run("Error.isError([])") == "false");

    // 4. Every native error kind is an error.
    assert(is_true("Error.isError(new Error('m'))"));
    assert(is_true("Error.isError(new TypeError('m'))"));
    assert(is_true("Error.isError(new RangeError('m'))"));
    assert(is_true("Error.isError(new SyntaxError('m'))"));
    assert(is_true("Error.isError(new ReferenceError('m'))"));
    assert(is_true("Error.isError(new URIError('m'))"));
    assert(is_true("Error.isError(new EvalError('m'))"));
    assert(is_true("Error.isError(new AggregateError([], 'm'))"));

    // 5. Subclass instances are errors.
    assert(is_true("class MyError extends Error {} ; Error.isError(new MyError('x'))"));
    assert(is_true("class MyType extends TypeError {} ; Error.isError(new MyType('x'))"));

    // 6. Throwing and catching preserves identity.
    assert(is_true("(function(){ try { throw new TypeError('bad'); } catch(e){ return Error.isError(e); } })()"));
    assert(is_true("(function(){ try { null.foo; } catch(e){ return Error.isError(e); } })()"));

    // 7. An object that merely looks like an error is not one.
    assert(run("(function(){ var o={name:'Error',message:'m',stack:'s'}; return Error.isError(o); })()") == "false");
    assert(run("Error.isError(Object.create(Error.prototype))") == "false");

    // 8. Cross-realm: error from another realm is still an error.
    {
        static const char *page_src = R"html(<!doctype html><html><body>
          <iframe id="f" src="data:text/html,<html></html>"></iframe>
        </body></html>)html";
        js.load_page(page_src, true);
        js.notify_document_loaded();
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(2));
        assert(is_true("var w=document.getElementById('f').contentWindow; Error.isError(new w.Error('m'))"));
        assert(is_true("var w=document.getElementById('f').contentWindow; w.Error.isError(new Error('m'))"));
        js.eval("w=null;");
        EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
    }

    // 9. DOMException in this engine shares klass Error — documented limitation.
    assert(is_true("Error.isError(new DOMException('m','SyntaxError'))"));

    // 10. The method is on Error, not on instances.
    assert(run("typeof (new Error('x')).isError") == "undefined");
}
